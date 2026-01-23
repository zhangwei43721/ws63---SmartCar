/**
 * UDP Control App for Smart Car (Modified)
 * 修复了摇杆混控逻辑和模式切换协议
 */

// --- 配置管理 ---
const config = {
    // 代理服务器地址（WebSocket）
    proxyUrl: 'ws://192.168.111.122:8081',
    discoveryInterval: 2000
};

// --- 全局状态 ---
const appState = {
    mode: 'standby',           // 界面显示用的模式字符串
    motor1: 0,                 // 左电机 -100~100
    motor2: 0,                 // 右电机 -100~100
    servo: 90,                 // 舵机角度 0~180
    // 传感器数据
    distance: 0,
    ir: [1, 1, 1],            // 左中右红外
    connected: false,
    carIP: '',
    carFound: false,
    // 上次发送的控制值（用于检测变化）
    lastSent: { motor1: 0, motor2: 0, servo: 90 },
    lastControlSendAt: 0
};

// --- C语言固件定义的模式枚举 ---
// 必须与 C 代码中的 CarStatus 枚举保持一致
const MODE_MAP = {
    'standby': 0,  // CAR_STOP_STATUS
    'tracking': 1, // CAR_TRACE_STATUS
    'avoid': 2,    // CAR_OBSTACLE_AVOID_STATUS
    'remote': 3    // CAR_WIFI_CONTROL_STATUS (C代码里你是3)
};

// --- 全局变量 ---
let socket = null;
let sendLoopTimer = null;
let reconnectTimer = null;
let isManualClose = false;
const DPAD_SPEED = 100;

const otaState = {
    active: false,
    phase: 'idle',
    file: null,
    data: null,
    total: 0,
    offset: 0,
    chunkSize: 1400,
    fallbackChunkSize: 200,
    fallbackTried: false,
    ackTimeoutMs: 600,
    maxRetries: 8,
    waitingForOffset: null,
    waitingForLen: 0,
    retries: 0,
    ackTimer: null
};

function isFwpkg(u8) {
    return !!(u8 && u8.length >= 4 && u8[0] === 0xDF && u8[1] === 0xAD && u8[2] === 0xBE && u8[3] === 0xEF);
}

function setDrive(m1, m2) {
    appState.motor1 = clamp(m1, -100, 100);
    appState.motor2 = clamp(m2, -100, 100);
    updateLocalAnimations();
}

function bindHoldButton(el, onPress) {
    if (!el) return;

    const press = (evt) => {
        evt.preventDefault();
        if (appState.mode !== 'remote') return;
        onPress();
    };

    const release = (evt) => {
        evt.preventDefault();
        if (appState.mode !== 'remote') return;
        setDrive(0, 0);
    };

    el.addEventListener('pointerdown', press);
    el.addEventListener('pointerup', release);
    el.addEventListener('pointercancel', release);
    el.addEventListener('pointerleave', release);
}

// --- PID 调试功能 ---
function updatePidVal(id, val) {
    const el = document.getElementById(id);
    if (el) el.innerText = val;
}

function sendPid(type) {
    console.log(`[Frontend] sendPid called with type: ${type}`);
    if (!appState.carIP) {
        console.warn('[Frontend] sendPid aborted: No carIP set');
        alert('请先连接小车！');
        return;
    }

    let val = 0;
    // float 类型需要 / 100 还原为实际 float (后端会除以100，这里前端直接发整数)
    if (type === 1) val = parseFloat(document.getElementById('pidKp').value);
    else if (type === 2) val = parseFloat(document.getElementById('pidKi').value);
    else if (type === 3) val = parseFloat(document.getElementById('pidKd').value);
    else if (type === 4) val = parseInt(document.getElementById('pidSpeed').value);

    console.log(`[Frontend] Sending PID: type=${type}, val=${val}, IP=${appState.carIP}`);

    if (socket && socket.readyState === WebSocket.OPEN) {
        socket.send(JSON.stringify({
            type: 'setPid',
            deviceIP: appState.carIP,
            paramType: type,
            value: val
        }));
        console.log('[Frontend] PID command sent to proxy');
    } else {
        console.error('[Frontend] Socket not ready');
    }
}

