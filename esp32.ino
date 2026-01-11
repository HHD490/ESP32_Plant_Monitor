#include <WebServer.h>
#include <WiFi.h>

// ============== Wi-Fi 配置 ==============
const char *ssid = "老爷保号";
const char *password = "B26110825";

// ============== 引脚定义 ==============
const int sensorPin = 34;   // 土壤传感器信号
const int lightPin = 32;    // 光敏传感器信号 (AO)
const int sensorPower = 25; // 传感器供电引脚
const int pumpPin = 2;      // 模拟水泵 LED 指示灯
const int relayPin = 27;    // 继电器控制引脚 (IN)
const bool RELAY_ACTIVE_LOW =
    false; // 低电平触发继电器 (true) / 高电平触发 (false)

// ============== 校准值 ==============
const int airValue = 4095;
const int waterValue = 0;

// ============== 滞后控制逻辑设定 ==============
const int startWatering = 50;
const int stopWatering = 70;
const int lightStartThreshold = 80;
const int lightStopThreshold = 70;

// ============== 状态变量 ==============
bool isWatering = false;
bool autoMode = true; // 自动模式 (true) / 手动模式 (false)
int currentHumidity = 0;
int currentLight = 0;

// ============== 非阻塞计时 ==============
unsigned long lastSensorRead = 0;
const unsigned long sensorInterval = 2000; // 传感器读取间隔 (ms)

// ============== Web 服务器 ==============
WebServer server(80);

