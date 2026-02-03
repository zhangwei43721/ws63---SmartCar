const WebSocket = require("ws");
const dgram = require("dgram");

// --- 配置 ---
const CONFIG = {
  WS_PORT: 8081,
  UDP_SEND_PORT: 8888,
  UDP_RECV_PORT: 8889,
  TIMEOUT: 20000,
};

// --- 全局状态 ---
const devices = new Map(); // ip -> { lastSeen, status, discovered }
const udpClient = dgram.createSocket("udp4");
const wss = new WebSocket.Server({ port: CONFIG.WS_PORT });

// --- 辅助函数 ---

// 8位累加校验和
const checksum8 = (buf, len) => {
  let sum = 0;
  for (let i = 0; i < len; i++) sum = (sum + buf[i]) & 0xff;
  return sum & 0xff;
};

// 发送广播给前端
const broadcast = (data) => {
  const msg = JSON.stringify(data);
  wss.clients.forEach((c) => c.readyState === WebSocket.OPEN && c.send(msg));
};

// UDP 发送封装
const sendToCar = (buf, ip) => {
  udpClient.send(
    buf,
    CONFIG.UDP_SEND_PORT,
    ip,
    (err) => err && console.error(`发送失败 ${ip}:`, err),
  );
};

// 通用车辆协议包构建 (6字节: Type, Cmd, Data*3, Checksum)
const buildCarPacket = (type, cmd, bytes = [0, 0, 0]) => {
  const buf = Buffer.alloc(6);
  buf[0] = type;
  buf[1] = cmd;
  bytes.forEach((b, i) => (buf[2 + i] = b));
  buf[5] = checksum8(buf, 5);
  return buf;
};

// WiFi配置包构建
const buildWifiConfigPacket = (cmd, ssid, password) => {
  const ssidBuf = Buffer.from(ssid, "utf8");
  const passBuf = Buffer.from(password, "utf8");
  const ssidLen = ssidBuf.length;
  const passLen = passBuf.length;
  const totalLen = 2 + ssidLen + 1 + passLen + 1; // cmd + ssid + ssid_len + pass + pass_len + checksum

  const buf = Buffer.alloc(totalLen);
  let idx = 0;

  buf[idx++] = cmd;
  buf[idx++] = ssidLen;
  ssidBuf.copy(buf, idx);
  idx += ssidLen;
  buf[idx++] = passLen;
  passBuf.copy(buf, idx);
  idx += passLen;
  buf[idx] = checksum8(buf, idx);

  return buf;
};

// 更新设备状态并处理超时重连
const updateDeviceState = (ip, newStatus = null) => {
  const now = Date.now();
  const dev = devices.get(ip);
  const isNew = !dev || now - dev.lastSeen > CONFIG.TIMEOUT;

  devices.set(ip, {
    lastSeen: now,
    discovered: true,
    status: newStatus || dev?.status || null,
  });

  if (isNew) {
    console.log(`设备上线/重连: ${ip}`);
    broadcast({ type: "deviceDiscovered", device: { ip } });
  }
  return isNew;
};

