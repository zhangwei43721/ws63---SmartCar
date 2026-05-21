# WiFi AP 配网与 Captive Portal 实现指南

本文档总结在 HiSpark WS63（LiteOS + lwIP）平台上实现 WiFi AP 配网（强制门户 / Captive Portal）的完整流程，以及开发过程中遇到的关键问题与解决方案。

---

## 1. 什么是 Captive Portal

Captive Portal（强制门户）是一种网络认证机制。当设备（手机/电脑）连接到一个新的 WiFi 热点时，操作系统会自动检测该网络是否需要额外认证。如果检测到"有网关在但无法访问互联网"，系统就会自动弹出一个窗口，引导用户访问配网页面。

iOS、Android、Windows 都有各自的检测 URL（如 `captive.apple.com`、`connectivitycheck.gstatic.com`、`msftconnecttest.com`），通过访问这些 URL 来判断网络状态。

---

## 2. 整体架构

小车启动时**强制进入 AP 模式**（跳过 STA 尝试），SSID 为 `WS63_Robot`，固定 IP 为 `192.168.1.1`。

```
┌─────────────────────────────────────────────────────────────┐
│                        WS63 设备                             │
│                                                             │
│  ┌──────────────┐  ┌──────────────┐  ┌────────────────────┐ │
│  │   WiFi AP    │  │  DNS Server  │  │   HTTP Server      │ │
│  │ (SoftAP)     │  │  (UDP 53)    │  │   (TCP 80)         │ │
│  │              │  │              │  │                    │ │
│  │ SSID:        │  │ 拦截所有     │  │  ┌──────────────┐  │ │
│  │ WS63_Robot   │──▶│ A记录查询    │──▶│  │ GET /        │  │ │
│  │              │  │ 返回1.1      │  │  │ → 配网页面   │  │ │
│  │ IP:          │  │              │  │  ├──────────────┤  │ │
│  │ 192.168.1.1  │  │              │  │  │ POST /config │  │ │
│  └──────────────┘  └──────────────┘  │  │ → 处理配网   │  │ │
│                                      │  ├──────────────┤  │ │
│  ┌──────────────┐                    │  │ GET /status  │  │ │
│  │   WiFi STA   │◀───────────────────│  │ → JSON状态   │  │ │
│  │ (连接目标)    │                    │  ├──────────────┤  │ │
│  └──────────────┘                    │  │ 其他 → 302   │  │ │
│                                      │  └──────────────┘  │ │
│                                      └────────────────────┘ │
└─────────────────────────────────────────────────────────────┘
```

---

## 3. 实现流程

### 3.1 强制启动 AP 模式

在 `bsp_wifi.c` 中，`bsp_wifi_smart_init()` 直接启动 AP 模式，不再先尝试连接预设 WiFi：

```c
int bsp_wifi_smart_init(void) {
    printf("[WiFi] 强制启动 AP 模式，等待用户配网...\r\n");
    return bsp_wifi_init_ex(BSP_WIFI_MODE_AP);
}
```

AP 配置参数定义在 `bsp_wifi.h`：

```c
#define BSP_WIFI_AP_SSID     "WS63_Robot"
#define BSP_WIFI_AP_PASSWORD ""          // 开放网络，无密码
#define BSP_WIFI_AP_CHANNEL  13
```

### 3.2 AP 网络接口配置

`init_ap_mode()` 中设置静态 IP 并启动 DHCP 服务器：

```c
struct netif* netif_p = netifapi_netif_find("ap0");
if (netif_p) {
    ip4_addr_t ip, mask, gw;
    IP4_ADDR(&ip,   192, 168, 1, 1);
    IP4_ADDR(&mask, 255, 255, 255, 0);
    IP4_ADDR(&gw,   192, 168, 1, 1);

    netifapi_netif_set_addr(netif_p, &ip, &mask, &gw);
    netifapi_dhcps_start(netif_p, NULL, 0);
    g_wifi_status = BSP_WIFI_STATUS_GOT_IP;
    return 0;
}
```

### 3.3 Captive Portal 服务初始化

在 `robot_mgr.c` 的初始化流程中调用：

```c
captive_portal_service_init();
```

该函数创建 `captive_portal_task`，同时监听两个 socket：

- **UDP 53**：DNS 查询
- **TCP 80**：HTTP 请求

使用 `lwip_select` 实现单线程多路复用：

```c
fd_set read_fds;
FD_ZERO(&read_fds);
FD_SET(g_dns_socket, &read_fds);
FD_SET(g_http_socket, &read_fds);
int max_fd = (g_dns_socket > g_http_socket) ? g_dns_socket : g_http_socket;

if (lwip_select(max_fd + 1, &read_fds, NULL, NULL, NULL) > 0) {
    if (FD_ISSET(g_dns_socket,  &read_fds)) dns_server_handle();
    if (FD_ISSET(g_http_socket, &read_fds)) handle_http_accept();
}
```

### 3.4 DNS 劫持

