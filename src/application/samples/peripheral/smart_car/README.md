# 智能小车模块 (Smart Car Module)

## 概述

本模块提供了完整的智能小车控制功能，包括：

- **L9110S 电机驱动** - 控制小车前进、后退、左转、右转、停止
- **HC-SR04 超声波传感器** - 距离测量，用于避障
- **TCRT5000 红外循迹传感器** - 黑线检测，用于循迹
- **SG90 舵机控制** - 控制舵机转动，用于扫描障碍物
- **SSD1306 OLED 显示屏** - 显示小车状态信息
- **智能循迹避障** - 综合应用，支持模式切换

## 目录结构

```
application/samples/peripheral/smart_car/
├── CMakeLists.txt          # 主构建配置
├── Kconfig                 # 配置选项
├── README.md               # 本文件
├── l9110s/                 # L9110S 电机驱动模块
│   ├── bsp_include/        # BSP层头文件
│   ├── bsp_src/            # BSP层源文件
│   ├── l9110s_example.c    # 示例代码
│   ├── CMakeLists.txt
│   └── Kconfig
├── hcsr04/                 # HC-SR04 超声波传感器模块
│   ├── bsp_include/
│   ├── bsp_src/
│   ├── hcsr04_example.c
│   ├── CMakeLists.txt
│   └── Kconfig
├── tcrt5000/               # TCRT5000 红外循迹传感器模块
│   ├── bsp_include/
│   ├── bsp_src/
│   ├── tcrt5000_example.c
│   ├── CMakeLists.txt
│   └── Kconfig
├── sg90/                   # SG90 舵机模块
│   ├── bsp_include/        # BSP层头文件
│   ├── bsp_src/            # BSP层源文件
│   ├── sg90_example.c      # 示例代码
│   ├── CMakeLists.txt
│   └── Kconfig
├── ssd1306/                # SSD1306 OLED 显示屏模块
│   ├── ssd1306.h           # OLED驱动头文件
│   ├── ssd1306.c           # OLED驱动源文件
│   ├── ssd1306_fonts.h     # 字体数据
│   ├── ssd1306_fonts.c     # 字体数据
│   ├── ssd1306_example.c   # 示例代码
│   ├── CMakeLists.txt
│   └── Kconfig
└── robot_demo/             # 智能循迹避障综合模块
    ├── bsp_include/        # 包含机器人控制BSP
    ├── bsp_src/
    ├── robot_demo.c
    ├── CMakeLists.txt
    └── Kconfig
```

## 硬件连接

> **重要提示**: 以下引脚定义根据硬件原理图确定，请严格按照此接线！

### L9110S 电机驱动引脚 (板载已硬连线)

| 功能 | GPIO引脚 | 原理图标识 | 说明 |
|------|---------|-----------|------|
| 左轮电机 A | GPIO 6 | MOTOR_IN1 | 左轮电机控制A |
| 左轮电机 B | GPIO 7 | MOTOR_IN2 | 左轮电机控制B |
| 右轮电机 A | GPIO 8 | MOTOR_IN3 | 右轮电机控制A |
| 右轮电机 B | GPIO 9 | MOTOR_IN4 | 右轮电机控制B |

### HC-SR04 超声波传感器引脚 (使用独立GPIO)

| 功能 | GPIO引脚 | 原理图标识 | 接线位置 |
|------|---------|-----------|----------|
| TRIG | GPIO 11 | - | JP5排母上方 |
| ECHO | GPIO 12 | - | JP5排母上方 |
| VCC | 5V | - | JP5排母 VCC5 |
| GND | GND | - | JP5排母 GND |

### TCRT5000 红外循迹传感器引脚 (复用按键接口 KEY4-KEY2)

| 功能 | GPIO引脚 | 原理图标识 | 接线位置 |
|------|---------|-----------|----------|
| 左侧传感器 | GPIO 0 | KEY4 | JP5排母上方 |
| 中间传感器 | GPIO 1 | KEY3 | JP5排母上方 |
| 右侧传感器 | GPIO 2 | KEY2 | JP5排母上方 |
| VCC | 3.3V/5V | - | 根据模块电压要求 |
| GND | GND | - | JP5排母 GND |

