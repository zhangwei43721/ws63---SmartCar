# CLAUDE.md

This file provides guidance to Claude Code when working with code in this repository.

## 项目背景

本项目是 **HiSpark WS63 SDK**（FBB - Family Big Box），面向 WS63/WS63E 芯片（RISC-V，Wi-Fi 6 + 星闪/SLE 多模）的嵌入式物联网 SDK。工作目录为 `src/`；Git 仓库根目录在其上一级（`D:\SmartCar`）。

**重点关注**：位于 `application/samples/peripheral/smart_car/` 的智能小车代码。

## 构建系统

项目使用 CMake，通过 Python 脚本封装。所有构建命令在 `src/` 下执行。

## 常用命令

### OTA 烧录

**必须使用以下命令进行烧录：**

```powershell
python .\OTA_Workflow.py
```

该脚本会自动完成编译、打包升级文件、在局域网内发现小车并推送固件。


```powershell
# 完整构建（默认目标：ws63-liteos-app）
python build.py -j16 ws63-liteos-app

# 清理后构建
python build.py -c ws63-liteos-app
```

### Menuconfig（Kconfig）

```powershell
python build.py menuconfig
```

- 配置文件保存至 `build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config`
- 智能小车模块通过 `CONFIG_SAMPLE_SUPPORT_SMART_CAR=y` 启用
- 当前运行的应用通过 `CONFIG_SMART_CAR_RUN_car_demo`（或其他 `CONFIG_SMART_CAR_RUN_*` 选项）选择

### 构建产物

| 产物 | 路径 |
|------|------|
| ELF | `output/ws63/acore/ws63-liteos-app/ws63-liteos-app.elf` |
| 二进制 | `output/ws63/acore/ws63-liteos-app/ws63-liteos-app.bin` |
| 签名包 | `output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg` |
| OTA 包 | `output/ws63/upgrade/update.fwpkg` |

## OTA 工作流程

### 一键 OTA（推荐，编译→打包→烧录全自动）
```powershell
python OTA_Workflow.py
```
该脚本会自动完成编译、打包升级文件、在局域网内发现小车并推送固件的完整流程，无需手动干预各步骤。

### 手动 OTA 步骤（按需参考）
```powershell
# 1. 编译
python build.py ws63-liteos-app

# 2. 打包升级文件
python build/config/target_config/ws63/build_ws63_update.py --pkt app

# 3. 推送到小车（通过 UDP 广播端口 8889 自动发现）
python tools/ota_sender.py output/ws63/upgrade/update.fwpkg

# 或直接指定 IP
python tools/ota_sender.py output/ws63/upgrade/update.fwpkg 192.168.43.1
```

小车在未连接控制器时，会通过 UDP 端口 8889 以 500ms 间隔广播发现包（type=0xFF，23 字节）。OTA 触发使用 UDP 端口 8888，固件传输使用 TCP 端口 8890。

## 智能小车架构

### 目录结构

