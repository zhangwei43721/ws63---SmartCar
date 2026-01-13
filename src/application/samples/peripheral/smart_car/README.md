# 智能小车模块 (Smart Car Module)

## 概述

本模块基于海思 WS63 芯片，提供了完整的智能小车控制框架。采用分层架构设计，将底层硬件驱动（Drivers）与上层业务逻辑（Apps）完全解耦，支持模块化开发与独立测试。

主要功能模块包括：

- **L9110S 电机驱动** - 基础运动控制（进/退/转）
- **HC-SR04 超声波传感器** - 测距与避障
- **TCRT5000 红外循迹传感器** - 黑线识别与循迹
- **SG90 舵机控制** - 角度控制与扫描
- **SSD1306 OLED 显示屏** - 状态显示 (I2C)
- **WiFi Client** - WiFi 网络连接管理
- **Bluetooth SPP Server** - 蓝牙透传控制服务
- **Robot Demo** - 集成循迹、避障、遥控的综合应用

## 架构设计

### 设计理念

1.  **驱动与应用分离**：
    *   `drivers/`：只包含硬件控制逻辑，提供纯粹的 C API，不含任何 `main` 函数。
    *   `apps/`：包含具体的业务逻辑（如测试用例或综合 Demo），每个 App 都是独立的入口。
2.  **扁平化结构**：
    *   为了简化开发，驱动模块不再嵌套 `src/include` 文件夹，头文件与源文件直接存放在模块根目录。
3.  **互斥运行模式**：
    *   通过 Kconfig 的 `Choice` 机制，确保同一时间只有一个 `main` 入口被编译，避免符号冲突。

### 目录结构

```text
application/samples/peripheral/smart_car/
├── CMakeLists.txt          # 顶层构建脚本（负责调度 Drivers 和 Apps）
├── Kconfig                 # 顶层菜单配置（定义 Run Mode）
├── README.md               # 本文件
│
├── drivers/                # 【硬件驱动层】(Hardware Abstraction Layer)
│   ├── CMakeLists.txt      # 驱动自动收集脚本
│   ├── l9110s/             # L9110S 驱动 (bsp_l9110s.c/h)
│   ├── hcsr04/             # HC-SR04 驱动
│   ├── tcrt5000/           # TCRT5000 驱动
│   ├── sg90/               # SG90 驱动
│   ├── ssd1306/            # SSD1306 OLED 驱动
│   ├── wifi_client/        # WiFi 连接封装
│   └── bt_spp_server/      # 蓝牙 SPP 封装
│
└── apps/                   # 【应用业务层】(Applications & Tests)
    ├── robot_demo/         # 智能小车综合应用 (循迹+避障+遥控)
    │   ├── CMakeLists.txt
    │   ├── robot_demo.c    # 主程序入口
    │   └── bsp_robot_...   # 仅供 Demo 使用的控制逻辑
    │
    ├── test_l9110s/        # 单元测试：电机
    ├── test_hcsr04/        # 单元测试：超声波
    ├── test_sg90/          # 单元测试：舵机
    ├── test_.../           # 其他单元测试
    └── CMakeLists.txt      # (各 App 内部均有独立的构建脚本)
```

## 硬件连接

> **重要提示**: 以下引脚定义基于当前代码配置，请严格按照此接线，否则可能导致设备无法工作或损坏！

### 1. L9110S 电机驱动

| 功能 | GPIO引脚 | 标识 | 说明 |
|------|---------|-----------|------|
| 左轮 A | GPIO 6 | MOTOR_IN1 | PWM控制 |
| 左轮 B | GPIO 7 | MOTOR_IN2 | PWM控制 |
| 右轮 A | GPIO 8 | MOTOR_IN3 | PWM控制 |
| 右轮 B | GPIO 9 | MOTOR_IN4 | PWM控制 |

### 2. 传感器与外设

