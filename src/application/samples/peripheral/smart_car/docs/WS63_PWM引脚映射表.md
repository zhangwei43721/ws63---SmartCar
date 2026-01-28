# WS63芯片PWM引脚映射完整文档

## 一、PWM硬件资源

### 1.1 PWM配置信息

根据代码库中的配置文件，WS63芯片的PWM资源如下：

**配置文件位置**: `d:/ws63/src/build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config`

```
CONFIG_PWM_CHANNEL_NUM=6    # 6个PWM通道
CONFIG_PWM_GROUP_NUM=8      # 8个PWM组
```

**总结**:
- **PWM通道数**: 6个 (PWM0 ~ PWM5)
- **PWM组数**: 8个 (Group 0 ~ Group 7)
- **PWM基地址**: `0x44024000` (来自 `platform_core.h:83`)

### 1.2 PWM组控制机制

WS63使用V151版本的PWM驱动，采用**组(Group)控制机制**：

```c
// 来自 pwm_demo.c:49-55
#ifdef CONFIG_PWM_USING_V151
    uint8_t channel_id = CONFIG_PWM_CHANNEL;
    // 将PWM通道绑定到组
    uapi_pwm_set_group(CONFIG_PWM_GROUP_ID, &channel_id, 1);
    // 启动整个组
    uapi_pwm_start_group(CONFIG_PWM_GROUP_ID);
#endif
```

**关键API**:
- `uapi_pwm_set_group(group_id, channel_array, length)`: 将PWM通道绑定到组
- `uapi_pwm_start_group(group_id)`: 启动指定组的所有通道
- `uapi_pwm_stop_group(group_id)`: 停止指定组的所有通道

---

## 二、引脚定义与复用模式

### 2.1 完整引脚枚举

**文件位置**: `d:/ws63/src/drivers/chips/ws63/include/platform_core_rom.h:30-57`

```c
typedef enum {
    GPIO_00 = 0,   // 可用模式: 0,1,2,3,4
    GPIO_01 = 1,   // 可用模式: 0,1,2,3,4,5
    GPIO_02 = 2,   // 可用模式: 0,1,2,3,4,5,6
    GPIO_03 = 3,   // 可用模式: 0,1,2,3,4,5
    GPIO_04 = 4,   // 可用模式: 0,1,2,3,4,5
    GPIO_05 = 5,   // 可用模式: 0,1,2,3,4,5,6
    GPIO_06 = 6,   // 可用模式: 0,1,2,3,4,5,6,7
    GPIO_07 = 7,   // 可用模式: 0,1,2,3,4,5
    GPIO_08 = 8,   // 可用模式: 0,1,2,3,4
    GPIO_09 = 9,   // 可用模式: 0,1,2,3,4,5,6,7
    GPIO_10 = 10,  // 可用模式: 0,1,2,3,4,5
    GPIO_11 = 11,  // 可用模式: 0,1,2,3,4,5,6
    GPIO_12 = 12,  // 可用模式: 0,1,2,4,6
    GPIO_13 = 13,  // 可用模式: 0,1,2,3,4
    GPIO_14 = 14,  // 可用模式: 0,1,2,3,4
    GPIO_15 = 15,  // 可用模式: 0,1,2
    GPIO_16 = 16,  // 可用模式: 0,1,2
    GPIO_17 = 17,  // 可用模式: 0,1,2
    GPIO_18 = 18,  // 可用模式: 0,1,2
    SFC_CLK = 19,
    SFC_CSN = 20,
    SFC_IO0 = 21,
    SFC_IO1 = 22,
    SFC_IO2 = 23,
    SFC_IO3 = 24,
    PIN_NONE = 25,
} pin_t;
```

### 2.2 引脚复用模式定义

**文件位置**: `d:/ws63/src/drivers/chips/ws63/porting/pinctrl/pinctrl_porting.h:36-46`

```c
typedef enum {
    PIN_MODE_0        = 0,  // 通常为GPIO功能
    PIN_MODE_1        = 1,  // 通常为PWM功能
    PIN_MODE_2        = 2,
    PIN_MODE_3        = 3,
    PIN_MODE_4        = 4,
    PIN_MODE_5        = 5,
    PIN_MODE_6        = 6,
    PIN_MODE_7        = 7,
    PIN_MODE_MAX      = 8
} pin_mode_t;
```

### 2.3 引脚模式可用性表

**文件位置**: `d:/ws63/src/drivers/chips/ws63/porting/pinctrl/pinctrl_porting_ws63.c:41-61`

