/**
 * UDP Control App for Smart Car
 * 使用UDP通信控制智能小车
 */

// --- 配置管理 ---
const config = {
    localPort: 8890,           // 本地监听端口
    remoteIP: '',              // 小车IP（自动发现）
    remotePort: 8888,          // 小车控制端口
    broadcastPort: 8889,       // 小车广播端口
    discoveryInterval: 2000     // 发现间隔
};

// --- 全局状态 ---
const appState = {
    mode: 'standby',           // standby, remote, tracking, avoid
    motor1: 0,                 // 左电机 -100~100
    motor2: 0,                 // 右电机 -100~100
    servo: 90,                 // 舵机角度 0~180
    // 传感器数据
    distance: 0,
    ir: [1, 1, 1],            // 左中右红外 (1=白/无, 0=黑/有)
    connected: false,
    carIP: '',
    carFound: false,
    // 上次发送的控制值（用于检测变化）
    lastSent: { motor1: 0, motor2: 0, servo: 90 }
};

// --- 全局变量 ---
let discoveryTimer = null;
let statusTimer = null;
let controlTimer = null;
let socket = null;
let sendLoopTimer = null;
let reconnectTimer = null;
let isManualClose = false;  // 标记是否为手动关闭连接

// --- 摇杆初始化 (Nipple.js) ---
const joystickManager = nipplejs.create({
    zone: document.getElementById('joystick-zone'),
    mode: 'static',
    position: { left: '50%', top: '50%' },
    color: '#4f46e5',
    size: 120,
    restOpacity: 0.8
});

// 摇杆事件监听（只更新状态，不直接发送）
joystickManager.on('move', (_evt, data) => {
    if (appState.mode !== 'remote') return;

    const x = Math.round(data.vector.x * 100);  // -100 ~ 100
    const y = Math.round(data.vector.y * 100);  // -100 ~ 100

    // 差速计算
    if (y > 0) {  // 前进
        appState.motor1 = clamp(y + x, -100, 100);
        appState.motor2 = clamp(y - x, -100, 100);
    } else {  // 后退
        appState.motor1 = clamp(y - x, -100, 100);
        appState.motor2 = clamp(y + x, -100, 100);
    }

    console.log(`摇杆移动: m1=${appState.motor1}, m2=${appState.motor2}`);
    updateLocalAnimations();
});

joystickManager.on('end', () => {
    if (appState.mode !== 'remote') return;

    appState.motor1 = 0;
    appState.motor2 = 0;
    updateLocalAnimations();
});

// --- 核心函数 ---

/**
 * 创建UDP数据包
 */
function createUDPPacket(type, cmd, motor, servo1, servo2) {
    const buffer = new ArrayBuffer(6);
    const view = new DataView(buffer);

    view.setUint8(0, type);    // 类型
    view.setUint8(1, cmd);     // 命令
    view.setInt8(2, motor);    // 电机值
    view.setInt8(3, servo1);   // 舵机1值
    view.setInt8(4, servo2);   // 舵机2值
    view.setUint8(5, 0);       // 校验和（占位）

    // 计算校验和
    const checksum = (view.getUint8(0) ^ view.getUint8(1) ^
                     view.getUint8(2) ^ view.getUint8(3) ^
                     view.getUint8(4)) & 0xFF;
    view.setUint8(5, checksum);

    return buffer;
}

/**
 * 发送UDP控制包（只在控制值变化时发送）
 */
function sendUDPControl() {
    if (!socket || socket.readyState !== WebSocket.OPEN) return;

    // 检查控制值是否变化
    const changed = (appState.motor1 !== appState.lastSent.motor1 ||
                     appState.motor2 !== appState.lastSent.motor2 ||
                     appState.servo !== appState.lastSent.servo);

    if (!changed) return;  // 没有变化则不发送

    try {
        const controlMsg = {
            type: 'control',
            deviceIP: appState.carIP,
            motor1: appState.motor1,
            motor2: appState.motor2,
            servo: appState.servo
        };

        socket.send(JSON.stringify(controlMsg));

        // 更新上次发送的值
        appState.lastSent = {
            motor1: appState.motor1,
            motor2: appState.motor2,
            servo: appState.servo
        };
    } catch (error) {
        console.error('发送控制消息失败:', error);
        appState.connected = false;
        setConnectionStatus(false);
    }
}

/**
 * 启动通信循环（定时检查并发送变化的控制值）
 */
