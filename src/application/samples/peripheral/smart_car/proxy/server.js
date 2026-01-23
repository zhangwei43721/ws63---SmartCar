/**
 * 智能小车 WebSocket-UDP 代理服务器
 *
 * 功能说明：
 * 1. 监听 WebSocket 连接，接收前端控制命令
 * 2. 转发控制命令到智能小车（UDP 协议）
 * 3. 监听智能小车的 UDP 广播和状态数据
 * 4. 将设备状态和 OTA 进度转发给前端
 * 5. 管理设备的在线状态和超时检测
 *
 * 通信协议：
 * - WebSocket: 前端 <-> 代理（JSON 格式）
 * - UDP: 代理 <-> 智能小车（二进制协议）
 */

const WebSocket = require('ws');
const dgram = require('dgram');

/**
 * 代理服务器配置
 */
const CONFIG = {
    WS_PORT: 8081,              // WebSocket 服务端口
    UDP_CONTROL_PORT: 8888,     // UDP 控制端口（发送到设备）
    UDP_BROADCAST_PORT: 8889,    // UDP 广播端口（监听设备消息）
    DEVICE_TIMEOUT: 20000       // 设备超时时间 20 秒
};

/**
 * 8 位累加校验和计算
 * @param {Buffer} buf - 数据缓冲区
 * @returns {number} 8 位校验和
 */
function checksum8(buf) {
    let sum = 0;
    for (let i = 0; i < buf.length; i++) sum = (sum + buf[i]) & 0xFF;
    return sum & 0xFF;
}

/**
 * 构建 OTA 升级数据包
 * @param {number} type - 包类型 (0x10=Start, 0x11=Data, 0x12=End, 0x13=Ping)
 * @param {number} cmd - 命令字节
 * @param {number} offset - 数据偏移量
 * @param {Buffer} payloadBuf - 负载数据
 * @returns {Buffer} 完整的 OTA 数据包
 */
function buildOtaPacket(type, cmd, offset, payloadBuf) {
    const payload = payloadBuf || Buffer.alloc(0);
    const headerLen = 8;
    const totalLen = headerLen + payload.length + 1;
    const buf = Buffer.alloc(totalLen);

    buf.writeUInt8(type & 0xFF, 0);                      // [0] type
    buf.writeUInt8(cmd & 0xFF, 1);                        // [1] cmd
    buf.writeUInt32BE(offset >>> 0, 2);                  // [2-5] offset (大端序)
    buf.writeUInt16BE(payload.length & 0xFFFF, 6);       // [6-7] payload_len (大端序)
    payload.copy(buf, 8);                                // [8...] payload
    buf.writeUInt8(checksum8(buf.subarray(0, totalLen - 1)), totalLen - 1);  // [last] checksum

    return buf;
}

/**
 * 解析 OTA 应答数据包
 * @param {Buffer} msg - 接收到的数据包
 * @returns {Object|null} 解析结果 {type, cmd, offset, length, payload} 或 null
 */
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

/**
 * 设备管理: ip -> { lastSeen, status, discovered, lastNotified }
 * - lastSeen: 上次收到消息的时间戳
 * - status: 设备状态 {mode, servo, distance, ir}
 * - discovered: 是否已被发现
 * - lastNotified: 上次通知前端的时间戳
 */
const devices = new Map();

/**
 * 处理来自设备的 UDP 消息
 */
