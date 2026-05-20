/**
 * @file        ota_service.c
 * @brief       局域网 OTA 远程烧录服务实现
 * @details     UDP 触发后启动 TCP 服务端，接收固件并写入 UPG 暂存区
 */

#include "ota_service.h"

#include <stdio.h>
#include <string.h>

#include "errcode.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "securec.h"
#include "soc_osal.h"
#include "ui_service.h"
#include "upg.h"
#include "uart.h"
#include "watchdog.h"

/* 状态字符串表 */
#define OTA_STATE_EXPAND(s, str) str,
const char *const g_ota_state_str[] = { OTA_STATE_MAP(OTA_STATE_EXPAND) };

/* ---------- 内部状态 ---------- */
static volatile ota_state_t g_ota_state = OTA_STATE_IDLE;
static volatile uint8_t     g_ota_progress = 0;
static volatile uint32_t    g_ota_received = 0;
static volatile uint32_t    g_ota_total = 0;

static int       g_tcp_listen_fd = -1;
static int       g_tcp_conn_fd = -1;
static osal_task *g_ota_task_handle = NULL;
static osal_mutex g_ota_mutex;
static bool       g_ota_mutex_inited = false;

/* ---------- UPG 回调 ---------- */
static void *fota_upg_malloc(const uint32_t size)
{
  return osal_kmalloc(size, OSAL_GFP_ATOMIC);
}

static void fota_upg_free(void *ptr)
{
  osal_kfree(ptr);
}

static void fota_upg_serial_putc(const char c)
{
  uint8_t ch = (uint8_t)c;
  (void)uapi_uart_write(0, &ch, 1, 0);
}

static const upg_func_t s_upg_funcs = {
  .malloc = fota_upg_malloc,
  .free = fota_upg_free,
  .serial_putc = fota_upg_serial_putc
};

/* ---------- UPG 校验 ---------- */
static errcode_t fota_upg_verify_stored_package(void)
{
  upg_package_header_t hdr;
  (void)memset_s(&hdr, sizeof(hdr), 0, sizeof(hdr));
  errcode_t r = uapi_upg_read_package(0U, (uint8_t *)&hdr, (uint32_t)sizeof(hdr));
  if (r != ERRCODE_SUCC) {
    printf("[OTA] UPG read_package(0, header) failed, ret=0x%x\r\n", (unsigned)r);
    return r;
  }
  r = uapi_upg_verify_file(&hdr);
  if (r != ERRCODE_SUCC) {
    printf("[OTA] uapi_upg_verify_file failed, ret=0x%x\r\n", (unsigned)r);
    return r;
  }
  printf("[OTA] UPG verify_file OK\r\n");
  return ERRCODE_SUCC;
}

/* ---------- UPG 预准备 ---------- */
static errcode_t fota_upg_prepare_once(uint32_t package_len)
{
  if (package_len == 0U) {
    printf("[OTA] uapi_upg_prepare: package_len=0, skip (invalid)\r\n");
    return ERRCODE_FAIL;
  }

  uint32_t max_len = uapi_upg_get_storage_size();
  if (max_len > 0U && package_len > max_len) {
    printf("[OTA] package_len[%u] > max_len[%u] skip (no enough space)\r\n",
           package_len, max_len);
    return ERRCODE_UPG_NO_ENOUGH_SPACE;
  }

  upg_prepare_info_t prepare_info;
  (void)memset_s(&prepare_info, sizeof(prepare_info), 0, sizeof(prepare_info));
  prepare_info.package_len = package_len;

  errcode_t ret = uapi_upg_prepare(&prepare_info);
  if (ret != ERRCODE_SUCC) {
    printf("[OTA] uapi_upg_prepare(len=%u) ret=0x%x\r\n",
           package_len, (unsigned)ret);
  }
  return ret;
}

/* ---------- UI 进度更新 ---------- */
static void ota_update_ui(void)
{
  if (!ui_service_is_ready()) return;

  const char *status_str = "等待中";
  switch (g_ota_state) {
    case OTA_STATE_WAITING:   status_str = "等待连接"; break;
    case OTA_STATE_RECEIVING: status_str = "接收中";   break;
    case OTA_STATE_VERIFYING: status_str = "校验中";   break;
    case OTA_STATE_UPGRADING: status_str = "重启中";   break;
    case OTA_STATE_FAILED:    status_str = "失败";     break;
    default:                  status_str = "等待中";    break;
  }
  ui_show_ota_progress(g_ota_progress, status_str);
}

