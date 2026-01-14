# 鸿蒙 WS63 智能小车 (Smart Car K12 Edition)

## 概述

本模块基于海思 WS63 芯片，为 K12 教育及嵌入式教学提供了完整的智能小车控制框架。采用分层架构设计，将底层硬件驱动（Drivers）、功能逻辑层（Middleware）与上层业务逻辑（Apps）解耦，支持模块化开发与独立测试。

主要功能模块包括：

- **L9110S 电机驱动** - 基础运动控制（PWM 线性调速）
- **HC-SR04 超声波传感器** - 测距与避障
- **TCRT5000 红外循迹传感器** - 黑线识别与循迹
- **SG90 舵机控制** - 角度控制与扫描
- **SSD1306 OLED 显示屏** - 状态显示 (I2C)
- **WiFi Client** - WiFi 网络连接与 TCP/UDP 通信
- **Bluetooth SPP Server** - 蓝牙透传控制服务
- **Robot Demo** - 集成状态机管理的综合应用

## 架构设计

### 1. 软件分层架构

为了实现代码的高复用性与清晰度，本项目采用三层逻辑架构：

1.  **应用业务层 (Applications)**
    *   位于 `apps/` 目录。
    *   负责任务调度、状态机管理 (State Machine)、人机交互 (OLED/按键)。
    *   核心入口：`robot_demo.c`。
2.  **功能服务层 (Services/Middleware)**
    *   负责将底层硬件原子能力封装为车辆的具体动作（如“直行”、“摇头扫描”、“解析协议”）。
    *   核心逻辑：`robot_service.c`。
3.  **硬件驱动层 (Drivers)**
    *   位于 `drivers/` 目录。
    *   纯粹的硬件抽象层 (HAL)，只提供标准的 Init/Write/Read 接口，不包含业务逻辑。

### 2. 目录结构

```text
application/samples/peripheral/smart_car/
├── CMakeLists.txt          # 顶层构建脚本
├── Kconfig                 # 顶层配置菜单
├── README.md               # 本文件
│
├── drivers/                # 【硬件驱动层】
│   ├── l9110s/             # 电机驱动
│   ├── hcsr04/             # 超声波驱动
│   ├── tcrt5000/           # 红外循迹驱动
│   ├── sg90/               # 舵机驱动
│   ├── ssd1306/            # OLED 驱动
│   ├── wifi_client/        # WiFi 通信封装
│   └── bt_spp_server/      # 蓝牙 SPP 封装
│
└── apps/                   # 【应用业务层】
    ├── robot_demo/         # [核心] 智能小车综合应用 (循迹+避障+遥控)
    │   ├── CMakeLists.txt
    │   ├── robot_demo.c    # 主程序：状态机与任务入口
    │   └── robot_service.c # 业务逻辑：车辆控制算法与协议解析
    │
    ├── test_l9110s/        # 单元测试：电机
    ├── test_hcsr04/        # 单元测试：超声波
    ├── test_sg90/          # 单元测试：舵机
    ├── test_tcrt5000/      # 单元测试：红外循迹
    ├── test_ssd1306/       # 单元测试：OLED显示
    ├── test_wifi/          # 单元测试：WiFi连接
    ├── test_bt_spp/        # 单元测试：蓝牙透传
    └── CMakeLists.txt
```

## 硬件连接

> **重要提示**: 以下引脚定义基于当前代码配置，请严格按照此接线。

| 模块 | 功能 | GPIO引脚 | 备注 |
| :--- | :--- | :--- | :--- |
| **L9110S 电机** | 左轮 A/B | GPIO 6 / 7 | PWM 控制 |
| | 右轮 A/B | GPIO 8 / 9 | PWM 控制 |
| **HC-SR04** | TRIG / ECHO | GPIO 11 / 12 | 超声波测距 |
| **TCRT5000** | 左 / 中 / 右 | GPIO 4 / 2 / 0 | 循迹传感器 |
| **SG90 舵机** | PWM 信号 | GPIO 13 | 头部舵机 |
| **SSD1306** | SCL / SDA | GPIO 15 / 16 | I2C OLED 显示屏 |
| **按键** | 模式切换 | GPIO 3 | KEY1 (低电平触发) |

## 运行模式与功能说明

本模块采用 **“单应用模式”**，请在 `menuconfig` 的 `Select Running Application` 中选择对应的任务。