```
application/samples/peripheral/smart_car/
├── Kconfig                          # 模块级 Kconfig：运行模式 choice + 底盘类型 choice + 驱动开关
├── CMakeLists.txt                   # 顶层 CMake：编译 platform + 分发 drivers + apps
├── board_config.h                   # 板级引脚/硬件配置（被 car_common.h 包含）
├── platform/                        # 平台层（跨应用共享基础设施）
│   └── storage_service.c/h          # 设置项 NV 存储（PID 参数 / 循迹阈值 / WiFi 配置）
├── drivers/
│   ├── CMakeLists.txt               # 根据 Kconfig 标志 GLOB 驱动源码
│   ├── l9110s/                      # L9110S 双电机驱动（GPIO/PWM）
│   ├── motor_control/               # 电机抽象层 bsp_motor（任务 + 消息队列 + 400ms 看门狗）
│   ├── chassis_uart/                # 串口舵机底盘驱动（UART1 GPIO15/16，2400 波特率，4 字节 HEX 协议）
│   ├── hcsr04/                      # HC-SR04 超声波传感器
│   ├── tcrt5000/                    # TCRT5000 循迹红外传感器
│   ├── ssd1306/                     # SSD1306 OLED 显示屏（128x64）
│   ├── wifi_client/                 # WiFi 驱动（bsp_wifi_sta STA 模式 + bsp_wifi_ap AP 模式）
│   ├── bt_spp_server/               # 蓝牙 SPP 服务器
│   ├── sle/                         # 星闪（SLE）设备驱动
│   └── uart/                        # UART 命令接口
└── apps/
    ├── car_demo/                  # 主集成应用（循迹/避障/遥控）
    │   ├── car_main.c             # 入口：初始化编排 + 模式切换按键中断
    │   ├── car_common.h           # 共享类型与协议定义（CarStatus/CarState/CarPktType/包格式），不含逻辑
    │   ├── car_utils.c            # OS 任务/队列辅助函数
    │   ├── core/                  # 控制核心（全车唯一决策层）
    │   │   ├── car_ctrl.c/h         # 控制中枢：安全驾驶网关 + 统一命令总线
    │   │   ├── car_state.c/h        # 状态仓库：CarState 互斥保护读写
    │   │   ├── mode_mgr.c/h         # 模式状态机：切换命令队列 + enter/exit 调度
    │   │   ├── mode_trace.c/h       # 循迹模式（PID/硬编码/校准子模式）
    │   │   └── mode_obstacle.c/h    # 避障模式
    │   ├── channels/              # 通道适配层（只翻译，不决策）
    │   │   ├── udp_channel.c/h      # WiFi UDP 控制通道（广播发现+心跳）
    │   │   ├── sle_channel.c/h      # 星闪（SLE）控制通道
    │   │   ├── voice_channel.c/h    # UART 语音/串口命令通道
    │   │   └── http_channel.c/h     # AP 门户 HTTP REST 控制通道
    │   └── services/              # 支撑服务（非控制通道）
    │       ├── ui_service.c/h       # OLED UI 渲染
    │       ├── wifi_mgr_service.c/h # WiFi 管理器（STA/AP 生命周期，接口前缀 wifi_mgr_）
    │       ├── captive_portal_service.c/h # 强制门户（HTTP/DNS 服务器、配网）
    │       ├── ota_service.c/h      # OTA 接收端
    │       ├── debug_log_service.c/h# UDP 虚拟串口日志
    │       ├── udp_net_common.c/h   # 共享网络工具函数
    │       ├── portal_html.c/h      # CMake 自动生成的网页资源（勿手改）
    │       └── html/                # 门户网页源文件（编译时由 CMake 嵌入固件）
    └── test/                      # 各驱动的独立测试应用
        ├── test_l9110s/
        ├── test_hcsr04/
        ├── test_tcrt5000/
        ├── test_ssd1306/
        ├── test_wifi/
        ├── test_bt_spp/
        ├── test_chassis_uart/      # 串口舵机底盘驱动测试
        └── sle_test/
```

### 分层调用规约（重要）

- **channels/ 只做翻译不做决策**：收包/收请求 → 调 `car_ctrl_manual_drive()` / `mode_mgr_post()` / `car_ctrl_post_cmd()`，**禁止 include `bsp_motor.h`**。
- **统一命令总线**：二进制协议通道（UDP/SLE/语音）不直接调处理函数，而是把原始包 `car_cmd_t` 经 `car_ctrl_post_cmd()` 投递到总线，由 `car_ctrl` 任务串行消费，实现单点仲裁、单点日志、单点应答路由。带应答能力的包可携带 `reply` 回调（"从哪来回哪去"）。
- **`bsp_motor_push_cmd()` 只允许 core/ 调用**（mode_trace / mode_obstacle / car_ctrl 网关），电机抽象层见下"底盘抽象层"。
- 所有遥控方式（UDP/SLE/HTTP/语音/按键）的放行/拦截决策只在 `car_ctrl.c` 一处，排查遥控类 bug 先看这里的日志。
- WiFi 状态变化一律通过 `wifi_mgr_subscribe()` 订阅广播（追加式，禁止覆盖式单指针）；`g_wifi_status` 仅作一次性查询，**禁止在任务循环里轮询它**。

### 底盘抽象层

`core/` 不直接操作具体电机硬件，而是依赖 `drivers/motor_control/bsp_motor.h` 抽象：

