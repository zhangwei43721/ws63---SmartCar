const WebSocket = require("ws");
const dgram = require("dgram");
const net = require("net");

// --- 严格的配置常量 ---
const CONFIG = {
  WS_PORT: 8081,
  UDP_SEND_PORT: 8888, // 小车接收端口
  UDP_RECV_PORT: 8889, // 代理监听端口 (对应小车广播目标端口)
  HEARTBEAT_INTERVAL: 1000, // 心跳发送间隔 1s
  TIMEOUT_THRESHOLD: 5000, // 超时判定阈值 5s (增加容错)
};

// --- 全局状态 ---
const devices = new Map(); // Key: IP, Value: { lastSeen, mac, name, status }
const activeIntervals = new Map(); // 存储快速回复定时器
const udpSocket = dgram.createSocket("udp4");
const wss = new WebSocket.Server({ port: CONFIG.WS_PORT });

// --- OTA 配置 ---
const OTA_CONFIG = {
  TCP_PORT: 8890,
  CHUNK_SIZE: 32768,
};

const otaSessions = new Map(); // deviceIP -> { state, totalSize, receivedSize, progress, tcpSocket }

// --- 工具函数 ---

// 发送数据给前端
const broadcastToFrontend = (data) => {
  const msg = JSON.stringify(data);
  wss.clients.forEach((c) => {
    if (c.readyState === WebSocket.OPEN) c.send(msg);
  });
};

// 发送 UDP 包给小车
const sendToCar = (buf, ip) => {
  udpSocket.send(buf, CONFIG.UDP_SEND_PORT, ip, (err) => {
    if (err) console.error(`发送失败 -> ${ip}:`, err.message);
  });
};

// 构建标准控制包 (Type, Cmd, M1, M2, Extra)
const buildPacket = (type, cmd, m1 = 0, m2 = 0, extra = 0) => {
  const buf = Buffer.alloc(5);
  buf[0] = type;
  buf[1] = cmd;
  buf[2] = m1;
  buf[3] = m2;
  buf[4] = extra;
  return buf;
};

// --- OTA 工具函数 ---

const broadcastOtaStatus = (deviceIP, state, progress, message) => {
  broadcastToFrontend({
    type: "otaStatus",
    deviceIP,
    state,
    progress,
    message,
  });
};

function handleOtaUdpResponse(ip, cmd) {
  const session = otaSessions.get(ip);
  if (!session || session.state !== "triggering") return;

  if (cmd === 0x00) {
    session.state = "connecting";
    connectOtaTcp(ip, session);
  } else if (cmd === 0x01) {
    session.state = "error";
    broadcastOtaStatus(ip, "error", 0, "设备正忙，正在进行 OTA");
    otaSessions.delete(ip);
  } else {
    session.state = "error";
    broadcastOtaStatus(ip, "error", 0, `设备返回未知状态: ${cmd}`);
    otaSessions.delete(ip);
  }
}

function connectOtaTcp(ip, session) {
  const tcpSock = new net.Socket();
  session.tcpSocket = tcpSock;
  tcpSock.setTimeout(30000);

  tcpSock.connect(OTA_CONFIG.TCP_PORT, ip, () => {
    const header = Buffer.alloc(8);
    header.write("OTAx", 0, 4, "ascii");
    header.writeUInt32BE(session.totalSize, 4);
    tcpSock.write(header);
    session.state = "header_sent";
  });

  tcpSock.on("data", (data) => {
    if (session.state === "header_sent" && data.length >= 1 && data[0] === 0x00) {
      session.state = "ready";
      broadcastOtaStatus(ip, "ready", 0, "TCP 已就绪，等待固件数据");
    } else if (session.state === "verifying" && data.length >= 1) {
      if (data[0] === 0x00) {
        session.state = "done";
        broadcastOtaStatus(ip, "done", 100, "OTA 成功，设备即将重启");
      } else {
        session.state = "error";
        broadcastOtaStatus(ip, "error", 100, `校验失败: ${data[0]}`);
      }
      tcpSock.end();
      otaSessions.delete(ip);
    }
  });

  tcpSock.on("error", (err) => {
    session.state = "error";
    broadcastOtaStatus(ip, "error", session.progress || 0, `TCP 错误: ${err.message}`);
    otaSessions.delete(ip);
  });

  tcpSock.on("timeout", () => {
    tcpSock.destroy();
    if (otaSessions.has(ip)) {
      session.state = "error";
      broadcastOtaStatus(ip, "error", session.progress || 0, "TCP 连接超时");
      otaSessions.delete(ip);
    }
  });

  tcpSock.on("close", () => {
    if (otaSessions.has(ip) && session.state !== "done" && session.state !== "error") {
      broadcastOtaStatus(ip, "error", session.progress || 0, "TCP 连接断开");
      otaSessions.delete(ip);
    }
  });
}

