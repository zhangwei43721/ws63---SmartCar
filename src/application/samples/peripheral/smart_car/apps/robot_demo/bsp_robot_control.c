/**
 ****************************************************************************************************
 * @file        bsp_robot_control.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       智能小车控制BSP层实现
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：智能小车循迹避障控制
 *
 ****************************************************************************************************
 */

#include "bsp_robot_control.h"
#include "../../drivers/l9110s/bsp_l9110s.h"
#include "../../drivers/hcsr04/bsp_hcsr04.h"
#include "../../drivers/tcrt5000/bsp_tcrt5000.h"
#include "../../drivers/sg90/bsp_sg90.h"
#include "../../drivers/wifi_client/bsp_wifi.h"
#include "../../drivers/ssd1306/ssd1306.h"
// #include "../../drivers/bt_spp_server/bsp_bt_spp.h" // 暂时不开发蓝牙相关内容

#include "i2c.h"
#include <stdbool.h>
#include "gpio.h"
#include "pinctrl.h"
#include "soc_osal.h"
#include "osal_timer.h"
#include "osal_mutex.h"
#include "securec.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include "lwip/ip_addr.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* --- 全局变量 --- */
static CarStatus g_car_status = CAR_STOP_STATUS;

/* --- 配置参数 --- */
#define ROBOT_STANDBY_REFRESH_MS 500
#define ROBOT_I2C_BUS_ID 1
#define ROBOT_I2C_BAUDRATE 400000
#define ROBOT_I2C_HS_CODE 0x0
#define ROBOT_I2C_SCL_PIN 15
#define ROBOT_I2C_SDA_PIN 16
#define ROBOT_I2C_PIN_MODE 2

#define ROBOT_WIFI_SERVER_IP "192.168.3.150"
#define ROBOT_WIFI_SERVER_PORT 8888
#define ROBOT_WIFI_TCP_STACK_SIZE 4096
#define ROBOT_WIFI_TCP_TASK_PRIORITY 24
#define ROBOT_WIFI_TCP_RECONNECT_DELAY_MS 2000
#define ROBOT_WIFI_PACKET_TIMEOUT_MS 500
#define ROBOT_WIFI_RECV_TIMEOUT_MS 100

static bool g_oled_ready = false;

/* --- WiFi 相关变量 --- */
static bool g_standby_wifi_inited = false;
static bool g_standby_wifi_connected = false;
static bool g_standby_wifi_has_ip = false;
static char g_standby_ip[32] = "0.0.0.0";
static unsigned int g_wifi_last_retry = 0;
#define WIFI_RETRY_INTERVAL_MS 5000

static void robot_wifi_tcp_start(void);
static void *robot_wifi_tcp_task(const char *arg);
static int robot_wifi_read_frame(int sockfd, uint8_t frame_out[4]);
static void robot_wifi_handle_frame(const uint8_t frame[4]);
static void robot_wifi_apply_motor(int8_t motor_cmd);
static void robot_wifi_apply_servo(int8_t servo_cmd);
static void robot_wifi_stop_due_to_timeout(void);
static void robot_wifi_mutex_init(void);
static void robot_wifi_lock(void);
static void robot_wifi_unlock(void);
static const char *robot_wifi_server_ip(void);
static uint16_t robot_wifi_server_port(void);

static osal_task *g_wifi_tcp_task_handle = NULL;
static bool g_wifi_tcp_task_started = false;
static osal_mutex g_wifi_mutex;
static bool g_wifi_mutex_inited = false;
static int g_wifi_socket_fd = -1;
static bool g_wifi_tcp_connected = false;
static unsigned long long g_wifi_last_cmd_tick = 0;
static uint8_t g_wifi_rx_buffer[4];
static size_t g_wifi_rx_filled = 0;
static bool g_wifi_timeout_braked = false;
static unsigned long long g_trace_last_report = 0;
static unsigned long long g_obstacle_last_report = 0;

/* =========================================================================
 *  底层辅助函数 (OLED / WiFi / Motor)
 * ========================================================================= */

static void robot_oled_prepare(void)
{
    if (g_oled_ready) {
        return;
    }

    uapi_pin_set_mode(ROBOT_I2C_SCL_PIN, ROBOT_I2C_PIN_MODE);
    uapi_pin_set_mode(ROBOT_I2C_SDA_PIN, ROBOT_I2C_PIN_MODE);

    errcode_t ret = uapi_i2c_master_init(ROBOT_I2C_BUS_ID, ROBOT_I2C_BAUDRATE, ROBOT_I2C_HS_CODE);
    if (ret != ERRCODE_SUCC) {
        printf("小车待机: I2C 初始化失败, ret=0x%x\r\n", ret);
        return;
    }

    ssd1306_Init();
    g_oled_ready = true;
}