### SG90 舵机引脚 (板载舵机接口)

| 功能 | GPIO引脚 | 原理图标识 | 说明 |
|------|---------|-----------|------|
| 舵机控制 | GPIO 13 | SG1 (JP4) | 注意: 与LED2共用，控制时蓝灯会闪烁 |
| VCC | 5V | - | 外部供电 |
| GND | GND | - | 公共地 |

### 模式切换按键 (复用KEY1)

| 功能 | GPIO引脚 | 原理图标识 | 接线位置 |
|------|---------|-----------|----------|
| 模式切换 | GPIO 3 | KEY1 | JP5排母上方 |

### SSD1306 OLED 显示屏引脚 (I2C接口)

| 功能 | GPIO引脚 | 原理图标识 | 接线位置 |
|------|---------|-----------|----------|
| SCL | GPIO 15 | I2C1_SCL / UART1_TX | JP5排母下方 |
| SDA | GPIO 16 | I2C1_SDA / UART1_RX | JP5排母下方 |
| VCC | 3.3V/5V | - | 根据模块要求 |
| GND | GND | - | JP5排母 GND |

## 使用方法

### 1. 启用模块

运行 menuconfig 配置：

```bash
python build.py menuconfig
```

导航到：`Application` → `Config the application` → `Enable the Sample of peripheral` → 启用 `Support Smart Car Sample.`

然后选择需要启用的子模块：

**外设模块（自动被robot_demo依赖）:**
- L9110S Motor Driver
- HC-SR04 Ultrasonic Sensor
- TCRT5000 Infrared Line Tracking Sensor
- SG90 Servo Motor
- SSD1306 OLED Display

**示例程序（可选）:**
- SG90 Servo Example
- SSD1306 OLED Display Example

**综合应用:**
- Robot Demo (Tracking & Obstacle Avoidance) - 自动依赖上述外设模块

### 2. 编译项目

```bash
python build.py
```

### 3. 独立模块测试

如果只想测试单个模块，可以在对应的 `Kconfig` 中禁用 `robot_demo`，单独启用需要测试的模块。

#### L9110S 电机测试

小车会依次执行：前进 → 后退 → 左转 → 右转 → 停止，循环往复。

#### HC-SR04 超声波测试

持续测量前方障碍物距离并通过串口输出（单位：cm）。

#### TCRT5000 红外传感器测试

持续读取左、中、右三路传感器状态并输出检测结果。

#### SG90 舵机测试

舵机从0度转到180度，再从180度转回0度，循环往复。

#### SSD1306 OLED 显示测试

OLED屏幕显示欢迎信息和系统状态。

### 4. 智能循迹避障模式

启用 `robot_demo` 后，小车支持三种工作模式，通过按键切换：

1. **停止模式** (默认) - 小车停止不动
2. **循迹模式** - 小车根据红外传感器自动循迹
3. **避障模式** - 小车根据超声波传感器自动避障

**模式切换方法：**
- 按一下按键：从停止 → 循迹
- 再按一下：从循迹 → 避障
- 再按一下：从避障 → 停止

## 代码特性

1. **独立性** - 智能小车模块完全独立，不依赖项目中的其他模块（如 sg90、oled）
2. **BSP层设计** - 每个外设都有独立的BSP层，便于移植和维护
3. **统一接口** - 所有外设使用统一的WS63 HAL API
4. **模块化** - 可以单独启用/禁用各个子模块

## 注意事项

1. 确保硬件连接正确，特别是GPIO引脚定义
2. 电机驱动需要外部供电，WS63的GPIO无法直接驱动电机
3. 超声波传感器测量范围约为 2cm - 400cm
4. 红外传感器需要正确调整高度和角度，以准确检测黑线
5. 舵机使用软件模拟PWM，会占用CPU时间
6. OLED显示屏使用I2C1接口，波特率默认400kHz


## 作者

SkyForever

## 更新日期

2025-01-12
