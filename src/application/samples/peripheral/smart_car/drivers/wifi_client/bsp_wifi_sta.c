/**
 * @file bsp_wifi_sta.c
 * @brief STA Task：常驻挂起、高响应扫描版
 */

#include "bsp_wifi_sta.h"
#include <stdio.h>
#include <string.h>
#include "lwip/netifapi.h"
#include "securec.h"
#include "soc_osal.h"
#include "td_type.h"
#include "wifi_device.h"
#include "wifi_event.h"

static bsp_wifi_status_t s_status = BSP_WIFI_STATUS_IDLE; // WiFi 当前连接状态
static bsp_wifi_mode_t s_mode = BSP_WIFI_MODE_STA;        // WiFi 工作模式（STA/AP）

static bsp_wifi_event_cb_t s_event_cb = NULL; // WiFi 事件回调函数指针
static void *s_event_arg = NULL;              // 事件回调用户参数
static osal_task *s_sta_task = NULL;

static char s_ssid[33] = {0};
static char s_pwd[64] = {0};
static void bsp_wifi_get_current_config(char *ssid, uint32_t ssid_len, char *pwd, uint32_t pwd_len);

static osal_semaphore s_conn_sem;      // WiFi 连接/断开/扫描完成通知信号量
osal_semaphore s_wait_sem;             // 关键挂起信号量，通过 bsp_wifi_sta_wakeup() 唤醒
static bool s_wait_sem_inited = false; // wait 信号量初始化标志
static osal_semaphore s_sync_sem;      // 同步阻塞连接信号量
static bool s_sync_inited = false;     // sync 信号量初始化标志
static volatile bool s_should_exit = false;
static int sta_task_main(void *arg);
static osal_semaphore s_exit_sem;      // STA 任务退出确认信号量
static bool s_exit_sem_inited = false; // exit 信号量初始化标志
static osal_semaphore s_dhcp_sem;      // DHCP 地址获取完成信号量
static bool s_dhcp_sem_inited = false; // dhcp 信号量初始化标志

/* 获取当前 WiFi 连接状态 */
bsp_wifi_status_t bsp_wifi_get_status(void)
{
    return s_status;
}
/* 获取当前 WiFi 工作模式（STA/AP） */
bsp_wifi_mode_t bsp_wifi_get_mode(void)
{
    return s_mode;
}
/* 设置 WiFi 连接状态 */
void bsp_wifi_set_status(bsp_wifi_status_t st)
{
    s_status = st;
}
/* 设置 WiFi 工作模式 */
void bsp_wifi_set_mode(bsp_wifi_mode_t m)
{
    s_mode = m;
}

