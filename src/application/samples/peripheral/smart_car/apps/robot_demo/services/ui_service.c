#include "ui_service.h"

#include "captive_portal_service.h"
#include "soc_osal.h"
#include "udp_net_common.h"
#include "udp_service.h"

/* ============================================================
 * 消息驱动 UI 任务：
 *   - 对外接口只往消息队列投递请求，立即返回；
 *   - 独立 UI 任务阻塞在消息队列上，只有收到消息才刷屏，
 *     从而避免在主循环里轮询 OLED 导致的 I2C 占用与闪烁。
 * ============================================================ */

#define UI_TASK_STACK_SIZE 4096
#define UI_TASK_PRIO 28
#define UI_MSG_QUEUE_DEPTH 4

typedef enum {
    UI_MSG_MODE = 0,
    UI_MSG_STANDBY,
    UI_MSG_OTA,
} ui_msg_type_t;

typedef struct {
    ui_msg_type_t type;
    CarStatus mode;
    WifiConnectStatus wifi_state;
    uint8_t percent;
    char text[32];
} ui_msg_t;

static bool g_oled_ready = false; /* OLED 是否已初始化并可用 */
static bool g_ui_busy = false;    /* OLED 是否被独占（OTA 等高优场景） */

static unsigned long g_ui_queue = 0;
static bool g_queue_inited = false;
static osal_task *g_ui_task = NULL;

/* UI 当前缓存的模式，用于待机页面判断 */
static CarStatus g_ui_current_mode = CAR_STOP_STATUS;

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
    // CAR_STOP_STATUS (0)
    {"模式: 停止", "等待...", ""},
    // CAR_TRACE_STATUS (1)
    {"模式: 循迹", "循迹中...", ""},
    // CAR_OBSTACLE_AVOIDANCE_STATUS (2)
    {"模式: 避障", "避障中...", ""},
    // CAR_WIFI_CONTROL_STATUS (3)
    {"模式: 遥控", "遥控中...", ""},
    // CAR_BT_CONTROL_STATUS (4)
    {"模式: 蓝牙", "未启用", ""},
};

/* ---------- 实际渲染函数（仅 UI 任务调用） ---------- */

static void ui_render_mode(CarStatus status)
{
    if (!g_oled_ready)
        return;
    if (g_ui_busy)
        return;

    size_t mode_count = sizeof(g_mode_display) / sizeof(g_mode_display[0]);
    if (status >= CAR_STOP_STATUS && (size_t)status < mode_count) {
        ssd1306_Fill(Black);
        ssd1306_DrawString16(0, 0, g_mode_display[status].line0, White);
        ssd1306_DrawString16(0, 16, g_mode_display[status].line1, White);
        ssd1306_DrawString16(0, 32, g_mode_display[status].line2, White);
        ssd1306_UpdateScreen();
    }
}

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

    ssd1306_Fill(Black);
    ssd1306_DrawString16(0, 0, "模式: 停止", White);
    ssd1306_DrawString16(0, 16, state_str, White);
    ssd1306_DrawString16(0, 32, ip_addr, White);
    ssd1306_UpdateScreen();
}

static void ui_render_ota(uint8_t percent, const char *status_line)
{
    if (!g_oled_ready)
        return;

    char line2[32] = {0};
    (void)snprintf(line2, sizeof(line2), "%s %u%%", status_line, percent);

    ssd1306_Fill(Black);
    ssd1306_DrawString16(0, 0, "OTA 升级", White);
    ssd1306_DrawString16(0, 16, line2, White);
    ssd1306_UpdateScreen();
}

/* ---------- UI 任务主循环 ---------- */

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
                    /* 待机模式：直接读取网络层缓存的 IP，无轮询 */
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

            case UI_MSG_STANDBY:
                // 仅在当前处于待机模式时刷新，避免干扰循迹/避障等模式显示
                if (g_ui_current_mode == CAR_STOP_STATUS) {
                    ui_render_standby_impl(msg.wifi_state, msg.text);
                }
                break;

            case UI_MSG_OTA:
                ui_render_ota(msg.percent, msg.text);
                break;
        }
    }
    return 0;
}

