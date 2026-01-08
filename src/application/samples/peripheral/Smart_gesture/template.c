/**
 ****************************************************************************************************
 * @file        template.c
 * @author      jack
 * @version     V1.0
 * @date        2025-04-22
 * @brief       智慧医疗实验
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
#include "PAJ7620/PAJ7620.h"
#include "PAJ7620/PAJ_I2c.h"
#include "string.h"

#define WIFI_TASK_STACK_SIZE 0x1000
#define WIFI_TASK_PRIO 17

#define DHT11_TASK_STACK_SIZE 0x1000
#define DHT11_TASK_PRIO 17

#define MAX30102_TASK_STACK_SIZE 0x1000
#define MAX30102_TASK_PRIO 17

#define GETMAX30102DATA_TASK_STACK_SIZE 0x1000
#define GETMAX30102DATA_TASK_PRIO 17

#define GESTURE_TASK_STACK_SIZE 0x1000
#define GESTURE_TASK_PRIO 17

#define CONFIG_WIFI_SSID            "JACK"                              // 要连接的WiFi 热点账号
#define CONFIG_WIFI_PWD             "12345678"                        // 要连接的WiFi 热点密码
#define CONFIG_SERVER_IP            "192.168.19.198"                     // 要连接的服务器IP
#define CONFIG_SERVER_PORT           8888                               // 要连接的服务器端口

#define TIME 500

// 温湿度数据buf
uint8_t tempHumiData_buf[50];
// 血氧心率数据buf
uint8_t sp02Heart_buf[50];
// 手势识别数据buf
uint8_t gesture_buf[50];

void *Wifi_Task(void *arg)
{
    unused(arg);

    printf("wifi task start\r\n");

    struct sockaddr_in send_addr;
    socklen_t addr_length = sizeof(send_addr);
    //char recvBuf[512];

    // 连接wifi
    wifi_connect(CONFIG_WIFI_SSID, CONFIG_WIFI_PWD);

    printf("wifi connect success\r\n");

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
        // result = sendto(sock_fd, tempHumiData_buf, sizeof(tempHumiData_buf), 0, (struct sockaddr *)&send_addr, addr_length);
        // if(result)
        // {
        //     printf("result: %d, sendData:%s\r\n", result, tempHumiData_buf);
        // }

        // result =  sendto(sock_fd, sp02Heart_buf, sizeof(sp02Heart_buf), 0,(struct sockaddr *)&send_addr, addr_length);
        // if (result) 
        // {
        //     printf("result: %d, sendData:%s\r\n", result, sp02Heart_buf);
        // }

        result =  sendto(sock_fd, gesture_buf, sizeof(gesture_buf), 0,(struct sockaddr *)&send_addr, addr_length);
        if (result) 
        {
            printf("result: %d, sendData:%s\r\n", result, gesture_buf);
        }

        osal_msleep(300);
        memset(gesture_buf,0,sizeof(gesture_buf));

        osal_msleep(700);
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
       // printf("%s\n",tempHumiData_buf);
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
           // printf("%s\n",sp02Heart_buf);
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


void *Sensor_Gesture_Task(void *arg)
{
    unused(arg);

    I2C_Config();
    if(PAJ7620_Init()==false)
        printf("PAJ7620 initialize error.\r\n");
    //printf("PAJ7620 initialize sucess.\r\n");
    while (1) 
    {
        uint8_t data[2]={0,0};	
        IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);

        switch (data[0])
        {
        case GES_RIGHT_FLAG: // 右标志
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
			if(data[0] == GES_LEFT_FLAG) 
				printf("Right-Left\r\n"); // 从右到左
            else if(data[0] == GES_FORWARD_FLAG)
            {
                printf("Forward\r\n"); // 向前
                osal_msleep(500);
            }
            else if(data[0] == GES_BACKWARD_FLAG)
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
            else
                printf("Right\r\n"); // 右

        break;
        
        case GES_LEFT_FLAG: //左标志
            osal_msleep(TIME); 
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_RIGHT_FLAG) 
                printf("Left-Right\r\n"); // 从左到右
            else if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Forward\r\n"); // 向前
                osal_msleep(TIME);
            }
            else if(data[0] == GES_BACKWARD_FLAG) 
            {
                printf("Backward\r\n"); // 向后
                osal_msleep(TIME);
            }
            else
                printf("Left\r\n"); // 左          
        break;

        case GES_UP_FLAG: // 向上标志
            osal_msleep(TIME); 
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_DOWN_FLAG) 
                printf("Up-Down\r\n"); // 上下
            else if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Forward\r\n");
                osal_msleep(TIME);
            }
            else if(data[0] == GES_BACKWARD_FLAG) 
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
            else
            {
                printf("Up\r\n"); // 上
                strcpy((char*)gesture_buf,"up");
            }
                
        break;

        case GES_DOWN_FLAG: // 下标志
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_UP_FLAG) 
                printf("Down-Up\r\n");
            else if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Forward\r\n");
                osal_msleep(TIME);
            }
            else if(data[0] == GES_BACKWARD_FLAG) 
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
            else
            {
                printf("Down\r\n");
                // 字符串合并
                strcpy((char*)gesture_buf,"down");
            }
                
        break;

        case GES_FORWARD_FLAG: // 获取向前标志
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_BACKWARD_FLAG)  // 向后标志
            {
                printf("Forward-Backward\r\n"); // 前后
                osal_msleep(TIME);
            }
            else
            {
                printf("Forward\r\n"); // 向前
                osal_msleep(TIME);
            }
        break;

        case GES_BACKWARD_FLAG:	// 向后倒退	  
            osal_msleep(TIME);
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x43, &data[0]);
            if(data[0] == GES_FORWARD_FLAG) 
            {
                printf("Backward-Forward\r\n"); // 向后向前
                osal_msleep(TIME);
            }
            else
            {
                printf("Backward\r\n");
                osal_msleep(TIME);
            }
        break;

        case GES_CLOCKWISE_FLAG: // 顺时针方向
				printf("Clockwise\r\n");
            break;
                
			case GES_COUNT_CLOCKWISE_FLAG:
				printf("anti-clockwise\r\n"); // 逆时针方向
            break;  
                
		default:
            IIC_ReadData(PAJ7620_I2C_ADDR, 0x44, &data[1]);
            if (data[1] == GES_WAVE_FLAG) 
                printf("wave\r\n");
        break;

        }
        osal_msleep(100);
        uapi_watchdog_kick();
    }

    return NULL;
    
}

void sensor_gesture_task_create(void)
{
    osal_task *task_handle = NULL;
    
    osal_kthread_lock();

    task_handle = osal_kthread_create((osal_kthread_handler)Sensor_Gesture_Task, 0, "DHT11Task", DHT11_TASK_STACK_SIZE);
    if (task_handle != NULL)
    {
        osal_kthread_set_priority(task_handle, DHT11_TASK_PRIO);
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
    printf("Hi3863开发板--WIFI UDP 发送温湿度，血氧心率数据实验\n");

    wifi_task_create(); // wifi任务创建
    //uapi_tcxo_delay_ms(1000); // 延时3ms
    //osal_msleep(1000);
    
    // 创建传感器获取温湿度数据线程
    //sensor_DHT11_task_create();

    // 创建传感器获取血氧心率数据线程
    //sensor_spo2_task_create();
    
    // 创建传感器获取手势识别数据线程
    sensor_gesture_task_create();
    
}

app_run(sample_entry);