function startCommsLoop() {
    if (sendLoopTimer) {
        clearInterval(sendLoopTimer);
    }

    // 每 100ms 检查一次控制值变化
    // 只有在值真正变化时才发送，减少设备负担
    sendLoopTimer = setInterval(() => {
        if (!socket || socket.readyState !== WebSocket.OPEN || !appState.carIP) return;

        // 只有在遥控模式下才检查并发送控制命令
        if (appState.mode === 'remote') {
            sendUDPControl();
        }
    }, 100);

    console.log('通信循环已启动');
}

/**
 * 停止通信循环
 */
function stopCommsLoop() {
    if (sendLoopTimer) {
        clearInterval(sendLoopTimer);
        sendLoopTimer = null;
    }
}

/**
 * 发送模式切换包
 */
function sendModeChange(mode) {
    if (!socket || socket.readyState !== WebSocket.OPEN) {
        console.log('WebSocket 未连接，无法发送模式切换');
        return;
    }

    try {
        const modeMsg = {
            type: 'modeChange',
            deviceIP: appState.carIP,
            mode: mode
        };

        socket.send(JSON.stringify(modeMsg));
        console.log(`发送模式切换: ${mode} -> ${appState.carIP}`);
    } catch (error) {
        console.error('发送模式切换消息失败:', error);
    }
}

/**
 * 连接到代理服务器
 * 后端会主动推送设备发现信息，无需手动搜索
 */
function connectToProxy() {
    // 关闭之前的连接
    if (socket) {
        isManualClose = true;  // 标记为手动关闭，避免触发重连
        socket.close();
        socket = null;
    }

    // 重置手动关闭标记（如果是自动重连调用）
    isManualClose = false;

    // 清除之前的重连定时器
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    // 创建 WebSocket 连接到代理服务器
    try {
        socket = new WebSocket('ws://localhost:8081');

        socket.onopen = () => {
            console.log('WebSocket 连接到代理服务器成功');
            document.getElementById('discoveryStatus').textContent = '已连接，等待设备...';
        };

        socket.onmessage = (event) => {
            console.log('收到 WebSocket 消息:', event.data);
            try {
                const msg = JSON.parse(event.data);
                handleProxyMessage(msg);
            } catch (e) {
                console.error('解析消息失败:', e, event.data);
            }
        };

        socket.onclose = () => {
            console.log('WebSocket 连接已关闭');
            document.getElementById('discoveryStatus').textContent = '连接已关闭';
            appState.connected = false;
            appState.carFound = false;
            appState.carIP = '';
            setConnectionStatus(false);

            // 如果不是手动关闭，则自动重连
            if (!isManualClose) {
                console.log('重连中...');
                document.getElementById('discoveryStatus').textContent = '重连中...';
                reconnectTimer = setTimeout(() => {
                    console.log('开始重连...');
                    connectToProxy();
                }, 1000);
            }
        };

        socket.onerror = (error) => {
            console.error('WebSocket 错误:', error);
            document.getElementById('discoveryStatus').textContent = '连接错误';
        };

    } catch (error) {
        console.error('创建WebSocket连接失败:', error);
        document.getElementById('discoveryStatus').textContent = '创建连接失败';

        // 创建失败也尝试重连
        if (!isManualClose) {
            reconnectTimer = setTimeout(() => {
                connectToProxy();
            }, 3000);
        }
    }
}

/**
 * 处理代理服务器消息
 */
function handleProxyMessage(msg) {
    console.log('收到代理消息:', msg.type, msg);
    if (msg.type === 'deviceDiscovered') {
        appState.carIP = msg.device.ip;
        appState.connected = true;
        setConnectionStatus(true);
        document.getElementById('cfgIP').value = msg.device.ip;
        document.getElementById('discoveryStatus').textContent = `发现小车: ${msg.device.ip}`;
        console.log(`发现小车: ${msg.device.ip}, connected=${appState.connected}`);
    }
    else if (msg.type === 'deviceLost') {
        if (appState.carIP === msg.ip) {
            appState.connected = false;
            appState.carFound = false;
            appState.carIP = '';
            document.getElementById('discoveryStatus').textContent = '设备已断开连接';
            setConnectionStatus(false);
            console.log(`设备丢失: ${msg.ip}`);
        }
    }
    else if (msg.type === 'statusUpdate') {
        // 更新传感器数据和模式
        if (appState.carIP === msg.ip) {
            appState.servo = msg.status.servo || 90;
            appState.distance = msg.status.distance || 0;
            appState.ir = msg.status.ir || [1, 1, 1];

            // CarStatus 枚举值：0=STOP, 1=TRACE, 2=AVOID, 3=WIFI
            const modeNames = ['standby', 'tracking', 'avoid', 'remote'];
            const newMode = modeNames[msg.status.mode] || 'standby';

            // 只有模式真正改变时才更新UI
            if (appState.mode !== newMode) {
                appState.mode = newMode;
                updateModeButtons(newMode);
                console.log(`模式已更新: ${newMode} (mode=${msg.status.mode})`);
            }

            renderVisuals();
        }
    }
}

