# 鸿蒙 WS63 智能小车 (Smart Car K12 Edition)

## 概述

本模块基于海思 WS63 芯片，为嵌入式教学及智能硬件开发提供了一套完整的智能小车控制框架。采用分层解耦与控制中枢总线设计，将底层硬件驱动（Drivers）、通用平台层（Platform）、通信通道层（Channels）、功能服务层（Services）与上层业务控制中枢（Apps/Core）解耦，支持模块化开发与独立测试。

主要功能模块包括：

- **多底盘支持** - 支持 L9110S 双电机 PWM 差速底盘与新版串口 UART 舵机底盘（UART1 2400波特率）
- **电机控制抽象与看门狗** - `bsp_motor` 统一抽象层，提供方向/差速驱动并内置 400ms 遥控防掉线看门狗
- **HC-SR04 超声波传感器** - 中断+信号量精确测距与定时避障状态机
- **TCRT5000 红外循迹传感器** - 3 路 ADC 采样，支持 PID 巡线、机械寻线与阈值校准模式
- **SSD1306 OLED 显示屏** - 消息驱动异步刷新，支持中文字库与状态显示 (I2C)
- **多通道遥控交互** - WiFi UDP 通信、SLE 华为星闪无线遥控、UART 语音控制、Web 强制门户 HTTP 控制
- **AP 配网与 Captive Portal** - 智能 WiFi 状态机，AP 模式下自动弹出 Web 控制配网页面
- **OTA 固件在线升级** - 支持通过 Web/UDP 发起固件在线分包升级与自校验
- **NV 存储持久化** - PID 参数、WiFi 配置与循迹探头校准阈值持久化存储

## 架构设计

### 1. 软件分层架构

本项目采用清晰的多层解耦架构：

```text
+-------------------------------------------------------------------------+
|                         应用业务层 (Apps / car_demo)                    |
|  - 状态机管理 (mode_mgr) | 控制中枢 (car_ctrl) | 状态仓库 (car_state)     |
|  - 运行模式：待机 (Standby) / 循迹 (Trace) / 避障 (Avoid) / 遥控 (Remote)|
+-------------------------------------------------------------------------+
       ^                                    ^                    ^
       | 事件投递                           | 调用               | 读写
+-------------------------+      +--------------------+      +------------+
| 通信通道层 (Channels)    |      | 功能服务层 (Services)|     | 平台层     |
| - UDP 通道              |      | - UI Service (OLED)|      | (Platform) |
| - SLE 星闪通道           |      | - WiFi Mgr (STA/AP)|     | - NV 存储  |
| - Voice 语音通道 (UART) |      | - Captive Portal   |      |   (Storage)|
| - HTTP 门户通道         |      | - OTA 升级服务     |      +------------+
+-------------------------+      | - 虚拟串口日志服务 |
                                 +--------------------+
                                            |
+-------------------------------------------------------------------------+
|                         硬件驱动层 (Drivers)                            |
|  - 电机抽象 (bsp_motor): 差速/方向统一接口 + 400ms 看门狗执行器          |
|  - 底盘驱动: L9110S (PWM差速) / chassis_uart (串口舵机底盘 UART1)       |
|  - 传感器: HC-SR04 (超声波) / TCRT5000 (3路ADC红外) / SSD1306 (I2C OLED)|
|  - 无线与通信: WiFi Client / SLE 星闪 / UART 串口 / BT SPP 蓝牙         |
+-------------------------------------------------------------------------+
```

### 2. car_demo 模块架构

`car_demo` 是智能小车的核心应用，由初始化总入口、控制中枢总线、统一状态仓库、模式状态机、输入通道及服务构成：