### 1. 综合应用 (Robot Demo)

**功能**：集成循迹、避障、WiFi遥控的完整业务逻辑。
**操作**：单击 KEY1 按键循环切换以下 4 种模式。

*   **待机模式 (Standby)**
    *   **行为**：电机锁定，LED 呼吸闪烁。
    *   **显示**：OLED 显示本机 IP 地址及 "Mode: Standby"。
*   **遥控模式 (Remote)**
    *   **行为**：响应 TCP/UDP 上位机指令，支持线性调速与舵机转向。
    *   **保护**：具备 500ms 超时急停保护。
*   **循迹模式 (Tracking)**
    *   **行为**：根据底部 3 路红外传感器状态，自动沿黑线行驶。
*   **避障模式 (Avoid)**
    *   **行为**：自动前进。当距离 < 20cm 时，停车并操作舵机“摇头”寻找无障碍路径。

#### 远程控制协议 (Remote Protocol)

上位机通过 WiFi 发送 4 字节 Hex 数据流（25ms/帧）：

| 字节 | 类型 | 含义 | 说明 |
| :--- | :--- | :--- | :--- |
| **Byte 0** | `int8_t` | **动力** | `0`:停, `[1,100]`:前, `[-100,-1]`:后 (补码) |
| **Byte 1** | `int8_t` | **方向** | `0`:中, `[1,100]`:左, `[-100,-1]`:右 (补码) |
| **Byte 2** | `int8_t` | **备用** | 保留位 |
| **Byte 3** | `int8_t` | **校验** | `Byte[0] ^ Byte[1] ^ Byte[2]` |

---

### 2. 单元测试 (Unit Tests)

为确保硬件正常，提供了独立的测试工程供开发者逐一验证：

*   **Test: L9110S Motor**
    *   功能：循环执行“前进 -> 后退 -> 左转 -> 右转 -> 停止”。
    *   验证：观察轮子转动方向是否正确。
*   **Test: HC-SR04 Ultrasonic**
    *   功能：每 500ms 采集一次距离并通过串口打印。
    *   验证：使用串口助手查看日志 `Distance: xx cm`。
*   **Test: TCRT5000 Line Sensor**
    *   功能：实时读取 3 路 GPIO 电平。
    *   验证：遮挡传感器，观察串口日志电平变化。
*   **Test: SG90 Servo**
    *   功能：舵机在 0° ~ 180° 之间往复扫描。
    *   验证：观察舵机摆动角度。
*   **Test: SSD1306 OLED**
    *   功能：刷屏显示测试图案、字符和数字。
    *   验证：屏幕显示正常，无坏点或乱码。
*   **Test: WiFi Client**
    *   功能：连接指定路由器并获取 IP 地址。
    *   验证：串口打印 `WiFi Connected, IP: ...`。
*   **Test: Bluetooth SPP**
    *   功能：开启蓝牙广播 "WS63_SPP_Test"，等待手机连接并透传数据。
    *   验证：手机搜到蓝牙设备，发送数据能在串口看到回显。

## 开发者指南

### 如何编译与烧录

1.  **进入配置菜单**
    ```bash
    python build.py menuconfig
    ```
2.  **选择应用**
    导航至 `Application` -> `Support Smart Car Sample` -> `Select Running Application`。
    选择 `Robot Demo` 或具体的 `Test: ...` 模块。
3.  **编译**
    保存配置并退出，脚本自动开始编译。

### 如何添加新模块

1.  **创建驱动**: 在 `drivers/` 下新建文件夹（如 `beep`），放入 `bsp_beep.c/h`。
2.  **注册驱动**:
    *   在 `Kconfig` 的 "Drivers Status" 中添加 `CONFIG_SMART_CAR_DRIVER_BEEP`。
    *   在 `drivers/CMakeLists.txt` 中添加对应的编译规则。
3.  **创建测试**:
    *   在 `apps/` 下新建 `test_beep`，放入 `beep_example.c` 和 `CMakeLists.txt`。
    *   在 `Kconfig` 的 "Run Mode" Choice 中添加 `RUN_BEEP_TEST` 选项。

## 更新记录

*   **2025-01-14**:
    *   升级通信协议为 4字节 HEX 格式，支持线性调速。
    *   重构项目目录，分离业务逻辑 (`robot_demo`) 与 控制逻辑 (`robot_service`)。
    *   完善单元测试说明文档。