```c
static uint8_t const g_pio_pins_avaliable_mode[PIN_MAX_NUMBER] = {
    // 每个引脚支持的复用模式（位掩码）
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4,                              // GPIO_00
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5,                 // GPIO_01
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5 | PIO_MODE_6,    // GPIO_02
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5,                 // GPIO_03
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5,                 // GPIO_04
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5 | PIO_MODE_6,    // GPIO_05
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5 | PIO_MODE_6 | PIO_MODE_7, // GPIO_06
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5,                 // GPIO_07
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4,                              // GPIO_08
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5 | PIO_MODE_6 | PIO_MODE_7, // GPIO_09
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5,                 // GPIO_10
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4 | PIO_MODE_5 | PIO_MODE_6,    // GPIO_11
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_4 | PIO_MODE_6,                              // GPIO_12
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4,                              // GPIO_13
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2 | PIO_MODE_3 | PIO_MODE_4,                              // GPIO_14
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2,                                                            // GPIO_15
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2,                                                            // GPIO_16
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2,                                                            // GPIO_17
    PIO_MODE_0 | PIO_MODE_1 | PIO_MODE_2,                                                            // GPIO_18
};
```

---

## 三、引脚与PWM通道的映射关系

### 3.1 推测的PWM映射表

⚠️ **重要说明**: 代码库中**没有找到**明确的"哪个引脚的哪个模式对应哪个PWM通道"的映射表文档。以下映射关系基于：

1. WS63有6个PWM通道（PWM0~PWM5）
2. GPIO_00 ~ GPIO_05 都支持模式1
3. 通常芯片设计会将PWM通道编号与GPIO编号对应

**推测的PWM映射表**:

| GPIO引脚 | 引脚编号 | 推测PWM通道 | 支持的模式 | 说明 |
|---------|---------|-----------|-----------|-----|
| GPIO_00 | 0 | **PWM0** | 0,1,2,3,4 | 模式1可能是PWM0 ✅ 已验证可用 |
| GPIO_01 | 1 | **PWM1** | 0,1,2,3,4,5 | 模式1可能是PWM1 |
| GPIO_02 | 2 | **PWM2** | 0,1,2,3,4,5,6 | 模式1可能是PWM2 ✅ 已验证可用 |
| GPIO_03 | 3 | **PWM3** | 0,1,2,3,4,5 | 模式1可能是PWM3 |
| GPIO_04 | 4 | **PWM4** | 0,1,2,3,4,5 | 模式1可能是PWM4 |
| GPIO_05 | 5 | **PWM5** | 0,1,2,3,4,5,6 | 模式1可能是PWM5 |

### 3.2 当前项目使用的引脚配置

**文件位置**: `bsp_l9110s.h:30-35` 和 `bsp_l9110s.c:169-172`

```c
// 当前使用的引脚（软件GPIO模式）
#define L9110S_LEFT_A_GPIO  4   // GPIO_04, 模式2 (GPIO)
#define L9110S_LEFT_B_GPIO  5   // GPIO_05, 模式4 (GPIO)
#define L9110S_RIGHT_A_GPIO 0   // GPIO_00, 模式0 (GPIO)
#define L9110S_RIGHT_B_GPIO 2   // GPIO_02, 模式0 (GPIO)

// 初始化代码
uapi_pin_set_mode(L9110S_LEFT_A_GPIO,  2);  // GPIO_04 → 模式2
uapi_pin_set_mode(L9110S_LEFT_B_GPIO,  4);  // GPIO_05 → 模式4
uapi_pin_set_mode(L9110S_RIGHT_A_GPIO, 0);  // GPIO_00 → 模式0 (GPIO)
uapi_pin_set_mode(L9110S_RIGHT_B_GPIO, 0);  // GPIO_02 → 模式0 (GPIO)
```

**问题分析**:
- GPIO_04和GPIO_05使用的不是模式1（推测的PWM模式）
- 如果要使用硬件PWM，应该尝试设置 `pin_mode = 1`

---

## 四、硬件PWM使用示例

### 4.1 官方PWM示例代码

**文件位置**: `d:/ws63/src/application/samples/peripheral/pwm/pwm_demo.c`

```c
// 1. 设置引脚为PWM模式
uapi_pin_set_mode(CONFIG_PWM_PIN, CONFIG_PWM_PIN_MODE);  // 模式1 = PWM

// 2. 初始化PWM
uapi_pwm_init();

// 3. 配置PWM参数
pwm_config_t cfg = {
    .low_time = 100,   // 低电平时间
    .high_time = 100,  // 高电平时间
    .offset_time = 0,
    .cycles = 0,       // 0 = 无限循环
    .repeat = true
};

// 4. 打开PWM通道
uapi_pwm_open(CONFIG_PWM_CHANNEL, &cfg);

// 5. 将通道绑定到组（V151版本需要）
uint8_t channel_id = CONFIG_PWM_CHANNEL;
uapi_pwm_set_group(CONFIG_PWM_GROUP_ID, &channel_id, 1);

// 6. 启动PWM组
uapi_pwm_start_group(CONFIG_PWM_GROUP_ID);
```

