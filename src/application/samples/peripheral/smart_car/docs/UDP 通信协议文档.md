# UDP 通信协议文档

## 概述

智能小车通过 UDP 协议与上位机（Web 前端、Python 控制程序等）进行通信。协议支持以下功能：

- **控制命令** - 差速电机控制
- **模式切换** - 远程切换运行模式
- **参数配置** - 实时调整 PID 参数
- **WiFi 配置** - AP 模式下配置并切换 WiFi（保存到 NV 存储）
- **状态上报** - 传感器状态、模式状态、距离等
- **设备发现** - 广播设备存在信息
- **心跳保活** - 保持连接活跃
- **定向通信** - STA 模式下连接后使用点对点通信

## 网络配置

| 参数 | 值 |
|:---|:---|
| **监听端口** | 8888 |
| **广播端口** | 8889 |
| **通信方式** | UDP 单播 + 广播 |
| **数据格式** | 二进制结构体 |

## WiFi 工作模式

设备支持三种 WiFi 工作模式，可通过 Kconfig 配置：

| 模式 | 说明 | 特点 |
|:---|:---|:---|
| **智能模式** | STA 优先，失败后自动切换 AP | 启动时尝试连接预设 WiFi（15 秒超时），失败后进入 AP 模式 |
| **STA 模式** | 固定连接到指定路由器 | 启动时直接连接预设 WiFi |
| **AP 模式** | 作为热点供设备连接 | 启动时作为热点，SSID: WS63_Robot, 密码: 12345678 |

### 智能模式工作流程

```
启动 → 尝试从 NV 加载 WiFi 配置
        ↓
   有配置？
   ↙        ↘
  是          否
  ↓           ↓
尝试连接 STA  进入 AP 模式
(15秒超时)    作为热点
  ↓
成功?         ↓
 ↙    ↘       AP 模式下
是   否       可通过 UDP 命令
↓    ↓        配置并切换 STA
STA  STA
模式 切换AP
```

### 通信模式差异

| 状态 | AP 模式 | STA 模式 |
|:---|:---|:---|
| **设备发现** | 每 2 秒广播 (type=0xFF) | 连接前广播，连接后停止 |
| **心跳包** | 每 5 秒广播心跳 (type=0xFE) | 向已知服务器点对点发送 |
| **状态上报** | 广播状态包 (type=0x02) | 向已知服务器点对点发送 |
| **控制命令** | 接收广播/单播 | 仅接收已连接服务器的命令 |

---

## 通用数据包格式

所有控制/状态类数据包使用统一的 **6 字节**结构（已删除舵机）：

```c
typedef struct __attribute__((packed)) {
    uint8_t type;     // 数据包类型
    uint8_t cmd;      // 命令/模式编号
    int8_t  motor1;   // 左电机值 (-100~100) 或复用
    int8_t  motor2;   // 右电机值 (-100~100) 或复用
    int8_t  data3;    // 扩展数据 (PID高位/保留)
    uint8_t checksum; // 校验和（累加和）
} udp_packet_t;
```

### 校验和计算

```c
checksum = (type + cmd + motor1 + motor2 + data3) & 0xFF;
```

---

## 上位机 → 设备（控制命令）

### 1. 电机控制包 (type = 0x01)

**功能**：控制左右电机差速

| 字节 | 字段 | 类型 | 范围 | 说明 |
|:---|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x01 | 控制包类型 |
| 1 | cmd | uint8_t | - | 保留（填 0） |
| 2 | motor1 | int8_t | -100~100 | 左电机速度（负=后退，正=前进，0=停止） |
| 3 | motor2 | int8_t | -100~100 | 右电机速度（负=后退，正=前进，0=停止） |
| 4 | data3 | int8_t | - | 保留（填 0） |
| 5 | checksum | uint8_t | - | 校验和 |

**示例**（前进）：
```
0x01 0x00 0x50 0x50 0x00 0x01
 type  cmd  m1   m2   d3    chk
```
- m1=0x50 (80): 左电机 80% 前进
- m2=0x50 (80): 右电机 80% 前进

**示例**（原地左转）：
```
0x01 0x00 0x50 0x50 0x00 0x01
 type  cmd  m1   m2   d3    chk
```
- m1=0x50 (80): 左电机 80% 前进
- m2=0x50 (-80): 右电机 80% 后退

---

### 2. 模式切换包 (type = 0x03)

**功能**：远程切换小车运行模式

| 字节 | 字段 | 类型 | 范围 | 说明 |
|:---|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x03 | 模式切换类型 |
| 1 | cmd | uint8_t | 0~3 | 目标模式编号（见下表） |
| 2~5 | - | - | - | 保留（填 0） |
| 6 | checksum | uint8_t | - | 校验和 |