udpClient.on('message', (msg, rinfo) => {
    if (msg.length >= 7) {
        const type = msg[0];
        const now = Date.now();
        const existing = devices.get(rinfo.address);

        if (type === 0x14) {
            // OTA 应答包
            const pkt = parseOtaPacket(msg);
            if (!pkt) return;

            let status = null;
            if (pkt.length >= 10) {
                status = {
                    otaStatus: pkt.payload.readUInt8(0),      // OTA 状态
                    progress: pkt.payload.readUInt8(1),        // 进度 0-100
                    received: pkt.payload.readUInt32BE(2),    // 已接收字节数
                    total: pkt.payload.readUInt32BE(6)        // 总字节数
                };
            }

            broadcastToClients({
                type: 'otaResponse',
                ip: rinfo.address,
                code: pkt.cmd,       // 应答码 (0=成功)
                offset: pkt.offset,   // 已接收偏移量
                status
            });
            return;
        }

        // 计算普通包的校验和
        const checksum = (msg[0] + msg[1] + msg[2] + msg[3] + msg[4] + msg[5]) & 0xFF;
        if (checksum === msg[6]) {
            const now = Date.now();
            const existing = devices.get(rinfo.address);

            if (type === 0xFF) {
                // 设备存在广播 (type=0xFF)
                const wasTimedOut = !existing || (now - existing.lastSeen > CONFIG.DEVICE_TIMEOUT);
                const shouldNotify = wasTimedOut || !existing || !existing.discovered;

                devices.set(rinfo.address, {
                    lastSeen: now,
                    discovered: true,
                    status: existing ? existing.status : null,
                    lastNotified: shouldNotify ? now : (existing?.lastNotified || now)
                });

                // 新设备或重新连接的设备需要通知前端
                if (shouldNotify) {
                    console.log(`设备发现: ${rinfo.address}${wasTimedOut && existing ? ' (重新连接)' : ''}`);
                    broadcastToClients({
                        type: 'deviceDiscovered',
                        device: { ip: rinfo.address }
                    });
                }
            }
            else if (type === 0x02) {
                // 传感器状态数据包 (type=0x02)
                // 字节布局: [type, cmd, motor1(servo), motor2(dist*10), servo(0), ir_data, checksum]
                const mode = msg[1];           // 模式编号
                const servo = msg[2];         // 舵机角度 (存于 motor1 字段)
                const distance = msg[3] / 10;  // 距离 (存于 motor2 字段，放大10倍)
                const ir_data = msg[5];        // 红外数据
                const ir = [(ir_data >> 0) & 1, (ir_data >> 1) & 1, (ir_data >> 2) & 1];  // [左, 中, 右]

                const wasTimedOut = !existing || (now - existing.lastSeen > CONFIG.DEVICE_TIMEOUT);
                const shouldNotify = wasTimedOut || !existing || !existing.discovered;

                devices.set(rinfo.address, {
                    lastSeen: now,
                    discovered: true,
                    status: { mode, servo, distance, ir },
                    lastNotified: shouldNotify ? now : (existing?.lastNotified || now)
                });

                // 新设备或重新连接，先发送设备发现通知
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

                console.log(`状态更新 ${rinfo.address}: mode=${mode}, servo=${servo}, dist=${distance}, ir=[${ir}]`);
            }
            else if (type === 0xFE) {
                // 心跳包 (type=0xFE)
                const wasTimedOut = !existing || (now - existing.lastSeen > CONFIG.DEVICE_TIMEOUT);
                const shouldNotify = wasTimedOut || !existing || !existing.discovered;

                if (existing) {
                    devices.set(rinfo.address, {
                        lastSeen: now,
                        discovered: existing.discovered,
                        status: existing.status,
                        lastNotified: shouldNotify ? now : (existing?.lastNotified || now)
                    });

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
                    // 首次通过心跳发现设备
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

/**
 * WebSocket 连接处理
 */
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

    /**
     * 处理前端 WebSocket 消息
     */
    ws.on('message', (data) => {
        try {
            let dataStr = data;

            // 处理 OTA 二进制数据包 (type=0xF0)
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

            /**
             * 发送 PID 参数到设备
             * @param {string} ip - 设备 IP 地址
             * @param {number} type - 参数类型 (1=Kp, 2=Ki, 3=Kd, 4=Speed)
             * @param {number} value - 参数值 (PID 参数乘以 100，Speed 参数为整型)
             */
            function sendPidParam(ip, type, value) {
                let valToSend = value;
                if (type <= 3) {
                    valToSend = Math.round(value * 100);  // PID 参数需要乘以 100
                }

                const buf = Buffer.alloc(7);
                buf.writeUInt8(0x04, 0);   // type: PID 设置
                buf.writeUInt8(type, 1);   // cmd: 参数类型

                // 将值拆分为高低 8 位 (大端序)
                buf.writeUInt8((valToSend >> 8) & 0xFF, 2);  // motor1: 高字节
                buf.writeUInt8(valToSend & 0xFF, 3);         // motor2: 低字节

                buf.writeInt8(0, 4);       // servo: 未使用
                buf.writeUInt8(0, 5);      // ir_data: 未使用

                // 计算校验和
                let sum = 0;
                for (let i = 0; i < 6; i++) {
                    sum += buf.readUInt8(i);
                }
                buf.writeUInt8(sum & 0xFF, 6);

                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, ip, (error) => {
                    if (error) {
                        console.error(`发送PID参数到 ${ip} 失败:`, error);
                    } else {
                        console.log(`PID参数已发送 ${ip}: type=${type} val=${valToSend}`);
                    }
                });
            }

            // 处理不同类型的消息
            if (msg.type === 'control') {
                sendControl(msg.deviceIP, msg.motor1, msg.motor2, msg.servo);
            } else if (msg.type === 'modeChange') {
                sendModeChange(msg.deviceIP, msg.mode);
            } else if (msg.type === 'setPid') {
                sendPidParam(msg.deviceIP, msg.paramType, msg.value);
            } else if (msg.type === 'otaStart') {
                // 开始 OTA 升级
                const payload = Buffer.alloc(4);
                payload.writeUInt32BE((msg.size >>> 0), 0);
                const buf = buildOtaPacket(0x10, 0x00, 0, payload);
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            } else if (msg.type === 'otaData') {
                // OTA 数据传输
                const dataBuf = Buffer.from(msg.data || '', 'base64');
                const buf = buildOtaPacket(0x11, 0x00, (msg.offset >>> 0), dataBuf);
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            } else if (msg.type === 'otaEnd') {
                // OTA 结束
                const buf = buildOtaPacket(0x12, 0x00, 0, Buffer.alloc(0));
                udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, msg.deviceIP);
            } else if (msg.type === 'otaQuery') {
                // OTA 查询进度
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

/**
 * 发送控制命令到设备
 * @param {string} ip - 设备 IP 地址
 * @param {number} m1 - 左电机值 (-100~100)
 * @param {number} m2 - 右电机值 (-100~100)
 * @param {number} servo - 舵机角度 (0~180)
 */
function sendControl(ip, m1, m2, servo) {
    const buf = Buffer.alloc(7);
    buf.writeUInt8(0x01, 0);   // type: 控制
    buf.writeUInt8(0x00, 1);   // cmd
    buf.writeInt8(m1, 2);        // motor1
    buf.writeInt8(m2, 3);        // motor2
    buf.writeInt8(servo, 4);     // servo
    buf.writeUInt8(0, 5);       // ir_data (未使用)

    // 计算校验和：前 6 个字节累加和的低 8 位
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

/**
 * 发送模式切换命令到设备
 * @param {string} ip - 设备 IP 地址
 * @param {string} mode - 模式名称 ('standby', 'tracking', 'avoid', 'remote')
 */
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

    // 计算校验和
    let sum = 0;
    for (let i = 0; i < 6; i++) {
        sum += buf.readUInt8(i);
    }
    buf.writeUInt8(sum & 0xFF, 6);

    udpClient.send(buf, CONFIG.UDP_CONTROL_PORT, ip, (error) => {
        if (error) {
            console.error(`发送模式切换到 ${ip} 失败:`, error);
        } else {
            console.log(`模式切换包已发送到 ${ip}`);
        }
    });
}

/**
 * 广播消息到所有 WebSocket 客户端
 * @param {Object} data - 要广播的数据对象
 */
function broadcastToClients(data) {
    wss.clients.forEach(c => {
        if (c.readyState === WebSocket.OPEN) {
            c.send(JSON.stringify(data));
        }
    });
}

/**
 * 清理超时设备（每 5 秒检查一次）
 */
setInterval(() => {
    const now = Date.now();
    const timeoutList = [];

    // 找出所有超时设备
    for (const [ip, device] of devices) {
        if (now - device.lastSeen > CONFIG.DEVICE_TIMEOUT) {
            timeoutList.push(ip);
        }
    }

    // 清理并通知前端
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

// 启动日志
console.log('代理服务器启动:');
console.log(`  WebSocket: ws://localhost:${CONFIG.WS_PORT}`);
console.log(`  UDP 监听: ${CONFIG.UDP_BROADCAST_PORT}`);
console.log(`  UDP 目标: ${CONFIG.UDP_CONTROL_PORT}`);
console.log('等待设备连接...');
