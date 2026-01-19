const WebSocket = require('ws');
const dgram = require('dgram');

const CONFIG = {
    WS_PORT: 8081,
    UDP_CONTROL_PORT: 8888,
    UDP_BROADCAST_PORT: 8889,
    DEVICE_TIMEOUT: 20000  // 设备超时时间 20秒
};

const wss = new WebSocket.Server({ port: CONFIG.WS_PORT });
const udpClient = dgram.createSocket('udp4');
udpClient.bind(CONFIG.UDP_BROADCAST_PORT);

function checksum8(buf) {
    let sum = 0;
    for (let i = 0; i < buf.length; i++) sum = (sum + buf[i]) & 0xFF;
    return sum & 0xFF;
}

function buildOtaPacket(type, cmd, offset, payloadBuf) {
    const payload = payloadBuf || Buffer.alloc(0);
    const headerLen = 8;
    const totalLen = headerLen + payload.length + 1;
    const buf = Buffer.alloc(totalLen);
    buf.writeUInt8(type & 0xFF, 0);
    buf.writeUInt8(cmd & 0xFF, 1);
    buf.writeUInt32BE(offset >>> 0, 2);
    buf.writeUInt16BE(payload.length & 0xFFFF, 6);
    payload.copy(buf, 8);
    buf.writeUInt8(checksum8(buf.subarray(0, totalLen - 1)), totalLen - 1);
    return buf;
}

function parseOtaPacket(msg) {
    if (!Buffer.isBuffer(msg) || msg.length < 9) return null;
    const chk = checksum8(msg.subarray(0, msg.length - 1));
    if (chk !== msg[msg.length - 1]) return null;
    const type = msg.readUInt8(0);
    const cmd = msg.readUInt8(1);
    const offset = msg.readUInt32BE(2);
    const length = msg.readUInt16BE(6);
    if (msg.length !== 9 + length) return null;
    const payload = msg.subarray(8, 8 + length);
    return { type, cmd, offset, length, payload };
}

// 设备管理: ip -> { lastSeen, status, discovered, lastNotified }
// lastNotected: 上次通知前端的时间戳（用于防止重复通知）
const devices = new Map();

