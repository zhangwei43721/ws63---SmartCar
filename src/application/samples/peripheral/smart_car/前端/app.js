/**
 * UDP Control App for Smart Car
 * 支持：遥控、循迹、避障、PID 调试、WiFi 配置、OTA 固件升级
 */

// --- 配置管理 ---
const config = {
  // 代理服务器地址（WebSocket）
  proxyUrl: "ws://localhost:8081",
  discoveryInterval: 2000,
};

// --- 全局状态 ---
const appState = {
  mode: "standby", // 界面显示用的模式字符串
  dir: 0, // 当前驾驶方向（CarDriveCmd：0停 1前进 2后退 3左转 4右转）
  // 传感器数据
  distance: 0,
  ir: [1, 1, 1], // 左中右红外
  connected: false,
  // 设备管理（基于 MAC）
  devices: new Map(), // mac -> {name, ip, lastSeen, status}
  selectedMAC: "",  // 当前选中的设备 MAC
  lastConnectedMAC: localStorage.getItem("lastConnectedMAC") || "", // 最后连接的设备
  // 上次发送的控制值（用于检测变化）
  lastSent: { dir: 0 },
  lastControlSendAt: 0,
  // OTA 状态
  ota: {
    active: false,
    deviceIP: null,
    file: null,
    totalSize: 0,
    sentSize: 0,
    chunkSize: 16384,
  },
};

// --- 辅助函数：获取当前选中设备的 IP ---
function getSelectedDeviceIP() {
  if (!appState.selectedMAC) return null;
  const device = appState.devices.get(appState.selectedMAC);
  return device ? device.ip : null;
}

// --- 辅助函数：获取当前选中设备 ---
function getSelectedDevice() {
  if (!appState.selectedMAC) return null;
  return appState.devices.get(appState.selectedMAC);
}

// --- C语言固件定义的模式枚举 ---
// 必须与 C 代码中的 CarStatus 枚举保持一致
const MODE_MAP = {
  standby: 0, // CAR_STOP_STATUS
  tracking: 1, // CAR_TRACE_STATUS
  avoid: 2, // CAR_OBSTACLE_AVOIDANCE_STATUS
  remote: 3, // CAR_WIFI_CONTROL_STATUS
};

// --- 全局变量 ---
let socket = null;
let sendLoopTimer = null;
let reconnectTimer = null;
let isManualClose = false;
// 驾驶方向（与固件 CarDriveCmd 对齐）：前端只上报"按下哪个按钮"，速度由固件固定
const DRIVE = {
  STOP: 0,
  FORWARD: 1,
  BACKWARD: 2,
  LEFT: 3,
  RIGHT: 4,
};

function setDir(dir) {
  appState.dir = dir;
  updateLocalAnimations();
}

function bindHoldButton(el, onPress) {
  if (!el) return;

  const press = (evt) => {
    evt.preventDefault();
    if (!appState.connected) {
      showConfigConnect();
      return;
    }
    if (appState.mode !== "remote" && appState.mode !== "tracking") return;
    onPress();
  };

  const release = (evt) => {
    evt.preventDefault();
    if (appState.mode !== "remote" && appState.mode !== "tracking") return;
    setDir(DRIVE.STOP);
  };

  el.addEventListener("pointerdown", press);
  el.addEventListener("pointerup", release);
  el.addEventListener("pointercancel", release);
  el.addEventListener("pointerleave", release);
}

// --- PID 调试功能 ---
// 滑块 input 时实时更新显示值，松开滑块（onchange）时自动同步到小车
function updatePidVal(id, val) {
  const el = document.getElementById(id);
  if (el) el.innerText = val;
}