// --- 核心函数 ---

/**
 * 发送UDP控制包（通过WebSocket发给代理）
 */
function sendUDPControl() {
    if (!socket || socket.readyState !== WebSocket.OPEN) return;

    // 检查控制值是否变化
    const changed = (appState.motor1 !== appState.lastSent.motor1 ||
        appState.motor2 !== appState.lastSent.motor2 ||
        appState.servo !== appState.lastSent.servo);

    const now = Date.now();
    // 如果没有变化，但距离上次发送超过200ms，也发送一次（作为心跳/保活）
    const shouldKeepAliveSend = (appState.lastControlSendAt === 0 || (now - appState.lastControlSendAt) >= 200);

    if (!changed && !shouldKeepAliveSend) return;

    try {
        // 构建发送给代理服务器的JSON
        // 代理服务器负责将其打包成 C 结构体所需的二进制
        const controlMsg = {
            type: 'control',      // 对应 C 代码 UDP 包 type=0x01
            deviceIP: appState.carIP,
            motor1: parseInt(appState.motor1), // 确保是整数
            motor2: parseInt(appState.motor2), // 确保是整数
            servo: parseInt(appState.servo)    // 确保是整数
        };

        socket.send(JSON.stringify(controlMsg));

        // 更新上次发送的值
        appState.lastSent = {
            motor1: appState.motor1,
            motor2: appState.motor2,
            servo: appState.servo
        };
        appState.lastControlSendAt = now;
    } catch (error) {
        console.error('发送控制消息失败:', error);
        appState.connected = false;
        setConnectionStatus(false);
    }
}

/**
 * 启动通信循环
 */
function startCommsLoop() {
    if (sendLoopTimer) clearInterval(sendLoopTimer);

    // 每 50ms 检查并发送一次，提高响应速度
    sendLoopTimer = setInterval(() => {
        if (!socket || socket.readyState !== WebSocket.OPEN || !appState.carIP) return;

        // 只有在遥控模式下才持续发送控制命令
        // 其他模式下，车会自动跑，不需要前端一直发指令干扰
        if (appState.mode === 'remote') {
            sendUDPControl();
        }
    }, 50); // 建议改为 50ms 左右，100ms 略有延迟感

    console.log('通信循环已启动');
}

/**
 * 修复模式切换失效的问题
 * 将 type: 'modeChange' 改为标准代理通常识别的 type: 'mode'
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
 */
function connectToProxy() {
    if (socket) {
        isManualClose = true;
        socket.close();
        socket = null;
    }
    isManualClose = false;
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    try {
        socket = new WebSocket(config.proxyUrl);

        socket.onopen = () => {
            console.log('代理服务器连接成功');
            document.getElementById('discoveryStatus').textContent = '已连接，等待设备...';
        };

        socket.onmessage = (event) => {
            try {
                const msg = JSON.parse(event.data);
                handleProxyMessage(msg);
            } catch (e) {
                console.error('解析消息失败:', e);
            }
        };

        socket.onclose = () => {
            console.log('连接已关闭');
            document.getElementById('discoveryStatus').textContent = '连接已断开';
            setConnectionStatus(false);
            appState.connected = false;

            if (!isManualClose) {
                document.getElementById('discoveryStatus').textContent = '尝试重连...';
                reconnectTimer = setTimeout(connectToProxy, 1000);
            }
        };

        socket.onerror = (err) => {
            console.error('WebSocket 错误', err);
        };

    } catch (error) {
        console.error('连接失败:', error);
        if (!isManualClose) {
            reconnectTimer = setTimeout(connectToProxy, 3000);
        }
    }
}

/**
 * 处理接收到的消息
 */
