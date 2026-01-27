const WebSocket = require('ws');
const dgram = require('dgram');

// --- 配置 ---
const CONFIG = {
    WS_PORT: 8081,
    UDP_SEND_PORT: 8888,
    UDP_RECV_PORT: 8889,
    TIMEOUT: 20000
};

// --- 全局状态 ---
const devices = new Map(); // ip -> { lastSeen, status, discovered }
const udpClient = dgram.createSocket('udp4');
const wss = new WebSocket.Server({ port: CONFIG.WS_PORT });

// --- 辅助函数 ---

// 8位累加校验和
const checksum8 = (buf, len) => {
    let sum = 0;
    for (let i = 0; i < len; i++) sum = (sum + buf[i]) & 0xFF;
    return sum & 0xFF;
};

// 发送广播给前端
const broadcast = (data) => {
    const msg = JSON.stringify(data);
    wss.clients.forEach(c => c.readyState === WebSocket.OPEN && c.send(msg));
};

// UDP 发送封装
const sendToCar = (buf, ip) => {
    udpClient.send(buf, CONFIG.UDP_SEND_PORT, ip, (err) => err && console.error(`发送失败 ${ip}:`, err));
};

// 通用车辆协议包构建 (7字节: Type, Cmd, Data*4, Checksum)
const buildCarPacket = (type, cmd, bytes = [0, 0, 0, 0]) => {
    const buf = Buffer.alloc(7);
    buf[0] = type; buf[1] = cmd;
    bytes.forEach((b, i) => buf[2 + i] = b);
    buf[6] = checksum8(buf, 6);
    return buf;
};

// OTA 包构建
const buildOtaPacket = (type, cmd, offset, payload = Buffer.alloc(0)) => {
    const totalLen = 9 + payload.length;
    const buf = Buffer.alloc(totalLen);
    buf.writeUInt8(type, 0);
    buf.writeUInt8(cmd, 1);
    buf.writeUInt32BE(offset, 2);
    buf.writeUInt16BE(payload.length, 6);
    payload.copy(buf, 8);
    buf[totalLen - 1] = checksum8(buf, totalLen - 1);
    return buf;
};

// 更新设备状态并处理超时重连
const updateDeviceState = (ip, newStatus = null) => {
    const now = Date.now();
    const dev = devices.get(ip);
    const isNew = !dev || (now - dev.lastSeen > CONFIG.TIMEOUT);

    devices.set(ip, {
        lastSeen: now,
        discovered: true,
        status: newStatus || dev?.status || null
    });

    if (isNew) {
        console.log(`设备上线/重连: ${ip}`);
        broadcast({ type: 'deviceDiscovered', device: { ip } });
    }
    return isNew;
};

// --- UDP 监听 (处理设备消息) ---
udpClient.bind(CONFIG.UDP_RECV_PORT);
udpClient.on('message', (msg, rinfo) => {
    if (msg.length < 7) return;
    const type = msg[0];
    const ip = rinfo.address;

    // 1. 处理 OTA 应答 (Type 0x14)
    if (type === 0x14) {
        if (checksum8(msg, msg.length - 1) !== msg[msg.length - 1]) return;
        const len = msg.readUInt16BE(6);
        const payload = msg.subarray(8, 8 + len);
        const status = len >= 8 ? {
            otaStatus: payload[0], progress: payload[1],
            received: payload.readUInt32BE(2), total: payload.readUInt32BE(6)
        } : null;
        broadcast({ type: 'otaResponse', ip, code: msg[1], offset: msg.readUInt32BE(2), status });
        return;
    }

    // 2. 处理普通协议 (校验和在第7字节)
    if (checksum8(msg, 6) !== msg[6]) return;

    if (type === 0xFF || type === 0xFE) { // 广播 (0xFF) 或 心跳 (0xFE)
        updateDeviceState(ip);
    } else if (type === 0x02) { // 状态数据 (0x02)
        const irRaw = msg[5];
        const status = {
            mode: msg[1],
            servo: msg[2],
            distance: msg[3] / 10,
            ir: [irRaw & 1, (irRaw >> 1) & 1, (irRaw >> 2) & 1]
        };
        updateDeviceState(ip, status);
        broadcast({ type: 'statusUpdate', ip, status });
    }
});

// --- WebSocket 处理 (处理前端指令) ---
wss.on('connection', (ws) => {
    console.log(`前端连接: ${ws._socket.remoteAddress}`);
    // 发送当前设备列表
    devices.forEach((dev, ip) => {
        ws.send(JSON.stringify({ type: 'deviceDiscovered', device: { ip } }));
        if (dev.status) ws.send(JSON.stringify({ type: 'statusUpdate', ip, status: dev.status }));
    });

    ws.on('message', (raw) => {
        try {
            // 处理 OTA 二进制流 (Type 0xF0)
            if (Buffer.isBuffer(raw) && raw[0] === 0xF0 && raw.length >= 9) {
                const offset = raw.readUInt32BE(1);
                const ip = `${raw[5]}.${raw[6]}.${raw[7]}.${raw[8]}`;
                sendToCar(buildOtaPacket(0x11, 0x00, offset, raw.subarray(9)), ip);
                return;
            }

            const msg = JSON.parse(raw.toString());
            const ip = msg.deviceIP;
            if (!ip) return;

            switch (msg.type) {
                case 'control': // 电机舵机控制
                    sendToCar(buildCarPacket(0x01, 0x00, [msg.motor1, msg.motor2, msg.servo, 0]), ip);
                    break;

                case 'modeChange': // 模式切换
                    const modeMap = { 'standby': 0, 'tracking': 1, 'avoid': 2, 'remote': 3 };
                    sendToCar(buildCarPacket(0x03, modeMap[msg.mode] || 0), ip);
                    break;

                case 'setPid': // PID 参数设置
                    const val = msg.paramType <= 3 ? Math.round(msg.value * 100) : msg.value;
                    sendToCar(buildCarPacket(0x04, msg.paramType, [(val >> 8) & 0xFF, val & 0xFF, 0, 0]), ip);
                    break;

                // OTA 相关命令
                case 'otaStart':
                    const sizeBuf = Buffer.alloc(4); sizeBuf.writeUInt32BE(msg.size, 0);
                    sendToCar(buildOtaPacket(0x10, 0x00, 0, sizeBuf), ip);
                    break;
                case 'otaData':
                    sendToCar(buildOtaPacket(0x11, 0x00, msg.offset, Buffer.from(msg.data, 'base64')), ip);
                    break;
                case 'otaEnd':
                    sendToCar(buildOtaPacket(0x12, 0x00, 0), ip);
                    break;
                case 'otaQuery':
                    sendToCar(buildOtaPacket(0x13, 0x00, 0), ip);
                    break;
            }
        } catch (e) { console.error('WS 消息错误:', e); }
    });
});

// --- 定时清理超时设备 ---
setInterval(() => {
    const now = Date.now();
    for (const [ip, dev] of devices) {
        if (now - dev.lastSeen > CONFIG.TIMEOUT) {
            devices.delete(ip);
            console.log(`设备超时: ${ip}`);
            broadcast({ type: 'deviceLost', ip });
        }
    }
}, 5000);

// --- 启动日志 ---
console.log(`代理启动 WS:${CONFIG.WS_PORT} UDP:${CONFIG.UDP_RECV_PORT}->${CONFIG.UDP_SEND_PORT}`);