function sendPid(type) {
  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) return;

  let val = 0;
  if (type === 1) val = parseFloat(document.getElementById("pidKp").value);
  else if (type === 2) val = parseFloat(document.getElementById("pidKi").value);
  else if (type === 3) val = parseFloat(document.getElementById("pidKd").value);
  else if (type === 4) val = parseInt(document.getElementById("pidSpeed").value);

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(
      JSON.stringify({
        type: "setPid",
        deviceIP: deviceIP,
        paramType: type,
        value: val,
      }),
    );
  }
}

// --- 核心函数 ---

/**
 * 发送UDP控制包（通过WebSocket发给代理）
 */
function sendUDPControl() {
  if (!socket || socket.readyState !== WebSocket.OPEN) return;

  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) return;

  // 检查控制值是否变化
  const changed = appState.dir !== appState.lastSent.dir;

  const now = Date.now();
  // 如果没有变化，但距离上次发送超过200ms，也发送一次（作为心跳/保活）
  const shouldKeepAliveSend =
    appState.lastControlSendAt === 0 || now - appState.lastControlSendAt >= 200;

  if (!changed && !shouldKeepAliveSend) return;

  try {
    // 构建发送给代理服务器的JSON
    // 代理服务器负责将其打包成 C 结构体所需的二进制
    const controlMsg = {
      type: "control", // 对应 C 代码 UDP 包 type=0x01
      deviceIP: deviceIP,
      dir: appState.dir,
      speed: 0, // 速度由固件固定，前端只上报方向
    };

    socket.send(JSON.stringify(controlMsg));

    // 更新上次发送的值
    appState.lastSent = {
      dir: appState.dir,
    };
    appState.lastControlSendAt = now;
  } catch (error) {
    console.error("发送控制消息失败:", error);
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
    const deviceIP = getSelectedDeviceIP();
    if (!socket || socket.readyState !== WebSocket.OPEN || !deviceIP)
      return;

    // 只有在遥控模式下才持续发送控制命令
    // 其他模式下，车会自动跑，不需要前端一直发指令干扰
    if (appState.mode === "remote" || appState.mode === "tracking") {
      sendUDPControl();
    }
  }, 50); // 建议改为 50ms 左右，100ms 略有延迟感

  console.log("通信循环已启动");
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
    console.log("WebSocket 未连接，无法发送模式切换");
    return;
  }

  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    console.warn("未选择设备，无法发送模式切换");
    return;
  }

  try {
    const modeMsg = {
      type: "modeChange",
      deviceIP: deviceIP,
      mode: mode,
    };

    socket.send(JSON.stringify(modeMsg));
    console.log(`发送模式切换: ${mode} -> ${deviceIP}`);
  } catch (error) {
    console.error("发送模式切换消息失败:", error);
  }
}

/**
 * 选择设备
 * @param {string} mac - 要选择的设备 MAC 地址
 */
function selectDevice(mac) {
  if (!appState.devices.has(mac)) {
    console.warn(`设备不存在: ${mac}`);
    return;
  }

  appState.selectedMAC = mac;
  const device = appState.devices.get(mac);

  // 保存到 localStorage
  localStorage.setItem('lastConnectedMAC', mac);
  appState.lastConnectedMAC = mac;

  // 更新设备列表 UI 高亮
  updateDeviceListUI();

  // 更新连接状态显示
  document.getElementById("discoveryStatus").textContent =
    `已选择: ${device.name} (${device.ip})`;

  // 启用控制界面（只有用户主动点击连接后才算真正在线）
  setConnectionStatus(true);
  appState.connected = true;

  // 关闭设置弹窗
  const m = document.getElementById("configModal");
  if (m) m.style.display = "none";

  console.log(`已选择设备: ${device.name} (${mac})`);
}

/**
 * 更新设备列表 UI
 */
