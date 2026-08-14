# 智能小车任务清单与状态机分析

> 适用范围：`apps/car_demo`（含其依赖的 `drivers/` 内任务）。
> 背景约束：LiteOS 任务上限（`LOSCFG_BASE_CORE_TSK_LIMIT`）实际生效值仅 **28**，需与 WiFi 驱动、BTC/SLE 协议栈内部任务共享。
> 2026-08 曾因任务池耗尽导致 `voice_task` 创建失败、语音控制全程静默失效——任务槽是本项目的稀缺资源，新建任务必须论证必要性。

---

## 1. 任务清单（全景）

LiteOS 优先级**数值越小优先级越高**。"按需"指任务由模式切换/事件触发创建，退出后释放槽位。

| # | 任务名 | 栈(字节) | 优先级 | 常驻/按需 | 阻塞原语 | 职责 | 位置 |
|---|--------|---------|--------|-----------|----------|------|------|
| 1 | `car_main_task` | 4096 | 25 | 常驻 | 模式队列 | 系统初始化编排 + 模式状态机主循环（`mode_mgr_run`） | car_main.c:132 |
| 2 | `motor_exec` | 2048 | **10**（全车最高） | 常驻 | 电机命令队列 | 电机命令执行；非零命令 arm 400ms 看门狗，超时强制停车 | bsp_motor.c:71 |
| 3 | `car_ctrl` | 3072 | 24 | 常驻 | 命令总线队列 | 统一协议包分发、安全驾驶网关裁决 | car_ctrl.c:180 |
| 4 | `wifi_mgr` | 2048 | 20 | 常驻 | 消息队列 | WiFi STA/AP 生命周期管理（消息驱动） | wifi_mgr_service.c:166 |
| 5 | `wifi_sta` | 4096 | 默认 | 常驻 | 唤醒信号量 / DHCP 信号量 | STA 扫描连接流程执行体 | bsp_wifi_sta.c:191 |
| 6 | `wifi_ap` | 4096 | 默认 | 按需（无配置时） | — | softAP 启动与 DHCP 服务 | bsp_wifi_ap.c:50 |
| 7 | `ui_task` | 4096 | 23 左右 | 常驻 | UI 消息队列 | OLED 渲染（I2C 慢设备） | ui_service.c:233 |
| 8 | `udp_task` | 8192 | 24 | 常驻 | WiFi 事件队列 + socket 超时 | UDP 控制通道：发现广播/心跳/收包/状态机 | udp_channel.c:364 |
| 9 | `portal_task` | 8192 | 23 | 常驻 | `select` 500ms 超时 | 强制门户 HTTP+DNS（仅 AP 模式有用） | captive_portal_service.c:652 |
| 10 | `sle_adv_w` | 2048 | 默认 | 常驻 | 二值信号量 | SLE 广播重启 worker：等信号量 → 延时 50ms → 重启广播 | sle_device.c:536 |
| 11 | `bt_adv_w` | 2048 | 默认 | 视配置 | 二值信号量 | BT SPP 广播 worker（同构于 #10） | bsp_bt_spp.c:349 |
| 12 | `trace_task` | 4096 | 22 | 按需（循迹模式） | 事件 20ms 超时 | 循迹 PID 周期计算（20ms tick） | mode_trace.c:304 |
| 13 | `obst_task` | 2048 | 22 | 按需（避障模式） | 事件 20ms 超时 | 避障状态机周期步进（20ms tick + 喂狗） | mode_obstacle.c:211 |
| 14 | `ota_tcp_task` | 8192 | 默认 | 按需（OTA 触发） | socket 阻塞 | OTA 固件 TCP 接收 | ota_service.c:478 |
| ~~15~~ | ~~`voice_task`~~ | ~~2048~~ | ~~29~~ | ~~已删除~~ | — | 2026-08 合并进 car_ctrl 总线（ISR 翻译直投，见 voice_channel.c） | — |

**常驻任务栈合计**：约 36~38KB（不含按需任务与协议栈内部任务）。

### 不占用任务的模块

- **voice_channel**：UART ISR → 查表翻译 → `car_ctrl_post_cmd` 直投总线，无任务/队列/定时器；
- **debug_log_service**：仅 UDP 发送，无独立任务；
- **http_channel**：HTTP 请求由 `portal_task` 的 `select` 统一驱动，无独立任务；
- **hcsr04 / tcrt5000 / l9110s**：纯驱动，由使用方任务上下文调用。