所有 DNS A 记录查询都返回 AP IP（`192.168.1.1`）：

```c
// 解析 DNS 查询中的域名
char domain[64] = {0};
// ... 从 UDP 报文中提取域名 ...

// 构造 DNS 响应
uint8_t response[256];
memcpy(response, dns_buf, 2);  // 复制 Transaction ID
response[2] = 0x81; response[3] = 0x80;  // 标准响应，无错误
// ... 设置 Answer 数量、构造 A 记录 ...

// IP 地址字段写入 192.168.1.1
response[offset++] = 192;
response[offset++] = 168;
response[offset++] = 1;
response[offset++] = 1;
```

### 3.5 HTTP 路由与 302 重定向

这是 Captive Portal 的核心。浏览器去请求 `baidu.com` 时，必须让它**地址栏跳转到 `192.168.1.1`**，而不是直接返回 HTML 内容。

实现上分为两步：

#### 第一步：Host 头检查

只有直接访问小车 IP 的请求才走正常路由，其他一律 302：

```c
bool is_direct_ip = false;
char target_host[64];
snprintf(target_host, sizeof(target_host), "Host: %s", g_ap_ip_str);
if (strstr(buf, target_host) != NULL) {
    is_direct_ip = true;
}

if (!is_direct_ip) {
    char redirect_resp[256];
    snprintf(redirect_resp, sizeof(redirect_resp),
        "HTTP/1.1 302 Found\r\n"
        "Location: http://%s/\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n\r\n",
        g_ap_ip_str);
    send_response_and_close(client_fd, redirect_resp);
    return;
}
```

#### 第二步：请求分发

```c
if (is_get && strcmp(path, "/status") == 0) {
    handle_status_request(client_fd);    // JSON 状态
    return;
}
if (is_get && strcmp(path, "/") == 0) {
    send_response_and_close(client_fd, s_html_page);  // 配网页面
    return;
}
if (is_post && strcmp(path, "/config") == 0) {
    handle_config_request(client_fd, buf);  // 处理配网
    return;
}
// 兜底：其他路径也返回配网页面
send_response_and_close(client_fd, s_html_page);
```

### 3.6 配网页面与状态轮询

内嵌在固件中的 HTML 页面包含：

- WiFi 名称（SSID）和密码输入框
- "连接"按钮，通过 `POST /config` 提交
- JavaScript 轮询 `GET /status` 获取连接状态

连接状态由 `wifi_switch_task` 后台任务维护，通过全局变量 `g_captive_status` 暴露给 HTTP 服务。

### 3.7 WiFi 模式切换

`POST /config` 接收 SSID 和密码后，创建后台任务执行切换，**不阻塞 HTTP 服务**：

```c
static void wifi_switch_task(void* arg) {
    // 1. 关闭 AP 热点和 DHCP
    wifi_softap_disable();
    struct netif* ap = netifapi_netif_find("ap0");
    if (ap) netifapi_dhcps_stop(ap);

    // 2. 启用 STA 模式
    if (wifi_sta_enable() != ERRCODE_SUCC) {
        // 回退到 AP 模式
        bsp_wifi_smart_init();
        return;
    }

    // 3. 连接目标 WiFi
    if (bsp_wifi_start_sta_with_timeout(ssid, password, 15000) != 0) {
        wifi_sta_disable();
        bsp_wifi_smart_init();  // 失败回退
        return;
    }
    // 连接成功，状态更新为 CONNECTED
}
```

---

## 4. 遇到的问题及解决方案

### 问题 1：返回 HTTP 200，地址栏不跳转

**现象**：手机访问 `baidu.com`，小车返回了 HTML 内容（HTTP 200），但地址栏仍显示 `baidu.com`，没有弹窗。

**原因**：浏览器以为收到的 HTML 就是 `baidu.com` 的网页内容，自然不会跳转。

**解决**：必须返回 **302 Found**，让浏览器主动跳转到小车的 IP 地址：

```
HTTP/1.1 302 Found
Location: http://192.168.1.1/
```

### 问题 2：DNS 劫持后，连直接访问 192.168.1.1 也被 302

**现象**：加了全局 302 后，用户在浏览器地址栏输入 `192.168.1.1` 也会触发重定向，页面无法显示。

**原因**：没有区分"DNS 劫持过来的流量"和"用户直接访问小车 IP 的流量"。

**解决**：检查 HTTP 请求头中的 `Host` 字段。只有当 `Host` 是小车 IP 时，才走正常路由：

```c
if (strstr(buf, "Host: 192.168.1.1") == NULL) {
    // 这是 DNS 劫持流量 → 302 重定向
} else {
    // 这是直接访问 → 正常处理
}
```

### 问题 3：DHCP 没有下发网关（最隐蔽）

**现象**：手机 WiFi 详情里"路由器/网关"显示 `0.0.0.0`，系统完全不发起 Captive Portal 探测。串口日志中没有 DNS 查询和 HTTP 请求。