- 由 `SMART_CAR_CHASSIS_TYPE` Kconfig `choice` 选择物理底盘：`L9110S`（双电机差速）或 `UART_SERVO`（串口舵机转向，默认）。
- `bsp_motor_init()` 内部创建 `motor_exec` 任务（优先级 10），通过消息队列消费 `int8_t[2]`（left, right）速度指令，并带 400ms 看门狗自动停车（防止遥控指令丢失后小车失控）。
- 串口舵机底盘的实际收发封装在 `drivers/chassis_uart/bsp_chassis_uart.h`（5 字节帧：`AA [motor] [servo1] [servo2] BB`）。

### 构建集成模式

智能小车遵循 SDK 的 Kconfig + CMake 约定：

1. **Kconfig**（`smart_car/Kconfig`）定义两个 `choice`：运行模式（`SMART_CAR_RUN_*`）和底盘类型（`SMART_CAR_CHASSIS_TYPE`，L9110S / UART_SERVO），外加各驱动的 `config` 开关。`car_demo` 选项通过 `select` 自动启用所需驱动。
2. **顶层 CMake**（`smart_car/CMakeLists.txt`）先 `add_subdirectory(platform)` 编译平台层，再 `add_subdirectory(drivers)`，然后通过 `if(CONFIG_SMART_CAR_RUN_*)` 分发到选中的应用（`apps/car_demo` 或 `apps/test/*`）。
3. **驱动 CMake**（`drivers/CMakeLists.txt`）对每个启用的 `CONFIG_SMART_CAR_DRIVER_*` 标志使用 `file(GLOB_RECURSE ...)` 和 `include_directories()`。`motor_control/` 无条件编译（底盘抽象层常驻）。
4. **应用 CMake**（`apps/car_demo/CMakeLists.txt`）从根目录 + `core/` + `channels/` + `services/` GLOB 所有 `.c` 文件，并用纯 CMake 的 `embed_html_file()` 把 `services/html/*.html` 打包进 `portal_html.c/h`（无需外部脚本），最后通过 `PARENT_SCOPE` 追加到全局 `SOURCES` 变量。

### 状态机

小车有 4 种模式，通过 GPIO 按键（GPIO 3，下降沿中断，200ms 软件消抖）循环切换：
- `CAR_STOP_STATUS`（0）— 停止/空闲
- `CAR_TRACE_STATUS`（1）— 循迹模式，通过 TCRT5000 跟踪黑线
- `CAR_OBSTACLE_AVOIDANCE_STATUS`（2）— 避障模式，通过 HC-SR04 自动避障
- `CAR_WIFI_CONTROL_STATUS`（3）— WiFi/SLE 遥控模式

`core/mode_mgr.c` 负责模式切换：各来源通过 `mode_mgr_post()` 投递切换意图（消息队列），状态机任务对旧模式调用 `exit()`、对新模式调用 `enter()`，纯事件驱动，无消息时阻塞休眠。

**循迹子模式**（`mode_trace_set_submode`）：`0` PID 巡线 / `1` 硬编码巡线 / `2` 传感器校准。PID 参数（Kp/Ki/Kd/BaseSpeed）和循迹阈值可在线设置并经 `storage_service` 持久化到 NV；阈值校准与原始 ADC 值通过 `TRACE_INFO`/`TRACE_CALIB` 包回传上位机网页。

### 通信协议

所有二进制通道（UDP/SLE/语音）共用 `car_common.h` 里的统一包格式 `car_packet_t`（5 字节）与 `CarPktType` 类型码，收包后统一走 `car_ctrl_post_cmd()` 总线：

- **基础包**：`[type, cmd, motor1, motor2, ir_data]`（1 字节 type + 4 字节 payload；变长包最大 16 字节，见 `CAR_CMD_MAX_PAYLOAD`）。
- **`CarPktType` 类型码**：`0x01` 控制、`0x03` 模式切换、`0x04` PID 设参、`0x05` OTA 触发、`0x0A` 循迹遥测、`0x0B` 虚拟串口日志、`0x0C` 循迹校准、`0x0D` 循迹子模式、`0xE0/0xE1` WiFi 配置（变长）、`0xFE` 心跳、`0xFF` 设备发现。
- **WiFi 控制**：UDP 服务器，端口 8888；广播发现/心跳走端口 8889。
- **SLE 控制**：星闪低功耗协议，复用统一 5 字节遥控协议。
- **UART/语音**：单字节命令（`0x00-0x0F` 运动 / `0x10-0x1F` 模式），由 `channels/voice_channel` 翻译成 `car_packet_t` 后投递总线。
- **OTA**：TCP 二进制传输，端口 8890，由 type=0x05 触发。

