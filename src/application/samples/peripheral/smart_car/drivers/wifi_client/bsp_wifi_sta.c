/**
 * @file bsp_wifi_sta.c
 * @brief STA Task：常驻，连接 WiFi，成功后阻塞等待断线
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
#include "wifi_hotspot.h"

/* ---------- 状态 ---------- */
static bsp_wifi_status_t s_status = BSP_WIFI_STATUS_IDLE;
static bsp_wifi_mode_t s_mode = BSP_WIFI_MODE_STA;
static char s_ip[16] = "0.0.0.0";

static bsp_wifi_event_cb_t s_event_cb = NULL;
static void *s_event_arg = NULL;

/* ---------- Task 句柄 ---------- */
static osal_task *s_sta_task = NULL;

/* ---------- 当前 WiFi 配置 ---------- */
static char s_ssid[33] = {0};
static char s_pwd[64] = {0};

/* ---------- 信号量 ---------- */
static osal_semaphore s_conn_sem;
static osal_semaphore s_wait_sem;
static bool s_wait_sem_inited = false;
static osal_semaphore s_sync_sem;
static bool s_sync_inited = false;
static volatile bool s_should_exit = false;

/* ---------- 公共状态管理 ---------- */

/** @brief 获取当前 WiFi 状态 */
bsp_wifi_status_t bsp_wifi_get_status(void)
{
    return s_status;
}

/** @brief 获取当前 WiFi 模式（STA/AP） */
bsp_wifi_mode_t bsp_wifi_get_mode(void)
{
    return s_mode;
}

/** @brief 设置 WiFi 状态 */
void bsp_wifi_set_status(bsp_wifi_status_t st)
{
    s_status = st;
}

/** @brief 设置 WiFi 模式 */
void bsp_wifi_set_mode(bsp_wifi_mode_t m)
{
    s_mode = m;
}

/** @brief 获取本机 IP 地址字符串 */
int bsp_wifi_get_ip(char *ip_str, uint32_t len)
{
    if (!ip_str || len < 16)
        return -1;
    const char *ifname = (s_mode == BSP_WIFI_MODE_AP) ? "ap0" : "wlan0";
    struct netif *n = netifapi_netif_find(ifname);
    if (n && n->ip_addr.u_addr.ip4.addr != 0) {
        uint32_t a = n->ip_addr.u_addr.ip4.addr;
        snprintf(ip_str, len, "%u.%u.%u.%u", (unsigned)(a & 0xFF), (unsigned)((a >> 8) & 0xFF),
                 (unsigned)((a >> 16) & 0xFF), (unsigned)((a >> 24) & 0xFF));
    } else {
        strncpy_s(ip_str, len, s_ip, strlen(s_ip));
    }
    return 0;
}

/* ---------- 事件回调 ---------- */

/** @brief 注册 WiFi 事件回调函数 */
void bsp_wifi_register_event_cb(bsp_wifi_event_cb_t cb, void *arg)
{
    s_event_cb = cb;
    s_event_arg = arg;
}

/** @brief 通知 WiFi 事件（同步信号量用于阻塞连接） */
void bsp_wifi_notify_event(bsp_wifi_event_t event)
{
    if (s_event_cb) {
        s_event_cb(event, s_event_arg);
    }

    if (s_sync_inited && (event == BSP_WIFI_EVENT_STA_GOT_IP || event == BSP_WIFI_EVENT_STA_FAIL)) {
        osal_sem_up(&s_sync_sem);
    }
}

/* ---------- 配置管理 ---------- */

/** @brief 设置要连接的 WiFi SSID 和密码 */
void bsp_wifi_set_current_config(const char *ssid, const char *pwd)
{
    if (ssid)
        strncpy_s(s_ssid, sizeof(s_ssid), ssid, sizeof(s_ssid) - 1);
    if (pwd)
        strncpy_s(s_pwd, sizeof(s_pwd), pwd, sizeof(s_pwd) - 1);
}

/** @brief 获取当前配置的 WiFi SSID 和密码 */
void bsp_wifi_get_current_config(char *ssid, uint32_t ssid_len, char *pwd, uint32_t pwd_len)
{
    if (ssid && ssid_len > 0) {
        strncpy_s(ssid, ssid_len, s_ssid, ssid_len - 1);
    }
    if (pwd && pwd_len > 0) {
        strncpy_s(pwd, pwd_len, s_pwd, pwd_len - 1);
    }
}

/* ---------- 同步阻塞连接 ---------- */

