 /****************************************************************************************************
 * @file        template.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-11
 * @brief       WIFI MQTT实验
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
 * 实验现象：开发板连接路由器热点，打开通信猫MQTT调试客户端和串口调试助手，连接好服务器后设置正确的订阅和发布
 *          主题即可收到消息，同时串口助手也能收到发布的消息
 *
 ****************************************************************************************************
 */

#include "soc_osal.h"
#include "app_init.h"
#include "cmsis_os2.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common_def.h"
#include "MQTTClientPersistence.h"
#include "MQTTClient.h"
#include "errcode.h"
#include "wifi/wifi_connect.h"
osThreadId_t mqtt_init_task_id; // mqtt订阅数据任务

#define SERVER_IP_ADDR "44.232.241.40"  //broker.emqx.io
#define SERVER_IP_PORT 1883
#define CLIENT_ID "ADMIN"

#define MQTT_TOPIC_SUB "subTopic" // 订阅主题
#define MQTT_TOPIC_PUB "pubTopic" // 发布主题

char *g_msg = "hello jack";
MQTTClient client;
volatile MQTTClient_deliveryToken deliveredToken;
extern int MQTTClient_init(void);
/* 回调函数，处理消息到达 */
void delivered(void *context, MQTTClient_deliveryToken dt)
{
    unused(context);
    printf("Message with token value %d delivery confirmed\n", dt);
    deliveredToken = dt;
}

/* 回调函数，处理接收到的消息 */
int messageArrived(void *context, char *topicname, int topiclen, MQTTClient_message *message)
{
    unused(context);
    unused(topiclen);
    printf("Message arrived on topic: %s\n", topicname);
    printf("Message: %.*s\n", message->payloadlen, (char *)message->payload);
    return 1; // 表示消息已被处理
}

/* 回调函数，处理连接丢失 */
void connlost(void *context, char *cause)
{
    unused(context);
    printf("Connection lost: %s\n", cause);
}
/* 消息订阅 */
int mqtt_subscribe(const char *topic)
{
    printf("subscribe start\r\n");
    MQTTClient_subscribe(client, topic, 1);
    return 0;
}

int mqtt_publish(const char *topic, char *g_msg)
{
    MQTTClient_message pubmsg = MQTTClient_message_initializer;
    MQTTClient_deliveryToken token;
    int ret = 0;
    pubmsg.payload = g_msg;
    pubmsg.payloadlen = (int)strlen(g_msg);
    pubmsg.qos = 1;
    pubmsg.retained = 0;
    ret = MQTTClient_publishMessage(client, topic, &pubmsg, &token);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("mqtt_publish failed\r\n");
    }
    printf("mqtt_publish(), the payload is %s, the topic is %s\r\n", g_msg, topic);
    return ret;
}
static errcode_t mqtt_connect(void)
{
    int ret;
    MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
    /* 初始化MQTT客户端 */
    MQTTClient_init();
    /* 创建 MQTT 客户端 */
    ret = MQTTClient_create(&client, SERVER_IP_ADDR, CLIENT_ID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
    if (ret != MQTTCLIENT_SUCCESS) {
        printf("Failed to create MQTT client, return code %d\n", ret);
        return ERRCODE_FAIL;
    }
    conn_opts.keepAliveInterval = 120; /* 120: 保活时间  */
    conn_opts.cleansession = 1;
    // 绑定回调函数
    MQTTClient_setCallbacks(client, NULL, connlost, messageArrived, delivered);

    // 尝试连接
    if ((ret = MQTTClient_connect(client, &conn_opts)) != MQTTCLIENT_SUCCESS) {
        printf("Failed to connect, return code %d\n", ret);
        MQTTClient_destroy(&client); // 连接失败时销毁客户端
        return ERRCODE_FAIL;
    }
    printf("Connected to MQTT broker!\n");
    osDelay(200); /* 200: 延时2s  */
    // 订阅MQTT主题
    mqtt_subscribe(MQTT_TOPIC_SUB);
    while (1) {
        osDelay(100); /* 100: 延时1s  */
        mqtt_publish(MQTT_TOPIC_PUB, g_msg);
    }

    return ERRCODE_SUCC;
}
void mqtt_init_task(const char *argument)
{
    unused(argument);
    wifi_connect();
    osDelay(200); /* 200: 延时2s  */
    mqtt_connect();
}

static void network_wifi_mqtt_example(void)
{
    printf("Enter network_wifi_mqtt_example()!");

    osThreadAttr_t options;
    options.name = "mqtt_init_task";
    options.attr_bits = 0;
    options.cb_mem = NULL;
    options.cb_size = 0;
    options.stack_mem = NULL;
    options.stack_size = 0x3000;
    options.priority = osPriorityNormal;

    mqtt_init_task_id = osThreadNew((osThreadFunc_t)mqtt_init_task, NULL, &options);
    if (mqtt_init_task_id != NULL) {
        printf("ID = %d, Create mqtt_init_task_id is OK!", mqtt_init_task_id);
    }
}
/* Run the sample. */
app_run(network_wifi_mqtt_example);