```text
apps/car_demo/
├── car_main.c               # 主入口：系统统一初始化、按键中断与状态机任务启动
├── car_common.h             # 跨模块公共定义：CarStatus、CarDriveCmd、统一5字节包协议
├── car_utils.c              # 任务创建与消息队列安全操作等公用辅助函数
│
├── core/                    # 【核心控制与状态中枢】
│   ├── car_ctrl.c/h         # 统一控制中枢：提供推行指令/差速指令/停止指令并派发到驱动
│   ├── car_state.c/h        # 统一状态仓库：缓存模式、超声波距离、红外采样及校准阈值
│   ├── mode_mgr.c/h         # 模式状态机调度器：消息队列驱动的模式进入/退出/流转
│   ├── mode_trace.c/h       # 循迹模式：PID控制、机械寻线、传感器校准及丢线记忆
│   └── mode_obstacle.c/h    # 避障模式：基于定时器事件的测距-刹车-后退-转弯决策状态机
│
├── channels/                # 【通信通道接入层】
│   ├── udp_channel.c/h      # WiFi UDP 遥控通道（5字节指令解析、心跳与状态回传）
│   ├── sle_channel.c/h      # SLE 星闪遥控通道（低功耗无线遥控）
│   ├── voice_channel.c/h    # UART 语音控制通道（单字节指令转运动与模式）
│   └── http_channel.c/h     # Captive Portal HTTP Web 控制通道
│
└── services/                # 【软件功能服务层】
    ├── ui_service.c/h       # OLED 显示服务（独立任务+消息驱动渲染，支持中文字库）
    ├── wifi_mgr_service.c/h # WiFi 管理服务（STA 自动连网与 AP 智能回退状态机）
    ├── captive_portal_service.c/h # Web 强制门户配网与 HTTP 服务器
    ├── portal_html.c/h      # 强制门户前端 Web 页面 Gzip 压缩数据
    ├── ota_service.c/h      # OTA 在线固件分包升级与校验服务
    ├── debug_log_service.c/h# 虚拟串口日志服务（支持 Web/UDP 实时日志输出）
    └── udp_net_common.c/h   # UDP 网络公共接口封装
```

### 3. 运行模式定义

小车由 `mode_mgr` 统一调度 4 种运行模式：

| 模式         | 状态枚举                            | 功能描述                                                     |
| :----------- | :---------------------------------- | :----------------------------------------------------------- |
| **待机模式** | `CAR_STOP_STATUS` (0)               | 电机刹车锁定，OLED 循环显示 WiFi 连接状态和 IP 地址          |
| **循迹模式** | `CAR_TRACE_STATUS` (1)              | 3 路红外循迹，支持 PID 循迹、机械寻线与动态阈值校准          |
| **避障模式** | `CAR_OBSTACLE_AVOIDANCE_STATUS` (2) | HC-SR04 超声波测距，定时状态机自动后退与转向避障             |
| **遥控模式** | `CAR_WIFI_CONTROL_STATUS` (3)       | 响应 UDP/SLE/语音/Web 遥控，`bsp_motor` 400ms 看门狗自动急停 |

### 4. 项目目录结构

```text
application/samples/peripheral/smart_car/
├── CMakeLists.txt           # 顶层构建脚本（全局 include 配置与应用选择）
├── Kconfig                  # 顶层配置菜单（应用选择、底盘类型配置、驱动选择）
├── README.md                # 项目文档
├── board_config.h           # 硬件身份常量（MAC 地址等）
│
├── platform/                # 【共享平台层】
│   ├── CMakeLists.txt
│   └── storage_service.c/h  # NV 存储服务（PID 参数、WiFi 配置、红外校准阈值持久化）
│
├── drivers/                 # 【硬件驱动层】
│   ├── motor_control/       # 电机控制抽象层 (bsp_motor.c/h, 统一控制+400ms看门狗)
│   ├── l9110s/              # L9110S 双电机 PWM 驱动 (GPIO 4/5/0/2)
│   ├── chassis_uart/        # 串口 UART 舵机底盘驱动 (UART1, GPIO 15/16, 2400波特率)
│   ├── hcsr04/              # HC-SR04 超声波测距驱动 (TRIG: GPIO 6, ECHO: GPIO 11)
│   ├── tcrt5000/            # TCRT5000 3路红外 ADC 驱动 (GPIO 12/10/9)
│   ├── ssd1306/             # SSD1306 128x64 OLED 驱动 (I2C1, 支持中文字模)
│   ├── uart/                # UART 驱动 (GPIO 8/7, 9600波特率, 语音模块接口)
│   ├── sle/                 # SLE 星闪无线协议驱动
│   ├── bt_spp_server/       # 蓝牙 SPP 串口服务驱动
│   └── wifi_client/         # WiFi 驱动支持
│
├── apps/                    # 【应用层】
│   ├── car_demo/            # [核心] 智能小车综合应用
│   └── test/                # [单元测试] 各硬件模块独立测试工程
│       ├── test_l9110s/     # L9110S 电机 PWM 单元测试
│       ├── test_chassis_uart/# 串口舵机底盘测试
│       ├── test_hcsr04/     # 超声波测距测试
│       ├── test_tcrt5000/   # 红外循迹 ADC 测试
│       ├── test_ssd1306/    # OLED 屏幕显示测试
│       ├── test_wifi/       # WiFi 连接测试
│       ├── test_bt_spp/     # 蓝牙 SPP 测试
│       └── sle_test/        # SLE 星闪遥控测试
│
├── proxy/                   # Node.js WebSocket 调试代理服务
├── 前端/                    # Web 端上位机遥控与循迹校准界面
└── docs/                    # 详细设计与协议文档
```