function updateDeviceListUI() {
  const deviceList = document.getElementById("deviceList");
  if (!deviceList) return;

  // 清空列表
  deviceList.innerHTML = "";

  // 遍历设备并创建 UI 元素
  for (const [mac, device] of appState.devices) {
    const item = document.createElement("div");
    item.className = "device-item";
    if (mac === appState.selectedMAC) {
      item.classList.add("selected");
    }
    item.dataset.mac = mac;

    item.innerHTML = `
      <span class="device-name">${device.name}</span>
      <span class="device-mac">${mac}</span>
      <span class="device-ip">${device.ip}</span>
      <button class="btn-select" onclick="selectDevice('${mac}')">
        ${mac === appState.selectedMAC ? "已连接" : "连接"}
      </button>
    `;

    deviceList.appendChild(item);
  }

  // 如果没有设备，显示提示
  if (appState.devices.size === 0) {
    deviceList.innerHTML = '<div class="device-empty">等待设备发现...</div>';
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
      console.log("代理服务器连接成功");
      document.getElementById("discoveryStatus").textContent =
        "已连接，等待设备...";
    };

    socket.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data);
        handleProxyMessage(msg);
      } catch (e) {
        console.error("解析消息失败:", e);
      }
    };

    socket.onclose = () => {
      console.log("连接已关闭");
      document.getElementById("discoveryStatus").textContent = "连接已断开";
      setConnectionStatus(false);
      appState.connected = false;

      if (!isManualClose) {
        document.getElementById("discoveryStatus").textContent = "尝试重连...";
        reconnectTimer = setTimeout(connectToProxy, 1000);
      }
    };

    socket.onerror = (err) => {
      console.error("WebSocket 错误", err);
    };
  } catch (error) {
    console.error("连接失败:", error);
    if (!isManualClose) {
      reconnectTimer = setTimeout(connectToProxy, 3000);
    }
  }
}

/**
 * 处理接收到的消息
 */
