# UDP 通信协议文档

## 概述

智能小车通过 UDP 协议与上位机（Web 前端、Python 控制程序等）进行通信。协议支持以下功能：

- **控制命令** - 差速电机控制、舵机控制
- **模式切换** - 远程切换运行模式
- **参数配置** - 实时调整 PID 参数
- **状态上报** - 传感器状态、模式状态、距离等
- **设备发现** - 广播设备存在信息
- **心跳保活** - 保持连接活跃
- **OTA 升级** - 固件远程升级

## 网络配置

| 参数 | 值 |
|:---|:---|
| **监听端口** | 8888 |
| **广播端口** | 8889 |
| **通信方式** | UDP 单播 + 广播 |
| **数据格式** | 二进制结构体 |

> 设备启动后会自动连接默认 WiFi，连接成功后每 2 秒广播一次设备存在信息。

---

## 通用数据包格式

所有控制/状态类数据包使用统一的 7 字节结构：

```c
typedef struct __attribute__((packed)) {
    uint8_t type;     // 数据包类型
    uint8_t cmd;      // 命令/模式编号
    int8_t  motor1;   // 左电机值 (-100~100) 或复用
    int8_t  motor2;   // 右电机值 (-100~100) 或复用
    int8_t  servo;    // 舵机角度 (0~180) 或复用
    int8_t  ir_data;  // 红外传感器数据 (bit0=左, bit1=中, bit2=右)
    uint8_t checksum; // 校验和（累加和）
} udp_packet_t;
```

### 校验和计算

```c
checksum = (type + cmd + motor1 + motor2 + servo + ir_data) & 0xFF;
```

---

## 上位机 → 设备（控制命令）

### 1. 电机控制包 (type = 0x01)

**功能**：控制左右电机差速和舵机角度

| 字节 | 字段 | 类型 | 范围 | 说明 |
|:---|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x01 | 控制包类型 |
| 1 | cmd | uint8_t | - | 保留（填 0） |
| 2 | motor1 | int8_t | -100~100 | 左电机速度（负=后退，正=前进，0=停止） |
| 3 | motor2 | int8_t | -100~100 | 右电机速度（负=后退，正=前进，0=停止） |
| 4 | servo | int8_t | 0~180 | 舵机角度（0=右，90=中，180=左） |
| 5 | ir_data | int8_t | - | 保留（填 0） |
| 6 | checksum | uint8_t | - | 校验和 |

**示例**（前进+舵机居中）：
```
0x01 0x00 0x50 0x50 0x5A 0x00 0x01
 type  cmd  m1   m2   srv   -    chk
```
- m1=0x50 (80): 左电机 80% 前进
- m2=0x50 (80): 右电机 80% 前进
- srv=0x5A (90): 舵机居中

**示例**（原地左转）：
```
0x01 0x00 0x50 0x50 0x5A 0x00 0x01
 type  cmd  m1   m2   srv   -    chk
```
- m1=0x50 (80): 左电机 80% 前进
- m2=0x50 (80): 右电机 80% 前进
- srv=0xB4 (180): 舵机左转 180°

---

### 2. 模式切换包 (type = 0x03)

**功能**：远程切换小车运行模式

| 字节 | 字段 | 类型 | 范围 | 说明 |
|:---|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x03 | 模式切换类型 |
| 1 | cmd | uint8_t | 0~4 | 目标模式编号（见下表） |
| 2~6 | - | - | - | 保留（填 0） |
| 7 | checksum | uint8_t | - | 校验和 |

**模式编号**：

| cmd 值 | 模式 | 说明 |
|:---|:---|:---|
| 0 | CAR_STOP_STATUS | 待机模式 |
| 1 | CAR_TRACE_STATUS | 循迹模式 |
| 2 | CAR_OBSTACLE_AVOIDANCE_STATUS | 避障模式 |
| 3 | CAR_WIFI_CONTROL_STATUS | WiFi 遥控模式 |

**示例**（切换到避障模式）：
```
0x03 0x02 0x00 0x00 0x00 0x00 0x05
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
| 4~6 | - | - | - | 保留（填 0） |
| 7 | checksum | uint8_t | - | 校验和 |

**参数类型**：

| cmd 值 | 参数 | value 格式 | 说明 |
|:---|:---|:---|:---|
| 1 | Kp | value × 0.001 | 比例系数（如 25000 → 25.0） |
| 2 | Ki | value × 0.0001 | 积分系数（如 5000 → 0.5） |
| 3 | Kd | value × 0.002 | 微分系数（如 5000 → 10.0） |
| 4 | Speed | value | 基础速度（直接使用，0~100） |

**示例**（设置 Kp = 16.0）：
```
value = 16.0 / 0.001 = 16000 = 0x3E80

0x04 0x01 0x3E 0x80 0x00 0x00 0x00 0xC6
 type  cmd  hi   lo   ---- 保留 ----   chk
```

**示例**（设置基础速度 = 50）：
```
0x04 0x04 0x00 0x32 0x00 0x00 0x00 0x3B
 type  cmd  hi   lo   ---- 保留 ----   chk