static void robot_standby_render(const char *wifi_line, const char *ip_line)
{
    if (!g_oled_ready) {
        return;
    }

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString("Mode: Standby", Font_7x10, White);
    ssd1306_SetCursor(0, 16);
    ssd1306_DrawString((char *)wifi_line, Font_7x10, White);
    ssd1306_SetCursor(0, 32);
    ssd1306_DrawString((char *)ip_line, Font_7x10, White);
    ssd1306_UpdateScreen();
}

static void robot_wifi_ensure_connected(void)
{
    unsigned long long now_jiffies = osal_get_jiffies();
    unsigned int now = (unsigned int)now_jiffies;

    if (!g_standby_wifi_inited) {
        if (bsp_wifi_init() != 0) {
            return;
        }
        g_standby_wifi_inited = true;
        g_wifi_last_retry = now;
    }

    // 定时重连机制
    if (!g_standby_wifi_connected && (now - g_wifi_last_retry >= WIFI_RETRY_INTERVAL_MS)) {
        g_wifi_last_retry = now;
        // 尝试连接默认配置的 WiFi (SSID/PWD 在 bsp_wifi 中定义)
        if (bsp_wifi_connect_default() == 0) {
            g_standby_wifi_connected = true;
        }
    }

    // 状态检测
    if (g_standby_wifi_connected) {
        bsp_wifi_status_t status = bsp_wifi_get_status();
        if (status == BSP_WIFI_STATUS_GOT_IP) {
            if (bsp_wifi_get_ip(g_standby_ip, sizeof(g_standby_ip)) == 0) {
                g_standby_wifi_has_ip = true;
            }
        } else if (status == BSP_WIFI_STATUS_DISCONNECTED) {
            g_standby_wifi_connected = false;
            g_standby_wifi_has_ip = false;
        }
    }
}

/* =========================================================================
 *  对外控制接口
 * ========================================================================= */

/**
 * @brief 初始化智能小车控制系统
 */
void robot_control_init(void)
{
    l9110s_init();   // 电机
    hcsr04_init();   // 超声波
    tcrt5000_init(); // 循迹红外
    sg90_init();     // 舵机

    // OLED 初始化推迟到 Standby 模式中按需调用，或在此处调用均可
    // robot_oled_prepare();

    printf("智能小车控制系统已初始化\r\n");

    // 启动 WiFi 长连接任务（仅启动一次）
    robot_wifi_tcp_start();
}

CarStatus robot_get_status(void)
{
    return g_car_status;
}

void robot_set_status(CarStatus status)
{
    g_car_status = status;
}

/* =========================================================================
 *  模式 1: 待机模式 (Standby)
 * ========================================================================= */
void robot_standby_mode(void)
{
    car_stop();
    robot_oled_prepare();

    while (robot_get_status() == CAR_STOP_STATUS) {
        char wifi_line[32] = {0};
        char ip_line[32] = {0};

        // 维持 WiFi 连接
        robot_wifi_ensure_connected();

        if (!g_standby_wifi_inited) {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: Init...");
        } else if (!g_standby_wifi_connected) {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: Connecting");
        } else {
            snprintf(wifi_line, sizeof(wifi_line), "WiFi: Connected");
        }

        if (g_standby_wifi_has_ip) {
            snprintf(ip_line, sizeof(ip_line), "IP: %s", g_standby_ip);
        } else {
            snprintf(ip_line, sizeof(ip_line), "IP: Pending");
        }

        robot_standby_render(wifi_line, ip_line);
        osal_msleep(ROBOT_STANDBY_REFRESH_MS);
    }

    car_stop();
}

/* =========================================================================
 *  模式 2: 循迹模式 (Trace)
 * ========================================================================= */