function handleOtaTrigger(deviceIP, totalSize) {
  const existing = otaSessions.get(deviceIP);
  if (existing && existing.state !== "done" && existing.state !== "error" && existing.state !== "cancelled") {
    broadcastOtaStatus(deviceIP, "error", 0, "该设备已有 OTA 在进行中");
    return;
  }

  const session = {
    state: "triggering",
    totalSize,
    receivedSize: 0,
    tcpSocket: null,
    progress: 0,
  };
  otaSessions.set(deviceIP, session);

  broadcastOtaStatus(deviceIP, "triggering", 0, "正在触发 OTA...");
  sendToCar(buildPacket(0x05, 0x01, 0, 0, 0), deviceIP);
}

function handleOtaChunk(deviceIP, chunkBuf) {
  const session = otaSessions.get(deviceIP);
  if (!session) {
    broadcastOtaStatus(deviceIP, "error", 0, "无活跃的 OTA 会话");
    return;
  }
  if (session.state !== "ready" && session.state !== "transferring") {
    broadcastOtaStatus(deviceIP, "error", 0, `OTA 状态错误: ${session.state}`);
    return;
  }

  session.state = "transferring";
  session.tcpSocket.write(chunkBuf);
  session.receivedSize += chunkBuf.length;
  session.progress = Math.round((session.receivedSize / session.totalSize) * 100);

  broadcastOtaStatus(deviceIP, "transferring", session.progress,
    `传输中 ${session.receivedSize}/${session.totalSize} bytes`);

  if (session.receivedSize >= session.totalSize) {
    session.state = "verifying";
    broadcastOtaStatus(deviceIP, "verifying", 100, "固件发送完毕，等待设备校验...");
    session.tcpSocket.setTimeout(60000);
  }
}

function handleOtaCancel(deviceIP) {
  const session = otaSessions.get(deviceIP);
  if (session?.tcpSocket) {
    session.tcpSocket.destroy();
  }
  if (session) {
    otaSessions.delete(deviceIP);
  }
  sendToCar(buildPacket(0x05, 0x02, 0, 0, 0), deviceIP);
  broadcastOtaStatus(deviceIP, "cancelled", 0, "OTA 已取消");
}

// --- UDP 核心逻辑 ---

udpSocket.on("error", (err) => {
  console.log(`UDP 错误:\n${err.stack}`);
  udpSocket.close();
});

udpSocket.on("message", (msg, rinfo) => {
  const ip = rinfo.address;
  const type = msg[0];
  const now = Date.now();

  let dev = devices.get(ip);
  let isNew = false;

  // 1. 处理广播发现包 (0xFF)
  // 结构: [FF, MAC(6), NAME(16)]
  if (type === 0xff) {
    // 解析 MAC
    let mac = "";
    if (msg.length >= 7) {
      const macBytes = msg.subarray(1, 7);
      mac = Array.from(macBytes)
        .map((b) => b.toString(16).padStart(2, "0").toUpperCase())
        .join(":");
    }

    // 解析 Name
    let name = "Unknown";
    if (msg.length >= 23) {
      // 查找 C 字符串结束符 \0
      const nameBuf = msg.subarray(7, 23);
      const nullIdx = nameBuf.indexOf(0);
      name = nameBuf
        .subarray(0, nullIdx >= 0 ? nullIdx : 16)
        .toString("utf8");
    }

    if (!dev || now - dev.lastSeen > CONFIG.TIMEOUT_THRESHOLD) {
      console.log(`[发现] 新设备/重连 IP:${ip} MAC:${mac} Name:${name}`);
      dev = { ip, mac, name, lastSeen: now, status: null };
      devices.set(ip, dev);
      isNew = true;

      broadcastToFrontend({
        type: "deviceDiscovered",
        device: { ip, mac, name, deviceId: `${name} (${mac})` },
      });
    } else {
      // 已存在且活跃，更新时间
      dev.lastSeen = now;
    }

    // **快速恢复机制**: 收到广播包，连续回复 3 次心跳，确保小车能收到
    // 清除之前的定时器（防止重复触发）
    if (activeIntervals.has(ip)) {
      clearInterval(activeIntervals.get(ip));
    }

    let count = 0;
    const fastReply = setInterval(() => {
      sendToCar(buildPacket(0xfe, 0), ip);
      if (++count >= 3) {
        clearInterval(fastReply);
        activeIntervals.delete(ip);
      }
    }, 100); // 每 100ms 回复一次，发三次

    activeIntervals.set(ip, fastReply);
    return;
  }

  // 2. 处理 OTA UDP 响应 (0x05)
  if (type === 0x05) {
    handleOtaUdpResponse(ip, msg[1]);
    return;
  }

  // 3. 处理普通业务包 (心跳0xFE 或 状态0x02)
  if (dev) {
    dev.lastSeen = now; // 刷新保活时间

    if (type === 0x02 && msg.length >= 5) {
      // 解析状态包
      const irRaw = msg[4];
      const status = {
        mode: msg[1],
        distance: msg[2] / 10,
        ir: [irRaw & 1, (irRaw >> 1) & 1, (irRaw >> 2) & 1],
      };
      dev.status = status;

      // 推送给前端
      broadcastToFrontend({
        type: "statusUpdate",
        ip,
        mac: dev.mac,
        status,
      });
    }
  }
});