## 硬件连接

> **重要提示**: 以下引脚定义基于当前代码与 Kconfig 配置，请根据选择的底盘类型正确接线。

| 模块                      | 功能         | GPIO 引脚         | 说明 / 复用模式                                      |
| :------------------------ | :----------- | :---------------- | :--------------------------------------------------- |
| **L9110S  (传统底盘)**    | 左轮 A / B   | GPIO 4 / GPIO 5   | PWM 4 / PWM 5 控制                                   |
|                           | 右轮 A / B   | GPIO 0 / GPIO 2   | PWM 0 / PWM 2 控制                                   |
| **串口\|舵机 (新版底盘)** | TX / RX      | GPIO 15 / GPIO 16 | UART1, 2400 波特率 (5字节帧 `AA [mot] [s1] [s2] BB`) |
| **HC-SR04 超声波**        | TRIG / ECHO  | GPIO 6 / GPIO 11  | 中断 + 信号量测距 (有效范围 2cm~500cm)               |
| **TCRT5000 循迹**         | 左 / 中 / 右 | GPIO 12 / 10 / 9  | ADC 采样通道 5 / 3 / 2                               |
| **SSD1306 OLED**          | SCL / SDA    | GPIO 15 / GPIO 16 | I2C1 (波特率 400kHz, 仅在非串口底盘时共用)           |
| **KEY1 按键**             | 模式循环切换 | GPIO 3            | 内部上拉，下降沿中断 + 200ms 消抖                    |
| **语音控制模块**          | TX / RX      | GPIO 8 / GPIO 7   | UART 中断接收 (9600 波特率)                          |
| **SLE 星闪 / WiFi**       | 射频通信     | 芯片内置          | 华为星闪协议 / 2.4G WiFi (STA + AP)                  |

## 运行模式与功能说明

本模块支持在 `menuconfig` 的 `Select Running Application` 中选择 `Car Demo` 综合应用或具体单元测试模块。

### 1. 综合应用 (Car Demo)

#### 模式切换逻辑

按下板载 **KEY1 按键 (GPIO 3)** 或通过网络/语音发送模式命令，小车将在以下 4 种模式间循环流转：

```text
┌─────────────────────────────────────────────────────────────┐
│                    模式切换状态机                            │
├─────────────────────────────────────────────────────────────┤
│   待机 (0) ──► 循迹 (1) ──► 避障 (2) ──► 遥控 (3) ──► 待机 (0)│
│  (Standby)     (Trace)      (Avoid)     (Remote)            │
└─────────────────────────────────────────────────────────────┘
```

#### 模式详细行为

**1. 待机模式 (Standby / CAR_STOP_STATUS)**

- **行为**：电机刹车停止。
- **显示**：OLED 实时显示当前 WiFi 连接状态（Connecting/Success/AP Mode）和本机 IP。
- **用途**：系统开机默认状态，等待上位机连接或用户按键切换。

**2. 循迹模式 (Tracking / CAR_TRACE_STATUS)**