function handleProxyMessage(msg) {
  // 调试：输出所有消息类型
  console.log(`[前端] 收到消息: type=${msg.type}`, msg);

  // 处理设备发现（广播包，仅表示小车在线，不代表已建立控制连接）
  if (msg.type === "deviceDiscovered") {
    const { ip, mac, name, deviceId } = msg.device;
    console.log(`[前端] 发现设备详情:`, { ip, mac, name, deviceId });

    const deviceKey = mac || ip;
    const isNew = !appState.devices.has(deviceKey);
    appState.devices.set(deviceKey, {
      name: name || `Car_${ip.split('.').pop()}`,
      ip: ip,
      lastSeen: Date.now(),
      status: null,
      mac: mac || "",
    });

    console.log(`发现设备: ${deviceId || ip}`);
    updateDeviceListUI();

    // 如果当前没有选择任何设备，自动弹出设置窗口让用户手动连接
    if (!appState.selectedMAC) {
      showConfigConnect();
      const ds = document.getElementById("discoveryStatus");
      if (ds) ds.textContent = isNew ? "发现小车，请点击连接" : "发现小车更新";
    }
  }
  // 处理设备丢失
  else if (msg.type === "deviceLost") {
    // 通过 IP 找到对应的 MAC
    let lostMAC = null;
    for (const [mac, device] of appState.devices) {
      if (device.ip === msg.ip) {
        lostMAC = mac;
        break;
      }
    }

    if (lostMAC) {
      appState.devices.delete(lostMAC);
      console.log(`设备离线: ${lostMAC}`);

      // 更新设备列表 UI
      updateDeviceListUI();

      // 如果是当前选中的设备丢失了
      if (lostMAC === appState.selectedMAC) {
        appState.selectedMAC = "";
        document.getElementById("discoveryStatus").textContent = "设备离线";
        setConnectionStatus(false);
      }
    }
  }
  // 处理状态更新 (从车发回来的数据)
  else if (msg.type === "statusUpdate") {
    // 只处理当前选中设备的状态更新
    if (msg.mac === appState.selectedMAC) {
      // 更新设备 IP（可能变化）
      const device = appState.devices.get(msg.mac);
      if (device) {
        device.ip = msg.ip;
        device.status = msg.status;
      }

      // 更新传感器数据
      appState.distance = msg.status.distance || 0;
      appState.ir = msg.status.ir || [1, 1, 1];

      // 更新模式状态 (反向映射：整数 -> 字符串)
      const serverModeId = msg.status.mode;
      const modeNames = ["standby", "tracking", "avoid", "remote"];
      const newModeStr = modeNames[serverModeId] || "standby";

      // 只有当模式真的变了，才更新UI
      if (appState.mode !== newModeStr) {
        appState.mode = newModeStr;
        updateModeButtons(newModeStr);
        console.log(`同步设备模式: ${newModeStr} (${serverModeId})`);
      }

      renderVisuals();
    }
  } else if (msg.type === "traceInfo") {
    if (msg.mac === appState.selectedMAC || !appState.selectedMAC) {
      document.getElementById("valL").textContent = msg.adc[0];
      document.getElementById("valM").textContent = msg.adc[1];
      document.getElementById("valR").textContent = msg.adc[2];

      const stL = document.getElementById("stL");
      const stM = document.getElementById("stM");
      const stR = document.getElementById("stR");

      if (stL) {
        stL.textContent = msg.adc[0] >= msg.th[0] ? "黑" : "白";
        stL.className = msg.adc[0] >= msg.th[0] ? "status-badge black-line" : "status-badge white-bg";
      }
      if (stM) {
        stM.textContent = msg.adc[1] >= msg.th[1] ? "黑" : "白";
        stM.className = msg.adc[1] >= msg.th[1] ? "status-badge black-line" : "status-badge white-bg";
      }
      if (stR) {
        stR.textContent = msg.adc[2] >= msg.th[2] ? "黑" : "白";
        stR.className = msg.adc[2] >= msg.th[2] ? "status-badge black-line" : "status-badge white-bg";
      }

      if (!window.thInited) {
        const rangeL = document.getElementById("rangeL");
        const rangeM = document.getElementById("rangeM");
        const rangeR = document.getElementById("rangeR");
        if (rangeL) rangeL.value = msg.th[0];
        if (rangeM) rangeM.value = msg.th[1];
        if (rangeR) rangeR.value = msg.th[2];

        const txtL = document.getElementById("txtL");
        const txtM = document.getElementById("txtM");
        const txtR = document.getElementById("txtR");
        if (txtL) txtL.textContent = msg.th[0];
        if (txtM) txtM.textContent = msg.th[1];
        if (txtR) txtR.textContent = msg.th[2];

        window.thInited = true;
      }
    }
  } else if (msg.type === "carLog") {
    const consoleEl = document.getElementById("logConsole");
    if (consoleEl) {
      if (consoleEl.textContent === "等待接收日志...") {
        consoleEl.textContent = "";
      }
      consoleEl.textContent += msg.log;
      if (consoleEl.textContent.length > 20000) {
        consoleEl.textContent = consoleEl.textContent.substring(consoleEl.textContent.length - 10000);
      }
      consoleEl.scrollTop = consoleEl.scrollHeight;
    }
  } else if (msg.type === "otaStatus") {
    handleOtaStatus(msg);
  } else if (msg.type === "wifiConfigResponse") {
    // WiFi 配置响应需要发送到当前选中设备
    let deviceMAC = null;
    for (const [mac, device] of appState.devices) {
      if (device.ip === msg.ip) {
        deviceMAC = mac;
        break;
      }
    }
    if (deviceMAC && deviceMAC === appState.selectedMAC) {
      const result = msg.result;
      if (result.cmd === 0xe0) {
        if (result.success) {
          alert("WiFi配置已保存");
        } else {
          alert("WiFi配置保存失败");
        }
      } else if (result.cmd === 0xe1) {
        if (result.success) {
          alert("正在连接WiFi，请稍候...");
        } else {
          alert("WiFi连接失败");
        }
      }
    }
  }
}

// --- 模式切换与UI ---