/* ---------- 状态管理 ---------- */
static void ota_set_state(ota_state_t s)
{
  g_ota_state = s;
  printf("[OTA] state -> %s\r\n", OTA_STATE_TO_STR(s));
  /* OTA 活跃阶段独占 OLED，IDLE 时释放，避免与 standby/mode 页面交替刷屏 */
  if (s == OTA_STATE_IDLE) {
    ui_service_release();
  } else {
    ui_service_acquire();
  }
  ota_update_ui();
}

static void ota_set_progress(uint8_t pct)
{
  g_ota_progress = pct;
  /* OLED 通过 I2C@400KHz 全屏刷写 ~50ms，刷太频繁会卡死接收 */
  static uint8_t last_ui_pct = 0xFF;
  if (last_ui_pct == 0xFF || pct == 100 || (pct / 10) != (last_ui_pct / 10)) {
    last_ui_pct = pct;
    ota_update_ui();
  }
}

bool ota_service_is_active(void)
{
  return (g_ota_state != OTA_STATE_IDLE && g_ota_state != OTA_STATE_FAILED);
}

void ota_service_get_status(ota_status_t *out)
{
  if (out == NULL) return;
  out->state = g_ota_state;
  out->progress_percent = g_ota_progress;
  out->received_size = g_ota_received;
  out->total_size = g_ota_total;
}

/* ---------- TCP 接收与 UPG 写入 ---------- */

/**
 * @brief 接收固定长度的数据（处理 lwip_recv 可能返回部分数据的情况）
 */
static int recv_all(int sock, uint8_t *buf, int want_len, int timeout_ms)
{
  int received = 0;
  int64_t start = (int64_t)osal_get_jiffies();
  int64_t timeout_ticks = osal_msecs_to_jiffies(timeout_ms);

  while (received < want_len) {
    int64_t now = (int64_t)osal_get_jiffies();
    if ((now - start) > timeout_ticks) {
      return -1; /* 超时 */
    }
    int n = lwip_recv(sock, buf + received, want_len - received, 0);
    if (n < 0) {
      /* lwIP 非阻塞/超时返回负值，短暂等待后重试 */
      osal_msleep(10);
      continue;
    }
    if (n == 0) {
      return received; /* 对端关闭 */
    }
    received += n;
  }
  return received;
}

/**
 * @brief 发送 1 byte ACK
 */
static void tcp_send_ack(int sock, uint8_t ack)
{
  (void)lwip_send(sock, &ack, 1, 0);
}

/**
 * @brief OTA TCP 服务端任务（UDP 触发后创建）
 */