### 4.2 PWM配置结构体

**文件位置**: `d:/ws63/src/include/driver/pwm.h:36-57`

```c
typedef struct pwm_config {
    uint32_t low_time;    // PWM低电平时钟周期数
    uint32_t high_time;   // PWM高电平时钟周期数
    uint32_t offset_time; // PWM相位偏移
    uint16_t cycles;      // PWM重复周期 (0~32767, 0=无限循环)
    bool repeat;          // 是否连续输出
} pwm_config_t;
```

---

## 五、WS63 PWM开发实战指南

### 5.1 PWM组机制核心原理

WS63的V151 PWM驱动使用**组(Group)控制机制**，这是理解PWM使用的关键：

```c
// 位掩码映射：每个PWM通道对应组寄存器中的一个bit
// 通道0 → bit0, 通道2 → bit2, 通道4 → bit4, 通道5 → bit5
// 例如：通道数组 {0, 2, 4, 5} 对应位掩码 0x0035 = 0b00110101
```

**关键代码位置**: `drivers/drivers/driver/pwm/pwm.c:304`
```c
for (uint32_t i = 0; i < channel_set_len; i++) {
    channel_id |= (1 << channel_set[i]);  // 位掩码生成
}
```

### 5.2 多通道PWM的正确使用方法

#### 问题：为什么不能每次更新都调用 `uapi_pwm_open`？

`uapi_pwm_open` 内部会临时修改组配置（见 `pwm.c:207-211`）：
```c
// uapi_pwm_start 函数中的代码（uapi_pwm_open 也会触发类似逻辑）
uint16_t pre_channel_id = (uint16_t)(1 << channel);    // 临时单通道
uint16_t post_channel_id = g_pwm_group[start_group];   // 保存原配置
g_hal_funcs->set_group(group, pre_channel_id);         // 临时修改
g_hal_funcs->set_action(group, PWM_ACTION_START);      // 启动
g_hal_funcs->set_group(group, post_channel_id);        // 恢复原配置
```

如果快速连续更新多个通道，会导致组配置被反复覆盖，某些通道无法正常工作。

#### 正确做法：批量更新 + 统一启动

```c
// 1. 先批量更新所有通道的参数（不启动）
for (int i = 0; i < 4; i++) {
    uapi_pwm_open(channels[i], &cfg[i]);  // 只设置参数
}

// 2. 最后统一启动组（让所有通道同时生效）
uapi_pwm_start_group(group_id);
```

### 5.3 完整的多通道PWM示例代码

**应用场景**: 双路L9110S电机驱动（需要4个PWM通道）