function handleProxyMessage(msg) {
    // 处理设备发现
    if (msg.type === 'deviceDiscovered') {
        // 如果是新发现或者IP变了
        if (appState.carIP !== msg.device.ip) {
            appState.carIP = msg.device.ip;
            console.log(`发现小车: ${appState.carIP}`);
            document.getElementById('discoveryStatus').textContent = `已连接: ${appState.carIP}`;
            document.getElementById('cfgIP').value = appState.carIP;
        }

        appState.connected = true;
        setConnectionStatus(true);
    }
    // 处理设备丢失
    else if (msg.type === 'deviceLost') {
        if (appState.carIP === msg.ip) {
            appState.connected = false;
            setConnectionStatus(false);
            document.getElementById('discoveryStatus').textContent = '设备信号丢失';
        }
    }
    // 处理状态更新 (从车发回来的数据)
    else if (msg.type === 'statusUpdate') {
        if (appState.carIP === msg.ip) {
            // 更新传感器数据
            appState.servo = msg.status.servo || 90;
            appState.distance = msg.status.distance || 0;
            appState.ir = msg.status.ir || [1, 1, 1];

            // 更新模式状态 (反向映射：整数 -> 字符串)
            // C代码发回来的是整数
            const serverModeId = msg.status.mode;
            const modeNames = ['standby', 'tracking', 'avoid', 'remote'];
            const newModeStr = modeNames[serverModeId] || 'standby';

            // 只有当模式真的变了，才更新UI
            // 注意：如果在前端刚点了切换，还没收到回包，这里可能会短暂跳变，属于正常现象
            if (appState.mode !== newModeStr) {
                // 如果当前正在遥控操作中，暂时不被动切换，防止冲突？
                // 这里选择信任回包
                appState.mode = newModeStr;
                updateModeButtons(newModeStr);
                console.log(`同步设备模式: ${newModeStr} (${serverModeId})`);
            }

            renderVisuals();
        }
    }
    else if (msg.type === 'otaResponse') {
        if (appState.carIP === msg.ip) {
            handleOtaResponse(msg);
        }
    }
}

// --- 模式切换与UI ---

function updateModeButtons(mode) {
    document.querySelectorAll('.mode-btn').forEach(btn => {
        // 检查按钮onclick属性中是否包含当前模式名
        const btnMode = btn.getAttribute('onclick').match(/'(.*?)'/)[1];
        if (btnMode === mode) {
            btn.classList.add('active');
        } else {
            btn.classList.remove('active');
        }
    });

    // PID 面板仅在循迹模式下显示
    const pidPanel = document.getElementById('pidPanel');
    if (pidPanel) {
        if (mode === 'tracking') {
            pidPanel.style.display = 'block';
        } else {
            pidPanel.style.display = 'none';
        }
    }

    const dpad = document.getElementById('dpad');
    if (mode === 'remote') {
        dpad?.classList.remove('disabled');
    } else {
        dpad?.classList.add('disabled');
        // 非遥控模式，停止发送电机指令
        setDrive(0, 0);
    }
}

function changeMode(modeStr) {
    console.log(`请求切换模式: ${modeStr}`);

    // 立即更新UI，不等回包，提升体验
    appState.mode = modeStr;
    updateModeButtons(modeStr);

    if (appState.connected) {
        sendModeChange(modeStr);
    } else {
        console.warn('设备未连接，模式切换指令可能无法发送');
    }
}

// --- 视觉渲染 ---

function renderVisuals() {
    document.getElementById('statusDist').innerText = appState.distance.toFixed(1);
    document.getElementById('statusAngle').innerText = appState.servo;

    // 舵机角度修正：前端显示可能需要根据实际安装调整
    // 假设 90度是正前
    const rotation = appState.servo - 90;
    document.getElementById('servoHead').style.transform = `translateX(-50%) rotate(${rotation}deg)`;

    // 雷达波束
    const beam = document.getElementById('radarBeam');
    // 限制最大显示长度
    const beamLen = Math.min(appState.distance * 3, 200);
    beam.style.borderTopWidth = `${beamLen}px`;

    if (appState.distance > 0 && appState.distance < 20) {
        beam.classList.add('danger');
    } else {
        beam.classList.remove('danger');
    }

    // 循迹传感器 (0为黑线/感应到)
    const irs = ['irL', 'irM', 'irR'];
    appState.ir.forEach((val, idx) => {
        const el = document.getElementById(irs[idx]);
        if (val === 0) el.classList.add('active'); // 黑色/感应到显示活跃色
        else el.classList.remove('active');
    });
}