udpSocket.bind(CONFIG.UDP_RECV_PORT, () => {
  console.log(`UDP 代理服务已启动，监听端口: ${CONFIG.UDP_RECV_PORT}`);
});

// --- 定时任务: 心跳发送与超时清理 ---
setInterval(() => {
  const now = Date.now();

  devices.forEach((dev, ip) => {
    // 1. 超时检测
    if (now - dev.lastSeen > CONFIG.TIMEOUT_THRESHOLD) {
      console.log(`[丢失] 设备超时断开: ${ip}`);
      devices.delete(ip);
      broadcastToFrontend({ type: "deviceLost", ip });
      return;
    }

    // 2. 主动发送心跳 (维持小车端的连接状态)
    // 即使没有控制命令，也要每2秒发一次 0xFE
    sendToCar(buildPacket(0xfe, 0), ip);
  });
}, CONFIG.HEARTBEAT_INTERVAL);

// --- WebSocket 前端指令处理 ---
wss.on("connection", (ws) => {
  console.log("前端页面已连接");

  // 发送当前已连接设备列表
  devices.forEach((dev) => {
    ws.send(
      JSON.stringify({
        type: "deviceDiscovered",
        device: {
          ip: dev.ip,
          mac: dev.mac,
          name: dev.name,
          deviceId: `${dev.name} (${dev.mac})`,
        },
      }),
    );
  });

  ws.on("message", (message) => {
    try {
      const data = JSON.parse(message);
      const ip = data.deviceIP;
      if (!ip || !devices.has(ip)) return;

      switch (data.type) {
        case "control": // 摇杆控制
          sendToCar(buildPacket(0x01, 0, data.motor1, data.motor2), ip);
          break;
        case "modeChange": // 模式切换
          const modeMap = { standby: 0, tracking: 1, avoid: 2, remote: 3 };
          sendToCar(buildPacket(0x03, modeMap[data.mode] || 0), ip);
          break;
        case "setPid": { // PID参数
          const val =
            data.paramType <= 3 ? Math.round(data.value * 100) : data.value;
          const high = (val >> 8) & 0xff;
          const low = val & 0xff;
          sendToCar(buildPacket(0x04, data.paramType, high, low), ip);
          break;
        }
        case "savePid": { // 显式保存 PID 到 NV
          sendToCar(buildPacket(0x04, 0xFF, 0, 0), ip);
          break;
        }
        case "wifiConfigSet": { // 保存 WiFi 配置 (0xE0)
          const ssidBuf = Buffer.from(data.ssid || "", "utf8");
          const pwdBuf = Buffer.from(data.password || "", "utf8");
          const buf = Buffer.alloc(3 + 64);
          buf[0] = 0xE0;
          buf[1] = Math.min(ssidBuf.length, 32);
          buf[2] = Math.min(pwdBuf.length, 63);
          ssidBuf.copy(buf, 3);
          pwdBuf.copy(buf, 3 + buf[1]);
          sendToCar(buf, ip);
          break;
        }
        case "wifiConfigConnect": { // 保存并连接 WiFi (0xE1)
          const ssidBuf = Buffer.from(data.ssid || "", "utf8");
          const pwdBuf = Buffer.from(data.password || "", "utf8");
          const buf = Buffer.alloc(3 + 64);
          buf[0] = 0xE1;
          buf[1] = Math.min(ssidBuf.length, 32);
          buf[2] = Math.min(pwdBuf.length, 63);
          ssidBuf.copy(buf, 3);
          pwdBuf.copy(buf, 3 + buf[1]);
          sendToCar(buf, ip);
          break;
        }
        case "wifiConfigGet": { // 获取 WiFi 配置 (0xE2)
          const buf = Buffer.alloc(3);
          buf[0] = 0xE2;
          buf[1] = 0;
          buf[2] = 0;
          sendToCar(buf, ip);
          break;
        }
        case "otaTrigger": { // OTA 触发
          handleOtaTrigger(ip, data.totalSize);
          break;
        }
        case "otaChunk": { // OTA 固件分片
          const chunk = Buffer.from(data.data, "base64");
          handleOtaChunk(ip, chunk);
          break;
        }
        case "otaCancel": { // OTA 取消
          handleOtaCancel(ip);
          break;
        }
      }
    } catch (e) {
      console.error("WS 解析错误:", e);
    }
  });
});