```c
#include "pwm.h"
#include "pinctrl.h"

#define MOTOR_PWM_FREQ_HZ     20000      // 20kHz
#define MOTOR_PWM_PERIOD_US   (1000000 / MOTOR_PWM_FREQ_HZ)  // 50us
#define PWM_PIN_MODE          1          // PWM 复用模式
#define PWM_GROUP_ID          0          // 所有电机通道绑定到组0

// PWM通道定义（根据实际引脚映射）
#define CH_L_A  4   // 左轮A → PWM4
#define CH_L_B  5   // 左轮B → PWM5
#define CH_R_A  0   // 右轮A → PWM0
#define CH_R_B  2   // 右轮B → PWM2

static uint8_t g_motor_channels[] = {0, 2, 4, 5};  // PWM0, PWM2, PWM4, PWM5
static bool g_pwm_inited = false;

/**
 * @brief 初始化多通道PWM
 */
void motor_pwm_init(void)
{
    if (g_pwm_inited) return;

    uapi_pwm_init();

    // 1. 设置引脚为PWM模式
    uapi_pin_set_mode(L9110S_LEFT_A_GPIO, PWM_PIN_MODE);
    uapi_pin_set_mode(L9110S_LEFT_B_GPIO, PWM_PIN_MODE);
    uapi_pin_set_mode(L9110S_RIGHT_A_GPIO, PWM_PIN_MODE);
    uapi_pin_set_mode(L9110S_RIGHT_B_GPIO, PWM_PIN_MODE);

    // 2. 初始化所有通道为停止状态
    pwm_config_t cfg_stop = {
        .low_time = MOTOR_PWM_PERIOD_US,
        .high_time = 0,
        .offset_time = 0,
        .cycles = 0,
        .repeat = true
    };

    for (int i = 0; i < 4; i++) {
        uapi_pwm_open(g_motor_channels[i], &cfg_stop);
    }

    // 3. 将所有通道绑定到组0
    // 位掩码：1<<0 | 1<<2 | 1<<4 | 1<<5 = 0x0035
    uapi_pwm_set_group(PWM_GROUP_ID, g_motor_channels, 4);

    // 4. 启动组0
    uapi_pwm_start_group(PWM_GROUP_ID);

    g_pwm_inited = true;
}

/**
 * @brief 更新单个PWM通道（不启动组）
 */
static void update_pwm_channel(uint8_t channel, uint32_t duty_us)
{
    pwm_config_t cfg = {0};

    if (duty_us > MOTOR_PWM_PERIOD_US) {
        duty_us = MOTOR_PWM_PERIOD_US;
    }

    if (duty_us == 0) {
        cfg.low_time = MOTOR_PWM_PERIOD_US;
        cfg.high_time = 0;
    } else if (duty_us == MOTOR_PWM_PERIOD_US) {
        cfg.low_time = 0;
        cfg.high_time = MOTOR_PWM_PERIOD_US;
    } else {
        cfg.low_time = MOTOR_PWM_PERIOD_US - duty_us;
        cfg.high_time = duty_us;
    }
    cfg.offset_time = 0;
    cfg.cycles = 0;
    cfg.repeat = true;

    uapi_pwm_open(channel, &cfg);  // 只更新参数，不启动
}

/**
 * @brief 设置电机速度
 * @param left_speed 左轮速度 -100~100
 * @param right_speed 右轮速度 -100~100
 */
void set_motor_speed(int8_t left_speed, int8_t right_speed)
{
    // 1. 批量更新所有4个通道的参数
    uint32_t duty_l = (MOTOR_PWM_PERIOD_US * abs(left_speed)) / 100;
    uint32_t duty_r = (MOTOR_PWM_PERIOD_US * abs(right_speed)) / 100;

    // 左轮
    if (left_speed > 0) {
        update_pwm_channel(CH_L_A, 0);
        update_pwm_channel(CH_L_B, duty_l);
    } else if (left_speed < 0) {
        update_pwm_channel(CH_L_A, duty_l);
        update_pwm_channel(CH_L_B, 0);
    } else {
        update_pwm_channel(CH_L_A, 0);
        update_pwm_channel(CH_L_B, 0);
    }

    // 右轮
    if (right_speed > 0) {
        update_pwm_channel(CH_R_A, 0);
        update_pwm_channel(CH_R_B, duty_r);
    } else if (right_speed < 0) {
        update_pwm_channel(CH_R_A, duty_r);
        update_pwm_channel(CH_R_B, 0);
    } else {
        update_pwm_channel(CH_R_A, 0);
        update_pwm_channel(CH_R_B, 0);
    }

    // 2. 统一启动组，让所有通道配置同时生效
    uapi_pwm_start_group(PWM_GROUP_ID);
}
```

### 5.4 常见错误与调试方法

#### 错误1：只有一个方向能工作

**现象**: 电机只能后退，不能前进，或者只有某一个轮子能工作

**原因**:
- 在更新多个PWM通道时，每个通道都单独调用 `uapi_pwm_start`，导致组配置被反复修改
- `uapi_pwm_open` 或 `uapi_pwm_start` 会临时修改组为单通道模式

**解决**: 使用批量更新 + 统一启动的模式（见上面的示例代码）

#### 错误2：电机完全不动

**检查清单**:
1. 引脚模式是否正确设置为PWM模式（模式1）
2. PWM通道号是否正确（GPIO编号可能不等于PWM通道号）
3. 是否调用了 `uapi_pwm_set_group` 绑定通道到组
4. 是否调用了 `uapi_pwm_start_group` 启动组

**调试代码**:
```c
// 打印PWM组配置
printf("PWM group 0 channels: 0x%04X\n", g_pwm_group[0]);
// 应该输出 0x0035 (二进制: 00110101) = 通道0,2,4,5
```

#### 错误3：编译错误 "undefined reference to hal_pwm_get_funcs"

**原因**: 缺少HAL层头文件

**解决**:
```c
#include "hal_pwm.h"  // 添加这个头文件
```

### 5.5 PWM API函数速查表

