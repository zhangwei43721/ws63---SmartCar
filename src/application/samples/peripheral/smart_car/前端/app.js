/**
 * Smart Car Control Logic
 * 保持原有接口和控制逻辑不变
 */

// --- 配置管理 ---
const config = {
  ip: localStorage.getItem('car_ip') || '192.168.3.151',
  port: localStorage.getItem('car_port') || '8080',
  baseUrl: ''
};

function updateBaseUrl() {
  config.baseUrl = `http://${config.ip}:${config.port}`;
}
updateBaseUrl(); // 初始化执行

// --- 全局状态 ---
const appState = {
  mode: 'standby', // standby, remote, tracking, avoid
  power: 0,        // -100 ~ 100
  turn: 0,         // -100 ~ 100 (左负右正)
  // 传感器数据
  servoAngle: 90,
  distance: 0,
  ir: [1, 1, 1],   // 1为白(无), 0为黑(有)
  connected: false
};

// --- 摇杆初始化 (Nipple.js) ---
const joystickManager = nipplejs.create({
  zone: document.getElementById('joystick-zone'),
  mode: 'static',
  position: { left: '50%', top: '50%' },
  color: '#4f46e5', // 对应CSS --primary
  size: 120,
  restOpacity: 0.8
});

// 摇杆事件监听
joystickManager.on('move', (evt, data) => {
  if (appState.mode !== 'remote') return;

  // 计算逻辑保持不变
  // vector.y: 上正下负; vector.x: 右正左负
  const p = Math.round(data.vector.y * 100);
  const t = Math.round(data.vector.x * 100);

  appState.power = clamp(p, -100, 100);
  appState.turn = clamp(t, -100, 100);

  updateLocalAnimations();
});

joystickManager.on('end', () => {
  appState.power = 0;
  appState.turn = 0;
  updateLocalAnimations();
  sendControlCmd(); // 立即发送停止指令
});

// --- 核心循环 ---

// 1. 发送控制指令 (仅遥控模式, 50ms/次)
setInterval(() => {
  if (appState.mode === 'remote') {
    sendControlCmd();
  }
}, 50);

// 2. 获取状态 (200ms/次)
setInterval(fetchStatus, 200);

// --- 网络请求 ---

function sendControlCmd() {
  // 接口保持不变: /api/ctrl?m=POWER&s1=TURN&s2=0
  const url = `${config.baseUrl}/api/ctrl?m=${appState.power}&s1=${appState.turn}&s2=0`;

  // 使用 AbortSignal 防止请求堆积
  const controller = new AbortController();
  const timeoutId = setTimeout(() => controller.abort(), 100);

  fetch(url, { method: 'GET', signal: controller.signal })
    .catch(() => { }); // 忽略控制指令的错误，以免刷屏报错

  clearTimeout(timeoutId);
}

async function fetchStatus() {
  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 300);

    const resp = await fetch(`${config.baseUrl}/api/status`, { signal: controller.signal });
    clearTimeout(timeoutId);

    if (resp.ok) {
      const data = await resp.json();
      // 解析数据: { "mode": "...", "ang": 90, "dist": 20, "ir": [0,1,0] }
      if (data.ang !== undefined) appState.servoAngle = data.ang;
      if (data.dist !== undefined) appState.distance = data.dist;
      if (data.ir) appState.ir = data.ir;

      setConnectionStatus(true);
      renderVisuals();
    } else {
      throw new Error("Status Error");
    }
  } catch (e) {
    setConnectionStatus(false);
    // [调试用] 如果在避障模式且断开连接，模拟一下雷达动画效果
    if (appState.mode === 'avoid') simulateRadar();
  }
}

// 模式切换
function changeMode(mode) {
  appState.mode = mode;

  // 更新UI按钮
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
    appState.power = 0;
    appState.turn = 0;
    updateLocalAnimations();
  }

  // 发送模式指令 (映射保持原样)
  const modeMap = {
    'standby': 'stop',
    'remote': 'remote',
    'tracking': 'trace',
    'avoid': 'obstacle'
  };

  fetch(`${config.baseUrl}/api/mode?val=${modeMap[mode]}`).catch(console.error);
}

