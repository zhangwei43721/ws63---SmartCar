/**
 ****************************************************************************************************
 * @file        template.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-22
 * @brief       智慧农业实验
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

#include "bsp_hrSpo2/max30102.h"
#include "bsp_wifi/wifi_connect.h"
#include "bsp_dht11/bsp_dht11.h"
#include "bsp_soil/bsp_soil.h"

#define WIFI_TASK_STACK_SIZE 0x1000
#define WIFI_TASK_PRIO 17

#define DHT11_TASK_STACK_SIZE 0x1000
#define DHT11_TASK_PRIO 17

#define MAX30102_TASK_STACK_SIZE 0x1000
#define MAX30102_TASK_PRIO 17

#define GETMAX30102DATA_TASK_STACK_SIZE 0x1000
#define GETMAX30102DATA_TASK_PRIO 17

#define SOIL_TASK_STACK_SIZE               0x1000
#define SOIL_TASK_PRIO                     (osPriority_t)(17)
#define SOIL_AUTO_SAMPLE_TEST_TIMES        1000

#define CONFIG_WIFI_SSID            "JACK"                              // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD             "12345678"                        // 要连接的WiFi 热点密码
#define CONFIG_SERVER_IP            "192.168.50.198"                     // 要连接的服务器IP
#define CONFIG_SERVER_PORT          8888                                // 要连接的服务器端口

// 温湿度数据buf
uint8_t tempHumiData_buf[50];
// 血氧心率数据buf
uint8_t sp02Heart_buf[50];

// 土壤湿度数据
uint8_t soilData_buf[50];


void *Wifi_Task(void *arg)
{
    unused(arg);

    struct sockaddr_in send_addr;
    socklen_t addr_length = sizeof(send_addr);
    //char recvBuf[512];

    // 连接wifi
    wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);

    // 创建socket套接字
    int sock_fd;
    printf("crate socket start!\n");
    if((sock_fd = socket(AF_INET, SOCK_DGRAM, 0)) == -1)
    {
        printf("create socket failed!\n");
        return NULL;
    }

    printf("create socket end!\n");

    // 初始化连接服务器地址
    send_addr.sin_family = AF_INET;
    send_addr.sin_port = htons(CONFIG_SERVER_PORT);
    send_addr.sin_addr.s_addr = inet_addr(CONFIG_SERVER_IP);
    addr_length = sizeof(send_addr);
    
    int result;

    while(1)
    {
        // 发送数据到服务端
        result = sendto(sock_fd, tempHumiData_buf, sizeof(tempHumiData_buf), 0, (struct sockaddr *)&send_addr, addr_length);
        if(result)
        {
            printf("result: %d, sendData:%s\r\n", result, tempHumiData_buf);
        }

        result =  sendto(sock_fd, soilData_buf, sizeof(soilData_buf), 0,(struct sockaddr *)&send_addr, addr_length);
        if (result) 
        {
            printf("result: %d, sendData:%s\r\n", result, soilData_buf);
        }

        osal_msleep(1000);
        uapi_watchdog_kick();
    }
    // 关闭socket
    closesocket(sock_fd);
}

// wifi任务
void wifi_task_create(void)
{
    osal_task *task_handle = NULL;
    
    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)Wifi_Task, 0, "WifiTask", WIFI_TASK_STACK_SIZE);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, WIFI_TASK_PRIO);
    }

    osal_kthread_unlock();

}

void *Sensor_DHT11_Task(void *arg)
{
    unused(arg);

    uint8_t temp; // 温度
    uint8_t humi; // 湿度

    while (dht11_init())
    {
        printf("DHT11检测失败，请接好!\n");
        osal_msleep(1000);
    }

    printf("DHT11检测成功\n");

    while (1)
    {
        // 读取温湿度数据
        dht11_read_data(&temp, &humi);
        // 字符串合并
        sprintf((char *)tempHumiData_buf, "temp:%d:%d", temp, humi);
        printf("%s\n",tempHumiData_buf);
        osal_msleep(500);
        uapi_watchdog_kick();
    }
}

void sensor_DHT11_task_create(void)
{
    osal_task *task_handle = NULL;
    
    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)Sensor_DHT11_Task, 0, "DHT11Task", DHT11_TASK_STACK_SIZE);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, DHT11_TASK_PRIO);
    }

    osal_kthread_unlock();
}

void *GetMax30102DataTask(void *arg)
{
    unused(arg);

    while (1)
    {
        uint8_t heartRate = 0;
        uint8_t sp02 = 0;
        while (1)
        {
            heartRate = Max30102GetHeartRate();
            sp02 = Max30102GetSpO2();

            // 字符串合并
            sprintf((char *)sp02Heart_buf, "HeartRate:%d:%d", heartRate, sp02);
            printf("%s\n",sp02Heart_buf);
            osal_msleep(500);
        }
    }

}

void sensor_spo2_task_create(void)
{
     osal_task *task_handle = NULL;
    
    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)Max30102Task, 0, "MAX30102Task", MAX30102_TASK_STACK_SIZE);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, MAX30102_TASK_PRIO);
    }

     task_handle = osal_kthread_create((osal_kthread_handler)GetMax30102DataTask, 0, "GetMax30102DataTask", GETMAX30102DATA_TASK_STACK_SIZE);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, GETMAX30102DATA_TASK_PRIO);
    }

    osal_kthread_unlock();
}

static void *Soil_Task(const char *arg)
{
    UNUSED(arg);
    osal_printk("start soil sample test");
    uapi_adc_init(ADC_CLOCK_500KHZ);
    uapi_adc_power_en(AFE_SCAN_MODE_MAX_NUM, true);
    adc_scan_config_t config = {
        .type = 0,
        .freq = 1,
    };
    
    while (1)
    {
        uapi_adc_auto_scan_ch_enable(ADC_CHANNEL_2, config, test_soil_callback);
        uapi_adc_auto_scan_ch_disable(ADC_CHANNEL_2);
        uint16_t value = TS_GetData();
        //printf("soil : %d\r\n",value);
        // 字符串合并
        sprintf((char *)soilData_buf, "soil:%d", value);
        printf("%s\n",soilData_buf);
        osal_msleep(500);
    }
    
    return NULL;
}

void sensor_soil_task_create(void)
{
    osal_task *task_handle = NULL;
    
    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)Soil_Task, 0, "Soil_Task", SOIL_TASK_STACK_SIZE);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, SOIL_TASK_PRIO);
    }

    osal_kthread_unlock();
}

/**
 * @description: 初始化并创建任务
 * @param {*}
 * @return {*}
 */
static void sample_entry(void)
{
    printf("Hi3863开发板--WIFI UDP 发送温湿度，土壤湿度数据实验\n");

    wifi_task_create(); // wifi任务创建
    uapi_tcxo_delay_ms(3); // 延时3ms

    // 创建传感器获取血氧心率数据线程
    //sensor_spo2_task_create();

    // 创建传感器获取土壤湿度数据线程
    sensor_soil_task_create();

    // 创建传感器获取温湿度数据线程
    sensor_DHT11_task_create();
    

}

app_run(sample_entry);