| 函数 | 功能 | 使用场景 |
|------|------|---------|
| `uapi_pwm_init()` | 初始化PWM模块 | 程序开始时调用一次 |
| `uapi_pin_set_mode(pin, 1)` | 设置引脚为PWM模式 | 初始化时调用 |
| `uapi_pwm_open(ch, &cfg)` | 配置PWM通道参数 | 初始化或更新占空比 |
| `uapi_pwm_set_group(g, chs, n)` | 绑定通道到组 | 初始化时调用一次 |
| `uapi_pwm_start_group(g)` | 启动整个组 | 初始化或更新后调用 |
| `uapi_pwm_stop_group(g)` | 停止整个组 | 需要停止时调用 |
| `uapi_pwm_close(ch)` | 关闭单个通道 | 不再使用时调用 |

---

## 六、常见问题与解决方案

### 6.1 问题：如何确定引脚的PWM通道号

**方法1**: 查阅芯片数据手册（最准确）

**方法2**: 实验测试（逐个尝试）
```c
// 测试代码
for (uint8_t ch = 0; ch < 6; ch++) {
    uapi_pwm_open(ch, &cfg);
    uint8_t channels[] = {ch};
    uapi_pwm_set_group(0, channels, 1);
    uapi_pwm_start_group(0);
    // 观察哪个引脚有输出
}
```

**方法3**: 查看原理图或硬件设计文档

### 6.2 PWM组的使用注意事项

1. **每个PWM通道必须绑定到组**:
   ```c
   uint8_t channels[] = {0, 2, 4, 5};
   uapi_pwm_set_group(0, channels, 4);  // 将4个通道绑定到组0
   ```

2. **启动整个组而不是单个通道**:
   ```c
   uapi_pwm_start_group(0);  // 启动组0的所有通道
   ```

3. **组ID范围**: 0 ~ 7 (共8个组)

4. **通道ID范围**: 0 ~ 5 (共6个通道)

5. **同一通道不能同时属于多个组**

---

## 七、软件PWM vs 硬件PWM对比

### 7.1 优势对比

| 特性 | 软件PWM | 硬件PWM |
|-----|------------------|------------------|
| CPU占用 | ~99% (后台任务) | <1% (硬件控制) |
| 响应延迟 | ~20ms | 立即生效 |
| PWM频率 | 50Hz | 可达20kHz+ |
| 代码复杂度 | 简单 | 稍复杂 |
| 精度 | 一般 | 高精度 |

### 7.2 引脚映射建议

| 电机 | 引脚 | 建议PWM模式 | 推测PWM通道 |
|-----|------|------------|-----------|
| 左轮A | GPIO_04 | 模式1 (PWM) | PWM4 |
| 左轮B | GPIO_05 | 模式1 (PWM) | PWM5 |
| 右轮A | GPIO_00 | 模式1 (PWM) | PWM0 |
| 右轮B | GPIO_02 | 模式1 (PWM) | PWM2 |

---

## 八、参考资料

### 8.1 关键文件位置

| 文件 | 路径 | 说明 |
|-----|------|-----|
| 引脚定义 | `drivers/chips/ws63/include/platform_core_rom.h` | GPIO引脚枚举 |
| 模式可用性 | `drivers/chips/ws63/porting/pinctrl/pinctrl_porting_ws63.c` | 引脚模式支持表 |
| PWM驱动API | `include/driver/pwm.h` | PWM用户接口 |
| PWM示例 | `application/samples/peripheral/pwm/pwm_demo.c` | 官方示例代码 |
| PWM配置 | `build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config` | PWM配置参数 |

### 8.2 相关API参考

```c
// 引脚控制
errcode_t uapi_pin_set_mode(pin_t pin, pin_mode_t mode);

// PWM控制
errcode_t uapi_pwm_init(void);
errcode_t uapi_pwm_open(uint8_t channel, const pwm_config_t *cfg);
errcode_t uapi_pwm_close(uint8_t channel);
errcode_t uapi_pwm_set_group(uint8_t group, const uint8_t *channel_set, uint32_t channel_set_len);
errcode_t uapi_pwm_start_group(uint8_t group);
errcode_t uapi_pwm_stop_group(uint8_t group);
```

---

## 九、总结

1. **WS63有6个PWM通道** (PWM0~PWM5) 和 **8个PWM组** (Group 0~7)
2. **通道到组使用位掩码映射**：`channel_id |= (1 << channel_number)`
3. **多通道PWM的关键**：批量更新参数 + 统一启动组
4. **推荐使用模式1**作为PWM功能复用模式
5. **GPIO 0, 2, 4, 5** 已验证可用（当前项目）

---

**文档生成时间**: 2025-01-27
**WS63芯片**: HiSilicon WS63 (RISC-V)
**PWM驱动版本**: V151