function updateModeButtons(mode) {
  document.querySelectorAll(".mode-btn").forEach((btn) => {
    // 检查按钮onclick属性中是否包含当前模式名
    const btnMode = btn.getAttribute("onclick").match(/'(.*?)'/)[1];
    if (btnMode === mode) {
      btn.classList.add("active");
    } else {
      btn.classList.remove("active");
    }
  });

  // PID 面板仅在循迹模式下显示
  const pidPanel = document.getElementById("pidPanel");
  if (pidPanel) {
    if (mode === "tracking") {
      pidPanel.style.display = "block";
      // 默认高亮“传感器校准”子模式按钮 (2)
      const pidBtn = document.getElementById("submodePidBtn");
      const hardBtn = document.getElementById("submodeHardedBtn");
      const calibBtn = document.getElementById("submodeCalibBtn");
      if (pidBtn) pidBtn.classList.remove("active");
      if (hardBtn) hardBtn.classList.remove("active");
      if (calibBtn) calibBtn.classList.add("active");
      
      // 默认显示传感器校准面板 (2)
      updateTraceSubmodeUI(2);
    } else {
      pidPanel.style.display = "none";
      // 非循迹大模式下，隐藏校准面板
      const calibPanel = document.getElementById("calibPanel");
      if (calibPanel) calibPanel.style.display = "none";
    }
  }

  const dpad = document.getElementById("dpad");
  if (mode === "remote" || mode === "tracking") {
    dpad?.classList.remove("disabled");
  } else {
    dpad?.classList.add("disabled");
    // 非遥控和循迹模式，停止发送电机指令
    setDir(DRIVE.STOP);
  }
}

function changeMode(modeStr) {
  if (!appState.connected) {
    showConfigConnect();
    return;
  }

  console.log(`请求切换模式: ${modeStr}`);
  appState.mode = modeStr;
  updateModeButtons(modeStr);
  sendModeChange(modeStr);
}

// --- 视觉渲染 ---

function renderVisuals() {
  document.getElementById("statusDist").innerText =
    appState.distance.toFixed(1);

  // 雷达波束
  const beam = document.getElementById("radarBeam");
  // 限制最大显示长度
  const beamLen = Math.min(appState.distance * 3, 200);
  beam.style.borderTopWidth = `${beamLen}px`;

  if (appState.distance > 0 && appState.distance < 20) {
    beam.classList.add("danger");
  } else {
    beam.classList.remove("danger");
  }

  // 循迹传感器 (0为黑线/感应到)
  const irs = ["irL", "irM", "irR"];
  appState.ir.forEach((val, idx) => {
    const el = document.getElementById(irs[idx]);
    if (val === 0)
      el.classList.add("active"); // 黑色/感应到显示活跃色
    else el.classList.remove("active");
  });
}

function updateLocalAnimations() {
  const wheelL = document.getElementById("wheelL");
  const wheelR = document.getElementById("wheelR");

  wheelL.classList.remove("spinning", "spinning-reverse");
  wheelR.classList.remove("spinning", "spinning-reverse");

  if (appState.mode !== "remote") return;

  // 根据方向设置轮子动画（前进/转向=两轮前进转，后退=两轮反转，停止=无动画）
  const dir = appState.dir;
  if (dir === DRIVE.FORWARD || dir === DRIVE.LEFT || dir === DRIVE.RIGHT) {
    wheelL.classList.add("spinning");
    wheelR.classList.add("spinning");
  } else if (dir === DRIVE.BACKWARD) {
    wheelL.classList.add("spinning-reverse");
    wheelR.classList.add("spinning-reverse");
  }
}

// --- 工具函数 ---

function setConnectionStatus(isOnline) {
  const dot = document.getElementById("statusConn");
  const text = document.getElementById("statusText");
  if (isOnline) {
    dot.classList.add("connected");
    dot.classList.remove("disconnected");
    if (text) text.textContent = "在线";
  } else {
    dot.classList.remove("connected");
    dot.classList.add("disconnected");
    if (text) text.textContent = "离线";
  }
}

