/*
 * ESP32 智能浇水系统 - Blynk 云端版本
 *
 * 功能：
 * - Blynk 云端数据同步
 * - 动态配网 (AP 模式)
 * - 远程控制水泵
 * - 可配置采样间隔和水泵时长
 * - NVS 持久化存储配置
 */

// ============== Blynk 配置 (必须在最前面) ==============
#define BLYNK_TEMPLATE_ID "TMPL6nDG-w0HN"
#define BLYNK_TEMPLATE_NAME "ESP32 Plant Monitor"
#define BLYNK_PRINT Serial

// ============== 库引用 ==============
#include <BlynkSimpleEsp32.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

// ============== 引脚定义 ==============
const int sensorPin = 34;   // 土壤传感器信号
const int lightPin = 32;    // 光敏传感器信号 (AO)
const int sensorPower = 25; // 传感器供电引脚
const int pumpLedPin = 2;   // 水泵 LED 指示灯
const int relayPin = 27;    // 继电器控制引脚 (IN)
const bool RELAY_ACTIVE_LOW = false;
const int buttonPin = 0; // Boot 按钮

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
bool autoMode = true;
int currentHumidity = 0;
int currentLight = 0;

// ============== 可配置参数 ==============
Preferences preferences;
int samplingIntervalHours = 1;   // 采样间隔 - 小时 (0-24)
int samplingIntervalMinutes = 0; // 采样间隔 - 分钟 (0-60)
int pumpDurationSeconds = 5;
char wifiSSID[64] = "";
char wifiPass[64] = "";
char blynkToken[64] = "";

// ============== 配网模式 ==============
WebServer configServer(80);
DNSServer dnsServer;
bool configModeActive = false;

// ============== 计时器 ==============
BlynkTimer timer;
unsigned long lastSensorRead = 0;
unsigned long sensorInterval = 3600000; // 默认1小时

// ============== Blynk 虚拟引脚 ==============
// V0: 土壤湿度 | V1: 光照强度 | V2: 水泵控制
// V3: 采样间隔(小时) | V4: 水泵时长 | V5: 自动/手动模式
// V6: 采样间隔(分钟)

// 计算总采样间隔 (毫秒)
void updateSensorInterval() {
  sensorInterval = (unsigned long)samplingIntervalHours * 3600000UL +
                   (unsigned long)samplingIntervalMinutes * 60000UL;
  // 最小间隔 1 分钟
  if (sensorInterval < 60000)
    sensorInterval = 60000;
  Serial.printf("⚙️ 采样间隔已更新: %d小时 %d分钟\n", samplingIntervalHours,
                samplingIntervalMinutes);
}

// ============== 水泵控制 ==============
void pumpOn() {
  isWatering = true;
  digitalWrite(pumpLedPin, HIGH);
  digitalWrite(relayPin, RELAY_ACTIVE_LOW ? LOW : HIGH);
  if (Blynk.connected())
    Blynk.virtualWrite(V2, 1);
  Serial.println("💧 水泵开启");
}

void pumpOff() {
  isWatering = false;
  digitalWrite(pumpLedPin, LOW);
  digitalWrite(relayPin, RELAY_ACTIVE_LOW ? HIGH : LOW);
  if (Blynk.connected())
    Blynk.virtualWrite(V2, 0);
  Serial.println("⏹️ 水泵关闭");
}

void pumpAutoOff() {
  if (isWatering) {
    pumpOff();
    Serial.println("⏱️ 水泵自动关闭 (定时)");
  }
}

// ============== 读取传感器 ==============
void readSensors() {
  digitalWrite(sensorPower, HIGH);
  delay(50);

  int rawSoil = analogRead(sensorPin);
  int rawLight = analogRead(lightPin);

  digitalWrite(sensorPower, LOW);

  currentHumidity = map(rawSoil, airValue, waterValue, 0, 100);
  currentHumidity = constrain(currentHumidity, 0, 100);

  currentLight = map(rawLight, 4095, 0, 0, 100);
  currentLight = constrain(currentLight, 0, 100);

  Serial.printf("📊 湿度: %d%% | 光照: %d%%\n", currentHumidity, currentLight);
}

// ============== 上传数据到 Blynk ==============
void uploadToBlynk() {
  if (Blynk.connected()) {
    Blynk.virtualWrite(V0, currentHumidity);
    Blynk.virtualWrite(V1, currentLight);
    Blynk.virtualWrite(V5, autoMode ? 1 : 0);
    Serial.println("☁️ 数据已上传到 Blynk");
  }
}

