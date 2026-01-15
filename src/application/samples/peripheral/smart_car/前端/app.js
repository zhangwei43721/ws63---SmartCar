      // --- 1. 配置管理 ---
      let config = {
        ip: localStorage.getItem("car_ip") || "192.168.3.151",
        port: localStorage.getItem("car_port") || "8080",
        baseUrl: "",
      };

      // 更新 BaseUrl
      function updateBaseUrl() {
        config.baseUrl = `http://${config.ip}:${config.port}`;
      }
      updateBaseUrl();

      // 状态变量
      let appState = {
        mode: "standby",
        power: 0,
        turn: 0,
        // 传感器数据
        servoAngle: 90, // 0~180
        distance: 0,
        ir: [0, 0, 0],
      };

      // --- 2. 摇杆逻辑 (Nipple.js) ---
      const joystickManager = nipplejs.create({
        zone: document.getElementById("joystick-zone"),
        mode: "static",
        position: { left: "50%", top: "50%" },
        color: "#007bff",
        size: 120,
      });

      joystickManager.on("move", (evt, data) => {
        if (appState.mode !== "remote") return;

        // 映射逻辑：Y轴向上为正(前进)，X轴向右为正
        // 转向协议：turn>0右转, turn<0左转
        // 摇杆数据：y [0~1], x [0~1]

        let p = Math.round(data.vector.y * 100);
        // 摇杆往左 x是负数(左转)，摇杆往右 x是正数(右转)
        let t = Math.round(data.vector.x * 100);

        appState.power = clamp(p, -100, 100);
        appState.turn = clamp(t, -100, 100);

        // 本地动画立即响应
        updateLocalCarAnim();
      });

      joystickManager.on("end", () => {
        appState.power = 0;
        appState.turn = 0;
        updateLocalCarAnim();
        sendControlCmd(); // 立即停
      });

      // --- 3. 核心通信循环 ---

      // A. 发送控制指令 (仅遥控模式, 50ms一次)
      setInterval(() => {
        if (appState.mode === "remote") {
          sendControlCmd();
        }
      }, 50);

      // B. 获取状态数据 (所有模式, 200ms一次)
      setInterval(fetchStatus, 200);

      function sendControlCmd() {
        // API: /api/ctrl?m=POWER&s1=TURN&s2=0
        const url = `${config.baseUrl}/api/ctrl?m=${appState.power}&s1=${appState.turn}&s2=0`;
        // 使用 fetch 发送，忽略返回值以提高性能
        fetch(url, { method: "GET", signal: AbortSignal.timeout(100) }).catch(
          () => {},
        );
      }

      async function fetchStatus() {
        // API: /api/status (需要嵌入式端配合实现返回 JSON)
        try {
          const resp = await fetch(`${config.baseUrl}/api/status`, {
            signal: AbortSignal.timeout(300),
          });
          if (resp.ok) {
            const data = await resp.json();
            // 期望数据格式: { "mode": "avoid", "ang": 45, "dist": 20, "ir": [0,1,0] }

            // 更新数据
            appState.servoAngle = data.ang !== undefined ? data.ang : 90;
            appState.distance = data.dist !== undefined ? data.dist : 0;
            if (data.ir) appState.ir = data.ir;

            // 界面反馈 - 更新连接状态指示灯
            const connEl = document.getElementById("statusConn");
            connEl.classList.remove("disconnected");
            connEl.classList.add("connected");

            renderVisuals();
          }
        } catch (e) {
          // 界面反馈 - 更新连接状态指示灯
          const connEl = document.getElementById("statusConn");
          connEl.classList.remove("connected");
          connEl.classList.add("disconnected");

          // [调试用] 如果连接断开，且处于避障模式，生成假动画以便查看效果
          if (appState.mode === "avoid") simulateAvoidance();
        }
      }

      function changeMode(mode) {
        appState.mode = mode;

        // UI 按钮高亮
        document.querySelectorAll(".mode-btn").forEach((b) => {
          b.classList.toggle("active", b.innerText === getModeName(mode)); // 简单匹配，实际可用dataset
        });

        // 摇杆显隐
        const joyZone = document.getElementById("joystick-zone");
        if (mode === "remote") joyZone.classList.remove("disabled");
        else joyZone.classList.add("disabled");

        // 发送切换指令 (映射前端模式名到后端API格式)
        const modeMap = {
          standby: "stop",
          remote: "remote",
          tracking: "trace",
          avoid: "obstacle"
        };
        fetch(`${config.baseUrl}/api/mode?val=${modeMap[mode]}`).catch(console.error);
      }

      function getModeName(m) {
        const map = {
          standby: "待机",
          remote: "遥控",
          tracking: "循迹",
          avoid: "避障",
        };
        return map[m];
      }

      // --- 4. 视觉渲染 (UI 更新) ---
      function renderVisuals() {
        // 更新小车下方的状态显示
        document.getElementById("statusDist").innerText = appState.distance + " cm";
        document.getElementById("statusAngle").innerText = appState.servoAngle + "°";

        // 1. 舵机与雷达动画
        const servoEl = document.getElementById("servoHead");
        const beamEl = document.getElementById("radarBeam");

        // 舵机旋转 (注意 CSS 旋转中心已设为 center)
        // 假设 90度是正前。CSS rotate(0) 默认朝向可能需要调整。
        // 这里假设 0度右，90度前，180度左。
        // 为了让视觉符合，我们需要映射：CSS rotate(0) 是正上方吗？不，通常是原样。
        // 假设 servoHead 默认是水平摆放的？
        // 让我们简单点：CSS rotate(X deg). 如果 C代码 90度是正前，而CSS 0度是正前，则：rotate(angle - 90)
        servoEl.style.transform = `rotate(${appState.servoAngle - 90}deg)`;

        // 雷达波束长度与颜色
        // 限制最大显示长度 150px
        let beamLen = Math.min(appState.distance * 3, 150);
        beamEl.style.borderTopWidth = `${beamLen}px`;

        if (appState.distance > 0 && appState.distance < 20) {
          beamEl.classList.add("danger");
        } else {
          beamEl.classList.remove("danger");
        }

        // 2. 循迹传感器 (0=检测到黑线时高亮, 1=白色时不亮)
        document
          .getElementById("irL")
          .classList.toggle("active", appState.ir[0] === 0);
        document
          .getElementById("irM")
          .classList.toggle("active", appState.ir[1] === 0);
        document
          .getElementById("irR")
          .classList.toggle("active", appState.ir[2] === 0);
      }

      function updateLocalCarAnim() {
        // 更新轮子旋转动画 - 差速驱动模拟
        const wheelL = document.getElementById("wheelL");
        const wheelR = document.getElementById("wheelR");

        if (appState.mode === "remote") {
          // 计算左右轮的动力
          // power: 前进正值, 后退负值
          // turn: 右转正值, 左转负值

          let leftPower = appState.power;
          let rightPower = appState.power;

          // 差速转向：转向时减少一侧轮子的动力
          if (appState.turn > 0) {
            // 右转：左轮减少/反转，右轮保持动力
            leftPower = leftPower - appState.turn;
          } else if (appState.turn < 0) {
            // 左转：右轮保持动力，左轮减少/反转
            rightPower = rightPower + appState.turn; // turn是负数，所以加
          }

          // 根据动力设置动画状态
          // 正动力=向前转(spinning)，负动力=向后转(spinning-reverse)，无动力=停止
          updateWheelAnimation(wheelL, leftPower);
          updateWheelAnimation(wheelR, rightPower);
        } else {
          // 非遥控模式，停止所有轮子
          updateWheelAnimation(wheelL, 0);
          updateWheelAnimation(wheelR, 0);
        }
      }

      function updateWheelAnimation(wheel, power) {
        // 清除所有动画类
        wheel.classList.remove("spinning", "spinning-reverse");

        if (power > 10) {
          // 向前转
          wheel.classList.add("spinning");
        } else if (power < -10) {
          // 向后转
          wheel.classList.add("spinning-reverse");
        }
        // power 在 -10 到 10 之间视为停止
      }

      // [调试用] 模拟避障模式下的雷达扫描效果
      let simDir = 1;
      function simulateAvoidance() {
        appState.servoAngle += 5 * simDir;
        if (appState.servoAngle > 170 || appState.servoAngle < 10) simDir *= -1;

        // 模拟前方有障碍
        if (Math.abs(appState.servoAngle - 90) < 30) appState.distance = 15;
        else appState.distance = 80;

        renderVisuals();
      }

      // --- 5. 辅助函数 ---
      function clamp(v, min, max) {
        return Math.min(Math.max(v, min), max);
      }

      function toggleConfig() {
        const m = document.getElementById("configModal");
        m.style.display = m.style.display === "flex" ? "none" : "flex";
      }

      function saveConfig() {
        config.ip = document.getElementById("cfgIP").value;
        config.port = document.getElementById("cfgPort").value;
        localStorage.setItem("car_ip", config.ip);
        localStorage.setItem("car_port", config.port);
        updateBaseUrl();
        toggleConfig();
        alert(`地址已更新为: ${config.baseUrl}`);
      }

      // 初始化 UI 选中态
      document.querySelectorAll(".mode-btn")[0].click();
      document.getElementById("cfgIP").value = config.ip;
      document.getElementById("cfgPort").value = config.port;

      // 初始化连接状态指示灯为断开状态
      const connEl = document.getElementById("statusConn");
      connEl.classList.add("disconnected");