// --- WiFi配置函数 ---
function saveWifiConfig() {
  const ssid = document.getElementById("wifiSSID").value.trim();
  const password = document.getElementById("wifiPassword").value;

  if (!ssid || !password) {
    alert("请输入WiFi名称和密码");
    return;
  }

  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    showConfigConnect();
    return;
  }

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(
      JSON.stringify({
        type: "wifiConfigSet",
        deviceIP: deviceIP,
        ssid: ssid,
        password: password,
      }),
    );
    console.log("[Frontend] WiFi配置已发送");
  }
}

function connectWifi() {
  const ssid = document.getElementById("wifiSSID").value.trim();
  const password = document.getElementById("wifiPassword").value;

  if (!ssid || !password) {
    alert("请输入WiFi名称和密码");
    return;
  }

  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    showConfigConnect();
    return;
  }

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(
      JSON.stringify({
        type: "wifiConfigConnect",
        deviceIP: deviceIP,
        ssid: ssid,
        password: password,
      }),
    );
    console.log("[Frontend] WiFi连接请求已发送");
    toggleConfig();
  }
}

// --- 设置面板 ---
function toggleConfig() {
  const m = document.getElementById("configModal");
  m.style.display = m.style.display === "flex" ? "none" : "flex";
}
function saveConfig() {
  toggleConfig();
}

function switchTab(tabId) {
  document.querySelectorAll(".tab-btn").forEach((btn) => {
    btn.classList.toggle("active", btn.dataset.tab === tabId);
  });
  document.querySelectorAll(".tab-page").forEach((page) => {
    page.style.display = page.id === `tab-${tabId}` ? "block" : "none";
  });
}

function showConfigConnect() {
  switchTab("connect");
  const m = document.getElementById("configModal");
  if (m) m.style.display = "flex";
}

// --- OTA 功能 ---

function showOtaModal(show) {
  const modal = document.getElementById("otaModal");
  if (modal) modal.style.display = show ? "flex" : "none";
}

function updateOtaUI(state, progress, message) {
  const bar = document.getElementById("otaProgressBar");
  const text = document.getElementById("otaStatusText");
  const meta = document.getElementById("otaMeta");
  const cancelBtn = document.getElementById("otaCancelBtn");

  if (bar) bar.style.width = `${progress}%`;
  if (text) text.textContent = message || state;

  const stateLabels = {
    triggering: "触发中",
    ready: "准备就绪",
    transferring: "传输中",
    verifying: "校验中",
    done: "完成",
    error: "失败",
    cancelled: "已取消",
  };

  if (meta) meta.textContent = `${stateLabels[state] || state} · ${progress}%`;

  if (cancelBtn) {
    cancelBtn.disabled = state === "done" || state === "verifying";
    cancelBtn.style.opacity = cancelBtn.disabled ? "0.5" : "1";
  }
}

function onOtaFileSelected(input) {
  const file = input.files?.[0];
  const nameEl = document.getElementById("otaFileName");
  const labelEl = document.getElementById("otaFileLabel");

  if (file && nameEl) {
    nameEl.textContent = file.name;
    nameEl.style.color = "var(--text-main)";
  } else if (nameEl) {
    nameEl.textContent = "未选择文件";
    nameEl.style.color = "var(--text-sub)";
  }

  if (labelEl) {
    if (file) labelEl.classList.add("has-file");
    else labelEl.classList.remove("has-file");
  }
}

async function startOta() {
  const fileInput = document.getElementById("otaFile");
  const file = fileInput?.files?.[0];
  if (!file) {
    alert("请先选择固件文件");
    return;
  }

  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    showConfigConnect();
    return;
  }

  appState.ota = {
    active: true,
    deviceIP,
    file,
    totalSize: file.size,
    sentSize: 0,
    chunkSize: 16384,
  };

  showOtaModal(true);
  updateOtaUI("triggering", 0, "正在触发 OTA...");

  const startBtn = document.getElementById("otaStartBtn");
  if (startBtn) startBtn.disabled = true;

  socket.send(JSON.stringify({
    type: "otaTrigger",
    deviceIP,
    totalSize: file.size,
  }));
}