- **行为**：基于 3 路 TCRT5000 采集的实时电压与动态阈值进行轨迹跟踪。
- **子模式支持**：
  - `PID 巡线` (默认)：动态调整转角与速度缩放，支持丢线方向记忆（300ms 内快速寻线）。
  - `机械式寻线`：硬编码状态分支控制，适合基础教学演示。
  - `探头校准模式`：实时上传探头原始电压与阈值，支持在 Web 端可视化校准。
- **参数持久化**：PID 参数（Kp/Ki/Kd/base_speed）及探头阈值均保存在 NV 存储中。

**3. 避障模式 (Avoid / CAR_OBSTACLE_AVOIDANCE_STATUS)**

- **行为**：基于定时器事件驱动的状态机自动测距与转向避障。
- **状态流转**：
  - `FORWARD (前进)`：以 80% 速度前进，主动测距（阈值 20cm）。
  - `STOP_BEFORE_BACK` (刹车 100ms) → `BACKING` (后退 300ms)。
  - `STOP_BEFORE_TURN` (刹车 100ms) → `TURNING` (原地转向 650ms)。
  - `STOP_BEFORE_CHECK` (等待稳定 300ms) → `CHECKING` (重新测距判断是否通行)。

**4. 遥控模式 (Remote / CAR_WIFI_CONTROL_STATUS)**

- **行为**：响应 UDP / SLE / 语音 / Web 门户下发的前进、后退、左转、右转、停止指令。
- **安全保护**：`bsp_motor` 内置 400ms 看门狗，未收到持续遥控帧时自动急停，防止脱网失控。
- **底盘适配**：方向指令由底层驱动自动翻译（差速底盘执行差速转向；舵机底盘执行舵机偏转+电机驱动）。

## 文档

- [UDP 通信协议文档](docs/UDP 通信协议文档.md) - 详细的 UDP 接口协议说明
- [WiFi AP 配网与 Captive Portal 实现指南](docs/WiFi AP 配网与 Captive Portal 实现指南.md) - 强制门户与 Web 控制指南
- [任务调度与状态机设计](docs/tasks-and-state-machines.md) - 多任务调度与状态机实现

### 通信协议概要

#### 1. 统一 5 字节二进制控制包格式 (`car_packet_t`)

UDP、SLE 星闪及 Web 门户共用统一的 5 字节结构体：

| 字节偏移   | 字段名           | 类型      | 说明                                         |
| :--------- | :--------------- | :-------- | :------------------------------------------- |
| **Byte 0** | `type`           | `uint8_t` | 包类型码 (`CarPktType`)                      |
| **Byte 1** | `cmd`            | `uint8_t` | 子命令 / 模式编号 / PID 参数索引 / 方向命令  |
| **Byte 2** | `motor1 / data1` | `int8_t`  | 方向模式下为速度幅值 / PID 高字节 / 心跳距离 |
| **Byte 3** | `motor2 / data2` | `int8_t`  | PID 低字节 / 扩展数据                        |
| **Byte 4** | `ir_data / ext`  | `int8_t`  | 红外状态数据 / 扩展保留                      |

**主要包类型码 (`CarPktType`)**：

- `0x01 (CAR_PKT_CONTROL)`: 运动控制命令（cmd 为 `CarDriveCmd`: 0=停止, 1=前进, 2=后退, 3=左转, 4=右转）
- `0x03 (CAR_PKT_MODE)`: 模式切换（cmd: 0=待机, 1=循迹, 2=避障, 3=遥控）
- `0x04 (CAR_PKT_PID)`: PID 参数设置（cmd: 1=Kp, 2=Ki, 3=Kd, 4=base_speed）
- `0x05 (CAR_PKT_OTA)`: OTA 升级触发与分包传输
- `0x0A (CAR_PKT_TRACE_INFO)`: 循迹探头原始 ADC 电压与阈值遥测上报
- `0x0B (CAR_PKT_LOG_DATA)`: 虚拟串口实时调试日志
- `0x0C (CAR_PKT_TRACE_CALIB)`: 循迹探头阈值校准设置
- `0x0D (CAR_PKT_TRACE_SUBMODE)`: 循迹子模式切换 (0=PID, 1=机械, 2=校准)
- `0xE0 / 0xE1`: WiFi 配置保存 / 配置并立即连接
- `0xFE`: 心跳保活与状态回传 (监听 `8889` 端口)
- `0xFF`: 设备广播发现 (端口 `8889`，广播周期 500ms)