/* ---------- 初始化 ---------- */

void ui_service_init(void)
{
    static bool init_attempted = false;

    if (init_attempted)
        return;
    init_attempted = true;

    uapi_pin_set_mode(ROBOT_I2C_SCL_PIN, ROBOT_I2C_PIN_MODE);
    uapi_pin_set_mode(ROBOT_I2C_SDA_PIN, ROBOT_I2C_PIN_MODE);

    errcode_t ret = uapi_i2c_master_init(ROBOT_I2C_BUS_ID, ROBOT_I2C_BAUDRATE, ROBOT_I2C_HS_CODE);
    if (ret != ERRCODE_SUCC) {
        printf("[OLED] I2C 初始化失败，跳过显示屏功能\r\n");
        return;
    }
    if (!ssd1306_Init()) {
        printf("[OLED] 屏幕初始化失败，跳过显示屏功能\r\n");
        return;
    }
    printf("[OLED] 显示屏初始化成功\r\n");
    g_oled_ready = true;

    /* 创建消息队列 + UI 任务（即使 OLED 失败也创建任务，避免接口堆积） */
    if (!g_queue_inited) {
        if (osal_msg_queue_create("ui_msgq", UI_MSG_QUEUE_DEPTH, &g_ui_queue, 0, sizeof(ui_msg_t)) == OSAL_SUCCESS) {
            g_queue_inited = true;
        } else {
            printf("[OLED] 消息队列创建失败\r\n");
            return;
        }
    }

    if (g_ui_task == NULL) {
        osal_kthread_lock();
        g_ui_task = osal_kthread_create((osal_kthread_handler)ui_task_entry, NULL, "ui_task", UI_TASK_STACK_SIZE);
        if (g_ui_task != NULL) {
            osal_kthread_set_priority(g_ui_task, UI_TASK_PRIO);
        }
        osal_kthread_unlock();
    }

    ui_show_mode_page(CAR_STOP_STATUS);
}

/* ---------- 对外接口：仅投递消息 ---------- */

static void ui_post(const ui_msg_t *msg)
{
    if (!g_queue_inited)
        return;

    /* 与 motor_executor_push_cmd 一致：队列满则丢弃最旧的，
     * 全程使用 NO_WAIT，保证 ISR 上下文也能安全投递。 */
    if (osal_msg_queue_get_msg_num(g_ui_queue) >= UI_MSG_QUEUE_DEPTH) {
        ui_msg_t dummy;
        unsigned int sz = sizeof(dummy);
        (void)osal_msg_queue_read_copy(g_ui_queue, &dummy, &sz, OSAL_MSGQ_NO_WAIT);
    }
    (void)osal_msg_queue_write_copy(g_ui_queue, (void *)msg, sizeof(*msg), OSAL_MSGQ_NO_WAIT);
}

void ui_show_mode_page(CarStatus status)
{
    ui_msg_t msg = {0};
    msg.type = UI_MSG_MODE;
    msg.mode = status;
    ui_post(&msg);
}

void ui_render_standby(WifiConnectStatus wifi_state, const char *ip_addr)
{
    ui_msg_t msg = {0};
    msg.type = UI_MSG_STANDBY;
    msg.wifi_state = wifi_state;
    if (ip_addr != NULL) {
        (void)strncpy_s(msg.text, sizeof(msg.text), ip_addr, sizeof(msg.text) - 1);
    }
    ui_post(&msg);
}

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

bool ui_service_is_ready(void)
{
    return g_oled_ready;
}

void ui_service_acquire(void)
{
    g_ui_busy = true;
}
void ui_service_release(void)
{
    g_ui_busy = false;
}
bool ui_service_is_busy(void)
{
    return g_ui_busy;
}