详细协议见 `docs/UDP 通信协议文档.md` 与 `docs/tasks-and-state-machines.md`（勿与本节重复维护）。

## 添加新的外设模块

使用提供的脚手架脚本：

```powershell
python add_module.py <module_name> --desc "描述"
# 带 BSP 层的复杂模块：
python add_module.py <module_name> --complex
```

该脚本会自动创建目录、C 模板、CMakeLists.txt 和 Kconfig，并自动注册到 `application/samples/peripheral/CMakeLists.txt` 和 `Kconfig` 中。

## 关键配置文件

| 文件 | 用途 |
|------|------|
| `config.in` | Kconfig 根入口 |
| `ws63.hiproj` | HiSpark Studio 项目设置（上传端口、波特率、JLink 配置） |
| `build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config` | 当前构建配置（自动生成） |
| `application/samples/peripheral/Kconfig` | 所有外设 sample 的父 Kconfig |
| `application/samples/peripheral/CMakeLists.txt` | 包含 `smart_car/` 的父 CMake |

## 调试

VS Code 启动配置位于 `.vscode/launch.json`：
- **GDB Launch**：通过 JLink 下载并复位（SWD，端口 3333）
- **GDB Attach**：附加到正在运行的程序

ELF 文件路径：`output/ws63/acore/ws63-liteos-app/ws63-liteos-app.elf`。

串口烧录（HiSpark Studio / VS Code 插件）使用 `.fwpkg` 文件，路径为 `output/ws63/fwpkg/ws63-liteos-app/ws63-liteos-app_all.fwpkg`，通过 COM 口上传（默认 COM7，波特率 2M）。

### 串口日志抓取

配置权威源：
- 物理端口 / 烧录波特率 → [ws63.hiproj](ws63.hiproj) `[upload]`（当前 COM7 / 2M）
- 运行期日志波特率 → `build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config`
  - `CONFIG_DEBUG_UART=0` + `CONFIG_DEBUG_UART_BAUDRATE=115200` → COM7（与烧录口共用，烧录完自动切到此波特率）
  - `CONFIG_LOG_UART=1` + `CONFIG_LOG_UART_BAUDRATE=921600` → UART1，独立物理口

用 `embed-ai-tool` skill中 的 `serial-monitor`抓取开发板串口日志，本机注意：

- 解释器用 `py -3.14`（HiSpark 自带 Python 无 pip，pyserial 装在系统 Python）
- 必须 `$env:PYTHONIOENCODING="utf-8"`，否则 GBK 编码报错

## 注意事项

- SDK 使用 **Huawei LiteOS**，并提供 OSAL 抽象层（`soc_osal.h`）。任务创建使用 `osal_kthread_create` / `osal_kthread_set_priority`，外层需包裹 `osal_kthread_lock/unlock`。
- 任务创建后**不要**立即释放任务句柄（LiteOS 中的常见错误模式）。
- `drivers/` 的 CMake 使用 `file(GLOB_RECURSE ...)`；在驱动目录中新增 `.c` 文件会自动被收录，无需修改 CMake。
- `car_common.h` 只定义跨 core / channels / services 共享的类型与协议（`CarState`、`CarStatus`、`CarPktType`、`car_packet_t`），控制逻辑在 `core/car_ctrl.c`、状态在 `core/car_state.c`、模式调度在 `core/mode_mgr.c`。


所有代码先想一下能不能用RTOS的方式来实现，而不是用裸机的方式实现
例如不要写一些狗屎全局标志位，不要写延时死等，不要写轮询浪费cpu
很多东西的结果都可以在底层代码接口中拿到结果，可以用状态机+事件/任务/信号量/互斥锁/定时器/消息队列/链表来实现

单行注释使用// 不要使用/* */