void robot_trace_mode(void)
{
    unsigned int left = 0;
    unsigned int right = 0;

    printf("进入循迹模式...\r\n");

    while (g_car_status == CAR_TRACE_STATUS) {
        left = tcrt5000_get_left();
        right = tcrt5000_get_right();

        if (left == TCRT5000_ON_BLACK && right == TCRT5000_ON_BLACK) {
            // 两边黑 -> 直行
            car_forward();
        } else if (left == TCRT5000_ON_BLACK && right == TCRT5000_ON_WHITE) {
            // 左黑右白 -> 左偏 -> 左转修正
            car_left();
        } else if (left == TCRT5000_ON_WHITE && right == TCRT5000_ON_BLACK) {
            // 左白右黑 -> 右偏 -> 右转修正
            car_right();
        } else {
            // 全白 -> 直行 (或考虑停车)
            car_forward();
        }

        unsigned long long now = osal_get_jiffies();
        if (now - g_trace_last_report > osal_msecs_to_jiffies(500)) {
            g_trace_last_report = now;
            robot_report_state("trace", (float)(left - right));
        }
        osal_msleep(20);
    }

    car_stop();
}

/* =========================================================================
 *  模式 3: 避障模式 (Obstacle Avoidance)
 * ========================================================================= */

static void engine_turn_left(void)
{
    sg90_set_angle(150);
}
static void engine_turn_right(void)
{
    sg90_set_angle(30);
}
static void regress_middle(void)
{
    sg90_set_angle(90);
}

static unsigned int engine_go_where(void)
{
    float left_distance = 0.0;
    float right_distance = 0.0;

    engine_turn_left();
    osal_msleep(200);
    left_distance = hcsr04_get_distance();
    osal_msleep(100);

    regress_middle();
    osal_msleep(200);

    engine_turn_right();
    osal_msleep(200);
    right_distance = hcsr04_get_distance();
    osal_msleep(100);

    regress_middle();

    printf("左侧: %.2f cm, 右侧: %.2f cm\r\n", left_distance, right_distance);

    return (left_distance > right_distance) ? CAR_TURN_LEFT : CAR_TURN_RIGHT;
}

static void car_where_to_go(float distance)
{
    if (distance < DISTANCE_BETWEEN_CAR_AND_OBSTACLE && distance > 0) {
        car_stop();
        osal_msleep(200);

        car_backward();
        osal_msleep(400);
        car_stop();

        unsigned int ret = engine_go_where();
        if (ret == CAR_TURN_LEFT) {
            car_left();
            osal_msleep(400);
        } else {
            car_right();
            osal_msleep(400);
        }
        car_stop();
    } else {
        car_forward();
    }
}

void robot_obstacle_avoidance_mode(void)
{
    float distance = 0.0;
    printf("进入避障模式...\r\n");
    regress_middle();

    while (g_car_status == CAR_OBSTACLE_AVOIDANCE_STATUS) {
        distance = hcsr04_get_distance();
        car_where_to_go(distance);
        unsigned long long now = osal_get_jiffies();
        if (now - g_obstacle_last_report > osal_msecs_to_jiffies(500)) {
            g_obstacle_last_report = now;
            robot_report_state("obstacle", distance);
        }
        osal_msleep(50);
    }

    car_stop();
    regress_middle();
}

/* =========================================================================
 *  模式 4: WiFi 遥控模式 (WiFi Control)
 * ========================================================================= */

void robot_wifi_control_mode(void)
{
    printf("WiFi 远控模式: 等待服务器 %s:%u 的 TCP 指令...\r\n", robot_wifi_server_ip(), robot_wifi_server_port());

    while (g_car_status == CAR_WIFI_CONTROL_STATUS) {
        robot_wifi_stop_due_to_timeout();
        osal_msleep(100);
    }

    car_stop();
}

/* =========================================================================
 *  OLED 状态展示
 * ========================================================================= */

void robot_display_mode(CarStatus status)
{
    char line0[32] = {0};
    char line1[32] = {0};
    char line2[32] = {0};

    robot_oled_prepare();
    if (!g_oled_ready) {
        return;
    }

    switch (status) {
        case CAR_STOP_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Standby");
            snprintf(line1, sizeof(line1), "WiFi: Checking");
            snprintf(line2, sizeof(line2), "Press KEY1...");
            break;
        case CAR_TRACE_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Trace");
            snprintf(line1, sizeof(line1), "Infrared ready");
            snprintf(line2, sizeof(line2), "KEY1 -> Next");
            break;
        case CAR_OBSTACLE_AVOIDANCE_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Obstacle");
            snprintf(line1, sizeof(line1), "Ultrasonic ON");
            snprintf(line2, sizeof(line2), "KEY1 -> Next");
            break;
        case CAR_WIFI_CONTROL_STATUS:
            snprintf(line0, sizeof(line0), "Mode: WiFi Ctrl");
            if (g_standby_wifi_connected && g_standby_wifi_has_ip) {
                snprintf(line1, sizeof(line1), "IP:%s", g_standby_ip);
                snprintf(line2, sizeof(line2), "TCP Port:8888");
            } else {
                snprintf(line1, sizeof(line1), "WiFi:Connecting");
                snprintf(line2, sizeof(line2), "KEY1 -> Stop");
            }
            break;
        case CAR_BT_CONTROL_STATUS:
            snprintf(line0, sizeof(line0), "Mode: Bluetooth");
            snprintf(line1, sizeof(line1), "Current: Disabled");
            snprintf(line2, sizeof(line2), "KEY1 -> Stop");
            break;
        default:
            snprintf(line0, sizeof(line0), "Mode: Unknown");
            snprintf(line1, sizeof(line1), "Resetting...");
            snprintf(line2, sizeof(line2), "");
            break;
    }

    ssd1306_Fill(Black);
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString(line0, Font_7x10, White);
    ssd1306_SetCursor(0, 16);
    ssd1306_DrawString(line1, Font_7x10, White);
    ssd1306_SetCursor(0, 32);
    ssd1306_DrawString(line2, Font_7x10, White);
    ssd1306_UpdateScreen();
}