/** @brief 同步阻塞方式连接指定 WiFi，超时返回 */
int bsp_wifi_connect_sync(const char *ssid, const char *pwd)
{
    if (!s_sync_inited) {
        osal_sem_binary_sem_init(&s_sync_sem, 0);
        s_sync_inited = true;
    }

    /* 清除旧信号，防止误触发 */
    while (osal_sem_trydown(&s_sync_sem) == OSAL_SUCCESS) {
    }

    bsp_wifi_set_current_config(ssid, pwd);

    if (s_sta_task) {
        sta_task_wakeup();
    } else {
        if (!sta_task_start())
            return -1;
    }

    int ret = osal_sem_down_timeout(&s_sync_sem, 20000);
    return (ret == OSAL_SUCCESS && bsp_wifi_get_status() == BSP_WIFI_STATUS_GOT_IP) ? 0 : -1;
}

/* ---------- 扫描 ---------- */

/** @brief 扫描周围 WiFi 并填充结果列表 */
int bsp_wifi_scan_list(bsp_wifi_scan_item_t *items, uint32_t max_count, uint32_t *out_count)
{
    if (!items || !out_count || !max_count)
        return -1;
    *out_count = 0;

    bool was_on = (wifi_is_sta_enabled() == 1);
    if (!was_on && wifi_sta_enable() != ERRCODE_SUCC)
        return -1;

    uint32_t num = 32;
    wifi_scan_info_stru *res = osal_kmalloc(sizeof(wifi_scan_info_stru) * num, OSAL_GFP_ATOMIC);
    if (!res) {
        if (!was_on)
            wifi_sta_disable();
        return -1;
    }

    if (wifi_sta_scan() != ERRCODE_SUCC) {
        osal_kfree(res);
        if (!was_on)
            wifi_sta_disable();
        return -1;
    }

    osal_msleep(2500);

    errcode_t ret = wifi_sta_get_scan_info(res, &num);
    if (!was_on)
        wifi_sta_disable();
    if (ret != ERRCODE_SUCC) {
        osal_kfree(res);
        return -1;
    }

    uint32_t idx = 0;
    for (uint32_t i = 0; i < num && idx < max_count; i++) {
        if (res[i].ssid[0] == 0)
            continue;
        memset_s(&items[idx], sizeof(items[idx]), 0, sizeof(items[idx]));
        strncpy_s(items[idx].ssid, sizeof(items[idx].ssid), res[i].ssid, sizeof(items[idx].ssid) - 1);
        items[idx].rssi = (int8_t)res[i].rssi;
        items[idx].security = (uint8_t)res[i].security_type;
        items[idx].channel = (uint8_t)res[i].channel_num;
        idx++;
    }
    *out_count = idx;
    osal_kfree(res);
    return 0;
}

/* ---------- Task 控制 ---------- */

/** @brief 设置 STA 任务退出标志 */
void sta_set_should_exit(bool v)
{
    s_should_exit = v;
}

/** @brief 获取 STA 任务退出标志 */
bool sta_get_should_exit(void)
{
    return s_should_exit;
}

/** @brief 唤醒 STA 任务（用于断线后重连） */
void sta_task_wakeup(void)
{
    osal_sem_up(&s_wait_sem);
}

/** @brief 启动 STA（WiFi 客户端）任务 */
bool sta_task_start(void)
{
    if (s_sta_task)
        return true;
    s_should_exit = false;
    osal_kthread_lock();
    s_sta_task = osal_kthread_create((osal_kthread_handler)sta_task_main, NULL, "wifi_sta", 4096);
    if (s_sta_task)
        osal_kthread_set_priority(s_sta_task, 24);
    osal_kthread_unlock();
    if (!s_sta_task) {
        printf("[STA] task 创建失败\r\n");
        return false;
    }
    printf("[STA] task 启动\r\n");
    return true;
}

/** @brief 停止 STA 任务 */
void sta_task_stop(void)
{
    if (!s_sta_task)
        return;
    printf("[STA] 停止 task\r\n");
    s_should_exit = true;
    if (s_wait_sem_inited) {
        osal_sem_up(&s_wait_sem);
    }
    int to = 0;
    while (s_sta_task && to < 100) {
        osal_msleep(10);
        to++;
    }
    s_sta_task = NULL;
}

/* ---------- 内部回调 ---------- */

/** @brief WiFi SDK 连接状态变化回调 */
static void wifi_cb(td_s32 state, const wifi_linked_info_stru *info, td_s32 reason);

/** @brief WiFi SDK 扫描完成回调 */
static void scan_cb(td_s32 state, td_s32 size);

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
        osal_sem_up(&s_wait_sem);
    }
}

/** @brief 扫描完成后通知 STA 任务继续 */
static void scan_cb(td_s32 state, td_s32 size)
{
    (void)state;
    (void)size;
    osal_sem_up(&s_conn_sem);
}

/* ---------- Task 入口 ---------- */