/**
 * 发送发现广播 (已弃用)
 */
function sendDiscoveryBroadcast() {
    // 通过代理设备发现，不再需要手动广播
    console.log('设备发现已通过代理服务器自动完成');
}

// --- 模式切换 ---

/**
 * 更新模式按钮UI
 */
function updateModeButtons(mode) {
    document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.toggle('active', btn.getAttribute('onclick').includes(mode));
    });

    // 摇杆显隐
    const joyZone = document.getElementById('joystick-zone');
    if (mode === 'remote') {
        joyZone.classList.remove('disabled');
    } else {
        joyZone.classList.add('disabled');
        // 退出遥控模式时重置动力
        appState.motor1 = 0;
        appState.motor2 = 0;
        updateLocalAnimations();
    }
}

/**
 * 切换模式
 */
function changeMode(mode) {
    console.log(`切换模式: ${mode}, connected=${appState.connected}, carIP=${appState.carIP}`);
    appState.mode = mode;

    // 更新UI按钮和摇杆显隐
    updateModeButtons(mode);

    // 发送模式切换
    if (appState.connected) {
        sendModeChange(mode);
    } else {
        console.log('设备未连接，无法切换模式');
    }
}

// --- 视觉渲染 ---
function renderVisuals() {
    // 1. 更新数值显示
    document.getElementById('statusDist').innerText = appState.distance.toFixed(1);
    document.getElementById('statusAngle').innerText = appState.servo;

    // 2. 舵机旋转
    const rotation = appState.servo - 90;
    document.getElementById('servoHead').style.transform = `translateX(-50%) rotate(${rotation}deg)`;

    // 3. 雷达波束
    const beam = document.getElementById('radarBeam');
    const beamLen = Math.min(appState.distance * 3, 150);
    beam.style.borderTopWidth = `${beamLen}px`;

    // 距离过近变红
    if (appState.distance > 0 && appState.distance < 20) {
        beam.classList.add('danger');
    } else {
        beam.classList.remove('danger');
    }

    // 4. 循迹传感器
    const irs = ['irL', 'irM', 'irR'];
    appState.ir.forEach((val, idx) => {
        const el = document.getElementById(irs[idx]);
        if (val === 0) el.classList.add('active');
        else el.classList.remove('active');
    });
}

function updateLocalAnimations() {
    const wheelL = document.getElementById('wheelL');
    const wheelR = document.getElementById('wheelR');

    // 清除动画
    wheelL.classList.remove('spinning', 'spinning-reverse');
    wheelR.classList.remove('spinning', 'spinning-reverse');

    if (appState.mode !== 'remote') return;

    // 根据电机速度控制动画
    if (appState.motor1 > 10) {  // 左轮前进
        wheelL.classList.add('spinning');
    } else if (appState.motor1 < -10) {  // 左轮后退
        wheelL.classList.add('spinning-reverse');
    }

    if (appState.motor2 > 10) {  // 右轮前进
        wheelR.classList.add('spinning');
    } else if (appState.motor2 < -10) {  // 右轮后退
        wheelR.classList.add('spinning-reverse');
    }
}

// --- 辅助工具 ---
function setConnectionStatus(isOnline) {
    const el = document.getElementById('statusConn');
    if (isOnline) {
        el.classList.add('connected');
        el.classList.remove('disconnected');
    } else {
        el.classList.remove('connected');
        el.classList.add('disconnected');
    }
}

function clamp(v, min, max) {
    return Math.min(Math.max(v, min), max);
}

// --- 设置弹窗逻辑 ---
function toggleConfig() {
    const m = document.getElementById('configModal');
    const isOpen = m.style.display === 'flex';
    m.style.display = isOpen ? 'none' : 'flex';
}

function saveConfig() {
    toggleConfig();
}

// --- 初始化 ---
window.onload = function() {
    // 自动连接到代理服务器
    connectToProxy();

    // 默认高亮待机
    changeMode('standby');

    // 启动通信循环
    startCommsLoop();
};

// --- 页面卸载时清理 ---
window.onbeforeunload = function() {
    isManualClose = true;  // 防止触发自动重连

    if (discoveryTimer) {
        clearInterval(discoveryTimer);
    }
    if (statusTimer) {
        clearInterval(statusTimer);
    }
    if (controlTimer) {
        clearInterval(controlTimer);
    }
    if (sendLoopTimer) {
        clearInterval(sendLoopTimer);
    }
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
    }
    if (socket) {
        socket.close();
    }
};