void robot_report_state(const char *mode, float metric)
{
    if (mode == NULL) {
        return;
    }
    printf("[Report] %s => %.2f\r\n", mode, metric);
}

/* =========================================================================
 *  WiFi TCP 长连接实现
 * ========================================================================= */

static void robot_wifi_mutex_init(void)
{
    if (g_wifi_mutex_inited) {
        return;
    }
    if (osal_mutex_init(&g_wifi_mutex) == OSAL_SUCCESS) {
        g_wifi_mutex_inited = true;
    } else {
        printf("WiFi TCP: mutex init failed\r\n");
    }
}

static void robot_wifi_lock(void)
{
    if (g_wifi_mutex_inited) {
        (void)osal_mutex_lock(&g_wifi_mutex);
    }
}

static void robot_wifi_unlock(void)
{
    if (g_wifi_mutex_inited) {
        osal_mutex_unlock(&g_wifi_mutex);
    }
}

static void robot_wifi_tcp_start(void)
{
    if (g_wifi_tcp_task_started) {
        return;
    }

    robot_wifi_mutex_init();

    osal_kthread_lock();
    g_wifi_tcp_task_handle = osal_kthread_create((osal_kthread_handler)robot_wifi_tcp_task, NULL, "wifi_tcp_task",
                                                 ROBOT_WIFI_TCP_STACK_SIZE);
    if (g_wifi_tcp_task_handle != NULL) {
        (void)osal_kthread_set_priority(g_wifi_tcp_task_handle, ROBOT_WIFI_TCP_TASK_PRIORITY);
        g_wifi_tcp_task_started = true;
        printf("WiFi TCP: task created\r\n");
    } else {
        printf("WiFi TCP: failed to create task\r\n");
    }
    osal_kthread_unlock();
}

static const char *robot_wifi_server_ip(void)
{
    return ROBOT_WIFI_SERVER_IP;
}

static uint16_t robot_wifi_server_port(void)
{
    return ROBOT_WIFI_SERVER_PORT;
}