/** @brief STA 任务主函数：扫描→连接→DHCP→阻塞等待断线→重连循环 */
int sta_task_main(void *arg)
{
    (void)arg;

    osal_sem_binary_sem_init(&s_conn_sem, 0);
    osal_sem_binary_sem_init(&s_wait_sem, 0);
    s_wait_sem_inited = true;

    static wifi_event_stru evt = {
        .wifi_event_connection_changed = wifi_cb,
        .wifi_event_scan_state_changed = scan_cb,
    };
    wifi_register_event_cb(&evt);

    if (wifi_sta_enable() != ERRCODE_SUCC) {
        printf("[STA] 启用失败\r\n");
        bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
        bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_STOPPED);
        s_sta_task = NULL;
        return 0;
    }

    while (!s_should_exit) {
        char ssid[32] = {0}, pwd[64] = {0};
        bsp_wifi_get_current_config(ssid, sizeof(ssid), pwd, sizeof(pwd));

        if (ssid[0] == '\0') {
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            osal_msleep(5000);
            continue;
        }

        /* 扫描 */
        if (wifi_sta_scan() != ERRCODE_SUCC) {
            osal_msleep(5000);
            continue;
        }
        if (osal_sem_down_timeout(&s_conn_sem, 5000) != OSAL_SUCCESS) {
            osal_msleep(5000);
            continue;
        }

        uint32_t num = 32;
        wifi_scan_info_stru *res = osal_kmalloc(sizeof(wifi_scan_info_stru) * num, OSAL_GFP_ATOMIC);
        if (!res) {
            osal_msleep(5000);
            continue;
        }
        if (wifi_sta_get_scan_info(res, &num) != ERRCODE_SUCC) {
            osal_kfree(res);
            osal_msleep(5000);
            continue;
        }

        bool found = false;
        wifi_sta_config_stru cfg = {0};
        for (uint32_t i = 0; i < num; i++) {
            if (strcmp(res[i].ssid, ssid) == 0) {
                memcpy_s(cfg.ssid, WIFI_MAX_SSID_LEN, res[i].ssid, WIFI_MAX_SSID_LEN);
                memcpy_s(cfg.bssid, WIFI_MAC_LEN, res[i].bssid, WIFI_MAC_LEN);
                cfg.security_type = res[i].security_type;
                memcpy_s(cfg.pre_shared_key, WIFI_MAX_KEY_LEN, pwd, strlen(pwd));
                cfg.ip_type = DHCP;
                found = true;
                break;
            }
        }
        osal_kfree(res);
        if (!found) {
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            osal_msleep(5000);
            continue;
        }

        /* 连接 */
        bsp_wifi_set_status(BSP_WIFI_STATUS_CONNECTING);
        while (osal_sem_trydown(&s_conn_sem) == OSAL_SUCCESS) {
        }

        if (wifi_sta_connect(&cfg) != ERRCODE_SUCC) {
            printf("[STA] wifi_sta_connect 失败\r\n");
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            osal_msleep(5000);
            continue;
        }

        if (osal_sem_down_timeout(&s_conn_sem, 10000) != OSAL_SUCCESS) {
            printf("[STA] 连接等待超时\r\n");
            wifi_sta_disconnect();
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            osal_msleep(5000);
            continue;
        }

        if (bsp_wifi_get_status() != BSP_WIFI_STATUS_CONNECTED) {
            printf("[STA] 连接后状态未达标\r\n");
            wifi_sta_disconnect();
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            osal_msleep(5000);
            continue;
        }

        /* DHCP */
        struct netif *n = netifapi_netif_find("wlan0");
        if (n) {
            netifapi_dhcp_start(n);
            for (int i = 0; i < 50 && !s_should_exit; i++) {
                if (n->ip_addr.u_addr.ip4.addr != 0) {
                    netifapi_netif_set_default(n);
                    bsp_wifi_set_status(BSP_WIFI_STATUS_GOT_IP);
                    bsp_wifi_set_mode(BSP_WIFI_MODE_STA);
                    break;
                }
                osal_msleep(100);
            }
        }

        if (bsp_wifi_get_status() != BSP_WIFI_STATUS_GOT_IP) {
            if (n)
                netifapi_dhcp_stop(n);
            wifi_sta_disconnect();
            bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_FAIL);
            osal_msleep(5000);
            continue;
        }

        /* 成功 */
        printf("[STA] 已连接\r\n");
        bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_GOT_IP);

        /* 阻塞等待断线 */
        osal_sem_down(&s_wait_sem);
        if (s_should_exit)
            break;
        printf("[STA] 断线，重连\r\n");
        wifi_sta_disconnect();
    }

    wifi_sta_disable();
    bsp_wifi_notify_event(BSP_WIFI_EVENT_STA_STOPPED);
    printf("[STA] 退出\r\n");
    s_sta_task = NULL;
    return 0;
}