#### 2. 语音模块通信协议 (UART 9600 8N1)

单字节协议格式，直通控制中枢：

| 命令 Hex | 功能     | 行为说明                                                 |
| :------- | :------- | :------------------------------------------------------- |
| `0x00`   | 停止     | 立即停车 (`CAR_DRIVE_STOP`)                              |
| `0x01`   | 前进     | 向前行驶 (`CAR_DRIVE_FORWARD`)，由 400ms 看门狗兜底      |
| `0x02`   | 后退     | 向后行驶 (`CAR_DRIVE_BACKWARD`)，由 400ms 看门狗兜底     |
| `0x03`   | 左转     | 左偏转/原地左转 (`CAR_DRIVE_LEFT`)，由 400ms 看门狗兜底  |
| `0x04`   | 右转     | 右偏转/原地右转 (`CAR_DRIVE_RIGHT`)，由 400ms 看门狗兜底 |
| `0x10`   | 待机模式 | 切换到待机模式 (`CAR_STOP_STATUS`)                       |
| `0x11`   | 循迹模式 | 切换到循迹模式 (`CAR_TRACE_STATUS`)                      |
| `0x12`   | 避障模式 | 切换到避障模式 (`CAR_OBSTACLE_AVOIDANCE_STATUS`)         |
| `0x13`   | 遥控模式 | 切换到遥控模式 (`CAR_WIFI_CONTROL_STATUS`)               |

> 注：收到运动控制命令（0x00~0x04）时，系统自动切入遥控模式。

### 2. 独立单元测试模块

在 `menuconfig` 的 `Select Running Application` 中可单选以下测试程序独立验证硬件：

| 测试模块              | 对应源码路径                  | 验证与观察方法                                |
| :-------------------- | :---------------------------- | :-------------------------------------------- |
| **test_l9110s**       | `apps/test/test_l9110s`       | 观察双电机"前进→后退→左转→右转→停止"周期动作  |
| **test_chassis_uart** | `apps/test/test_chassis_uart` | 观察串口舵机底盘运动与舵机转向回正            |
| **test_hcsr04**       | `apps/test/test_hcsr04`       | 串口日志查看实时测距输出 `Distance: xx cm`    |
| **test_tcrt5000**     | `apps/test/test_tcrt5000`     | 遮挡红外探头查看 3 路 ADC 采样电压与黑白电平  |
| **test_ssd1306**      | `apps/test/test_ssd1306`      | OLED 屏幕循环显示测试图案、英文字符与中文字符 |
| **test_wifi**         | `apps/test/test_wifi`         | 验证 WiFi 扫描与连接指定热点                  |
| **test_sle**          | `apps/test/sle_test`          | SLE 星闪服务端收发测试                        |
| **test_bt_spp**       | `apps/test/test_bt_spp`       | 蓝牙 SPP 虚拟串口收发测试                     |

## 配置参数说明

### NV 存储持久化配置 (Key = 0x2000, Magic = "ROBT", Version = 2)

| 配置项              | 默认值        | 存储换算             | 说明                      |
| :------------------ | :------------ | :------------------- | :------------------------ |
| **PID 参数**        |               |                      | 循迹模式控制参数          |
| `Kp`                | 21.0          | `val * 1000 = 21000` | 比例系数                  |
| `Ki`                | 0.0           | `val * 10000 = 0`    | 积分系数                  |
| `Kd`                | 0.0           | `val * 500 = 0`      | 微分系数                  |
| `base_speed`        | 80            | `80`                 | 基础行驶速度              |
| **WiFi 配置**       |               |                      | 网络连接参数              |
| `SSID`              | `"BSHZ-2.4G"` | 字符串 (<= 31B)      | 默认预设 WiFi 名称        |
| `Password`          | `"BS6668888"` | 字符串 (<= 63B)      | 默认预设 WiFi 密码        |
| **循迹传感器阈值**  |               |                      | 探头黑白分界电压阈值 (mV) |
| `trace_l_threshold` | 1750          | 16 位整型            | 左探头判定阈值            |
| `trace_m_threshold` | 1150          | 16 位整型            | 中探头判定阈值            |
| `trace_r_threshold` | 1600          | 16 位整型            | 右探头判定阈值            |