function updateLocalAnimations() {
    const wheelL = document.getElementById('wheelL');
    const wheelR = document.getElementById('wheelR');

    wheelL.classList.remove('spinning', 'spinning-reverse');
    wheelR.classList.remove('spinning', 'spinning-reverse');

    if (appState.mode !== 'remote') return;

    // 根据电机值设置动画
    if (appState.motor1 > 5) wheelL.classList.add('spinning');
    else if (appState.motor1 < -5) wheelL.classList.add('spinning-reverse');

    if (appState.motor2 > 5) wheelR.classList.add('spinning');
    else if (appState.motor2 < -5) wheelR.classList.add('spinning-reverse');
}

// --- 工具函数 ---

function setConnectionStatus(isOnline) {
    const el = document.getElementById('statusConn');
    if (isOnline) {
        el.classList.add('connected');
        el.classList.remove('disconnected');
        el.textContent = '在线';
    } else {
        el.classList.remove('connected');
        el.classList.add('disconnected');
        el.textContent = '离线';
    }
}

function clamp(v, min, max) {
    return Math.min(Math.max(v, min), max);
}

function setOtaUi(text, percent) {
    const meta = document.getElementById('otaStatusText');
    if (meta) meta.textContent = `OTA: ${text || '--'}`;
    const bar = document.getElementById('otaProgressBar');
    if (bar) {
        const p = clamp(Number.isFinite(percent) ? percent : 0, 0, 100);
        bar.style.width = `${p}%`;
    }
}

function setOtaButtonEnabled(enabled) {
    const btn = document.getElementById('btnOtaStart');
    if (btn) btn.disabled = !enabled;
}

function ipv4ToBytes(ip) {
    const parts = String(ip || '').trim().split('.');
    if (parts.length !== 4) return null;
    const out = new Uint8Array(4);
    for (let i = 0; i < 4; i++) {
        const n = Number(parts[i]);
        if (!Number.isFinite(n) || n < 0 || n > 255) return null;
        out[i] = n & 0xFF;
    }
    return out;
}

function sendOtaStart(size) {
    if (!socket || socket.readyState !== WebSocket.OPEN || !appState.carIP) return false;
    socket.send(JSON.stringify({ type: 'otaStart', deviceIP: appState.carIP, size: (size >>> 0) }));
    return true;
}

function sendOtaData(offset, len) {
    if (!socket || socket.readyState !== WebSocket.OPEN || !appState.carIP) return false;
    if (!otaState.data) return false;
    const ipBytes = ipv4ToBytes(appState.carIP);
    if (!ipBytes) return false;
    const start = offset >>> 0;
    const end = (start + (len >>> 0)) >>> 0;
    const chunk = otaState.data.subarray(start, end);
    const msg = new Uint8Array(9 + chunk.length);
    msg[0] = 0xF0;
    msg[1] = (start >>> 24) & 0xFF;
    msg[2] = (start >>> 16) & 0xFF;
    msg[3] = (start >>> 8) & 0xFF;
    msg[4] = (start >>> 0) & 0xFF;
    msg[5] = ipBytes[0];
    msg[6] = ipBytes[1];
    msg[7] = ipBytes[2];
    msg[8] = ipBytes[3];
    msg.set(chunk, 9);
    socket.send(msg);
    return true;
}

function sendOtaEnd() {
    if (!socket || socket.readyState !== WebSocket.OPEN || !appState.carIP) return false;
    socket.send(JSON.stringify({ type: 'otaEnd', deviceIP: appState.carIP }));
    return true;
}

