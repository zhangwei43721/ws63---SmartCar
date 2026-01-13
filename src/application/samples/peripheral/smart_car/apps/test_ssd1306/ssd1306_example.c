/**
 ****************************************************************************************************
 * @file        ssd1306_example.c
 * @author      Smart Car Team
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
#define CONFIG_I2C_SCL_MASTER_PIN 15
#define CONFIG_I2C_SDA_MASTER_PIN 16
#define CONFIG_I2C_MASTER_PIN_MODE 2  // I2C功能模式
#define I2C_MASTER_ADDR 0x0
#define I2C_SLAVE_ADDR 0x3C            // OLED默认地址
#define I2C_SET_BANDRATE 400000        // 400kHz
#define I2C_TASK_STACK_SIZE 0x1000
#define I2C_TASK_PRIO 17

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
 * @return NULL
 */
static void *ssd1306_task(const char *arg)
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
        return NULL;
    }
    printf("I2C master initialized successfully\n");

    // 初始化OLED
    ssd1306_Init();
    printf("SSD1306 initialized\n");

    // 清屏
    ssd1306_Fill(Black);
    ssd1306_UpdateScreen();

    // 显示欢迎信息
    ssd1306_SetCursor(0, 0);
    ssd1306_DrawString("Smart Car OLED", Font_7x10, White);

    ssd1306_SetCursor(0, 12);
    ssd1306_DrawString("==============", Font_7x10, White);

    ssd1306_SetCursor(0, 24);
    ssd1306_DrawString("WS63 Platform", Font_7x10, White);

    ssd1306_SetCursor(0, 36);
    ssd1306_DrawString("128x64 Pixels", Font_7x10, White);

    ssd1306_SetCursor(0, 48);
    ssd1306_DrawString("I2C Display", Font_7x10, White);

    // 更新屏幕
    ssd1306_UpdateScreen();

    printf("OLED display updated\n");

    // 持续运行
    while (1) {
        osal_msleep(1000);
    }

    return NULL;
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
    task_handle = osal_kthread_create((osal_kthread_handler)ssd1306_task, NULL,
                                      "ssd1306_task", I2C_TASK_STACK_SIZE);
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
