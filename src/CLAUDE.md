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
python build.py -j 16 ws63-liteos-app

# 清理后构建
python build.py -c ws63-liteos-app
```

### Menuconfig（Kconfig）

```powershell
python build.py menuconfig
```

- 配置文件保存至 `build/config/target_config/ws63/menuconfig/acore/ws63_liteos_app.config`
- 智能小车模块通过 `CONFIG_SAMPLE_SUPPORT_SMART_CAR=y` 启用
- 当前运行的应用通过 `CONFIG_SMART_CAR_RUN_ROBOT_DEMO`（或其他 `CONFIG_SMART_CAR_RUN_*` 选项）选择

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
├── Kconfig                          # 模块级 Kconfig：选择驱动/应用模式
├── CMakeLists.txt                   # 顶层 CMake：分发到 drivers + apps
├── drivers/
│   ├── CMakeLists.txt               # 根据 Kconfig 标志 GLOB 驱动源码
│   ├── l9110s/                      # L9110S 电机驱动（GPIO/PWM）
│   ├── hcsr04/                      # HC-SR04 超声波传感器
│   ├── tcrt5000/                    # TCRT5000 循迹红外传感器
│   ├── ssd1306/                     # SSD1306 OLED 显示屏（128x64）
│   ├── wifi_client/                 # WiFi STA 连接助手
│   ├── bt_spp_server/               # 蓝牙 SPP 服务器
│   ├── sle/                         # 星闪（SLE）设备驱动
│   └── uart/                        # UART 命令接口
└── apps/
    ├── robot_demo/                  # 主集成应用（循迹/避障/遥控）
    │   ├── robot_main.c             # 入口：任务创建、模式切换按键中断
    │   ├── core/
    │   │   ├── robot_mgr.c/h        # 状态机 & 模式生命周期管理器
    │   │   ├── robot_config.h       # 任务栈大小、优先级、时序常量
    │   │   ├── mode_trace.c/h       # 循迹模式
    │   │   ├── mode_obstacle.c/h    # 避障模式
    │   │   └── mode_remote.c/h      # WiFi/SLE 遥控模式
    │   └── services/
    │       ├── ui_service.c/h       # OLED UI 渲染
    │       ├── voice_service.c/h    # UART 命令解析（语音/串口指令）
    │       ├── udp_service.c/h      # WiFi UDP 控制服务器
    │       ├── sle_service.c/h      # SLE 遥控服务
    │       ├── storage_service.c/h  # 设置项 NV 存储
    │       ├── ota_service.c/h      # OTA 接收端
    │       └── udp_net_common.c/h   # 共享网络工具函数
    ├── test_l9110s/                 # 各驱动的独立测试应用
    ├── test_hcsr04/
    ├── test_tcrt5000/
    ├── test_ssd1306/
    ├── test_wifi/
    ├── test_bt_spp/
    └── sle_test/
```

### 构建集成模式

智能小车遵循 SDK 的 Kconfig + CMake 约定：

1. **Kconfig**（`smart_car/Kconfig`）定义运行模式的 `choice` 和各驱动的 `config` 开关。`robot_demo` 选项通过 `select` 自动启用所需驱动。
2. **顶层 CMake**（`smart_car/CMakeLists.txt`）使用 `add_subdirectory(drivers)`，然后通过 `if(CONFIG_SMART_CAR_RUN_*)` 分发到选中的应用。
3. **驱动 CMake**（`drivers/CMakeLists.txt`）对每个启用的 `CONFIG_SMART_CAR_DRIVER_*` 标志使用 `file(GLOB_RECURSE ...)` 和 `include_directories()`。
4. **应用 CMake**（`apps/robot_demo/CMakeLists.txt`）从 `core/` 和 `services/` GLOB 所有 `.c` 文件，添加本地 include 路径，并通过 `PARENT_SCOPE` 追加到全局 `SOURCES` 变量。

### 状态机

小车有 4 种模式，通过 GPIO 按键（GPIO 3，下降沿中断，200ms 软件消抖）循环切换：
- `CAR_STOP_STATUS`（0）— 停止/空闲
- `CAR_TRACE_STATUS`（1）— 循迹模式，通过 TCRT5000 跟踪黑线
- `CAR_OBSTACLE_AVOIDANCE_STATUS`（2）— 避障模式，通过 HC-SR04 自动避障
- `CAR_WIFI_CONTROL_STATUS`（3）— WiFi/SLE 遥控模式

`robot_mgr.c` 负责模式切换：对旧模式调用 `exit()`，对新模式调用 `enter()`，并在每 20ms 的主循环中调用 `tick()`。

### 通信协议

- **WiFi 控制**：UDP 服务器，端口 8888。接收 5 字节数据包：`[type, cmd, motor1, motor2, ir_data]`。
- **SLE 控制**：星闪低功耗协议。自定义类 GATT 服务用于遥控指令。
- **UART/语音**：串口命令接口，由 `voice_service` 解析。
- **OTA**：TCP 二进制传输，端口 8890，由 UDP 数据包 type=0x05 触发。

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
- `robot_common.h` 定义了跨 core 和 services 共享的结构体（`RobotState`、`CarStatus`、`WifiConnectStatus`）。


所有代码先想一下能不能用RTOS的方式来实现，而不是用裸机的方式实现
例如不要写一些狗屎全局标志位，不要写延时死等，不要写轮询浪费cpu
很多东西的结果都可以在底层代码接口中拿到结果，可以用状态机+事件/任务/信号量/互斥锁/定时器/消息队列/链表来实现

单行注释使用// 不要使用/* */