> **说明**：
>
> 1. 上述参数均保存在 Flash NV 区域，修改后掉电不丢失。
> 2. 可通过 Web 端配网门户、STA 模式上位机网页或 UDP 命令动态配置。

## 开发者指南

### 1. 编译与烧录

1. **进入配置菜单**

   ```bash
   python build.py menuconfig
   ```

2. **配置应用与底盘**
   - 导航至 `Application` -> `Support Smart Car Sample`
   - 在 `Select Running Application` 中选择 `Car Demo` 或具体硬件测试应用
   - 在 `Select Chassis Type` 中选择底盘物理驱动：
     - `L9110S Differential Dual Motor Chassis` (传统差速底盘)
     - `Serial UART Servo Steering Chassis` (串口舵机底盘)

3. **保存与编译**
   保存退出后执行编译命令，生成二进制固件后进行烧录。

### 2. 如何添加新运行模式

1. **创建模式文件**
   在 `apps/car_demo/core/` 下创建 `mode_xxx.c` 和 `mode_xxx.h`，实现模式逻辑。

2. **添加状态枚举**
   在 `car_common.h` 的 `CarStatus` 枚举中添加新状态值。

3. **在 mode_mgr 中注册**
   在 `apps/car_demo/core/mode_mgr.c` 中添加对应模式的进入、退出和事件处理逻辑。

### 3. 如何添加新控制通道或服务

1. **添加控制通道**：
   在 `apps/car_demo/channels/` 下创建 `xxx_channel.c/h`，解析外部指令后调用 `car_ctrl_post_drive()` 投递给控制中枢。
2. **添加功能服务**：
   在 `apps/car_demo/services/` 下创建服务文件，并在 `apps/car_demo/car_main.c` 的 `car_system_init()` 中统一初始化。

## 常见问题与故障排查

### 1. WiFi 连接与 AP 配网

**Q: 启动后无法连接预设 WiFi 路由器？**

- 检查路由器是否为 2.4GHz 频段（WS63 不支持 5GHz 频段）。
- 启动 15 秒连接超时后，小车将自动切换到 AP 热点模式（SSID: `WS63_Car`，密码: `12345678`），手机连接该热点可直接弹出 Captive Portal 强制配网页面修改 WiFi 配置。

**Q: 配置参数修改后重启失效？**

- 检查串口日志输出确认是否有 `[存储] NV 写入: 返回值=0`。
- NV Flash 首次烧录后会自动写入默认出厂配置并计算校验和。

### 2. 底盘与电机动作

**Q: 为什么遥控模式下发送单次前进指令后小车短时间停下？**

- `bsp_motor` 内置 400ms 看门狗保护，防止网络断连小车失控。上位机控制端需保持以 <= 200ms 周期发送控制帧或心跳。

**Q: 新版串口底盘无响应？**

- 确认 Kconfig 中底盘类型已选为 `Serial UART Servo Steering Chassis`，引脚连接到 GPIO 15 (TX) 和 GPIO 16 (RX)，波特率为 2400。

## 更新记录

| 日期           | 更新内容                                                                                                        |
| :------------- | :-------------------------------------------------------------------------------------------------------------- |
| **2026-05-29** | 重构控制总线与分层架构：引入 `car_ctrl` 控制中枢与 `bsp_motor` 统一抽象层，支持 L9110S PWM 差速与串口舵机双底盘 |
| **2026-05-20** | 新增 AP 模式 Captive Portal 强制门户配网与 Web 交互控制，新增 OTA 固件在线分包升级服务                          |
| **2026-05-15** | 统一升级为 5 字节二进制通信协议 `car_packet_t`，新增红外探头电压遥测与动态阈值校准                              |
| **2025-02-06** | 添加 SLE 星闪遥控支持与 UART 语音控制通道；优化引脚定义注释                                                     |
| **2025-01-18** | 重构 car_demo 架构，分离 core 与 services；添加 PID 循迹算法与 NV 存储服务                                      |
| **2025-01-12** | 初始版本，提供基础循迹、避障、遥控功能框架                                                                      |