// ============== 自动浇水逻辑 ==============
void autoWateringCheck() {
  if (!autoMode)
    return;

  if (!isWatering && currentHumidity < startWatering &&
      currentLight > lightStartThreshold) {
    Serial.println("🌱 [自动] 开始浇水");
    pumpOn();
    timer.setTimeout(pumpDurationSeconds * 1000L, pumpAutoOff);
  }
}

// ============== 定时任务 ==============
void periodicTask() {
  Serial.println("\n⏰ 定时任务触发");
  readSensors();
  uploadToBlynk();
  autoWateringCheck();
}

// ============== Blynk 回调 ==============
BLYNK_WRITE(V2) {
  int value = param.asInt();
  Serial.printf("📱 收到水泵控制: %d\n", value);
  if (value == 1) {
    pumpOn();
    timer.setTimeout(pumpDurationSeconds * 1000L, pumpAutoOff);
  } else {
    pumpOff();
  }
}

BLYNK_WRITE(V3) {
  int value = param.asInt();
  if (value >= 0 && value <= 24) {
    samplingIntervalHours = value;
    preferences.putInt("sampleInt", samplingIntervalHours);
    updateSensorInterval();
  }
}

BLYNK_WRITE(V6) {
  int value = param.asInt();
  if (value >= 0 && value <= 60) {
    samplingIntervalMinutes = value;
    preferences.putInt("sampleMin", samplingIntervalMinutes);
    updateSensorInterval();
  }
}

BLYNK_WRITE(V4) {
  int value = param.asInt();
  if (value >= 1 && value <= 60) {
    pumpDurationSeconds = value;
    preferences.putInt("pumpDur", pumpDurationSeconds);
    Serial.printf("⚙️ 水泵时长: %d 秒\n", pumpDurationSeconds);
  }
}

BLYNK_WRITE(V5) {
  autoMode = param.asInt() == 1;
  Serial.printf("⚙️ 模式: %s\n", autoMode ? "自动" : "手动");
  if (!autoMode)
    pumpOff();
}

BLYNK_CONNECTED() {
  Serial.println("✅ Blynk 已连接");
  Blynk.syncVirtual(V3, V4, V5, V6);
  readSensors();
  uploadToBlynk();
}

