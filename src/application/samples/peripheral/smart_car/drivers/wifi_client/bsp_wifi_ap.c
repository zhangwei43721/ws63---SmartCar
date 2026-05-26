/**
 * @file bsp_wifi_ap.c
 * @brief AP Task：启动热点，事件驱动退出
 */

#include "bsp_wifi_ap.h"
#include "bsp_wifi_sta.h"

#include <stdio.h>
#include <string.h>

#include "lwip/netifapi.h"
#include "securec.h"
#include "soc_osal.h"
#include "wifi_hotspot.h"

static volatile bool s_should_exit = false;
static osal_task *s_ap_task = NULL;
// 退出同步：任务结束 up，stop 端 down
static osal_semaphore s_ap_exit_sem;
static bool s_ap_exit_sem_inited = false;
// 唤醒主循环（替代 msleep(500) 轮询）
static osal_semaphore s_ap_wake_sem;
static bool s_ap_wake_sem_inited = false;

void ap_set_should_exit(bool v)
{
    s_should_exit = v;
}

bool ap_task_start(void)
{
    if (s_ap_task)
        return true;
    s_should_exit = false;
    if (!s_ap_exit_sem_inited) {
        osal_sem_binary_sem_init(&s_ap_exit_sem, 0);
        s_ap_exit_sem_inited = true;
    }
    if (!s_ap_wake_sem_inited) {
        osal_sem_binary_sem_init(&s_ap_wake_sem, 0);
        s_ap_wake_sem_inited = true;
    }
    while (osal_sem_trydown(&s_ap_exit_sem) == OSAL_SUCCESS) { }
    while (osal_sem_trydown(&s_ap_wake_sem) == OSAL_SUCCESS) { }

    osal_kthread_lock();
    s_ap_task = osal_kthread_create((osal_kthread_handler)ap_task_main, NULL, "wifi_ap", 4096);
    if (s_ap_task)
        osal_kthread_set_priority(s_ap_task, 24);
    osal_kthread_unlock();
    if (!s_ap_task) {
        printf("[AP] task 创建失败\r\n");
        return false;
    }
    printf("[AP] task 启动\r\n");
    return true;
}

void ap_task_stop(void)
{
    if (!s_ap_task)
        return;
    printf("[AP] 停止 task\r\n");
    s_should_exit = true;
    if (s_ap_wake_sem_inited) {
        osal_sem_up(&s_ap_wake_sem);
    }
    if (s_ap_exit_sem_inited) {
        (void)osal_sem_down_timeout(&s_ap_exit_sem, 2000);
    }
    s_ap_task = NULL;
}

int ap_task_main(void *arg)
{
    (void)arg;
    printf("[AP] 启动\r\n");

    wifi_softap_disable();
    struct netif *ap = netifapi_netif_find("ap0");
    if (ap)
        netifapi_dhcps_stop(ap);

    softap_config_stru conf = {0};
    memcpy_s(conf.ssid, sizeof(conf.ssid), BSP_WIFI_AP_SSID, strlen(BSP_WIFI_AP_SSID));
    memcpy_s(conf.pre_shared_key, WIFI_MAX_KEY_LEN, BSP_WIFI_AP_PASSWORD, strlen(BSP_WIFI_AP_PASSWORD));
    conf.security_type = (strlen(BSP_WIFI_AP_PASSWORD) == 0) ? 0 : 3;
    conf.channel_num = BSP_WIFI_AP_CHANNEL;

    softap_config_advance_stru adv = {0};
    adv.beacon_interval = 100;
    adv.protocol_mode = 4;
    wifi_set_softap_config_advance(&adv);

    if (wifi_softap_enable(&conf) != 0) {
        printf("[AP] 启动失败\r\n");
        bsp_wifi_notify_event(BSP_WIFI_EVENT_AP_STOPPED);
        if (s_ap_exit_sem_inited) osal_sem_up(&s_ap_exit_sem);
        return 0;
    }

    ap = netifapi_netif_find("ap0");
    if (ap) {
        ip4_addr_t ip, mask, gw;
        IP4_ADDR(&ip, 192, 168, 1, 1);
        IP4_ADDR(&mask, 255, 255, 255, 0);
        IP4_ADDR(&gw, 192, 168, 1, 1);
        netifapi_netif_set_addr(ap, &ip, &mask, &gw);
        netifapi_dhcps_start(ap, NULL, 0);
        netifapi_netif_set_default(ap);
        bsp_wifi_set_status(BSP_WIFI_STATUS_GOT_IP);
        bsp_wifi_set_mode(BSP_WIFI_MODE_AP);
        printf("[AP] IP=192.168.1.1\r\n");
    }

    bsp_wifi_notify_event(BSP_WIFI_EVENT_AP_READY);

    // 阻塞等待 stop 信号，不再 msleep 轮询
    while (!s_should_exit) {
        (void)osal_sem_down(&s_ap_wake_sem);
    }

    wifi_softap_disable();
    ap = netifapi_netif_find("ap0");
    if (ap)
        netifapi_dhcps_stop(ap);

    bsp_wifi_notify_event(BSP_WIFI_EVENT_AP_STOPPED);
    printf("[AP] 退出\r\n");
    if (s_ap_exit_sem_inited) {
        osal_sem_up(&s_ap_exit_sem);
    }
    return 0;
}