// --- UDP 监听 (处理设备消息) ---
udpClient.bind(CONFIG.UDP_RECV_PORT);
udpClient.on("message", (msg, rinfo) => {
  const type = msg[0];
  const ip = rinfo.address;

  // 1. 处理简化的心跳包 (2字节: Type=0xFE, Checksum)
  if (msg.length === 2 && type === 0xfe) {
    updateDeviceState(ip);
    return;
  }

  // 2. 处理WiFi配置应答 (Type 0xE0 ~ 0xE2)
  if (type >= 0xe0 && type <= 0xe2) {
    let result = null;

    if (type === 0xe0 || type === 0xe1) {
      // 应答格式: [cmd, result]
      result = { cmd: type, success: msg[1] === 0x00 };
    } else if (type === 0xe2 && msg.length >= 3) {
      // 获取配置应答: [cmd, ssid_len, ssid..., pass_len, pass..., checksum]
      const ssidLen = msg[1];
      const ssid = msg.subarray(2, 2 + ssidLen).toString("utf8");
      const passLen = msg[2 + ssidLen];
      const password = msg
        .subarray(3 + ssidLen, 3 + ssidLen + passLen)
        .toString("utf8");
      result = { cmd: type, ssid, password };
    }

    if (result) {
      broadcast({ type: "wifiConfigResponse", ip, result });
    }
    return;
  }

  // 3. 处理普通协议 (校验和在第6字节)
  if (msg.length < 6) return;
  if (checksum8(msg, 5) !== msg[5]) return;

  if (type === 0xff) {
    // 广播 (0xFF)
    updateDeviceState(ip);
  } else if (type === 0x02) {
    // 状态数据 (0x02)
    const irRaw = msg[4];
    const status = {
      mode: msg[1],
      distance: msg[2] / 10,
      ir: [irRaw & 1, (irRaw >> 1) & 1, (irRaw >> 2) & 1],
    };
    updateDeviceState(ip, status);
    broadcast({ type: "statusUpdate", ip, status });
  }
});

// --- WebSocket 处理 (处理前端指令) ---
wss.on("connection", (ws) => {
  console.log(`前端连接: ${ws._socket.remoteAddress}`);
  // 发送当前设备列表
  devices.forEach((dev, ip) => {
    ws.send(JSON.stringify({ type: "deviceDiscovered", device: { ip } }));
    if (dev.status)
      ws.send(JSON.stringify({ type: "statusUpdate", ip, status: dev.status }));
  });

  ws.on("message", (raw) => {
    try {
      const msg = JSON.parse(raw.toString());
      const ip = msg.deviceIP;
      if (!ip) return;

      switch (msg.type) {
        case "control": // 电机控制
          sendToCar(
            buildCarPacket(0x01, 0x00, [msg.motor1, msg.motor2, 0]),
            ip,
          );
          break;

        case "modeChange": // 模式切换
          const modeMap = { standby: 0, tracking: 1, avoid: 2, remote: 3 };
          sendToCar(buildCarPacket(0x03, modeMap[msg.mode] || 0), ip);
          break;

        case "setPid": // PID 参数设置
          const val =
            msg.paramType <= 3 ? Math.round(msg.value * 100) : msg.value;
          sendToCar(
            buildCarPacket(0x04, msg.paramType, [
              (val >> 8) & 0xff,
              val & 0xff,
              0,
            ]),
            ip,
          );
          break;

        // WiFi配置命令
        case "wifiConfigSet":
          const wifiBuf = buildWifiConfigPacket(0xe0, msg.ssid, msg.password);
          sendToCar(wifiBuf, ip);
          break;

        case "wifiConfigConnect":
          const wifiConnBuf = buildWifiConfigPacket(
            0xe1,
            msg.ssid,
            msg.password,
          );
          sendToCar(wifiConnBuf, ip);
          break;

        case "wifiConfigGet":
          const wifiGetBuf = Buffer.from([0xe2, 0x00]);
          sendToCar(wifiGetBuf, ip);
          break;
      }
    } catch (e) {
      console.error("WS 消息错误:", e);
    }
  });
});

// --- 定时清理超时设备 ---
setInterval(() => {
  const now = Date.now();
  for (const [ip, dev] of devices) {
    if (now - dev.lastSeen > CONFIG.TIMEOUT) {
      devices.delete(ip);
      console.log(`设备超时: ${ip}`);
      broadcast({ type: "deviceLost", ip });
    }
  }
}, 5000);

// --- 启动日志 ---
console.log(
  `代理启动 WS:${CONFIG.WS_PORT} UDP:${CONFIG.UDP_RECV_PORT}->${CONFIG.UDP_SEND_PORT}`,
);