**根因分析**：

手机连接 WiFi 后，操作系统会检查：
1. 是否拿到了 IP 地址？
2. 是否拿到了**网关（Router）**和 **DNS**？

如果网关是 `0.0.0.0`，系统认为这是一个"没有互联网能力的局域网设备"，直接静默处理，**绝不弹窗**。

lwIP 的 DHCP 服务器默认**不下发网关**（`LWIP_DHCPS_GW` 默认为 0），导致 DHCP Offer/ACK 报文中没有 Router 选项。

**排查曲折**：

- 第一次：在 `bsp_wifi.c` 里加 `#define CONFIG_DHCPS_GW 1` → **无效**  
  原因：C 宏只在定义它的 `.c` 编译单元内有效，`dhcps.c` 编译时看不到。

- 第二次：改到 `lwipopts.h` → **仍无效**  
  原因：CMake 中 `LWIP_CONFIG_FILE` 被硬编码为 `"lwip/lwipopts_default.h"`，`lwipopts.h` 根本没被 lwIP 源文件包含。

- **最终解决**：在 `lwipopts_default.h` 中直接定义 `#define CONFIG_DHCPS_GW 1`。  
  这个文件是实际被 `LWIP_CONFIG_FILE` 指定的配置文件，所有 lwIP 源文件都能看到。

**`lwipopts_default.h` 中的关键逻辑**：

```c
#if defined (CONFIG_DHCPS_GW)
#ifndef LWIP_DHCPS_GW
#define LWIP_DHCPS_GW 1    // 下发网关
#endif
#else
#ifndef LWIP_DHCPS_GW
#define LWIP_DHCPS_GW 0    // 默认关闭
#endif
#endif
```

当 `LWIP_DHCPS_GW = 1` 后，`dhcps.c` 在构造 DHCP Offer/ACK 时就会加入 Router 选项（Option 3），地址为 AP 接口的 IP（`192.168.1.1`）。同时 `LWIP_DHCPS_DNS_OPTION` 会确保 DNS 服务器也下发为 `192.168.1.1`。

---

## 5. 关键配置项汇总

| 配置项 | 值 | 说明 |
|--------|-----|------|
| `BSP_WIFI_AP_SSID` | `"WS63_Robot"` | 热点名称 |
| `BSP_WIFI_AP_PASSWORD` | `""` | 开放网络（无密码） |
| `BSP_WIFI_AP_CHANNEL` | `13` | WiFi 频道 |
| AP 静态 IP | `192.168.1.1` | 固定网关地址 |
| `CONFIG_DHCPS_GW` | `1` | 启用 DHCP 网关下发 |
| DNS 端口 | `53` | 标准 DNS 端口 |
| HTTP 端口 | `80` | 标准 HTTP 端口 |
| DHCP 租约起始偏移 | `2` | 客户端 IP 从 `192.168.1.2` 开始分配 |

---

## 6. 调试技巧

### 查看手机是否拿到网关

手机连接 `WS63_Robot` 后，在 WiFi 详情中查看：
- **路由器/网关**：必须是 `192.168.1.1`
- **DNS**：必须是 `192.168.1.1`

如果显示 `0.0.0.0`，说明 `LWIP_DHCPS_GW` 未生效，DHCP 配置有问题。

### 串口日志观察点

```
[WiFi] AP 热点已启用: SSID=WS63_Robot, 开放网络(无密码), 频道=13
[WiFi] AP 热点 IP: 192.168.1.1
[Portal] DNS 查询: connectivitycheck.gstatic.com -> 192.168.1.1
[Portal] 客户端连接: 192.168.1.2
[Portal] 302 重定向 -> http://192.168.1.1/
[Portal] 收到配网请求: SSID=MyHomeWiFi
[WiFi] 正在连接 MyHomeWiFi...
[WiFi] 已连接: MyHomeWiFi, 信号强度: -45
```

如果没有看到 `[Portal] DNS 查询` 日志，说明手机根本没发起探测 → 检查 DHCP 网关。

### 手动测试

1. 手机连接热点后，**不要等弹窗**，直接在浏览器地址栏输入 `192.168.1.1`
2. 如果能看到配网页面，说明 HTTP 服务正常
3. 如果看不到，检查串口是否有 `客户端连接` 日志
4. 在浏览器里访问 `http://neverssl.com`，观察是否 302 跳转到 `192.168.1.1`

---

## 7. 总结

实现 Captive Portal 的关键不是"搭一个网页"，而是**让操作系统"认为这个网络需要认证"**。这要求：

1. **DNS 劫持**：让所有域名解析到本地 IP
2. **302 重定向**：把劫持流量引导到配置页面
3. **DHCP 网关下发**：这是最容易被忽略的一点——手机必须拿到网关和 DNS，系统才会发起探测

如果只做了前两点而第三点缺失，结果就是：手机静默连接，没有任何弹窗，用户必须手动打开浏览器输入 IP 才能配网——体验大打折扣。