---

## 2. 状态机清单

### 2.1 模式状态机（`core/mode_mgr.c`）——全车顶层状态机

```
                    mode_mgr_post(status, source)
                    （按键ISR / 语音 / UDP / HTTP / SLE）
                              │
        ┌─────────────────────┼─────────────────────┐
        ▼                     ▼                     ▼
  ┌──────────┐         ┌──────────┐          ┌──────────┐
  │ 停止(0)  │         │ 循迹(1)  │          │ 避障(2)  │          ┌──────────┐
  │ STOP     │◄───────►│ TRACE    │◄────────►│ OBSTACLE │◄────────►│ 遥控(3)  │
  └──────────┘         └──────────┘          └──────────┘          │ WIFI_CTL │
   无模式任务           enter: 创建           enter: 创建           └──────────┘
   （双 exit）          trace_task            obst_task              无模式任务
                      exit: 销毁+停车        exit: 销毁+停车        （双 exit）
```

- **载体**：`car_main_task` 阻塞读模式队列，纯事件驱动，无消息永久休眠；
- **特性**：收到消息后先排空队列只取最后意图（避免连按按键产生无意义的 enter/exit 序列）；
- **切换动作**：仅 enter 新模式任务，旧模式任务 exit（互斥：trace/obst 不同时存在）；
- **按键**：GPIO3 下降沿 ISR，200ms 消抖，`mode+1 mod 4` 循环。

### 2.2 避障子状态机（`core/mode_obstacle.c`，7 状态）

`obst_task` 每 20ms 调用 `obstacle_step()` 推进；定时阶段由 osal_timer 单次触发事件 0x02 推进：

```
OBST_FORWARD ──(测距<阈值)──► OBST_STOP_BEFORE_BACK ──(100ms)──► OBST_BACKING
    ▲                                                              │(300ms)
    │(通畅)                                                        ▼
OBST_CHECKING ◄──(300ms稳定)── OBST_STOP_BEFORE_CHECK ◄──(650ms)── OBST_TURNING
    │(受阻)                                                        ▲
    └──────────────────────────────────── OBST_STOP_BEFORE_TURN ◄──┘(100ms)
```

- 每个 tick 复推目标速度喂 `motor_exec` 400ms 看门狗（TURNING=650ms > 400ms，不喂会被强停）；
- 模式退出 → 事件 0x01 → 任务退出销毁。

### 2.3 UDP 通道状态机（`channels/udp_channel.c`，3 状态）

```
UDP_STATE_WAIT_WIFI ──(WiFi事件:已连接)──► UDP_STATE_DISCOVERING
      ▲                                        │        ▲
      │(WiFi丢失:关socket)            (收到控制器包)      │(keepalive衰减至0, 判断连)
      │                                        ▼        │
      └────────────────────────────── UDP_STATE_CONNECTED
```

- DISCOVERING：每 500ms 向 8889 端口广播发现包（OTA 发现也靠它）；
- CONNECTED：每 1s 衰减 keepalive、每 2s 发心跳；收包刷新 keepalive；
- WiFi 事件全部经事件队列感知，不轮询全局变量。

### 2.4 WiFi 管理状态（`services/wifi_mgr_service.c`，消息驱动）

状态值即 `wifi_status_t` 消息：`START(初始)` → 有配置走 STA / 无配置走 AP：

```
START ──有配置──► [STA连接中] ──STA_GOT_IP──► 关AP, 标记就绪 ──► UDP/OTA等服务启动
  │                    │STA_FAIL
  │                    └────►（重试或按策略回退）
  └──无配置──► [AP配网中] ──AP_READY──► portal_task启动HTTP/DNS ──Portal配网──► 回START(带配置)
```

- 外部一律经 `wifi_mgr_send_msg()` / `wifi_mgr_subscribe()` 交互，禁止轮询 `g_wifi_status`；
- 状态变化追加式广播给所有订阅者（UDP 通道、UI 等）。

### 2.5 其他小型状态

- **循迹子模式**（`core/mode_trace.c`）：正常运行 / 传感器校准（`mode_trace_is_calibrating()`，校准中安全网关放行手动驱动以便上位机微调）；
- **Portal 状态**（`captive_portal_service.c`）：`PORTAL_STATUS_*` 随 AP/HTTP 生命周期迁移，仅用于 UI 显示。

---

## 3. 任务 vs 普通函数：判定标准与改造清单

