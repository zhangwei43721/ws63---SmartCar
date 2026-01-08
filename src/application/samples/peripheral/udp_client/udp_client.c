/**
 ****************************************************************************************************
 * @file        udp_client.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-10
 * @brief       WIFI UDP实验
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:Hi3863
 * 在线视频:
 * 公司网址:
 * 购买地址:
 *
 ****************************************************************************************************
 * 实验现象：开发板连接路由器热点，打开网络调试助手，选择UDP协议连接，即可与开发板进行UDP通信
 *
 ****************************************************************************************************
 */

#include "lwip/nettool/misc.h"
#include "lwip/ip4_addr.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/sockets.h"

#include "cmsis_os2.h"
#include "app_init.h"
#include "soc_osal.h"
#include "./wifi/wifi_connect.h"

#define UDP_CLIENT_TASK_PRIO                  (osPriority_t)(13)
#define UDP_CLIENT_TASK_DURATION_MS           2000
#define UDP_CLIENT_TASK_STACK_SIZE            0x1000

#define CONFIG_WIFI_SSID            "jack"                              // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD             "12345678"                        // 要连接的WiFi 热点密码
#define CONFIG_SERVER_IP            "192.168.246.65"                     // 要连接的服务器IP
#define CONFIG_SERVER_PORT          8888                                // 要连接的服务器端口

static const char *send_data = "Hello! I'm UDP Client!\r\n";

int udp_client_sample_task(void *param)
{
    param = param;

    // 服务器地址信息
    struct sockaddr_in send_addr;
    socklen_t addr_length = sizeof(send_addr);
    char recvBuf[512];

    // 连接wifi
    wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);

    // 创建socket套接字
    int sock_fd;
    printf("create socket start!\r\n");
    if((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        printf("create socket failed!\r\n");
        return 0;
    }
    printf("create socket end!\r\n");

    // 初始化连接服务器地址
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(CONFIG_SERVER_PORT);
    send_addr.sin_addr.s_addr = inet_addr(CONFIG_SERVER_IP);
    addr_length = sizeof(send_addr);

    while(1)
    {
        bzero(recvBuf, sizeof(recvBuf));

        // 发送数据到服务端
        printf("sendto start!\r\n");
        sendto(sock_fd, send_data, strlen(send_data), 0, (struct sockaddr *)&send_addr, addr_length);
        printf("sendto end!\r\n");
        // 线程休眠一段时间
        osDelay(100);

        // 接收服务器返回的字符串
        recvfrom(sock_fd, recvBuf, sizeof(recvBuf), 0, (struct sockaddr *)&send_addr, &addr_length);
        printf("%s:%d=>%s\n", inet_ntoa(send_addr.sin_addr), ntohs(send_addr.sin_port), recvBuf);
    }

    // 关闭socket
    lwip_close(sock_fd);
    return 0;
}

static void udp_client_sample_entry(void)
{
    osThreadAttr_t attr;
    attr.name       = "udp_client_sample_task";
    attr.attr_bits  = 0U;
    attr.cb_mem     = NULL;
    attr.cb_size    = 0U;
    attr.stack_mem  = NULL;
    attr.stack_size = UDP_CLIENT_TASK_STACK_SIZE;
    attr.priority   = UDP_CLIENT_TASK_PRIO;
    if (osThreadNew((osThreadFunc_t)udp_client_sample_task, NULL, &attr) == NULL) {
        printf("Create udp_client_sample_task fail.\r\n");
    }
    printf("Create udp_client_sample_task succ.\r\n");
}

/* Run the udp_client_sample_task. */
app_run(udp_client_sample_entry);