function clearOtaAckTimer() {
    if (otaState.ackTimer) {
        clearTimeout(otaState.ackTimer);
        otaState.ackTimer = null;
    }
}

function scheduleOtaRetry() {
    clearOtaAckTimer();
    otaState.ackTimer = setTimeout(() => {
        if (!otaState.active) return;
        const fastFail =
            otaState.phase === 'data' &&
            otaState.waitingForOffset === 0 &&
            otaState.chunkSize > otaState.fallbackChunkSize &&
            !otaState.fallbackTried;
        const maxRetries = fastFail ? 2 : otaState.maxRetries;

        if (otaState.retries >= maxRetries) {
            if (!otaState.fallbackTried && otaState.chunkSize > otaState.fallbackChunkSize) {
                otaState.fallbackTried = true;
                otaState.chunkSize = otaState.fallbackChunkSize;
                otaState.phase = 'start';
                otaState.offset = 0;
                otaState.waitingForOffset = null;
                otaState.waitingForLen = 0;
                otaState.retries = 0;
                setOtaUi('降级分片重试', 0);
                sendOtaStart(otaState.total);
                scheduleOtaRetry();
                return;
            }
            otaFail('超时');
            return;
        }
        otaState.retries += 1;
        if (otaState.phase === 'start') {
            sendOtaStart(otaState.total);
        } else if (otaState.phase === 'data' && otaState.waitingForOffset !== null) {
            sendOtaData(otaState.waitingForOffset, otaState.waitingForLen);
        } else if (otaState.phase === 'end') {
            sendOtaEnd();
        }
        scheduleOtaRetry();
    }, otaState.ackTimeoutMs);
}

function otaFail(reason) {
    otaState.active = false;
    otaState.phase = 'idle';
    otaState.waitingForOffset = null;
    otaState.waitingForLen = 0;
    otaState.retries = 0;
    clearOtaAckTimer();
    setOtaButtonEnabled(true);
    setOtaUi(`失败(${reason || '错误'})`, 0);
}

function otaFinish() {
    otaState.active = false;
    otaState.phase = 'idle';
    otaState.waitingForOffset = null;
    otaState.waitingForLen = 0;
    otaState.retries = 0;
    clearOtaAckTimer();
    setOtaButtonEnabled(true);
    setOtaUi('完成', 100);
}

function otaSendNextChunk() {
    if (!otaState.active || otaState.phase !== 'data') return;
    if (!otaState.data || otaState.total === 0) {
        otaFail('文件为空');
        return;
    }
    if (otaState.offset >= otaState.total) {
        otaState.phase = 'end';
        otaState.waitingForOffset = null;
        otaState.waitingForLen = 0;
        otaState.retries = 0;
        sendOtaEnd();
        setOtaUi('提交', 100);
        scheduleOtaRetry();
        return;
    }

    const start = otaState.offset;
    const end = Math.min(start + otaState.chunkSize, otaState.total);
    const chunk = otaState.data.subarray(start, end);

    otaState.waitingForOffset = start;
    otaState.waitingForLen = chunk.length;
    otaState.retries = 0;

    sendOtaData(start, chunk.length);
    const percent = Math.floor((end * 100) / otaState.total);
    setOtaUi(`上传中 ${percent}%`, percent);
    scheduleOtaRetry();
}

function handleOtaResponse(msg) {
    const code = msg.code;
    const status = msg.status || null;
    const progress = status && Number.isFinite(status.progress) ? status.progress : null;

    if (code !== 0) {
        otaFail(`code=${code}`);
        return;
    }

    otaState.retries = 0;
    clearOtaAckTimer();

    if (progress !== null) {
        setOtaUi('设备处理中', progress);
    }

    if (!otaState.active) return;

    if (otaState.phase === 'start') {
        otaState.phase = 'data';
        otaState.offset = 0;
        otaSendNextChunk();
        return;
    }

    if (otaState.phase === 'data') {
        if (otaState.waitingForOffset === null) return;
        if (msg.offset !== otaState.waitingForOffset) {
            scheduleOtaRetry();
            return;
        }
        otaState.offset = otaState.waitingForOffset + otaState.waitingForLen;
        otaState.waitingForOffset = null;
        otaState.waitingForLen = 0;
        otaSendNextChunk();
        return;
    }

    if (otaState.phase === 'end') {
        otaFinish();
    }
}