static void *robot_wifi_tcp_task(const char *arg)
{
    UNUSED(arg);

    while (1) {
        robot_wifi_ensure_connected();
        if (!g_standby_wifi_connected || !g_standby_wifi_has_ip) {
            osal_msleep(500);
            continue;
        }

        int sockfd = lwip_socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            printf("WiFi TCP: socket create failed, err=%d\r\n", errno);
            osal_msleep(ROBOT_WIFI_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        struct sockaddr_in server_addr;
        (void)memset_s(&server_addr, sizeof(server_addr), 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = lwip_htons(robot_wifi_server_port());
        server_addr.sin_addr.s_addr = ipaddr_addr(robot_wifi_server_ip());
        if (server_addr.sin_addr.s_addr == IPADDR_NONE) {
            printf("WiFi TCP: invalid server IP\r\n");
            lwip_close(sockfd);
            osal_msleep(ROBOT_WIFI_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        printf("WiFi TCP: connecting to %s:%u ...\r\n", robot_wifi_server_ip(), robot_wifi_server_port());
        if (lwip_connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            printf("WiFi TCP: connect failed, err=%d\r\n", errno);
            lwip_close(sockfd);
            osal_msleep(ROBOT_WIFI_TCP_RECONNECT_DELAY_MS);
            continue;
        }

        struct timeval tv;
        tv.tv_sec = ROBOT_WIFI_RECV_TIMEOUT_MS / 1000;
        tv.tv_usec = (ROBOT_WIFI_RECV_TIMEOUT_MS % 1000) * 1000;
        (void)lwip_setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

        robot_wifi_lock();
        g_wifi_socket_fd = sockfd;
        g_wifi_tcp_connected = true;
        g_wifi_last_cmd_tick = osal_get_jiffies();
        g_wifi_timeout_braked = false;
        g_wifi_rx_filled = 0;
        robot_wifi_unlock();

        printf("WiFi TCP: connected\r\n");

        bool connection_alive = true;
        uint8_t frame[4];
        while (connection_alive) {
            int ret = robot_wifi_read_frame(sockfd, frame);
            if (ret == 1) {
                robot_wifi_handle_frame(frame);
            } else if (ret < 0) {
                connection_alive = false;
                break;
            }
            robot_wifi_stop_due_to_timeout();
            osal_msleep(5);
        }

        lwip_close(sockfd);
        robot_wifi_lock();
        g_wifi_tcp_connected = false;
        g_wifi_socket_fd = -1;
        g_wifi_rx_filled = 0;
        robot_wifi_unlock();
        printf("WiFi TCP: disconnected, retry after %u ms\r\n", ROBOT_WIFI_TCP_RECONNECT_DELAY_MS);
        osal_msleep(ROBOT_WIFI_TCP_RECONNECT_DELAY_MS);
    }

    return NULL;
}

static int robot_wifi_read_frame(int sockfd, uint8_t frame_out[4])
{
    while (g_wifi_rx_filled < sizeof(g_wifi_rx_buffer)) {
        ssize_t n =
            lwip_recv(sockfd, g_wifi_rx_buffer + g_wifi_rx_filled, sizeof(g_wifi_rx_buffer) - g_wifi_rx_filled, 0);
        if (n > 0) {
            g_wifi_rx_filled += (size_t)n;
        } else if (n == 0) {
            return -1; // connection closed
        } else {
            if (errno == EAGAIN) {
                return 0; // no data yet
            }
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (g_wifi_rx_filled == sizeof(g_wifi_rx_buffer)) {
            break;
        }
    }

    if (g_wifi_rx_filled == sizeof(g_wifi_rx_buffer)) {
        memcpy_s(frame_out, 4, g_wifi_rx_buffer, 4);
        g_wifi_rx_filled = 0;
        return 1;
    }
    return 0;
}

static void robot_wifi_handle_frame(const uint8_t frame[4])
{
    uint8_t checksum = frame[0] ^ frame[1] ^ frame[2];
    if (checksum != frame[3]) {
        return;
    }

    robot_wifi_lock();
    g_wifi_last_cmd_tick = osal_get_jiffies();
    g_wifi_timeout_braked = false;
    robot_wifi_unlock();

    if (robot_get_status() != CAR_WIFI_CONTROL_STATUS) {
        return;
    }

    robot_wifi_apply_motor((int8_t)frame[0]);
    robot_wifi_apply_servo((int8_t)frame[1]);
}

static void robot_wifi_apply_motor(int8_t motor_cmd)
{
    if (motor_cmd > 5) {
        car_forward();
    } else if (motor_cmd < -5) {
        car_backward();
    } else {
        car_stop();
    }
}

static void robot_wifi_apply_servo(int8_t servo_cmd)
{
    int angle = 90 + (servo_cmd * 90) / 100;
    if (angle < (int)SG90_ANGLE_MIN) {
        angle = SG90_ANGLE_MIN;
    } else if (angle > (int)SG90_ANGLE_MAX) {
        angle = SG90_ANGLE_MAX;
    }
    sg90_set_angle((unsigned int)angle);
}

static void robot_wifi_stop_due_to_timeout(void)
{
    robot_wifi_lock();
    bool connected = g_wifi_tcp_connected;
    unsigned long long last_tick = g_wifi_last_cmd_tick;
    bool already_braked = g_wifi_timeout_braked;
    robot_wifi_unlock();

    if (!connected || last_tick == 0) {
        return;
    }

    unsigned long long now = osal_get_jiffies();
    if ((now - last_tick) > osal_msecs_to_jiffies(ROBOT_WIFI_PACKET_TIMEOUT_MS)) {
        if (!already_braked && robot_get_status() == CAR_WIFI_CONTROL_STATUS) {
            car_stop();
            robot_wifi_lock();
            g_wifi_timeout_braked = true;
            robot_wifi_unlock();
            printf("WiFi TCP: timeout, car stopped\r\n");
        }
    }
}