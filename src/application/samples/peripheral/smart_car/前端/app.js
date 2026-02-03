/**
 * UDP Control App for Smart Car (Modified)
 * 修复了摇杆混控逻辑和模式切换协议，删除OTA功能
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
  motor1: 0, // 左电机 -100~100
  motor2: 0, // 右电机 -100~100
  // 传感器数据
  distance: 0,
  ir: [1, 1, 1], // 左中右红外
  connected: false,
  // 设备管理（基于 MAC）
  devices: new Map(), // mac -> {name, ip, lastSeen, status}
  selectedMAC: "",  // 当前选中的设备 MAC
  lastConnectedMAC: localStorage.getItem("lastConnectedMAC") || "", // 最后连接的设备
  // 上次发送的控制值（用于检测变化）
  lastSent: { motor1: 0, motor2: 0 },
  lastControlSendAt: 0,
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
const DPAD_SPEED = 100;

function setDrive(m1, m2) {
  appState.motor1 = clamp(m1, -100, 100);
  appState.motor2 = clamp(m2, -100, 100);
  updateLocalAnimations();
}

function bindHoldButton(el, onPress) {
  if (!el) return;

  const press = (evt) => {
    evt.preventDefault();
    if (appState.mode !== "remote") return;
    onPress();
  };

  const release = (evt) => {
    evt.preventDefault();
    if (appState.mode !== "remote") return;
    setDrive(0, 0);
  };

  el.addEventListener("pointerdown", press);
  el.addEventListener("pointerup", release);
  el.addEventListener("pointercancel", release);
  el.addEventListener("pointerleave", release);
}

// --- PID 调试功能 ---
function updatePidVal(id, val) {
  const el = document.getElementById(id);
  if (el) el.innerText = val;
}

function sendPid(type) {
  console.log(`[Frontend] sendPid called with type: ${type}`);
  const deviceIP = getSelectedDeviceIP();
  if (!deviceIP) {
    console.warn("[Frontend] sendPid aborted: No device selected");
    alert("请先选择小车！");
    return;
  }

  let val = 0;
  // float 类型需要 / 100 还原为实际 float (后端会除以100，这里前端直接发整数)
  if (type === 1) val = parseFloat(document.getElementById("pidKp").value);
  else if (type === 2) val = parseFloat(document.getElementById("pidKi").value);
  else if (type === 3) val = parseFloat(document.getElementById("pidKd").value);
  else if (type === 4)
    val = parseInt(document.getElementById("pidSpeed").value);

  console.log(
    `[Frontend] Sending PID: type=${type}, val=${val}, IP=${deviceIP}`,
  );

  if (socket && socket.readyState === WebSocket.OPEN) {
    socket.send(
      JSON.stringify({
        type: "setPid",
        deviceIP: deviceIP,
        paramType: type,
        value: val,
      }),
    );
    console.log("[Frontend] PID command sent to proxy");
  } else {
    console.error("[Frontend] Socket not ready");
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
  const changed =
    appState.motor1 !== appState.lastSent.motor1 ||
    appState.motor2 !== appState.lastSent.motor2;

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
      motor1: parseInt(appState.motor1), // 确保是整数
      motor2: parseInt(appState.motor2), // 确保是整数
    };

    socket.send(JSON.stringify(controlMsg));

    // 更新上次发送的值
    appState.lastSent = {
      motor1: appState.motor1,
      motor2: appState.motor2,
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
    if (appState.mode === "remote") {
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

  // 启用控制界面
  setConnectionStatus(true);
  appState.connected = true;

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

  // 处理设备发现
  if (msg.type === "deviceDiscovered") {
    const { ip, mac, name, deviceId } = msg.device;
    console.log(`[前端] 发现设备详情:`, { ip, mac, name, deviceId });

    // 如果 MAC 为空，说明是旧格式广播包，使用 IP 作为 key
    const deviceKey = mac || ip;
    appState.devices.set(deviceKey, {
      name: name || `Robot_${ip.split('.').pop()}`,
      ip: ip,
      lastSeen: Date.now(),
      status: null,
      mac: mac || "",
    });

    console.log(`发现设备: ${deviceId || ip}`);

    // 自动选中上次连接的设备
    if (mac && mac === appState.lastConnectedMAC && !appState.selectedMAC) {
      selectDevice(mac);
    }

    // 更新设备列表 UI
    updateDeviceListUI();

    appState.connected = true;
    setConnectionStatus(true);
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
      } else if (result.cmd === 0xe2) {
        document.getElementById("wifiSSID").value = result.ssid || "";
        document.getElementById("wifiPassword").value = result.password || "";
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
    } else {
      pidPanel.style.display = "none";
    }
  }

  const dpad = document.getElementById("dpad");
  if (mode === "remote") {
    dpad?.classList.remove("disabled");
  } else {
    dpad?.classList.add("disabled");
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
    console.warn("设备未连接，模式切换指令可能无法发送");
  }
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

  // 根据电机值设置动画
  if (appState.motor1 > 5) wheelL.classList.add("spinning");
  else if (appState.motor1 < -5) wheelL.classList.add("spinning-reverse");

  if (appState.motor2 > 5) wheelR.classList.add("spinning");
  else if (appState.motor2 < -5) wheelR.classList.add("spinning-reverse");
}

// --- 工具函数 ---

function setConnectionStatus(isOnline) {
  const el = document.getElementById("statusConn");
  if (isOnline) {
    el.classList.add("connected");
    el.classList.remove("disconnected");
    el.textContent = "在线";
  } else {
    el.classList.remove("connected");
    el.classList.add("disconnected");
    el.textContent = "离线";
  }
}

function clamp(v, min, max) {
  return Math.min(Math.max(v, min), max);
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
    alert("请先选择小车");
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
    alert("请先选择小车");
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

// --- 初始化 ---
window.onload = function () {
  connectToProxy();

  // 如果没有保存的设备，自动打开设置弹窗进行设备发现
  if (!appState.lastConnectedMAC) {
    setTimeout(() => {
      const m = document.getElementById("configModal");
      if (m) m.style.display = "flex";
    }, 500);
  }

  // 默认UI状态
  updateModeButtons("standby");
  bindHoldButton(document.getElementById("btnForward"), () =>
    setDrive(DPAD_SPEED, DPAD_SPEED),
  );
  bindHoldButton(document.getElementById("btnBackward"), () =>
    setDrive(-DPAD_SPEED, -DPAD_SPEED),
  );
  bindHoldButton(document.getElementById("btnLeft"), () =>
    setDrive(0, DPAD_SPEED),
  );
  bindHoldButton(document.getElementById("btnRight"), () =>
    setDrive(DPAD_SPEED, 0),
  );
  bindHoldButton(document.getElementById("btnStop"), () => setDrive(0, 0));

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
