#include "ui_service.h"

#include <stdio.h>
#include <string.h>

#include "../../../drivers/ssd1306/ssd1306.h"
#include "captive_portal_service.h"
#include "i2c.h"
#include "pinctrl.h"
#include "securec.h"
#include "soc_osal.h"
#include "udp_net_common.h"
#include "udp_service.h"

// I2C 总线配置（用于 OLED 显示屏通信）
#define CAR_I2C_BUS_ID 1        // I2C 总线 ID
#define CAR_I2C_BAUDRATE 400000 // I2C 波特率
#define CAR_I2C_HS_CODE 0x0     // I2C 高速模式地址码
#define CAR_I2C_SCL_PIN 15      // I2C SCL 引脚号
#define CAR_I2C_SDA_PIN 16      // I2C SDA 引脚号
#define CAR_I2C_PIN_MODE 2      // I2C 引脚复用模式

/* ============================================================
 * 消息驱动 UI 任务：
 *   - 对外接口只往消息队列投递请求，立即返回；
 *   - 独立 UI 任务阻塞在消息队列上，只有收到消息才刷屏，
 *     从而避免在主循环里轮询 OLED 导致的 I2C 占用与闪烁。
 * ============================================================ */

#define UI_MSG_QUEUE_DEPTH 4    // UI 消息队列深度

typedef enum {
    UI_MSG_MODE = 0,
    UI_MSG_OTA,
} ui_msg_type_t;

typedef struct {
    ui_msg_type_t type;
    CarStatus mode;
    WifiConnectStatus wifi_state;
    uint8_t percent;
    char text[32];
} ui_msg_t;

static bool g_oled_ready = false;       // OLED 是否已初始化并可用
static volatile bool g_ui_busy = false; // OLED 是否被独占（OTA 写, ui_task 读，bool 32 位写入原子）

static unsigned long g_ui_queue = 0; // UI 消息队列 ID
static bool g_queue_inited = false;  // UI 消息队列是否已初始化
static osal_task *g_ui_task = NULL;  // UI 渲染任务句柄

// UI 当前缓存的模式，用于待机页面判断
static CarStatus g_ui_current_mode = CAR_STOP_STATUS; // UI 当前缓存的模式，用于待机页面判断

/**
 * @brief 模式显示信息结构体
 */
typedef struct {
    const char *line0; // 第 0 行显示文本（顶部）
    const char *line1; // 第 1 行显示文本（中部）
    const char *line2; // 第 2 行显示文本（底部）
} ModeDisplayInfo;

/*
 * 支持的字符:
 * 模, 式, 停, 止, 循, 迹, 避, 障, 遥, 控, 连, 接, 中, 成, 功, 失, 败, 等, 待,
 * 热, 点, 配, 置
 */

/**
 * @brief 模式显示信息查找表（按 CarStatus 枚举值索引）
 */
static const ModeDisplayInfo g_mode_display[] = {
    // 停止状态 (0)
    {"模式: 停止", "等待...", ""},
    // 循迹状态 (1)
    {"模式: 循迹", "循迹中...", ""},
    // 避障状态 (2)
    {"模式: 避障", "避障中...", ""},
    // WiFi遥控状态 (3)
    {"模式: 遥控", "遥控中...", ""},
    // 蓝牙遥控状态 (4)
    {"模式: 蓝牙", "未启用", ""},
};

// ---------- 实际渲染函数（仅 UI 任务调用） ----------

/* 根据小车状态渲染对应的模式页面到 OLED */
static void ui_render_mode(CarStatus status)
{
    if (!g_oled_ready)
        return;
    if (g_ui_busy)
        return;

    size_t mode_count = sizeof(g_mode_display) / sizeof(g_mode_display[0]);
    if (status >= CAR_STOP_STATUS && (size_t)status < mode_count) {
        bsp_ssd1306_fill(BSP_SSD1306_COLOR_BLACK);
        bsp_ssd1306_draw_string16(0, 0, g_mode_display[status].line0, BSP_SSD1306_COLOR_WHITE);
        bsp_ssd1306_draw_string16(0, 16, g_mode_display[status].line1, BSP_SSD1306_COLOR_WHITE);
        bsp_ssd1306_draw_string16(0, 32, g_mode_display[status].line2, BSP_SSD1306_COLOR_WHITE);
        bsp_ssd1306_update_screen();
    }
}

/* 渲染待机页面，显示WiFi状态和IP地址 */
static void ui_render_standby_impl(WifiConnectStatus wifi_state, const char *ip_addr)
{
    if (!g_oled_ready)
        return;
    if (g_ui_busy)
        return;

    static const char *wifi_state_str[] = {
        "WiFi: 未连接",   // WIFI_STATUS_DISCONNECTED (0)
        "WiFi: 连接中",   // WIFI_STATUS_CONNECTING (1)
        "WiFi: 连接成功", // WIFI_STATUS_CONNECTED (2)
        "热点模式"        // WIFI_STATUS_AP_MODE (3)
    };
    const char *state_str = (wifi_state >= 0 && wifi_state < 4) ? wifi_state_str[wifi_state] : "WiFi: 未知状态";

    bsp_ssd1306_fill(BSP_SSD1306_COLOR_BLACK);
    bsp_ssd1306_draw_string16(0, 0, "模式: 停止", BSP_SSD1306_COLOR_WHITE);
    bsp_ssd1306_draw_string16(0, 16, state_str, BSP_SSD1306_COLOR_WHITE);
    bsp_ssd1306_draw_string16(0, 32, ip_addr, BSP_SSD1306_COLOR_WHITE);
    bsp_ssd1306_update_screen();
}