function sendNextOtaChunk() {
  const state = appState.ota;
  if (!state.active || state.sentSize >= state.totalSize) return;

  const end = Math.min(state.sentSize + state.chunkSize, state.totalSize);
  const slice = state.file.slice(state.sentSize, end);

  const reader = new FileReader();
  reader.onload = (e) => {
    const arrayBuffer = e.target.result;
    const bytes = new Uint8Array(arrayBuffer);
    let binary = "";
    for (let i = 0; i < bytes.length; i++) {
      binary += String.fromCharCode(bytes[i]);
    }
    const base64 = btoa(binary);

    if (socket && socket.readyState === WebSocket.OPEN) {
      socket.send(JSON.stringify({
        type: "otaChunk",
        deviceIP: state.deviceIP,
        data: base64,
      }));
    }

    state.sentSize = end;
  };
  reader.readAsArrayBuffer(slice);
}

function handleOtaStatus(msg) {
  const { state, progress, message } = msg;
  updateOtaUI(state, progress, message);

  if (state === "ready") {
    sendNextOtaChunk();
  } else if (state === "transferring") {
    if (appState.ota.sentSize < appState.ota.totalSize) {
      sendNextOtaChunk();
    }
  } else if (state === "done" || state === "error" || state === "cancelled") {
    appState.ota.active = false;
    const startBtn = document.getElementById("otaStartBtn");
    if (startBtn) startBtn.disabled = false;

    if (state === "done" || state === "cancelled") {
      setTimeout(() => showOtaModal(false), 2000);
    }
  }
}

function cancelOta() {
  const state = appState.ota;
  if (!state.active) {
    showOtaModal(false);
    return;
  }

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(JSON.stringify({
      type: "otaCancel",
      deviceIP: state.deviceIP,
    }));
  }

  appState.ota.active = false;
  updateOtaUI("cancelled", 0, "正在取消...");
  const startBtn = document.getElementById("otaStartBtn");
  if (startBtn) startBtn.disabled = false;
  setTimeout(() => showOtaModal(false), 1500);
}

// --- 初始化 ---
window.onload = function () {
  connectToProxy();


  // 默认UI状态
  updateModeButtons("standby");
  bindHoldButton(document.getElementById("btnForward"), () =>
    setDir(DRIVE.FORWARD),
  );
  bindHoldButton(document.getElementById("btnBackward"), () =>
    setDir(DRIVE.BACKWARD),
  );
  bindHoldButton(document.getElementById("btnLeft"), () =>
    setDir(DRIVE.LEFT),
  );
  bindHoldButton(document.getElementById("btnRight"), () =>
    setDir(DRIVE.RIGHT),
  );
  bindHoldButton(document.getElementById("btnStop"), () => setDir(DRIVE.STOP));

  document.addEventListener("keydown", (e) => {
    if (e.key !== "Escape") return;
    const cm = document.getElementById("configModal");
    if (cm && cm.style.display === "flex") cm.style.display = "none";
  });

  startCommsLoop();
};

window.onbeforeunload = function () {
  isManualClose = true;
  if (sendLoopTimer) clearInterval(sendLoopTimer);
  if (socket) socket.close();
};

// --- TCRT5000 调试与校准辅助函数 ---

function updateRangeText(id, val) {
  const el = document.getElementById(id);
  if (el) el.textContent = val;
}

