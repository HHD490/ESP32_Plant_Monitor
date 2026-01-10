const int sensorPin = 34;    // 土壤传感器信号
const int lightPin = 32;     // 光敏传感器信号 (AO)
const int sensorPower = 25;  // 传感器供电引脚
const int pumpPin = 2;       // 模拟水泵

// 校准值
const int airValue = 3500;   
const int waterValue = 1000;  

// --- 滞后控制逻辑设定 ---
// 1. 土壤湿度滞后
const int startWatering = 30; 
const int stopWatering = 40;  

// 2. 光照强度滞后 (新增)
const int lightStartThreshold = 55; // 亮度高于 55% 才允许开始浇水
const int lightStopThreshold = 45;  // 亮度低于 45% 强制停止浇水

bool isWatering = false;      

void setup() {
  Serial.begin(115200);
  pinMode(sensorPower, OUTPUT);
  pinMode(pumpPin, OUTPUT);
  digitalWrite(sensorPower, LOW);
}

void loop() {
  // --- 步骤 1: 统一供电并读取数据 ---
  digitalWrite(sensorPower, HIGH); 
  delay(50);                      
  
  int rawSoil = analogRead(sensorPin);
  int rawLight = analogRead(lightPin); 
  
  digitalWrite(sensorPower, LOW);  

  // --- 步骤 2: 数据映射 ---
  int humidity = map(rawSoil, airValue, waterValue, 0, 100);
  humidity = constrain(humidity, 0, 100);

  // 亮度映射 (根据你的 rawLight 范围 0-2500)
  int lightLevel = map(rawLight, 2500, 0, 0, 100);
  lightLevel = constrain(lightLevel, 0, 100);

  // --- 步骤 3: 串口输出 ---
  Serial.print("湿度: "); Serial.print(humidity); Serial.print("% | ");
  Serial.print("光照: "); Serial.print(lightLevel); Serial.print("% | ");
  Serial.print("原始光敏: "); Serial.print(rawLight); 
  
  // --- 步骤 4: 逻辑判断 (双滞后控制) ---
  
  // 启动逻辑：没在浇水 + 湿度太低 + 阳光非常充足(>55)
  if (!isWatering && humidity < startWatering && lightLevel > lightStartThreshold) {
      isWatering = true;
      digitalWrite(pumpPin, HIGH);
      Serial.print(" >> [启动] 阳光明媚且土壤干燥");
  } 

  // 停止逻辑：正在浇水 + (水分够了 || 天色明显变暗(<45))
  else if (isWatering && (humidity > stopWatering || lightLevel < lightStopThreshold)) {
      isWatering = false;
      digitalWrite(pumpPin, LOW);
      Serial.print(" >> [停止] 水分已足或光线变暗");
  }

  // 状态打印
  if (isWatering) {
    Serial.println(" | 状态: 浇水中...💧");
  } else {
    Serial.println(" | 状态: 待机中✅");
  }

  delay(2000); 
}