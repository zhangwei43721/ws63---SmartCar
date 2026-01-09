/**
 ****************************************************************************************************
 * @file        mutex_example.c
 * @author      SkyForever
 * @version     V1.1
 * @date        2025-03-28
 * @brief       LiteOS 蜂鸣器播放《大东北是我的家乡》
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
 * 实验现象：蜂鸣器播放声音
 *
 ****************************************************************************************************
 */

#include "bsp_include/bsp_beep.h"
#include "tcxo.h" // 系统延时

#define PWM_TASK_PRIO 24
#define PWM_TASK_STACK_SIZE 0x1000

/* ================== 音律频率定义 (Hz) ================== */
#define S 0 // 不发音

#define L1 (262)
#define L2 (294)
#define L3 (330)
#define L4 (350)
#define L5 (392)
#define L6 (440)
#define L7 (494)

#define M1 (524)
#define M2 (588)
#define M3 (660)
#define M4 (700)
#define M5 (784)
#define M6 (880)
#define M7 (988)

#define H1 (1048)
#define H2 (1176)
#define H3 (1320)
#define H4 (1480)
#define H5 (1640)
#define H6 (1760)
#define H7 (1976)

// 基础拍子时长 (ms)
#define T_UNIT 100

// 乐谱数组：{音调频率, 时长单位}
static const uint16_t music[][2] = {
    // --- 第一句：大东北是我的家乡 ---
    {M6, 4},
    {M6, 4}, // 大 东
    {M3, 2},
    {M5, 1},
    {M5, 4}, // 北 是
    {M3, 4},
    {M5, 2},
    {H1, 2}, // 我 的
    {M6, 8},
    {S, 3}, // 家 乡

    // --- 第二句：唢呐吹出了美美的模样 ---
    {M6, 4},
    {H1, 4}, // 唢 呐
    {H2, 2},
    {H1, 2},
    {M6, 4}, // 吹 出 了
    {M6, 2},
    {M5, 2}, // 美 美
    {M2, 2},
    {M5, 2}, // 的 模
    {M3, 7}, // 样
    {S, 3},

    // --- 第三句：哥们相聚必须整二两 ---
    {M6, 4},
    {M6, 4}, // 哥们
    {M6, 6},
    {S, 2}, // 相聚
    {M3, 2},
    {M3, 2}, // 必须
    {M1, 2},
    {M3, 2}, // 整二
    {M2, 7}, // 两
    {S, 3},

    // --- 第四句：醉了月亮暖了我心肠 ---
    {M5, 3},
    {M3, 1}, // 醉了
    {M5, 3},
    {M3, 1}, // 月亮
    {M5, 2},
    {M5, 3}, // 暖了
    {M3, 3},
    {H1, 2}, // 我心
    {M6, 7}, // 肠
    {S, 7}};

/**
 * @brief 执行音符播放逻辑
 * @param freq 频率 (Hz)
 * @param duration_ms 持续时间 (ms)
 */
static void play_note(uint16_t freq, uint32_t duration_ms)
{
    if (freq == S) {
        // 如果是静音，直接延时
        uapi_tcxo_delay_us(duration_ms * 500);
    } else {
        // 计算半周期时间 (us) = 1,000,000 / (freq * 2) = 500,000 / freq
        // 加上 (freq/2) 是为了四舍五入
        uint32_t half_period_us = (500000 + (freq / 2)) / freq;

        // 计算震动次数 = 频率 * (持续时间秒) = freq * duration_ms / 1000
        uint32_t loops = (freq * duration_ms) / 1000;

        if (loops == 0)
            loops = 1; // 至少响一次

        // 调用底层的 beep_alarm (阻塞执行，响完才返回)
        beep_alarm((uint16_t)loops, (uint16_t)half_period_us);
    }
}

static void *beep_task(const char *arg)
{
    UNUSED(arg);
    uint32_t i;
    uint32_t notes_count;

    // 1. 初始化GPIO
    beep_init();

    notes_count = sizeof(music) / sizeof(music[0]);

    while (1) {
        for (i = 0; i < notes_count; i++) {
            uint16_t freq = music[i][0];
            uint32_t duration = music[i][1] * T_UNIT;

            // 播放当前音符 (beep_alarm 是阻塞的，所以这里不需要额外的 delay_ms)
            play_note(freq, duration);

            // 断音处理：音符之间短暂的停顿，让旋律清晰
            // 如果不加这个，相同音符连在一起会听不出来是两个字
            uapi_tcxo_delay_us(20 * 1000);
        }

        // 播放完一遍，等待2秒
        uapi_tcxo_delay_us(2000 * 1000);
    }

    return NULL;
}

static void pwm_entry(void)
{
    osal_task *task_handle = NULL;
    osal_kthread_lock();
    task_handle = osal_kthread_create((osal_kthread_handler)beep_task, 0, "PwmTask", PWM_TASK_STACK_SIZE);
    if (task_handle != NULL) {
        osal_kthread_set_priority(task_handle, PWM_TASK_PRIO);
        osal_kfree(task_handle);
    }
    osal_kthread_unlock();
}

/* Run the pwm_entry. */
app_run(pwm_entry);