```
- value = 50 = 0x0032

---

## 设备 → 上位机（状态上报）

### 1. 传感器状态包 (type = 0x02)

设备在以下情况发送状态包：
- 状态发生变化时立即发送
- 每 500ms 定期发送一次（保活）

| 字节 | 字段 | 类型 | 说明 |
|:---|:---|:---|:---|
| 0 | type | uint8_t | 0x02（传感器状态） |
| 1 | cmd | uint8_t | 当前模式 (0~3) |
| 2 | servo_angle | int8_t | 舵机角度 (0~180) |
| 3 | distance | int8_t | 超声波距离 × 10（如 15 = 1.5cm） |
| 4 | reserved | int8_t | 保留 |
| 5 | ir_data | int8_t | 红外传感器（bit0=左, bit1=中, bit2=右） |
| 6 | checksum | uint8_t | 校验和 |

**ir_data 位定义**：
```
bit 0: 左传感器 (1=检测到黑线)
bit 1: 中传感器 (1=检测到黑线)
bit 2: 右传感器 (1=检测到黑线)
```

**示例解析**：
```
接收: 0x02 0x01 0x5A 0x96 0x00 0x05 0xF8
type=0x02, mode=1(循迹), servo=90, dist=25.0cm, ir=左+中检测到黑线
```

---

### 2. 心跳包 (type = 0xFE)

设备每 10 秒发送一次心跳包。

| 字节 | 字段 | 值 |
|:---|:---|:---|
| 0 | type | 0xFE |
| 1~6 | - | 全 0 |
| 7 | checksum | 校验和 |

---

### 3. 设备存在广播 (type = 0xFF)

设备每 2 秒广播一次，用于设备发现。

| 字节 | 字段 | 值 |
|:---|:---|:---|
| 0 | type | 0xFF |
| 1~6 | - | 全 0 |
| 7 | checksum | 校验和 |

**广播目标**：255.255.255.255:8889

**上位机用途**：监听此端口可发现局域网内的所有智能小车设备。

---

## OTA 升级协议

OTA 升级使用独立的数据包格式，与控制协议分开。

### 数据包格式

```
[0]=type(0x10~0x13) [1]=cmd [2~5]=offset(BE) [6~7]=payload_len(BE) [8~]=payload [last]=checksum
```

| 字段 | 大小 | 说明 |
|:---|:---|:---|
| type | 1 | 0x10=Start, 0x11=Data, 0x12=End, 0x13=Ping |
| cmd | 1 | 保留（填 0） |
| offset | 4 | 大端序，数据偏移量 |
| payload_len | 2 | 大端序，负载长度 |
| payload | N | 实际数据 |
| checksum | 1 | 累加校验和 |

### 升级流程

```
上位机                          设备
  |                               |
  |------- 0x10 Start ------------>| (携带总大小)
  |<------ 0x14 Response (0) ------| (准备成功)
  |                               |
  |------- 0x11 Data (offset=0) -->| (分块传输)
  |<------ 0x14 Response (0) ------| (应答 offset)
  |------- 0x11 Data (offset=N) -->|
  |<------ 0x14 Response (0) ------|
  |          ...                   |
  |------- 0x12 End -------------->|
  |<------ 0x14 Response (0) ------| (触发升级)
  |                               |
```

### 应答包格式 (type = 0x14)

```
[0]=0x14 [1]=resp_code [2~5]=offset(BE) [6~7]=payload_len(BE) [8]=status [9]=percent
[10~13]=received_max(BE) [14~17]=total_size(BE) [18]=checksum
```

| 字段 | 说明 |
|:---|:---|
| resp_code | 0=成功, 1=状态错误, 2=参数错误, 3=内部错误 |
| status | 0=空闲, 1=已准备, 2=接收中, 3=升级中, 4=完成, 5=错误 |
| percent | 进度百分比 0~100 |

---

## Python 示例代码

```python
import socket
import struct

# 配置
UDP_IP = "192.168.3.151"  # 设备 IP
UDP_PORT = 8888
BROADCAST_PORT = 8889

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind(("", BROADCAST_PORT))  # 监听广播
sock.settimeout(0.1)

def calc_checksum(data):
    return sum(data) & 0xFF

def send_control(motor1, motor2, servo):
    """发送控制命令"""
    packet = struct.pack(">Bbbbbb", 0x01, 0, motor1, motor2, servo, 0)
    checksum = calc_checksum(packet)
    sock.sendto(packet + bytes([checksum]), (UDP_IP, UDP_PORT))

def set_mode(mode):
    """切换模式 (0=停止, 1=循迹, 2=避障, 3=遥控)"""
    packet = struct.pack(">BBBBBB", 0x03, mode, 0, 0, 0, 0)
    checksum = calc_checksum(packet)
    sock.sendto(packet + bytes([checksum]), (UDP_IP, UDP_PORT))

def set_pid(param_type, value):
    """设置 PID 参数 (1=Kp, 2=Ki, 3=Kd, 4=Speed)"""
    value_hi = (value >> 8) & 0xFF
    value_lo = value & 0xFF
    packet = struct.pack(">BBBBBB", 0x04, param_type, value_hi, value_lo, 0, 0)
    checksum = calc_checksum(packet)
    sock.sendto(packet + bytes([checksum]), (UDP_IP, UDP_PORT))

# 示例：前进并设置 PID
send_control(80, 80, 90)  # 前进
set_pid(1, 16000)         # Kp = 16.0
```

---

### Q1: 为什么使用 UDP 而不是 TCP？

**A**: UDP 适合实时控制场景：
- 低延迟（无需握手）
- 允许丢包（控制指令高频发送，丢一包不影响）
- 支持广播（设备发现）

### Q2: 超时保护机制？

**A**: 遥控模式下，500ms 未收到控制命令会自动急停并回中舵机，防止失控。

### Q3: 如何发现设备？

**A**: 监听广播端口 8889，设备每 2 秒发送 type=0xFF 的存在广播。

### Q4: 校验和计算错误怎么办？

**A**: 设备会丢弃校验错误的数据包，不做任何响应。