// ============== 配网页面 ==============
const char configPage[] PROGMEM = R"html(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width,initial-scale=1">
    <title>ESP32 配网</title>
    <style>
        body{font-family:sans-serif;background:#1a1a2e;color:#fff;padding:20px;margin:0}
        .card{background:rgba(255,255,255,0.1);border-radius:15px;padding:20px;max-width:400px;margin:0 auto}
        h1{text-align:center;color:#00ff88;font-size:1.5em}
        label{display:block;margin-top:15px;opacity:0.8}
        input{width:100%;padding:12px;margin:5px 0;border:none;border-radius:8px;box-sizing:border-box}
        button{width:100%;padding:15px;margin-top:20px;background:linear-gradient(90deg,#00d9ff,#00ff88);border:none;border-radius:8px;font-weight:bold;cursor:pointer;font-size:1em}
        .info{font-size:12px;opacity:0.6;margin-top:20px;text-align:center}
    </style>
</head>
<body>
    <div class="card">
        <h1>🌱 智能浇水系统配网</h1>
        <form action="/save" method="POST">
            <label>WiFi 名称</label>
            <input type="text" name="ssid" required>
            <label>WiFi 密码</label>
            <input type="password" name="pass">
            <label>Blynk Auth Token</label>
            <input type="text" name="token" placeholder="从 Blynk 控制台获取" required>
            <button type="submit">保存并连接</button>
        </form>
        <p class="info">请在 Blynk 控制台创建设备获取 Token</p>
    </div>
</body>
</html>
)html";

void handleConfigRoot() { configServer.send(200, "text/html", configPage); }

void handleConfigSave() {
  String ssid = configServer.arg("ssid");
  String pass = configServer.arg("pass");
  String token = configServer.arg("token");

  if (ssid.length() > 0 && token.length() > 0) {
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.putString("token", token);

    configServer.send(
        200, "text/html",
        "<html><body "
        "style='background:#1a1a2e;color:#fff;text-align:center;padding:50px'>"
        "<h1>✅ 配置已保存</h1><p>设备将在 3 秒后重启...</p></body></html>");

    delay(3000);
    ESP.restart();
  } else {
    configServer.send(400, "text/plain", "请填写所有必填项");
  }
}

void enterConfigMode() {
  if (configModeActive)
    return;
  configModeActive = true;

  WiFi.disconnect();
  delay(100);
  WiFi.mode(WIFI_AP);
  WiFi.softAP("ESP32-Plant-Setup", "12345678");

  dnsServer.start(53, "*", WiFi.softAPIP());

  configServer.on("/", handleConfigRoot);
  configServer.on("/save", HTTP_POST, handleConfigSave);
  configServer.onNotFound(handleConfigRoot);
  configServer.begin();

  Serial.println("\n📡 配网模式已启动");
  Serial.println("   热点名称: ESP32-Plant-Setup");
  Serial.println("   热点密码: 12345678");
  Serial.print("   配置地址: http://");
  Serial.println(WiFi.softAPIP());
}

void runConfigMode() {
  if (configModeActive) {
    dnsServer.processNextRequest();
    configServer.handleClient();
  }
}

// ============== 加载配置 ==============
bool loadConfig() {
  preferences.begin("plant", false);

  String ssid = preferences.getString("ssid", "");
  String pass = preferences.getString("pass", "");
  String token = preferences.getString("token", "");

  samplingIntervalHours = preferences.getInt("sampleInt", 1);
  samplingIntervalMinutes = preferences.getInt("sampleMin", 0);
  pumpDurationSeconds = preferences.getInt("pumpDur", 5);
  sensorInterval = (unsigned long)samplingIntervalHours * 3600000UL +
                   (unsigned long)samplingIntervalMinutes * 60000UL;
  if (sensorInterval < 60000)
    sensorInterval = 60000;

  if (ssid.length() > 0 && token.length() > 0) {
    ssid.toCharArray(wifiSSID, sizeof(wifiSSID));
    pass.toCharArray(wifiPass, sizeof(wifiPass));
    token.toCharArray(blynkToken, sizeof(blynkToken));
    return true;
  }
  return false;
}

// ============== 重置配置 ==============
void resetConfig() {
  preferences.clear();
  Serial.println("🔄 配置已重置");
  delay(500);
  ESP.restart();
}

// ============== 检查按钮 ==============
void checkButton() {
  static uint32_t pressTime = 0;
  static bool wasPressed = false;

  bool pressed = digitalRead(buttonPin) == LOW;

  if (pressed && !wasPressed) {
    pressTime = millis();
    wasPressed = true;
  } else if (!pressed && wasPressed) {
    wasPressed = false;
  }

  if (wasPressed && (millis() - pressTime > 5000)) {
    Serial.println("⚠️ 长按检测，重置配置...");
    resetConfig();
  }
}

// ============== 初始化 ==============
void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println("\n🌱 ESP32 智能浇水系统启动中...");

  // 引脚初始化
  pinMode(sensorPower, OUTPUT);
  pinMode(pumpLedPin, OUTPUT);
  pinMode(relayPin, OUTPUT);
  pinMode(buttonPin, INPUT_PULLUP);
  digitalWrite(sensorPower, LOW);
  digitalWrite(pumpLedPin, LOW);
  digitalWrite(relayPin, RELAY_ACTIVE_LOW ? HIGH : LOW);

  // 加载配置
  if (loadConfig()) {
    Serial.printf("📋 配置已加载: SSID=%s\n", wifiSSID);
    Serial.printf("   采样间隔=%d小时, 水泵时长=%d秒\n", samplingIntervalHours,
                  pumpDurationSeconds);

    // 连接 WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(wifiSSID, wifiPass);

    Serial.print("🔗 连接 WiFi");
    uint32_t timeout = millis() + 15000;
    while (WiFi.status() != WL_CONNECTED && millis() < timeout) {
      delay(500);
      Serial.print(".");
      checkButton();
    }

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\n✅ WiFi 已连接");
      Serial.print("   IP: ");
      Serial.println(WiFi.localIP());

      // 连接 Blynk
      Blynk.config(blynkToken);
      Blynk.connect(5000);
    } else {
      Serial.println("\n❌ WiFi 连接失败，进入配网模式");
      enterConfigMode();
    }
  } else {
    Serial.println("📋 未发现配置，进入配网模式");
    enterConfigMode();
  }

  // 设置定时任务
  timer.setInterval(sensorInterval, periodicTask);

  // 启动时执行一次
  if (!configModeActive) {
    periodicTask();
  }

  Serial.println("🌱 系统启动完成");
}

// ============== 主循环 ==============
void loop() {
  checkButton();

  if (configModeActive) {
    runConfigMode();
  } else {
    if (Blynk.connected()) {
      Blynk.run();
    } else if (WiFi.status() == WL_CONNECTED) {
      // 尝试重连 Blynk
      static uint32_t lastReconnect = 0;
      if (millis() - lastReconnect > 30000) {
        lastReconnect = millis();
        Serial.println("🔄 尝试重连 Blynk...");
        Blynk.connect(5000);
      }
    }

    timer.run();
  }
}