// HTML 页面
const char *htmlPage = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>智能浇水系统</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 50%, #0f3460 100%);
            min-height: 100vh;
            color: #fff;
            padding: 20px;
        }
        .container { max-width: 600px; margin: 0 auto; }
        h1 {
            text-align: center;
            font-size: 1.8em;
            margin-bottom: 30px;
            background: linear-gradient(90deg, #00d9ff, #00ff88);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .card {
            background: rgba(255,255,255,0.1);
            backdrop-filter: blur(10px);
            border-radius: 20px;
            padding: 25px;
            margin-bottom: 20px;
            border: 1px solid rgba(255,255,255,0.1);
        }
        .sensor-row {
            display: flex;
            justify-content: space-between;
            align-items: center;
            padding: 15px 0;
            border-bottom: 1px solid rgba(255,255,255,0.1);
        }
        .sensor-row:last-child { border-bottom: none; }
        .sensor-label { font-size: 1.1em; opacity: 0.8; }
        .sensor-value {
            font-size: 2em;
            font-weight: bold;
            background: linear-gradient(90deg, #00d9ff, #00ff88);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
        }
        .status-badge {
            display: inline-block;
            padding: 8px 20px;
            border-radius: 20px;
            font-weight: bold;
            font-size: 1.1em;
        }
        .status-on { background: linear-gradient(90deg, #00d9ff, #00ff88); color: #1a1a2e; }
        .status-off { background: rgba(255,255,255,0.2); color: #fff; }
        .btn-group { display: flex; gap: 15px; margin-top: 15px; }
        .btn {
            flex: 1;
            padding: 15px;
            border: none;
            border-radius: 15px;
            font-size: 1.1em;
            font-weight: bold;
            cursor: pointer;
            transition: all 0.3s ease;
        }
        .btn-on {
            background: linear-gradient(90deg, #00d9ff, #00ff88);
            color: #1a1a2e;
        }
        .btn-off {
            background: rgba(255,255,255,0.2);
            color: #fff;
        }
        .btn:hover { transform: translateY(-2px); opacity: 0.9; }
        .btn:active { transform: translateY(0); }
        .btn:disabled { opacity: 0.3; cursor: not-allowed; transform: none; }
        .update-time { text-align: center; opacity: 0.5; margin-top: 20px; font-size: 0.9em; }
        .icon { font-size: 1.5em; margin-right: 10px; }
        /* 模式切换开关 */
        .mode-switch {
            display: flex;
            align-items: center;
            justify-content: space-between;
            padding: 10px 0;
        }
        .switch-container {
            display: flex;
            align-items: center;
            gap: 12px;
        }
        .switch {
            position: relative;
            width: 60px;
            height: 32px;
        }
        .switch input { opacity: 0; width: 0; height: 0; }
        .slider {
            position: absolute;
            cursor: pointer;
            top: 0; left: 0; right: 0; bottom: 0;
            background: rgba(255,255,255,0.2);
            transition: 0.3s;
            border-radius: 32px;
        }
        .slider:before {
            position: absolute;
            content: "";
            height: 24px;
            width: 24px;
            left: 4px;
            bottom: 4px;
            background: white;
            transition: 0.3s;
            border-radius: 50%;
        }
        input:checked + .slider {
            background: linear-gradient(90deg, #00d9ff, #00ff88);
        }
        input:checked + .slider:before {
            transform: translateX(28px);
        }
        .mode-label {
            font-size: 1.1em;
            font-weight: bold;
        }
        .mode-auto { color: #00ff88; }
        .mode-manual { color: #ff9500; }
        .manual-controls { margin-top: 15px; }
        .manual-hint {
            text-align: center;
            opacity: 0.6;
            font-size: 0.9em;
            padding: 20px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌱 智能浇水系统</h1>
        <div class="card">
            <div class="sensor-row">
                <span class="sensor-label"><span class="icon">💧</span>土壤湿度</span>
                <span class="sensor-value" id="humidity">--</span>
            </div>
            <div class="sensor-row">
                <span class="sensor-label"><span class="icon">☀️</span>光照强度</span>
                <span class="sensor-value" id="light">--</span>
            </div>
            <div class="sensor-row">
                <span class="sensor-label"><span class="icon">🚿</span>浇水状态</span>
                <span class="status-badge status-off" id="status">待机中</span>
            </div>
        </div>
        <div class="card">
            <div class="mode-switch">
                <span class="sensor-label"><span class="icon">⚙️</span>控制模式</span>
                <div class="switch-container">
                    <span class="mode-label mode-manual" id="manualLabel">手动</span>
                    <label class="switch">
                        <input type="checkbox" id="modeSwitch" checked onchange="toggleMode()">
                        <span class="slider"></span>
                    </label>
                    <span class="mode-label mode-auto" id="autoLabel">自动</span>
                </div>
            </div>
            <div class="manual-controls" id="manualControls" style="display: none;">
                <div class="btn-group">
                    <button class="btn btn-on" id="btnOn" onclick="pumpControl('on')">开启水泵 💧</button>
                    <button class="btn btn-off" id="btnOff" onclick="pumpControl('off')">关闭水泵 ⏹️</button>
                </div>
            </div>
            <div class="manual-hint" id="autoHint">
                🤖 自动模式运行中<br>根据湿度和光照自动控制水泵
            </div>
        </div>
        <p class="update-time">每 2 秒自动更新</p>
    </div>
    <script>
        function fetchData() {
            fetch('/data')
                .then(r => r.json())
                .then(d => {
                    document.getElementById('humidity').textContent = d.humidity + '%';
                    document.getElementById('light').textContent = d.light + '%';
                    const statusEl = document.getElementById('status');
                    if (d.watering) {
                        statusEl.textContent = '浇水中 💧';
                        statusEl.className = 'status-badge status-on';
                    } else {
                        statusEl.textContent = '待机中 ✅';
                        statusEl.className = 'status-badge status-off';
                    }
                    // 同步模式状态
                    document.getElementById('modeSwitch').checked = d.autoMode;
                    updateModeUI(d.autoMode);
                    // 更新按钮状态
                    document.getElementById('btnOn').disabled = d.watering;
                    document.getElementById('btnOff').disabled = !d.watering;
                })
                .catch(e => console.error('Error:', e));
        }
        function updateModeUI(isAuto) {
            document.getElementById('manualControls').style.display = isAuto ? 'none' : 'block';
            document.getElementById('autoHint').style.display = isAuto ? 'block' : 'none';
            document.getElementById('autoLabel').style.opacity = isAuto ? 1 : 0.4;
            document.getElementById('manualLabel').style.opacity = isAuto ? 0.4 : 1;
        }
        function toggleMode() {
            const isAuto = document.getElementById('modeSwitch').checked;
            fetch('/mode/' + (isAuto ? 'auto' : 'manual'))
                .then(r => r.json())
                .then(d => {
                    updateModeUI(d.autoMode);
                    fetchData();
                })
                .catch(e => console.error('Error:', e));
        }
        function pumpControl(action) {
            fetch('/pump/' + action)
                .then(r => r.json())
                .then(d => { fetchData(); })
                .catch(e => console.error('Error:', e));
        }
        fetchData();
        setInterval(fetchData, 2000);
    </script>
</body>
</html>
)rawliteral";

// ============== HTTP 处理函数 ==============
void handleRoot() { server.send(200, "text/html", htmlPage); }

void handleData() {
  String json = "{\"humidity\":" + String(currentHumidity) +
                ",\"light\":" + String(currentLight) +
                ",\"watering\":" + (isWatering ? "true" : "false") +
                ",\"autoMode\":" + (autoMode ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handleModeAuto() {
  autoMode = true;
  server.send(200, "application/json", "{\"success\":true,\"autoMode\":true}");
  Serial.println(" >> [模式切换] 已切换到自动模式");
}

void handleModeManual() {
  autoMode = false;
  // 切换到手动模式时，先关闭水泵
  isWatering = false;
  digitalWrite(pumpPin, LOW);
  digitalWrite(relayPin, RELAY_ACTIVE_LOW ? HIGH : LOW); // 关闭继电器
  server.send(200, "application/json", "{\"success\":true,\"autoMode\":false}");
  Serial.println(" >> [模式切换] 已切换到手动模式");
}

void handlePumpOn() {
  if (!autoMode) { // 只有手动模式下才能控制
    isWatering = true;
    digitalWrite(pumpPin, HIGH);
    digitalWrite(relayPin, RELAY_ACTIVE_LOW ? LOW : HIGH); // 开启继电器
    server.send(200, "application/json", "{\"success\":true,\"pump\":\"on\"}");
    Serial.println(" >> [手动] 水泵已开启");
  } else {
    server.send(200, "application/json",
                "{\"success\":false,\"error\":\"auto mode\"}");
  }
}

void handlePumpOff() {
  if (!autoMode) { // 只有手动模式下才能控制
    isWatering = false;
    digitalWrite(pumpPin, LOW);
    digitalWrite(relayPin, RELAY_ACTIVE_LOW ? HIGH : LOW); // 关闭继电器
    server.send(200, "application/json", "{\"success\":true,\"pump\":\"off\"}");
    Serial.println(" >> [手动] 水泵已关闭");
  } else {
    server.send(200, "application/json",
                "{\"success\":false,\"error\":\"auto mode\"}");
  }
}

// ============== 初始化 ==============
void setup() {
  Serial.begin(115200);
  pinMode(sensorPower, OUTPUT);
  pinMode(pumpPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  digitalWrite(sensorPower, LOW);
  digitalWrite(pumpPin, LOW);
  // 继电器初始化为关闭状态
  digitalWrite(relayPin, RELAY_ACTIVE_LOW ? HIGH : LOW);

  // 连接 Wi-Fi
  Serial.println();
  Serial.print("正在连接 Wi-Fi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("✅ Wi-Fi 连接成功!");
  Serial.print("📱 Web 界面地址: http://");
  Serial.println(WiFi.localIP());

  // 配置 HTTP 路由
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/mode/auto", handleModeAuto);
  server.on("/mode/manual", handleModeManual);
  server.on("/pump/on", handlePumpOn);
  server.on("/pump/off", handlePumpOff);

  server.begin();
  Serial.println("🌐 Web 服务器已启动");
}

// ============== 主循环 ==============
void loop() {
  // 处理 HTTP 请求 (优先响应，无阻塞)
  server.handleClient();

  // --- 非阻塞传感器读取 ---
  unsigned long currentMillis = millis();
  if (currentMillis - lastSensorRead >= sensorInterval) {
    lastSensorRead = currentMillis;

    // 读取传感器数据
    digitalWrite(sensorPower, HIGH);
    delay(50); // 传感器稳定时间 (较短，可接受)

    int rawSoil = analogRead(sensorPin);
    int rawLight = analogRead(lightPin);

    digitalWrite(sensorPower, LOW);

    // 数据映射
    currentHumidity = map(rawSoil, airValue, waterValue, 0, 100);
    currentHumidity = constrain(currentHumidity, 0, 100);

    currentLight = map(rawLight, 4095, 0, 0, 100);
    currentLight = constrain(currentLight, 0, 100);

    // 串口输出
    Serial.print("湿度: ");
    Serial.print(currentHumidity);
    Serial.print("% | ");
    Serial.print("光照: ");
    Serial.print(currentLight);
    Serial.print("% | ");
    Serial.print("原始光照: ");
    Serial.print(rawLight);
    Serial.print(" | ");
    Serial.print("模式: ");
    Serial.print(autoMode ? "自动" : "手动");
    Serial.print(" | ");

    // 自动控制逻辑 (仅在自动模式下生效)
    if (autoMode) {
      // 启动逻辑
      if (!isWatering && currentHumidity < startWatering &&
          currentLight > lightStartThreshold) {
        isWatering = true;
        digitalWrite(pumpPin, HIGH);
        digitalWrite(relayPin, RELAY_ACTIVE_LOW ? LOW : HIGH); // 开启继电器
        Serial.print("[自动启动] ");
      }
      // 停止逻辑
      else if (isWatering && (currentHumidity > stopWatering ||
                              currentLight < lightStopThreshold)) {
        isWatering = false;
        digitalWrite(pumpPin, LOW);
        digitalWrite(relayPin, RELAY_ACTIVE_LOW ? HIGH : LOW); // 关闭继电器
        Serial.print("[自动停止] ");
      }
    }

    // 状态打印
    if (isWatering) {
      Serial.println("状态: 浇水中...💧");
    } else {
      Serial.println("状态: 待机中✅");
    }
  }
}