### 判定标准

一个执行体**必须**是任务，当且仅当满足至少一条：

1. **需要阻塞等待且与其他工作并发**（socket recv、慢设备 IO）；
2. **周期控制循环**（PID tick、状态机步进），周期精度要求高于 swtmr 抖动容忍；
3. **需要独立栈空间执行长流程**（OTA 大数据传输、WiFi 连接流程）。

满足以下任一特征的应**降级**为函数/定时器/合并：

- 只是"等一个信号 → 做一件毫秒级小事"→ 用 **osal_timer（swtmr 任务上下文）**或事件回调；
- 与某个已有任务**同构**（阻塞读队列 → 调同一组 core API）→ 合并，翻译前置到投递方；
- 大部分生命周期在**空转轮询**一个可以订阅的事件 → 改事件驱动，按需创建销毁。

### A 类：已完成

| 对象 | 处置 | 收益 |
|------|------|------|
| `voice_task` + 私有队列 + 停车定时器 | 删除，ISR 翻译直投 car_ctrl 总线；超时停车复用 motor 400ms 看门狗 | -1 任务、-1 队列、-1 定时器、-2KB 栈 |

### B 类：低风险，建议近期做

| 对象 | 现状问题 | 改造方案 | 收益 |
|------|----------|----------|------|
| `sle_adv_w`（#10） | 整个任务只为"等信号量→延时50ms→调一个函数" | 改为 osal_timer 单次定时器：断连回调里 `osal_timer_mod(50ms)`，回调中调 `sle_start_announce()`（swtmr 任务上下文，非 ISR；需验证 SLE API 在此上下文安全） | -1 任务、-2KB 栈 |
| `bt_adv_w`（#11，若启用） | 同构于上 | 同上 | -1 任务、-2KB 栈 |

### C 类：中风险中收益，任务池再紧张时做

| 对象 | 现状问题 | 改造方案 | 收益 |
|------|----------|----------|------|
| `portal_task`（#9） | STA 常连场景下 AP 已关，任务仍以 500ms `select` 空转轮询 `bsp_wifi_get_mode()`，且占**全车最大的 8KB 栈** | 改按需任务：订阅 `wifi_mgr` 广播，`AP_READY` 时创建、`STA_GOT_IP` 时退出（`ota_tcp_task` 已是同款按需范例） | 常态 -1 任务、**-8KB 栈** |
| `wifi_sta`+`wifi_ap`+`wifi_mgr`（#4/5/6） | 三层任务管一个 WiFi：mgr 已是队列驱动，sta/ap 又各开一个执行体互发信号量 | 合并为单一 WiFi 控制任务（队列驱动状态机，连接/AP 流程作为内部状态函数） | -2 任务、-8KB 栈；需重构 drivers/wifi_client 与 services/wifi_mgr 边界，回归测试配网全流程 |

### D 类：必须是任务，不要动

| 任务 | 理由 |
|------|------|
| `motor_exec` | 电机安全执行体，全车最高优先级（10），400ms 看门狗是遥控链路失效的最后防线 |
| `car_main_task`（模式状态机） | 状态机本体需要执行载体；阻塞于队列，零 CPU 浪费 |
| `car_ctrl` | 统一仲裁点，串行消费保证决策时序；voice 合并后责任更重了，不能再去 |
| `udp_task` | socket 收发 + 广播/心跳定时，网络 IO 必须独立栈 |
| `ui_task` | I2C OLED 渲染是慢阻塞 IO，放任何回调/swtmr 上下文都会拖累别人 |
| `trace_task` / `obst_task` | 20ms 周期控制 + 阻塞式传感器读取，swtmr 上下文承载不了；且已是按需创建（互斥，同时最多占 1 槽） |
| `ota_tcp_task` | 按需创建，大数据 TCP 传输，完成即销毁 |

### 任务槽预算（改造前后）

| 场景 | 常驻任务数（应用层） |
|------|---------------------|
| 改造前（voice_task 失败时） | 11（+1 想创建而失败） |
| 当前（voice 已合并） | 10 |
| B 类完成后 | 8~9 |
| B+C 全部完成后 | 5~6（STA 常连场景） |

> 注意：任务上限 28 由应用层与 WiFi 驱动、BTC/SLE 协议栈内部任务（约 10+ 个）共享，
> 应用层常驻任务建议长期控制在 **8 个以内**，给协议栈和按需任务留足余量。