| 模块 | 功能 | GPIO引脚 | 备注 |
|------|------|---------|------|
| **HC-SR04** | TRIG (触发) | GPIO 11 | - |
| | ECHO (接收) | GPIO 12 | - |
| **TCRT5000** | 左传感器 | GPIO 0 | KEY4 复用 |
| (循迹) | 中传感器 | GPIO 1 | KEY3 复用 |
| | 右传感器 | GPIO 2 | KEY2 复用 |
| **SG90** | PWM信号 | GPIO 13 | LED2 复用 |
| **SSD1306** | SCL (时钟) | GPIO 15 | I2C1 |
| (OLED) | SDA (数据) | GPIO 16 | I2C1 |
| **按键** | 模式切换 | GPIO 3 | KEY1 |

## 使用方法

### 1. 进入配置菜单

运行 menuconfig：

```bash
python build.py menuconfig
```

导航路径：
`Application` → `Config the application` → `Enable the Sample of peripheral` → 启用 `Support Smart Car Sample`。

进入 `Smart Car Configuration` 菜单。

### 2. 选择运行模式 (Run Mode)

本模块采用 **“单应用模式”**，你必须在 `Select Running Application` 菜单中选择**一项**具体的任务。系统会自动勾选该任务所需的底层驱动。

可选模式如下：

#### A. 综合应用
*   **Robot Demo (Tracking & Obstacle Avoidance)**
    *   这是默认的主程序。
    *   功能：集成循迹、避障、WiFi/蓝牙遥控。
    *   操作：通过 KEY1 按键切换工作模式（停止 -> 循迹 -> 避障 -> ...）。

#### B. 单元测试 (Unit Tests)
用于独立验证某个硬件模块是否正常工作：
*   **Test: L9110S Motor** - 电机转动测试（前/后/左/右）。
*   **Test: HC-SR04 Ultrasonic** - 串口打印测距数据 (cm)。
*   **Test: TCRT5000 Line Sensor** - 串口打印黑线识别状态。
*   **Test: SG90 Servo** - 舵机 0°~180° 往复扫描。
*   **Test: SSD1306 OLED** - 屏幕显示测试图案和文字。
*   **Test: WiFi Client** - 测试连接路由器并获取 IP。
*   **Test: Bluetooth SPP** - 开启蓝牙广播，测试数据透传。



## 功能详细说明

### 综合应用 (Robot Demo)

**工作模式循环：**
1.  **Idle (停止)**: 待机状态，屏幕显示 "Stop"。
2.  **Tracking (循迹)**: 依靠底部 3 路红外传感器沿黑线行驶。
3.  **Obstacle (避障)**: 依靠超声波测距，遇障碍物自动转向；舵机辅助扫描前方。
4.  **Remote (遥控)**:
    *   **WiFi**: 连接 TCP 服务端（端口 8888）。
    *   **Bluetooth**: 广播名称 "WS63_SmartCar"，支持 SPP 透传指令。

**控制协议 (TCP / SPP):**
*   `'0'` / `'F'`: 前进 (Forward)
*   `'1'` / `'B'`: 后退 (Backward)
*   `'2'` / `'L'`: 左转 (Left)
*   `'3'` / `'R'`: 右转 (Right)
*   `'4'` / `'S'`: 停止 (Stop)

### 驱动层 (Drivers)

所有驱动位于 `drivers/` 目录下，仅包含 `.c` 和 `.h` 文件。
*   **依赖管理**: 通过 CMake 的 `CONFIG_SMART_CAR_DRIVER_XXX` 开关自动控制编译。
*   **接口统一**: 提供标准的 Init / Deinit / Control 接口。

## 开发者指南

### 如何添加新模块？

1.  **创建驱动**: 在 `drivers/` 下新建文件夹（如 `beep`），放入 `bsp_beep.c/h`。
2.  **注册驱动**:
    *   在 `Kconfig` 的 "Drivers Status" 中添加 `CONFIG_SMART_CAR_DRIVER_BEEP`。
    *   在 `drivers/CMakeLists.txt` 中添加对应的编译规则。
3.  **创建测试**:
    *   在 `apps/` 下新建 `test_beep`，放入 `beep_example.c` 和 `CMakeLists.txt`。
    *   在 `Kconfig` 的 "Run Mode" Choice 中添加 `RUN_BEEP_TEST` 选项。


## 更新日期

2025-01-13