static void *ota_tcp_server_task(const char *arg)
{
  (void)arg;
  int listen_fd = -1;
  int conn_fd = -1;
  uint8_t *recv_buf = NULL;
  errcode_t ret;
  struct sockaddr_in srv_addr = {0};
  struct timeval tv = {0};
  struct sockaddr_in cli_addr = {0};
  socklen_t cli_len = sizeof(cli_addr);
  uint8_t header[8];
  int n = 0;
  uint32_t total_size = 0;
  uint32_t offset = 0;
  int opt = 1;
  int to_recv = 0;
  uint8_t pct = 0;

  printf("[OTA] TCP server task started, port=%d\r\n", OTA_TCP_PORT);

  /* 1. 创建监听 socket */
  listen_fd = lwip_socket(AF_INET, SOCK_STREAM, 0);
  if (listen_fd < 0) {
    printf("[OTA] socket() failed\r\n");
    goto cleanup;
  }

  /* 允许地址复用 */
  lwip_setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

  srv_addr.sin_family = AF_INET;
  srv_addr.sin_port = lwip_htons(OTA_TCP_PORT);
  srv_addr.sin_addr.s_addr = INADDR_ANY;

  if (lwip_bind(listen_fd, (struct sockaddr *)&srv_addr, sizeof(srv_addr)) < 0) {
    printf("[OTA] bind() failed\r\n");
    goto cleanup;
  }

  if (lwip_listen(listen_fd, 1) < 0) {
    printf("[OTA] listen() failed\r\n");
    goto cleanup;
  }

  ota_set_state(OTA_STATE_WAITING);
  printf("[OTA] Listening on port %d...\r\n", OTA_TCP_PORT);

  /* 2. 接受连接（带超时） */
  tv.tv_sec = 30;
  tv.tv_usec = 0;
  lwip_setsockopt(listen_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

  conn_fd = lwip_accept(listen_fd, (struct sockaddr *)&cli_addr, &cli_len);
  if (conn_fd < 0) {
    printf("[OTA] accept() timeout or failed\r\n");
    goto cleanup;
  }
  printf("[OTA] Client connected: %s\r\n", inet_ntoa(cli_addr.sin_addr));

  /* 关闭 Nagle：避免 1 字节 ACK 与小报文被合并延迟 */
  {
    int nodelay = 1;
    (void)lwip_setsockopt(conn_fd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
  }

  /* 3. 接收 8 字节 Header */
  n = recv_all(conn_fd, header, sizeof(header), 10000);
  if (n != sizeof(header)) {
    printf("[OTA] recv header failed, got %d bytes\r\n", n);
    goto cleanup;
  }

  if (memcmp(header, OTA_MAGIC_STR, OTA_MAGIC_LEN) != 0) {
    printf("[OTA] Bad magic: %.4s\r\n", header);
    tcp_send_ack(conn_fd, 0x01);
    goto cleanup;
  }

  total_size = ((uint32_t)header[4] << 24) | ((uint32_t)header[5] << 16) |
               ((uint32_t)header[6] << 8)  | (uint32_t)header[7];

  printf("[OTA] Header OK, firmware size=%u\r\n", total_size);

  if (total_size == 0) {
    printf("[OTA] firmware size=0, abort\r\n");
    tcp_send_ack(conn_fd, 0x01);
    goto cleanup;
  }

  /* 4. UPG 预准备 */
  ret = fota_upg_prepare_once(total_size);
  if (ret != ERRCODE_SUCC) {
    printf("[OTA] UPG prepare failed\r\n");
    tcp_send_ack(conn_fd, 0x01);
    goto cleanup;
  }

  /* 回复 OK */
  tcp_send_ack(conn_fd, 0x00);

  /* 5. 接收固件数据 */
  ota_set_state(OTA_STATE_RECEIVING);
  g_ota_total = total_size;
  g_ota_received = 0;
  ota_set_progress(0);

  recv_buf = (uint8_t *)osal_kmalloc(OTA_RECV_CHUNK_SIZE, OSAL_GFP_ATOMIC);
  if (recv_buf == NULL) {
    printf("[OTA] malloc recv_buf failed\r\n");
    goto cleanup;
  }

  offset = 0;
  while (offset < total_size) {
    /* 喂狗 */
    uapi_watchdog_kick();

    to_recv = (total_size - offset > OTA_RECV_CHUNK_SIZE) ? OTA_RECV_CHUNK_SIZE
                                                          : (int)(total_size - offset);
    n = recv_all(conn_fd, recv_buf, to_recv, 30000);
    if (n <= 0) {
      printf("[OTA] recv data failed at offset=%u, ret=%d\r\n", offset, n);
      goto cleanup;
    }

    /* 写入 UPG */
    ret = uapi_upg_write_package_sync(offset, recv_buf, (uint16_t)n);
    if (ret != ERRCODE_SUCC) {
      printf("[OTA] uapi_upg_write_package_sync failed at offset=%u, ret=0x%x\r\n",
             offset, (unsigned)ret);
      tcp_send_ack(conn_fd, 0x01);
      goto cleanup;
    }

    offset += (uint32_t)n;
    g_ota_received = offset;

    pct = (uint8_t)((offset * 100ULL) / total_size);
    ota_set_progress(pct);

    /* 每 32KB 打印一次日志 */
    if (offset % 32768 == 0 || offset == total_size) {
      printf("[OTA] received %u/%u bytes (%u%%)\r\n", offset, total_size, pct);
    }
  }

  printf("[OTA] All %u bytes received\r\n", offset);

  /* 6. UPG 校验 */
  ota_set_state(OTA_STATE_VERIFYING);
  ret = fota_upg_verify_stored_package();
  if (ret != ERRCODE_SUCC) {
    printf("[OTA] verify failed, abort\r\n");
    tcp_send_ack(conn_fd, 0x01);
    goto cleanup;
  }

  /* 回复最终成功 */
  tcp_send_ack(conn_fd, 0x00);
  osal_msleep(100); /* 确保 ACK 发出去 */

  /* 7. 请求升级并重启 */
  ota_set_state(OTA_STATE_UPGRADING);
  printf("[OTA] Requesting upgrade and reboot...\r\n");
  ret = uapi_upg_request_upgrade(true);
  if (ret != ERRCODE_SUCC) {
    printf("[OTA] uapi_upg_request_upgrade failed, ret=0x%x\r\n", (unsigned)ret);
    goto cleanup;
  }

  /* 正常情况下 request_upgrade(true) 会立即重启，不会执行到这里 */
  printf("[OTA] Reboot should have happened...\r\n");

cleanup:
  if (recv_buf != NULL) {
    osal_kfree(recv_buf);
  }
  if (conn_fd >= 0) {
    lwip_close(conn_fd);
  }
  if (listen_fd >= 0) {
    lwip_close(listen_fd);
  }
  g_tcp_listen_fd = -1;
  g_tcp_conn_fd = -1;
  g_ota_task_handle = NULL;

  if (g_ota_state != OTA_STATE_UPGRADING) {
    ota_set_state(OTA_STATE_FAILED);
    osal_msleep(500);
    ota_set_state(OTA_STATE_IDLE);
  }
  printf("[OTA] TCP server task exited\r\n");
  return NULL;
}

/* ---------- 公共接口 ---------- */

void ota_service_init(void)
{
  if (!g_ota_mutex_inited) {
    osal_mutex_init(&g_ota_mutex);
    g_ota_mutex_inited = true;
  }

  g_ota_state = OTA_STATE_IDLE;
  g_ota_progress = 0;
  g_ota_received = 0;
  g_ota_total = 0;

  printf("[OTA] Init: initializing UPG module...\r\n");
  errcode_t ret = uapi_upg_init(&s_upg_funcs);
  if (ret != ERRCODE_SUCC && ret != ERRCODE_UPG_ALREADY_INIT) {
    printf("[OTA] Init: uapi_upg_init failed, ret=0x%x\r\n", ret);
    return;
  }

  printf("[OTA] Init: resetting upgrade flag...\r\n");
  ret = uapi_upg_reset_upgrade_flag();
  if (ret != ERRCODE_SUCC) {
    printf("[OTA] Init: uapi_upg_reset_upgrade_flag failed, ret=0x%x\r\n", ret);
  }

  printf("[OTA] Init: done\r\n");
}

bool ota_service_start(uint32_t expected_size)
{
  (void)expected_size;

  if (g_ota_state != OTA_STATE_IDLE) {
    printf("[OTA] already active, state=%s\r\n", OTA_STATE_TO_STR(g_ota_state));
    return false;
  }

  ota_set_state(OTA_STATE_IDLE);
  g_ota_progress = 0;
  g_ota_received = 0;
  g_ota_total = 0;

  osal_kthread_lock();
  g_ota_task_handle = osal_kthread_create(
      (osal_kthread_handler)ota_tcp_server_task, NULL,
      "ota_tcp_task", OTA_TCP_STACK_SIZE);
  if (g_ota_task_handle != NULL) {
    osal_kthread_set_priority(g_ota_task_handle, OTA_TCP_TASK_PRIORITY);
  }
  osal_kthread_unlock();

  if (g_ota_task_handle == NULL) {
    printf("[OTA] create task failed\r\n");
    ota_set_state(OTA_STATE_FAILED);
    return false;
  }

  printf("[OTA] task created, waiting for TCP connection...\r\n");
  return true;
}

void ota_service_cancel(void)
{
  printf("[OTA] cancel requested\r\n");
  if (g_tcp_conn_fd >= 0) {
    lwip_close(g_tcp_conn_fd);
    g_tcp_conn_fd = -1;
  }
  if (g_tcp_listen_fd >= 0) {
    lwip_close(g_tcp_listen_fd);
    g_tcp_listen_fd = -1;
  }
  /* 任务会在 socket 关闭后自行退出 */
}