// --- 视觉渲染 ---

function renderVisuals() {
  // 1. 更新数值显示
  document.getElementById('statusDist').innerText = appState.distance;
  document.getElementById('statusAngle').innerText = appState.servoAngle;

  // 2. 舵机旋转 (假设90度是正前)
  // CSS中 transform-origin 已经设置到底部中心
  // 0度在CSS里通常是正上方，如果90度是正前，可能需要(ang - 90)或者其他修正
  // 这里假设: 90度=正前, 0度=右, 180度=左. 
  // HTML布局上，舵机初始是朝上的。
  // rotate(0deg) 是朝上。所以如果要向左转(180)，需要 rotate(90deg)? 
  // 让我们做个通用映射：angle - 90. 这样 90->0(正中), 180->90(左/右), 0->-90.
  const rotation = appState.servoAngle - 90;
  document.getElementById('servoHead').style.transform = `translateX(-50%) rotate(${rotation}deg)`;

  // 3. 雷达波束 (距离越远越长，最大150px)
  const beam = document.getElementById('radarBeam');
  const beamLen = Math.min(appState.distance * 3, 150);
  beam.style.borderTopWidth = `${beamLen}px`;

  // 距离过近变红
  if (appState.distance > 0 && appState.distance < 20) {
    beam.classList.add('danger');
  } else {
    beam.classList.remove('danger');
  }

  // 4. 循迹传感器 (0=黑线=Active)
  const irs = ['irL', 'irM', 'irR'];
  appState.ir.forEach((val, idx) => {
    const el = document.getElementById(irs[idx]);
    if (val === 0) el.classList.add('active'); // 检测到黑线
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

  // 简单的差速动画模拟
  // 前进
  if (appState.power > 10) {
    wheelL.classList.add('spinning');
    wheelR.classList.add('spinning');
    // 右转时右轮不动/反转? 这里简单处理：右转减速右轮
    if (appState.turn > 30) wheelR.classList.remove('spinning');
    // 左转时左轮减速
    if (appState.turn < -30) wheelL.classList.remove('spinning');
  }
  // 后退
  else if (appState.power < -10) {
    wheelL.classList.add('spinning-reverse');
    wheelR.classList.add('spinning-reverse');
  }
  // 原地转向 (无动力，纯转向)
  else if (Math.abs(appState.power) <= 10 && Math.abs(appState.turn) > 20) {
    if (appState.turn > 0) { // 右转: 左轮进，右轮退
      wheelL.classList.add('spinning');
      wheelR.classList.add('spinning-reverse');
    } else { // 左转
      wheelL.classList.add('spinning-reverse');
      wheelR.classList.add('spinning');
    }
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

// 模拟雷达摆动 (仅在断开连接且避障模式时为了UI演示)
let simDir = 1;
function simulateRadar() {
  appState.servoAngle += 5 * simDir;
  if (appState.servoAngle > 160 || appState.servoAngle < 20) simDir *= -1;
  appState.distance = Math.floor(Math.random() * 50) + 10;
  renderVisuals();
}

// --- 设置弹窗逻辑 ---
function toggleConfig() {
  const m = document.getElementById('configModal');
  m.style.display = (m.style.display === 'flex') ? 'none' : 'flex';
}

function saveConfig() {
  const ip = document.getElementById('cfgIP').value;
  const port = document.getElementById('cfgPort').value;

  if (ip) {
    config.ip = ip;
    localStorage.setItem('car_ip', ip);
  }
  if (port) {
    config.port = port;
    localStorage.setItem('car_port', port);
  }

  updateBaseUrl();
  toggleConfig();
  alert("配置已保存，尝试连接...");
}

// --- 初始化 ---
(function init() {
  // 填入配置
  document.getElementById('cfgIP').value = config.ip;
  document.getElementById('cfgPort').value = config.port;

  // 默认高亮待机
  changeMode('standby');
})();