// 处理来自设备的UDP消息
udpClient.on('message', (msg, rinfo) => {
    if (msg.length >= 7) {
        const type = msg[0];
        const now = Date.now();
        const existing = devices.get(rinfo.address);

        if (type === 0x14) {
            const pkt = parseOtaPacket(msg);
            if (!pkt) return;

            let status = null;
            if (pkt.length >= 10) {
                status = {
                    otaStatus: pkt.payload.readUInt8(0),
                    progress: pkt.payload.readUInt8(1),
                    received: pkt.payload.readUInt32BE(2),
                    total: pkt.payload.readUInt32BE(6),
                };
            }

            broadcastToClients({
                type: 'otaResponse',
                ip: rinfo.address,
                code: pkt.cmd,
                offset: pkt.offset,
                status
            });
            return;
        }

        const checksum = (msg[0] + msg[1] + msg[2] + msg[3] + msg[4] + msg[5]) & 0xFF;
        if (checksum === msg[6]) {
            const now = Date.now();
            const existing = devices.get(rinfo.address);

            if (type === 0xFF) {
                // 存在广播
                // 检查是否为新设备或已超时重新连接的设备
                const wasTimedOut = !existing || (now - existing.lastSeen > CONFIG.DEVICE_TIMEOUT);
                const shouldNotify = wasTimedOut || !existing || !existing.discovered;

                devices.set(rinfo.address, {
                    lastSeen: now,
                    discovered: true,
                    status: existing ? existing.status : null,
                    lastNotified: shouldNotify ? now : (existing?.lastNotified || now)
                });

                // 新设备或重新连接的设备需要通知
                if (shouldNotify) {
                    console.log(`设备发现: ${rinfo.address}${wasTimedOut && existing ? ' (重新连接)' : ''}`);
                    broadcastToClients({
                        type: 'deviceDiscovered',
                        device: { ip: rinfo.address }
                    });
                }
            }
            else if (type === 0x02) {
                // 传感器状态数据
                // 字节布局: [type, cmd, motor1(servo), motor2(dist*10), servo(0), ir_data, checksum]
                const mode = msg[1];
                const servo = msg[2];  // 舵机角度 (存于 motor1 字段)
                const distance = msg[3] / 10;  // 距离 (存于 motor2 字段，放大10倍)
                const ir_data = msg[5];  // 红外数据 (存于 ir_data 字节，索引5)
                const ir = [(ir_data >> 0) & 1, (ir_data >> 1) & 1, (ir_data >> 2) & 1];

                // 检查是否为重新连接的设备
                const wasTimedOut = !existing || (now - existing.lastSeen > CONFIG.DEVICE_TIMEOUT);
                const shouldNotify = wasTimedOut || !existing || !existing.discovered;

                devices.set(rinfo.address, {
                    lastSeen: now,
                    discovered: true,
                    status: { mode, servo, distance, ir },
                    lastNotified: shouldNotify ? now : (existing?.lastNotified || now)
                });

                // 如果是新设备或重新连接，先发送设备发现通知
                if (shouldNotify) {
                    console.log(`设备发现(状态包): ${rinfo.address}${wasTimedOut && existing ? ' (重新连接)' : ''}`);
                    broadcastToClients({
                        type: 'deviceDiscovered',
                        device: { ip: rinfo.address }
                    });
                }

                // 转发状态更新给所有前端
                broadcastToClients({
                    type: 'statusUpdate',
                    ip: rinfo.address,
                    status: { mode, servo, distance, ir }
                });

                // 调试日志
                console.log(`状态更新 ${rinfo.address}: mode=${mode}, servo=${servo}, dist=${distance}, ir=[${ir}]`);
            }
            else if (type === 0xFE) {
                // 心跳包
                // 检查是否为重新连接的设备
                const wasTimedOut = !existing || (now - existing.lastSeen > CONFIG.DEVICE_TIMEOUT);
                const shouldNotify = wasTimedOut || !existing || !existing.discovered;

                if (existing) {
                    devices.set(rinfo.address, {
                        lastSeen: now,
                        discovered: existing.discovered,
                        status: existing.status,
                        lastNotified: shouldNotify ? now : (existing?.lastNotified || now)
                    });

                    // 如果是重新连接，发送通知
                    if (shouldNotify) {
                        console.log(`设备发现(心跳): ${rinfo.address}${wasTimedOut ? ' (重新连接)' : ''}`);
                        broadcastToClients({
                            type: 'deviceDiscovered',
                            device: { ip: rinfo.address }
                        });
                    } else {
                        console.log(`设备心跳: ${rinfo.address}`);
                    }
                } else {
                    // 心跳包也视为首次发现
                    devices.set(rinfo.address, {
                        lastSeen: now,
                        discovered: true,
                        status: null,
                        lastNotified: now
                    });
                    console.log(`设备发现(心跳): ${rinfo.address}`);
                    broadcastToClients({
                        type: 'deviceDiscovered',
                        device: { ip: rinfo.address }
                    });
                }
            }
        }
    }
});

// WebSocket 连接
wss.on('connection', (ws) => {
    console.log(`新的 WebSocket 连接: ${ws._socket.remoteAddress}`);

    // 连接成功后立即发送当前已知设备列表
    for (const [ip, device] of devices) {
        if (device.discovered) {
            ws.send(JSON.stringify({
                type: 'deviceDiscovered',
                device: { ip: ip }
            }));
            // 如果有状态数据，也发送
            if (device.status) {
                ws.send(JSON.stringify({
                    type: 'statusUpdate',
                    ip: ip,
                    status: device.status
                }));
            }
        }
    }

    ws.on('message', (data) => {
        try {
            // 确保数据是字符串格式
            let dataStr = data;
            if (Buffer.isBuffer(data)) {
                if (data.length >= 9 && data.readUInt8(0) === 0xF0) {
                    const offset = data.readUInt32BE(1) >>> 0;
                    const ip = `${data[5]}.${data[6]}.${data[7]}.${data[8]}`;
                    const payload = data.subarray(9);
                    const buf = buildOtaPacket(0x11, 0x00, offset, payload);
                    udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, ip);
                    return;
                }
                dataStr = data.toString('utf8');
            }

            // 跳过空消息或非文本消息
            if (!dataStr || typeof dataStr !== 'string' || dataStr.trim().length === 0) {
                return;
            }

            const msg = JSON.parse(dataStr);
            if (msg && typeof msg === 'object') {
                if (msg.type !== 'otaData') console.log('收到前端消息:', msg);
            }

            if (msg.type === 'control') {
                sendControl(msg.deviceIP, msg.motor1, msg.motor2, msg.servo);
            } else if (msg.type === 'modeChange') {
                sendModeChange(msg.deviceIP, msg.mode);
            } else if (msg.type === 'otaStart') {
                const payload = Buffer.alloc(4);
                payload.writeUInt32BE((msg.size >>> 0), 0);
                const buf = buildOtaPacket(0x10, 0x00, 0, payload);
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            } else if (msg.type === 'otaData') {
                const dataBuf = Buffer.from(msg.data || '', 'base64');
                const buf = buildOtaPacket(0x11, 0x00, (msg.offset >>> 0), dataBuf);
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            } else if (msg.type === 'otaEnd') {
                const buf = buildOtaPacket(0x12, 0x00, 0, Buffer.alloc(0));
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            } else if (msg.type === 'otaQuery') {
                const buf = buildOtaPacket(0x13, 0x00, 0, Buffer.alloc(0));
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            }
        } catch (error) {
            console.error('解析 WebSocket 消息失败:', error);
        }
    });

    ws.on('close', () => {
        console.log(`WebSocket 连接断开: ${ws._socket.remoteAddress}`);
    });
});