**模式编号**：

| cmd 值 | 模式 | 说明 |
|:---|:---|:---|
| 0 | CAR_STOP_STATUS | 待机模式 |
| 1 | CAR_TRACE_STATUS | 循迹模式 |
| 2 | CAR_OBSTACLE_AVOIDANCE_STATUS | 避障模式 |
| 3 | CAR_WIFI_CONTROL_STATUS | WiFi 遥控模式 |

**示例**（切换到避障模式）：
```
0x03 0x02 0x00 0x00 0x00 0x08
 type  cmd  ---- 保留字段 ----  chk
```

---

### 3. PID 参数设置包 (type = 0x04)

**功能**：实时调整循迹模式的 PID 参数

| 字节 | 字段 | 类型 | 范围 | 说明 |
|:---|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x04 | PID 设置类型 |
| 1 | cmd | uint8_t | 1~4 | 参数类型（见下表） |
| 2 | value_hi | uint8_t | - | 参数值高 8 位 |
| 3 | value_lo | uint8_t | - | 参数值低 8 位 |
| 4 | - | int8_t | 0 | 保留 |
| 5 | checksum | uint8_t | - | 校验和 |

**参数类型**：

| cmd 值 | 参数 | value 格式 | 说明 |
|:---|:---|:---|:---|
| 1 | Kp | value × 0.01 | 比例系数（如 2500 → 25.0） |
| 2 | Ki | value × 0.1 | 积分系数（如 5 → 0.5） |
| 3 | Kd | value × 0.1 | 微分系数（如 100 → 10.0） |
| 4 | Speed | value | 基础速度（直接使用，0~100） |

**示例**（设置 Kp = 25.0）：
```
value = 25.0 / 0.01 = 2500 = 0x09C4

0x04 0x01 0x09 0xC4 0x00 0xD2
 type  cmd  hi   lo    d3    chk
```

**示例**（设置基础速度 = 50）：
```
0x04 0x04 0x00 0x32 0x00 0x3B
 type  cmd  hi   lo    d3    chk
```
- value = 50 = 0x0032

---

### 4. WiFi 配置命令 (type = 0xE0 ~ 0xE2)

**功能**：AP 模式下配置 WiFi 并保存到 NV 存储器

**重要说明**：
- 配置保存后会立即写入 NV 存储器，重启后自动连接
- 建议使用 0xE1 命令（连接并切换），配置成功后会立即尝试连接
- 可通过 0xE2 命令查询当前保存的配置

#### 4.1 设置 WiFi 配置 (type = 0xE0)

仅保存配置，不立即切换到 STA 模式。

| 字节 | 字段 | 类型 | 说明 |
|:---|:---|:---|:---|
| 0 | type | uint8_t | 0xE0 |
| 1 | ssid_len | uint8_t | SSID 长度 |
| 2~33 | ssid | char[32] | WiFi 名称 |
| 34 | pass_len | uint8_t | 密码长度 |
| 35~98 | password | char[64] | WiFi 密码 |
| 99 | checksum | uint8_t | 校验和 |

**应答**：2 字节
| 字节 | 值 | 说明 |
|:---|:---|:---|
| 0 | 0xE0 | 命令回显 |
| 1 | 0x00/0xFF | 0=成功, 0xFF=失败 |

#### 4.2 连接并切换 (type = 0xE1)

保存配置并立即切换到 STA 模式连接。

数据包格式同 0xE0。

**应答**：2 字节
| 字节 | 值 | 说明 |
|:---|:---|:---|
| 0 | 0xE1 | 命令回显 |
| 1 | 0x00/0xFF | 0=成功, 0xFF=失败 |

#### 4.3 获取配置 (type = 0xE2)

获取当前保存的 WiFi 配置。

| 字节 | 字段 | 值 |
|:---|:---|:---|
| 0 | type | 0xE2 |

**应答格式**：
| 字节 | 字段 | 说明 |
|:---|:---|:---|
| 0 | type | 0xE2 |
| 1 | ssid_len | SSID 长度 |
| 2~33 | ssid | WiFi 名称 |
| 34 | pass_len | 密码长度 |
| 35~98 | password | WiFi 密码 |
| 99 | checksum | 校验和 |

---

## 设备 → 上位机（状态上报）

### 1. 传感器状态包 (type = 0x02)

设备在以下情况发送状态包：
- 状态发生变化时立即发送
- 每 500ms 定期发送一次（保活）
- **STA 模式连接后**：向已知服务器点对点发送
- **AP 模式/未连接**：广播发送

