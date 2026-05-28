/**
 ****************************************************************************************************
 * @file        ssd1306_example.c
 * @author      SkyForever
 * @version     V1.0
 * @date        2025-01-12
 * @brief       SSD1306 OLED显示示例 (智能小车专用)
 * @license     Copyright (c) 2024-2034
 ****************************************************************************************************
 * @attention
 *
 * 实验平台:WS63
 *
 ****************************************************************************************************
 * 实验现象：OLED液晶显示字符信息和图形
 *
 ****************************************************************************************************
 **/

#include "pinctrl.h"
#include "common_def.h"
#include "soc_osal.h"
#include "i2c.h"
#include "osal_debug.h"
#include "../../drivers/ssd1306/ssd1306_fonts.h"
#include "../../drivers/ssd1306/ssd1306.h"
#include "app_init.h"

// I2C 引脚定义 (使用 I2C1)
#define CONFIG_I2C_SCL_MASTER_PIN 15 // I2C SCL引脚号
#define CONFIG_I2C_SDA_MASTER_PIN 16 // I2C SDA引脚号
#define CONFIG_I2C_MASTER_PIN_MODE 2 // I2C功能模式
#define I2C_MASTER_ADDR 0x0          // I2C主机地址
#define I2C_SLAVE_ADDR 0x3C          // OLED默认地址
#define I2C_SET_BANDRATE 400000      // 400kHz
#define I2C_TASK_STACK_SIZE 0x1000   // I2C OLED任务栈大小
#define I2C_TASK_PRIO 17             // I2C OLED任务优先级

/**
 * @brief 初始化I2C引脚
 * @return 无
 */
static void app_i2c_init_pin(void)
{
    uapi_pin_set_mode(CONFIG_I2C_SCL_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
    uapi_pin_set_mode(CONFIG_I2C_SDA_MASTER_PIN, CONFIG_I2C_MASTER_PIN_MODE);
}

/**
 * @brief OLED显示任务
 * @param arg 任务参数
 * @return 0
 */
static int ssd1306_task(void *arg)
{
    UNUSED(arg);
    uint32_t baudrate = I2C_SET_BANDRATE;
    uint32_t hscode = I2C_MASTER_ADDR;
    errcode_t ret;

    printf("SSD1306 OLED task start\n");

    // 初始化I2C引脚
    app_i2c_init_pin();

    // 初始化I2C主机
    ret = uapi_i2c_master_init(1, baudrate, hscode);
    if (ret != 0) {
        printf("I2C init failed, ret = 0x%x\n", ret);
        return 0;
    }
    printf("I2C master initialized successfully\n");

    // 初始化OLED
    bsp_ssd1306_init();
    printf("SSD1306 initialized\n");

    // 清屏
    bsp_ssd1306_fill(BSP_SSD1306_COLOR_BLACK);
    bsp_ssd1306_update_screen();

    // 显示欢迎信息
    bsp_ssd1306_set_cursor(0, 0);
    bsp_ssd1306_draw_string("Smart Car OLED", Font_7x10, BSP_SSD1306_COLOR_WHITE);

    bsp_ssd1306_set_cursor(0, 12);
    bsp_ssd1306_draw_string("==============", Font_7x10, BSP_SSD1306_COLOR_WHITE);

    bsp_ssd1306_set_cursor(0, 24);
    bsp_ssd1306_draw_string("WS63 Platform", Font_7x10, BSP_SSD1306_COLOR_WHITE);

    bsp_ssd1306_set_cursor(0, 36);
    bsp_ssd1306_draw_string("128x64 Pixels", Font_7x10, BSP_SSD1306_COLOR_WHITE);

    bsp_ssd1306_set_cursor(0, 48);
    bsp_ssd1306_draw_string("I2C Display", Font_7x10, BSP_SSD1306_COLOR_WHITE);

    // 更新屏幕
    bsp_ssd1306_update_screen();

    printf("OLED display updated\n");

    // 持续运行
    while (1) {
        osal_msleep(1000);
    }

    return 0;
}

/**
 * @brief SSD1306示例入口
 * @return 无
 */
static void ssd1306_example_entry(void)
{
    uint32_t ret;
    osal_task *task_handle = NULL;

    printf("SSD1306 OLED example entry\n");

    // 创建任务
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)ssd1306_task, NULL, "ssd1306_task", I2C_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        ret = osal_kthread_set_priority(task_handle, I2C_TASK_PRIO);
        if (ret != OSAL_SUCCESS) {
            printf("SSD1306: Failed to set task priority\n");
        }
    } else {
        printf("SSD1306: Failed to create task\n");
    }
    osal_kthread_unlock();
}

/* Run the ssd1306_example_entry. */
app_run(ssd1306_example_entry);