function sendTraceThresholds() {
  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    alert("请先选择并连接小车设备！");
    return;
  }

  const l = parseInt(document.getElementById("rangeL").value);
  const m = parseInt(document.getElementById("rangeM").value);
  const r = parseInt(document.getElementById("rangeR").value);

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(
      JSON.stringify({
        type: "setTraceThresholds",
        deviceIP: deviceIP,
        left: l,
        middle: m,
        right: r,
      })
    );
    console.log(`[前端] 发送阈值设置: L=${l} M=${m} R=${r}`);
  }
}

let calibBlackValues = null;
let calibWhiteValues = null;

function autoCalibrateTrace(type) {
  const valL = parseInt(document.getElementById("valL").textContent);
  const valM = parseInt(document.getElementById("valM").textContent);
  const valR = parseInt(document.getElementById("valR").textContent);

  if (isNaN(valL) || isNaN(valM) || isNaN(valR)) {
    alert("未获取到实时传感器数据，请等待连接或尝试推动探头！");
    return;
  }

  if (type === "black") {
    calibBlackValues = [valL, valM, valR];
    alert(`已记录黑线原始读数:\nL=${valL} mV\nM=${valM} mV\nR=${valR} mV\n\n请将所有三个探头移到“白色背景”上，然后点击“记录背景”`);
  } else if (type === "white") {
    calibWhiteValues = [valL, valM, valR];
    if (!calibBlackValues) {
      alert("请先将小车放在黑线上点击“记录黑线”！");
      return;
    }

    // 计算黑线与白背景的中间值
    const thL = Math.round((calibBlackValues[0] + calibWhiteValues[0]) / 2);
    const thM = Math.round((calibBlackValues[1] + calibWhiteValues[1]) / 2);
    const thR = Math.round((calibBlackValues[2] + calibWhiteValues[2]) / 2);

    document.getElementById("rangeL").value = thL;
    document.getElementById("rangeM").value = thM;
    document.getElementById("rangeR").value = thR;
    
    document.getElementById("txtL").textContent = thL;
    document.getElementById("txtM").textContent = thM;
    document.getElementById("txtR").textContent = thR;

    sendTraceThresholds();
    alert(`自动计算出校准阈值并同步:\nL=${thL} mV\nM=${thM} mV\nR=${thR} mV\n\n(已保存至 NV 闪存)`);
  }
}

function clearConsoleLog() {
  const consoleEl = document.getElementById("logConsole");
  if (consoleEl) {
    consoleEl.textContent = "";
  }
}

function updateTraceSubmodeUI(submode) {
  const debugSection = document.getElementById("pidDebugSection");
  const calibPanel = document.getElementById("calibPanel");

  if (submode === 0) { // PID 巡线
    if (debugSection) debugSection.style.display = "block";
    if (calibPanel) calibPanel.style.display = "none";
  } else if (submode === 1) { // 硬编码巡线
    if (debugSection) debugSection.style.display = "none";
    if (calibPanel) calibPanel.style.display = "none";
  } else if (submode === 2) { // 传感器校准
    if (debugSection) debugSection.style.display = "none";
    if (calibPanel) calibPanel.style.display = "block";
  }
}

function changeTraceSubmode(submode) {
  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    alert("请先选择并连接小车设备！");
    return;
  }

  // 更新UI按钮的高亮
  const buttons = {
    0: document.getElementById("submodePidBtn"),
    1: document.getElementById("submodeHardedBtn"),
    2: document.getElementById("submodeCalibBtn")
  };
  
  Object.keys(buttons).forEach(key => {
    const btn = buttons[key];
    if (btn) {
      if (parseInt(key) === submode) {
        btn.classList.add("active");
      } else {
        btn.classList.remove("active");
      }
    }
  });

  // 更新子模式界面显示 (PID参数调试 / 传感器在线校准)
  updateTraceSubmodeUI(submode);

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(
      JSON.stringify({
        type: "setTraceSubmode",
        deviceIP: deviceIP,
        submode: submode,
      })
    );
    console.log(`[前端] 发送切换循迹子模式指令: ${submode}`);
  }
}