| 字节 | 字段 | 类型 | 说明 |
|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x02（传感器状态） |
| 1 | cmd | uint8_t | 当前模式 (0~3) |
| 2 | distance | int8_t | 超声波距离 × 10（如 150 → 15.0cm） |
| 3 | reserved | int8_t | 保留（填 0） |
| 4 | ir_data | int8_t | 红外传感器（bit0=左, bit1=中, bit2=右） |
| 5 | checksum | uint8_t | 校验和 |

**ir_data 位定义**：
```
bit 0: 左传感器 (1=检测到黑线/障碍)
bit 1: 中传感器 (1=检测到黑线/障碍)
bit 2: 右传感器 (1=检测到黑线/障碍)
```

**示例解析**：
```
接收: 0x02 0x01 0x96 0x00 0x05 0xA0
type=0x02, mode=1(循迹), dist=15.0cm, ir=左+中检测到黑线
```

---

### 2. 心跳包 (type = 0xFE)

**简化格式（2 字节）**：

| 字节 | 字段 | 值 |
|:---|:---|:---|
| 0 | type | 0xFE |
| 1 | checksum | 0xFE |

设备每 **5 秒**发送一次心跳包：
- **STA 模式连接后**：向已知服务器点对点发送
- **AP 模式/未连接**：广播发送

---

### 3. 设备存在广播 (type = 0xFF)

设备每 **2 秒**广播一次，用于设备发现。

| 字节 | 字段 | 值 |
|:---|:---|:---|
| 0 | type | 0xFF |
| 1 | cmd | 0 |
| 2 | motor1 | 0 |
| 3 | motor2 | 0 |
| 4 | data3 | 0 |
| 5 | checksum | 校验和 |

**广播目标**：255.255.255.255:8889

**STA 模式优化**：连接到服务器后，设备会停止广播，改用定向心跳。

**上位机用途**：监听此端口可发现局域网内的所有智能小车设备。

---

## Python 示例代码

```python
import socket
import struct

# 配置
UDP_IP = "192.168.43.1"  # 设备 IP (AP 模式)
UDP_PORT = 8888
BROADCAST_PORT = 8889

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", BROADCAST_PORT))  # 监听广播
sock.settimeout(0.1)

def calc_checksum(data):
    return sum(data) & 0xFF

def send_control(motor1, motor2):
    """发送控制命令"""
    packet = struct.pack(">Bbbbb", 0x01, 0, motor1, motor2, 0)
    checksum = calc_checksum(packet)
    sock.sendto(packet + bytes([checksum]), (UDP_IP, UDP_PORT))

def set_mode(mode):
    """切换模式 (0=停止, 1=循迹, 2=避障, 3=遥控)"""
    packet = struct.pack(">Bbbbb", 0x03, mode, 0, 0, 0)
    checksum = calc_checksum(packet)
    sock.sendto(packet + bytes([checksum]), (UDP_IP, UDP_PORT))

def set_pid(param_type, value):
    """设置 PID 参数 (1=Kp, 2=Ki, 3=Kd, 4=Speed)"""
    value_hi = (value >> 8) & 0xFF
    value_lo = value & 0xFF
    packet = struct.pack(">Bbbbb", 0x04, param_type, value_hi, value_lo, 0)
    checksum = calc_checksum(packet)
    sock.sendto(packet + bytes([checksum]), (UDP_IP, UDP_PORT))

# 示例：前进并设置 PID
send_control(80, 80)    # 前进
set_pid(1, 2500)        # Kp = 25.0
```

---

## 常见问题

### Q1: 为什么使用 UDP 而不是 TCP？

**A**: UDP 适合实时控制场景：
- 低延迟（无需握手）
- 允许丢包（控制指令高频发送，丢一包不影响）
- 支持广播（设备发现）

### Q2: 超时保护机制？

**A**: 遥控模式下，500ms 未收到控制命令会自动急停，防止失控。

### Q3: 如何发现设备？

**A**: 监听广播端口 8889，设备每 2 秒发送 type=0xFF 的存在广播。
STA 模式连接到服务器后会停止广播。

### Q4: 校验和计算错误怎么办？

**A**: 设备会丢弃校验错误的数据包，不做任何响应。

### Q5: STA 模式和 AP 模式有什么区别？

**A**:
- **AP 模式**：设备作为热点，上位机连接到设备的 WiFi（WS63_Robot）
- **STA 模式**：设备连接到路由器，上位机和设备在同一局域网
- **智能模式**：启动时先尝试 STA，失败后自动切换 AP

### Q6: 为什么心跳包只有 2 字节？

**A**: 为节省带宽，心跳包已简化为最小格式，仅用于保活。

### Q7: 如何在 AP 模式下切换到 STA？

**A**: 发送 UDP 命令 0xE1（连接并切换），设备会保存配置并切换模式。