/* 渲染OTA升级进度页面 */
static void ui_render_ota(uint8_t percent, const char *status_line)
{
    if (!g_oled_ready)
        return;

    char line2[32] = {0};
    (void)snprintf(line2, sizeof(line2), "%s %u%%", status_line, percent);

    bsp_ssd1306_fill(BSP_SSD1306_COLOR_BLACK);
    bsp_ssd1306_draw_string16(0, 0, "OTA 升级", BSP_SSD1306_COLOR_WHITE);
    bsp_ssd1306_draw_string16(0, 16, line2, BSP_SSD1306_COLOR_WHITE);
    bsp_ssd1306_update_screen();
}

// ---------- UI 任务主循环 ----------

/* UI任务主循环，阻塞等待消息队列并渲染对应页面 */
static int ui_task_entry(void *arg)
{
    (void)arg;
    ui_msg_t msg;

    while (1) {
        uint32_t len = sizeof(msg);
        int ret = osal_msg_queue_read_copy(g_ui_queue, &msg, &len, OSAL_WAIT_FOREVER);
        if (ret != OSAL_SUCCESS)
            continue;

        switch (msg.type) {
            case UI_MSG_MODE:
                g_ui_current_mode = msg.mode;
                if (msg.mode == CAR_STOP_STATUS) {
                    // 待机模式：直接读取网络层缓存的 IP，无轮询
                    WifiConnectStatus wifi_status = udp_service_get_wifi_status();
                    char ip_line[32] = {0};
                    if (wifi_status == WIFI_STATUS_AP_MODE) {
                        const char *ap_ip = captive_portal_service_get_ap_ip();
                        (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ap_ip ? ap_ip : "...");
                    } else {
                        const char *ip = udp_service_get_ip();
                        (void)snprintf(ip_line, sizeof(ip_line), "IP: %s", ip ? ip : "Pending");
                    }
                    ui_render_standby_impl(wifi_status, ip_line);
                } else {
                    ui_render_mode(msg.mode);
                }
                break;

            case UI_MSG_OTA:
                ui_render_ota(msg.percent, msg.text);
                break;
        }
    }
    return 0;
}

// ---------- 初始化 ----------

/* 初始化OLED显示屏、消息队列和UI任务 */
void ui_service_init(void)
{
    static bool init_attempted = false;

    if (init_attempted)
        return;
    init_attempted = true;

    uapi_pin_set_mode(CAR_I2C_SCL_PIN, CAR_I2C_PIN_MODE);
    uapi_pin_set_mode(CAR_I2C_SDA_PIN, CAR_I2C_PIN_MODE);

    errcode_t ret = uapi_i2c_master_init(CAR_I2C_BUS_ID, CAR_I2C_BAUDRATE, CAR_I2C_HS_CODE);
    if (ret != ERRCODE_SUCC) {
        printf("[OLED] I2C 初始化失败，跳过显示屏功能\r\n");
        return;
    }
    if (!bsp_ssd1306_init()) {
        printf("[OLED] 屏幕初始化失败，跳过显示屏功能\r\n");
        return;
    }
    printf("[OLED] 显示屏初始化成功\r\n");
    g_oled_ready = true;

    // 创建消息队列 + UI 任务（即使 OLED 失败也创建任务，避免接口堆积）
    if (!g_queue_inited) {
        if (osal_msg_queue_create("ui_msgq", UI_MSG_QUEUE_DEPTH, &g_ui_queue, 0, sizeof(ui_msg_t)) == OSAL_SUCCESS) {
            g_queue_inited = true;
        } else {
            printf("[OLED] 消息队列创建失败\r\n");
            return;
        }
    }

    if (g_ui_task == NULL) {
        g_ui_task = car_task_create_locked("ui_task", (osal_kthread_handler)ui_task_entry, NULL, 4096,
                                             28);
    }

    ui_show_mode_page(CAR_STOP_STATUS);
}

// ---------- 对外接口：仅投递消息 ----------

/* 向UI消息队列投递消息，队列满时覆盖最旧消息 */
static void ui_post(const ui_msg_t *msg)
{
    if (!g_queue_inited)
        return;

    /* 队列满则丢弃最旧的，保证 ISR/任意上下文都能安全投递。 */
    (void)osal_msgq_overwrite(g_ui_queue, UI_MSG_QUEUE_DEPTH, msg, sizeof(*msg));
}

/* 异步显示指定模式的页面 */
void ui_show_mode_page(CarStatus status)
{
    ui_msg_t msg = {0};
    msg.type = UI_MSG_MODE;
    msg.mode = status;
    ui_post(&msg);
}

/* 异步显示OTA升级进度 */
void ui_show_ota_progress(uint8_t percent, const char *status_line)
{
    ui_msg_t msg = {0};
    msg.type = UI_MSG_OTA;
    msg.percent = percent;
    if (status_line != NULL) {
        (void)strncpy_s(msg.text, sizeof(msg.text), status_line, sizeof(msg.text) - 1);
    }
    ui_post(&msg);
}

/* 查询OLED是否已初始化就绪 */
bool ui_service_is_ready(void)
{
    return g_oled_ready;
}

/* 独占OLED，阻止UI任务刷新屏幕（OTA写入时使用） */
void ui_service_acquire(void)
{
    g_ui_busy = true;
}
/* 释放OLED独占，恢复UI任务刷新 */
void ui_service_release(void)
{
    g_ui_busy = false;
}