async function startOtaFromSelectedFile() {
    if (!appState.connected || !socket || socket.readyState !== WebSocket.OPEN || !appState.carIP) {
        setOtaUi('未连接', 0);
        return;
    }
    const input = document.getElementById('otaFile');
    const file = input && input.files && input.files[0] ? input.files[0] : null;
    if (!file) {
        setOtaUi('未选择文件', 0);
        return;
    }

    setOtaButtonEnabled(false);
    setOtaUi('读取文件', 0);

    let buf;
    try {
        buf = await file.arrayBuffer();
    } catch (e) {
        otaFail('读取失败');
        return;
    }

    const u8 = new Uint8Array(buf);
    if (!isFwpkg(u8)) {
        otaFail('请选择 .fwpkg');
        return;
    }
    otaState.active = true;
    otaState.phase = 'start';
    otaState.file = file;
    otaState.data = u8;
    otaState.total = u8.length >>> 0;
    otaState.offset = 0;
    otaState.fallbackTried = false;
    otaState.waitingForOffset = null;
    otaState.waitingForLen = 0;
    otaState.retries = 0;

    if (!sendOtaStart(otaState.total)) {
        otaFail('发送失败');
        return;
    }
    setOtaUi('准备', 0);
    scheduleOtaRetry();
}

// --- 设置面板 ---
function toggleConfig() {
    const m = document.getElementById('configModal');
    m.style.display = (m.style.display === 'flex') ? 'none' : 'flex';
}
function saveConfig() {
    toggleConfig();
}

function toggleOtaModal(force) {
    const m = document.getElementById('otaModal');
    if (!m) return;

    const shouldOpen = (force === true) ? true : (force === false) ? false : (m.style.display !== 'flex');
    m.style.display = shouldOpen ? 'flex' : 'none';
}

// --- 初始化 ---
window.onload = function () {
    connectToProxy();
    // 默认UI状态
    updateModeButtons('standby');
    bindHoldButton(document.getElementById('btnForward'), () => setDrive(DPAD_SPEED, DPAD_SPEED));
    bindHoldButton(document.getElementById('btnBackward'), () => setDrive(-DPAD_SPEED, -DPAD_SPEED));
    bindHoldButton(document.getElementById('btnLeft'), () => setDrive(0, DPAD_SPEED));
    bindHoldButton(document.getElementById('btnRight'), () => setDrive(DPAD_SPEED, 0));
    bindHoldButton(document.getElementById('btnStop'), () => setDrive(0, 0));

    setOtaUi('--', 0);
    const btnOta = document.getElementById('btnOtaStart');
    if (btnOta) {
        btnOta.addEventListener('click', (e) => {
            e.preventDefault();
            startOtaFromSelectedFile();
        });
    }

    const otaModal = document.getElementById('otaModal');
    if (otaModal) {
        otaModal.addEventListener('click', (e) => {
            if (e.target === otaModal) toggleOtaModal(false);
        });
    }

    document.addEventListener('keydown', (e) => {
        if (e.key !== 'Escape') return;
        const cm = document.getElementById('configModal');
        if (cm && cm.style.display === 'flex') cm.style.display = 'none';
        const om = document.getElementById('otaModal');
        if (om && om.style.display === 'flex') om.style.display = 'none';
    });

    startCommsLoop();
};

window.onbeforeunload = function () {
    isManualClose = true;
    if (sendLoopTimer) clearInterval(sendLoopTimer);
    if (socket) socket.close();
};
