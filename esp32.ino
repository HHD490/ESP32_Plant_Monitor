#include <WebServer.h>
#include <WiFi.h>

// ============== Wi-Fi 配置 ==============
const char *ssid = "老爷保号";
const char *password = "B26110825";

// ============== 引脚定义 ==============
const int sensorPin = 34;   // 土壤传感器信号
const int lightPin = 32;    // 光敏传感器信号 (AO)
const int sensorPower = 25; // 传感器供电引脚
const int pumpPin = 2;      // 模拟水泵

// ============== 校准值 ==============
const int airValue = 3500;
const int waterValue = 1000;

// ============== 滞后控制逻辑设定 ==============
const int startWatering = 30;
const int stopWatering = 40;
const int lightStartThreshold = 55;
const int lightStopThreshold = 45;

// ============== 状态变量 ==============
bool isWatering = false;
bool manualMode = false; // 手动模式标志
int currentHumidity = 0;
int currentLight = 0;

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
        .btn-group { display: flex; gap: 15px; margin-top: 20px; }
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
        .update-time { text-align: center; opacity: 0.5; margin-top: 20px; font-size: 0.9em; }
        .icon { font-size: 1.5em; margin-right: 10px; }
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
            <h3 style="margin-bottom: 15px; opacity: 0.8;">手动控制</h3>
            <div class="btn-group">
                <button class="btn btn-on" onclick="pumpControl('on')">开启水泵 💧</button>
                <button class="btn btn-off" onclick="pumpControl('off')">关闭水泵 ⏹️</button>
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
                ",\"watering\":" + (isWatering ? "true" : "false") + "}";
  server.send(200, "application/json", json);
}

void handlePumpOn() {
  manualMode = true;
  isWatering = true;
  digitalWrite(pumpPin, HIGH);
  server.send(200, "application/json", "{\"success\":true,\"pump\":\"on\"}");
  Serial.println(" >> [手动] 水泵已开启");
}

void handlePumpOff() {
  manualMode = false;
  isWatering = false;
  digitalWrite(pumpPin, LOW);
  server.send(200, "application/json", "{\"success\":true,\"pump\":\"off\"}");
  Serial.println(" >> [手动] 水泵已关闭");
}

// ============== 初始化 ==============
void setup() {
  Serial.begin(115200);
  pinMode(sensorPower, OUTPUT);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(sensorPower, LOW);
  digitalWrite(pumpPin, LOW);

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
  server.on("/pump/on", handlePumpOn);
  server.on("/pump/off", handlePumpOff);

  server.begin();
  Serial.println("🌐 Web 服务器已启动");
}

// ============== 主循环 ==============
void loop() {
  // 处理 HTTP 请求
  server.handleClient();

  // --- 读取传感器数据 ---
  digitalWrite(sensorPower, HIGH);
  delay(50);

  int rawSoil = analogRead(sensorPin);
  int rawLight = analogRead(lightPin);

  digitalWrite(sensorPower, LOW);

  // --- 数据映射 ---
  currentHumidity = map(rawSoil, airValue, waterValue, 0, 100);
  currentHumidity = constrain(currentHumidity, 0, 100);

  currentLight = map(rawLight, 2500, 0, 0, 100);
  currentLight = constrain(currentLight, 0, 100);

  // --- 串口输出 ---
  Serial.print("湿度: ");
  Serial.print(currentHumidity);
  Serial.print("% | ");
  Serial.print("光照: ");
  Serial.print(currentLight);
  Serial.print("% | ");
  Serial.print("原始光敏: ");
  Serial.print(rawLight);

  // --- 自动控制逻辑 (仅在非手动模式下生效) ---
  if (!manualMode) {
    // 启动逻辑
    if (!isWatering && currentHumidity < startWatering &&
        currentLight > lightStartThreshold) {
      isWatering = true;
      digitalWrite(pumpPin, HIGH);
      Serial.print(" >> [启动] 阳光明媚且土壤干燥");
    }
    // 停止逻辑
    else if (isWatering && (currentHumidity > stopWatering ||
                            currentLight < lightStopThreshold)) {
      isWatering = false;
      digitalWrite(pumpPin, LOW);
      Serial.print(" >> [停止] 水分已足或光线变暗");
    }
  }

  // 状态打印
  if (isWatering) {
    Serial.println(" | 状态: 浇水中...💧");
  } else {
    Serial.println(" | 状态: 待机中✅");
  }

  delay(2000);
}