/* 获取当前 WiFi 接口的 IP 地址字符串 */
int bsp_wifi_get_ip(char *ip_str, uint32_t len)
{
    if (!ip_str || len < 16)
        return -1;
    if (s_mode == BSP_WIFI_MODE_AP) {
        strncpy_s(ip_str, len, "192.168.1.1", 11);
        return 0;
    }
    struct netif *n = netifapi_netif_find("wlan0");
    if (n && n->ip_addr.u_addr.ip4.addr != 0) {
        uint32_t a = n->ip_addr.u_addr.ip4.addr;
        snprintf(ip_str, len, "%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
                 (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
    } else {
        strncpy_s(ip_str, len, "0.0.0.0", 7);
    }
    return 0;
}

/* 注册 WiFi 事件回调函数 */
void bsp_wifi_register_event_cb(bsp_wifi_event_cb_t cb, void *arg)
{
    s_event_cb = cb;
    s_event_arg = arg;
}

/* 通知已注册的事件回调，并在连接成功/失败时释放同步信号量 */
void bsp_wifi_notify_event(bsp_wifi_event_t event)
{
    if (s_event_cb) {
        s_event_cb(event, s_event_arg);
    }
    if (s_sync_inited && (event == BSP_WIFI_EVENT_STA_GOT_IP || event == BSP_WIFI_EVENT_STA_FAIL)) {
        osal_sem_up(&s_sync_sem);
    }
}

/* 设置当前 WiFi 连接的 SSID 和密码 */
void bsp_wifi_set_current_config(const char *ssid, const char *pwd)
{
    if (ssid)
        strncpy_s(s_ssid, sizeof(s_ssid), ssid, sizeof(s_ssid) - 1);
    if (pwd)
        strncpy_s(s_pwd, sizeof(s_pwd), pwd, sizeof(s_pwd) - 1);
}

/* 获取当前存储的 WiFi SSID 和密码 */
static void bsp_wifi_get_current_config(char *ssid, uint32_t ssid_len, char *pwd, uint32_t pwd_len)
{
    if (ssid && ssid_len > 0)
        strncpy_s(ssid, ssid_len, s_ssid, ssid_len - 1);
    if (pwd && pwd_len > 0)
        strncpy_s(pwd, pwd_len, s_pwd, pwd_len - 1);
}

// 同步阻塞连接接口
int bsp_wifi_connect_sync(const char *ssid, const char *pwd)
{
    if (!s_sync_inited) {
        osal_sem_binary_sem_init(&s_sync_sem, 0);
        s_sync_inited = true;
    }
    while (osal_sem_trydown(&s_sync_sem) == OSAL_SUCCESS) {
    }

    bsp_wifi_set_current_config(ssid, pwd);

    if (s_sta_task)
        bsp_wifi_sta_wakeup();
    else if (!sta_task_start())
        return -1;

    int ret = osal_sem_down_timeout(&s_sync_sem, 20000);
    return (ret == OSAL_SUCCESS && bsp_wifi_get_status() == BSP_WIFI_STATUS_GOT_IP) ? 0 : -1;
}

// 极其高效的扫描：因为 STA 驱动在初始化阶段就已经使能且永远不关，这里直接极速进行扫描，绝不会影响 AP 连接
int bsp_wifi_scan_list(bsp_wifi_scan_item_t *items, uint32_t max_count, uint32_t *out_count)
{
    if (!items || !out_count || !max_count)
        return -1;
    *out_count = 0;

    uint32_t num = 32;
    static wifi_scan_info_stru s_scan_buf[32];
    if (wifi_sta_scan() != ERRCODE_SUCC) { // 直接发起扫描
        return -1;
    }

    osal_msleep(2500); // 挂起当前线程等待硬件扫描完毕

    errcode_t ret = wifi_sta_get_scan_info(s_scan_buf, &num);
    if (ret != ERRCODE_SUCC)
        return -1;

    uint32_t idx = 0;
    for (uint32_t i = 0; i < num && idx < max_count; i++) {
        if (s_scan_buf[i].ssid[0] == 0)
            continue;
        memset_s(&items[idx], sizeof(items[idx]), 0, sizeof(items[idx]));
        strncpy_s(items[idx].ssid, sizeof(items[idx].ssid), s_scan_buf[i].ssid, sizeof(items[idx].ssid) - 1);
        items[idx].rssi = (int8_t)s_scan_buf[i].rssi;
        items[idx].security = (uint8_t)s_scan_buf[i].security_type;
        items[idx].channel = (uint8_t)s_scan_buf[i].channel_num;
        idx++;
    }
    *out_count = idx;
    return 0;
}

/* 启动 STA 常驻任务 */
bool sta_task_start(void)
{
    if (s_sta_task)
        return true;
    s_should_exit = false;
    if (!s_exit_sem_inited) {
        osal_sem_binary_sem_init(&s_exit_sem, 0);
        s_exit_sem_inited = true;
    }
    while (osal_sem_trydown(&s_exit_sem) == OSAL_SUCCESS) {
    }

    // 信号量在 task 创建前初始化，确保外部 osal_sem_up 不会因未初始化丢失信号
    if (!s_wait_sem_inited) {
        osal_sem_binary_sem_init(&s_wait_sem, 0);
        s_wait_sem_inited = true;
    }

    osal_kthread_lock();
    s_sta_task = osal_kthread_create((osal_kthread_handler)sta_task_main, NULL, "wifi_sta", 4096);
    if (s_sta_task)
        osal_kthread_set_priority(s_sta_task, 24);
    osal_kthread_unlock();
    return s_sta_task ? true : false;
}

/* 停止 STA 任务并等待退出 */
void sta_task_stop(void)
{
    if (!s_sta_task)
        return;
    s_should_exit = true;
    bsp_wifi_sta_wakeup();
    (void)osal_sem_down_timeout(&s_exit_sem, 3000);
    s_sta_task = NULL;
}

/* 唤醒 STA 任务（触发连接或断开重连） */
void bsp_wifi_sta_wakeup(void)
{
    if (s_wait_sem_inited)
        osal_sem_up(&s_wait_sem);
}

/* WiFi 连接状态变化回调：更新状态并释放连接信号量 */
static void wifi_cb(td_s32 state, const wifi_linked_info_stru *info, td_s32 reason)
{
    (void)info;
    (void)reason;
    if (state == WIFI_STATE_AVALIABLE) {
        bsp_wifi_set_status(BSP_WIFI_STATUS_CONNECTED);
        osal_sem_up(&s_conn_sem);
    } else if (state == WIFI_STATE_NOT_AVALIABLE) {
        bsp_wifi_set_status(BSP_WIFI_STATUS_DISCONNECTED);
        osal_sem_up(&s_conn_sem);
    }
}

/* WiFi 扫描完成回调：释放连接信号量 */
static void scan_cb(td_s32 state, td_s32 size)
{
    (void)state;
    (void)size;
    osal_sem_up(&s_conn_sem);
}

/* 网络接口状态回调：获取到 IP 时释放 DHCP 信号量 */
static void sta_netif_status_cb(struct netif *netif)
{
    if (netif && netif->ip_addr.u_addr.ip4.addr != 0 && s_dhcp_sem_inited) {
        osal_sem_up(&s_dhcp_sem);
    }
}

// 常驻核心线程：一键启动后不再频繁创建销毁，不连接时安静挂起不占用任何射频
static int sta_task_main(void *arg)
{
    (void)arg;
    osal_sem_binary_sem_init(&s_conn_sem, 0);

    static wifi_event_stru evt = {
        .wifi_event_connection_changed = wifi_cb,
        .wifi_event_scan_state_changed = scan_cb,
    };
    wifi_register_event_cb(&evt);

    // 启动时使能一次驱动即可，此后 AP 与 STA 共存，绝不频繁调用 wifi_sta_disable()
    if (wifi_sta_enable() != ERRCODE_SUCC) {
        printf("[STA] 使能底层 STA 失败\r\n");
        bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
        if (s_exit_sem_inited)
            osal_sem_up(&s_exit_sem);
        return 0;
    }

    printf("[STA] 线程启动并使能底层驱动，进入待命状态...\r\n");

    while (!s_should_exit) {
        // 1. 无事可做时在此彻底阻塞挂载，此时 STA 零开销、零射频干扰
        (void)osal_sem_down(&s_wait_sem);
        if (s_should_exit)
            break;

        char ssid[32] = {0}, pwd[64] = {0};
        bsp_wifi_get_current_config(ssid, sizeof(ssid), pwd, sizeof(pwd));
        if (ssid[0] == '\0') {
            continue; // 无 WiFi 配置，继续挂载待命
        }

        printf("[STA] 线程被唤醒，开始尝试连接: %s\r\n", ssid);

        // 变量统一前置，避免 goto 跳过初始化导致 -Werror
        uint32_t num = 32;
        wifi_sta_config_stru cfg = {0};
        bool found = false;
        struct netif *n = NULL;
        static wifi_scan_info_stru s_conn_scan_buf[32];

        // 2. 扫描附近网络（AP 运行时首次可能返回 0，重试一次）
        for (int scan_retry = 0; scan_retry < 2; scan_retry++) {
            if (wifi_sta_scan() != ERRCODE_SUCC)
                goto connect_fail;
            if (osal_sem_down_timeout(&s_conn_sem, 5000) != OSAL_SUCCESS)
                goto connect_fail;
            num = 32;
            if (wifi_sta_get_scan_info(s_conn_scan_buf, &num) != ERRCODE_SUCC)
                goto connect_fail;
            if (num > 0)
                break;
            printf("[STA] 扫描结果为空，重试...\r\n");
        }
        for (uint32_t i = 0; i < num; i++) {
            if (strcmp(s_conn_scan_buf[i].ssid, ssid) == 0) {
                memcpy_s(cfg.ssid, WIFI_MAX_SSID_LEN, s_conn_scan_buf[i].ssid, WIFI_MAX_SSID_LEN);
                memcpy_s(cfg.bssid, WIFI_MAC_LEN, s_conn_scan_buf[i].bssid, WIFI_MAC_LEN);
                cfg.security_type = s_conn_scan_buf[i].security_type;
                memcpy_s(cfg.pre_shared_key, WIFI_MAX_KEY_LEN, pwd, strlen(pwd));
                cfg.ip_type = DHCP;
                found = true;
                break;
            }
        }
        if (!found) {
            printf("[STA] 附近未扫描到目标 WiFi: %s\r\n", ssid);
            goto connect_fail;
        }

        // 3. 连接目标 AP
        bsp_wifi_set_status(BSP_WIFI_STATUS_CONNECTING);
        if (wifi_sta_connect(&cfg) != ERRCODE_SUCC)
            goto connect_fail;
        if (osal_sem_down_timeout(&s_conn_sem, 10000) != OSAL_SUCCESS) {
            wifi_sta_disconnect();
            goto connect_fail;
        }

        if (bsp_wifi_get_status() != BSP_WIFI_STATUS_CONNECTED) {
            wifi_sta_disconnect();
            goto connect_fail;
        }

        // 4. 获取 IP (DHCP)
        n = netifapi_netif_find("wlan0");
        if (n) {
            if (!s_dhcp_sem_inited) {
                osal_sem_binary_sem_init(&s_dhcp_sem, 0);
                s_dhcp_sem_inited = true;
            }
            while (osal_sem_trydown(&s_dhcp_sem) == OSAL_SUCCESS) {
            }
            netif_set_status_callback(n, sta_netif_status_cb);
            netifapi_dhcp_start(n);
            if (n->ip_addr.u_addr.ip4.addr == 0) {
                (void)osal_sem_down_timeout(&s_dhcp_sem, 5000);
            }
            netif_set_status_callback(n, NULL);
            if (!s_should_exit && n->ip_addr.u_addr.ip4.addr != 0) {
                netifapi_netif_set_default(n);
                bsp_wifi_set_status(BSP_WIFI_STATUS_GOT_IP);
                bsp_wifi_set_mode(BSP_WIFI_MODE_STA);
            }
        }

        if (bsp_wifi_get_status() != BSP_WIFI_STATUS_GOT_IP) {
            if (n)
                netifapi_dhcp_stop(n);
            wifi_sta_disconnect();
            goto connect_fail;
        }

        printf("[STA] 连接成功，成功获取 IP\r\n");
        bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_GOT_IP);

        // 5. 保持连接并阻塞。被唤醒（断线或切配置）后断旧连接，自唤醒一次读新配置
        (void)osal_sem_down(&s_wait_sem);
        if (!s_should_exit) {
            printf("[STA] 断开当前连接\r\n");
            if (n)
                netifapi_dhcp_stop(n);
            wifi_sta_disconnect();
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            while (osal_sem_trydown(&s_wait_sem) == OSAL_SUCCESS) {
            }
            bsp_wifi_sta_wakeup();
        }
        continue;

    connect_fail:
        // 连接失败：通知 Manager，然后回到最上方阻塞在 `s_wait_sem` 上继续待命
        bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
    }

    wifi_sta_disable();
    bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_STOPPED);
    if (s_exit_sem_inited)
        osal_sem_up(&s_exit_sem);
    return 0;
}