function sendControl(ip, m1, m2, servo) {
    const buf = Buffer.alloc(7);
    buf.writeUInt8(0x01, 0);   // type: 控制
    buf.writeUInt8(0x00, 1);   // cmd
    buf.writeInt8(m1, 2);      // motor1
    buf.writeInt8(m2, 3);      // motor2
    buf.writeInt8(servo, 4);   // servo
    buf.writeUInt8(0, 5);      // ir_data (不使用，用 UInt8)

    // 计算校验和：前6个字节累加和的低8位
    let sum = 0;
    for (let i = 0; i < 6; i++) {
        sum += buf.readUInt8(i);
    }
    buf.writeUInt8(sum & 0xFF, 6);

    udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, ip, (error) => {
        if (error) {
            console.error(`发送控制包到 ${ip} 失败:`, error);
        }
    });
}

function sendModeChange(ip, mode) {
    // 映射到 CarStatus 枚举值：STOP=0, TRACE=1, AVOID=2, WIFI=3
    const cmdMap = { 'standby': 0, 'tracking': 1, 'avoid': 2, 'remote': 3 };
    const cmd = cmdMap[mode] || 0;

    console.log(`发送模式切换: ${mode}(${cmd}) -> ${ip}`);

    const buf = Buffer.alloc(7);
    buf.writeUInt8(0x03, 0);   // type: 模式切换
    buf.writeUInt8(cmd, 1);    // mode cmd
    buf.writeInt8(0, 2);
    buf.writeInt8(0, 3);
    buf.writeInt8(0, 4);
    buf.writeUInt8(0, 5);

    // 计算校验和：前6个字节累加和的低8位
    let sum = 0;
    for (let i = 0; i < 6; i++) {
        sum += buf.readUInt8(i);
    }
    buf.writeUInt8(sum & 0xFF, 6);

    console.log('UDP 包内容:', Array.from(buf));

    udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, ip, (error) => {
        if (error) {
            console.error(`发送模式切换到 ${ip} 失败:`, error);
        } else {
            console.log(`模式切换包已发送到 ${ip}`);
        }
    });
}

function broadcastToClients(data) {
    wss.clients.forEach(c => {
        if (c.readyState === WebSocket.OPEN) {
            c.send(JSON.stringify(data));
        }
    });
}

// 清理超时设备（每5秒检查一次）
setInterval(() => {
    const now = Date.now();
    const timeoutList = [];

    for (const [ip, device] of devices) {
        if (now - device.lastSeen > CONFIG.DEVICE_TIMEOUT) {
            timeoutList.push(ip);
        }
    }

    // 清理并通知
    for (const ip of timeoutList) {
        devices.delete(ip);
        console.log(`设备超时断开: ${ip}`);
        broadcastToClients({
            type: 'deviceLost',
            ip: ip
        });
    }

    // 定期打印设备状态
    if (devices.size > 0) {
        console.log(`当前在线设备: ${devices.size} 台`);
        for (const [ip, device] of devices) {
            const age = Math.round((now - device.lastSeen) / 1000);
            console.log(`  ${ip} - ${age}秒前`);
        }
    }
}, 5000);

// 错误处理
udpClient.on('error', (error) => {
    console.error('UDP 客户端错误:', error);
});

wss.on('error', (error) => {
    console.error('WebSocket 服务器错误:', error);
});

console.log('代理服务器启动:');
console.log(`  WebSocket: ws://localhost:${CONFIG.WS_PORT}`);
console.log(`  UDP 监听: ${CONFIG.UDP_BROADCAST_PORT}`);
console.log(`  UDP 目标: ${CONFIG.UDP_CONTROL_PORT}`);
console.log('等待设备连接...');
