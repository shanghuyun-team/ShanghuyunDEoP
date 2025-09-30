#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ================== Wi-Fi & MQTT ==================
const char* WIFI_SSID     = "wenwen 的 S24 Ultra";
const char* WIFI_PASSWORD = "********";

const char* MQTT_SERVER   = "broker.MQTTGO.io";
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "";
const char* MQTT_PASS     = "";

// Topics
const char* TOPIC_REC_CONTROL = "ShangHuYun/DEoP/Sub/rec/control";
const char* TOPIC_ACK         = "ShangHuYun/DEoP/Pub/ack";

// ================== Sensor Config ==================
// 「MH Sensor Series Flying Fish（3 pins: VCC/GND/OUT）」
#define SENSOR_PIN          7

#define SENSOR_ACTIVE_HIGH  true

// 內建上拉/下拉（依模組輸出型態調整）
// - 若 ACTIVE_HIGH：多半使用 pulldown，等到 OUT 拉高觸發
// - 若 ACTIVE_LOW ：多半使用 pullup，等到 OUT 拉低觸發
#define USE_INTERNAL_PULL   true

// 去彈跳 + 節流
const unsigned long DEBOUNCE_MS  = 40;    // 連續穩定多少毫秒才算一次有效邊緣
const unsigned long COOLDOWN_MS  = 3000;  // 每次觸發後至少間隔多久才允許下一次發送

// ================== Objects ==================
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

// ================== States ==================
int lastStableState = -1;
int prevReadState   = -1;
unsigned long lastChangeTime   = 0;
unsigned long lastTriggerTime  = 0;

// ================== Prototypes ==================
void wifiConnect();
void mqttConnect();
void handleSensor();
void sendOneShotPlayback();
void onMqttMessage(char* topic, byte* payload, unsigned int length);

// ================== Setup ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  // 設定感測器腳位
  if (USE_INTERNAL_PULL) {
    if (SENSOR_ACTIVE_HIGH) {
      pinMode(SENSOR_PIN, INPUT_PULLDOWN);
    } else {
      pinMode(SENSOR_PIN, INPUT_PULLUP);
    }
  } else {
    pinMode(SENSOR_PIN, INPUT); // 外部拉電阻由模組/電路提供
  }

  // 初始讀一次，建立穩定狀態
  int r = digitalRead(SENSOR_PIN);
  lastStableState = r;
  prevReadState   = r;
  lastChangeTime  = millis();
  lastTriggerTime = 0;

  // Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  wifiConnect();

  // MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(onMqttMessage);
  mqttConnect();

  Serial.println("[INIT] Sensor trigger MQTT ready.");
}

// ================== Loop ==================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnect();
  }
  if (!mqtt.connected()) {
    mqttConnect();
  }
  mqtt.loop();

  handleSensor();
}

// ================== Wi-Fi / MQTT Helpers ==================
void wifiConnect() {
  if (WiFi.status() == WL_CONNECTED) return;
  Serial.printf("[WiFi] Connecting to %s ...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(300);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Connected! IP=%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("\n[WiFi] Failed to connect.");
  }
}

void mqttConnect() {
  while (!mqtt.connected() && WiFi.status() == WL_CONNECTED) {
    String clientId = "esp32-sensor-" + String((uint32_t)esp_random(), HEX);
    Serial.printf("[MQTT] Connecting as %s ...\n", clientId.c_str());
    bool ok;
    if (strlen(MQTT_USER) == 0 && strlen(MQTT_PASS) == 0) {
      ok = mqtt.connect(clientId.c_str());
    } else {
      ok = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASS);
    }
    if (ok) {
      Serial.println("[MQTT] Connected.");
      // 可選：訂閱 ACK 來看主控回應
      mqtt.subscribe(TOPIC_ACK);
    } else {
      Serial.printf("[MQTT] Failed (state=%d). Retry in 1s\n", mqtt.state());
      delay(1000);
    }
  }
}

// ================== Sensor Handling ==================
bool isActiveLevel(int level) {
  return SENSOR_ACTIVE_HIGH ? (level == HIGH) : (level == LOW);
}

void handleSensor() {
  // 輕量輪詢（約每 10ms）
  static unsigned long lastPoll = 0;
  if (millis() - lastPoll < 10) return;
  lastPoll = millis();

  int curr = digitalRead(SENSOR_PIN);

  // 邊緣偵測 + 去彈跳
  if (curr != prevReadState) {
    prevReadState  = curr;
    lastChangeTime = millis();
  }

  if (millis() - lastChangeTime >= DEBOUNCE_MS) {
    // 狀態已穩定
    if (curr != lastStableState) {
      // 這是一個「穩定的邊緣」事件
      lastStableState = curr;

      // 只在「進入有效狀態」時觸發
      if (isActiveLevel(curr)) {
        if (millis() - lastTriggerTime >= COOLDOWN_MS) {
          Serial.println("[SENSOR] Active edge detected -> trigger playback once");
          sendOneShotPlayback();
          lastTriggerTime = millis();
        } else {
          Serial.println("[SENSOR] Edge detected but in cooldown.");
        }
      }
    }
  }
}

// ================== Publish Command ==================
void sendOneShotPlayback() {
  StaticJsonDocument<128> doc;
  doc["action"] = "playback";
  doc["status"] = "start";
  doc["count"]  = 1;

  char buf[128];
  size_t n = serializeJson(doc, buf, sizeof(buf)); // 回傳實際長度，不含結尾 \0

  bool ok = mqtt.publish(
      TOPIC_REC_CONTROL,
      reinterpret_cast<const uint8_t*>(buf), // ★ 重點：轉成 uint8_t*
      n,
      false
  );
  Serial.printf("[MQTT] Publish to '%s' (%s): %.*s\n",
                TOPIC_REC_CONTROL, ok ? "OK" : "FAIL", (int)n, buf);
}

// ================== MQTT Callback（可看 ACK） ==================
void onMqttMessage(char* topic, byte* payload, unsigned int length) {
  String s; s.reserve(length);
  for (unsigned int i = 0; i < length; ++i) s += (char)payload[i];
  Serial.print("[ACK] "); Serial.print(topic); Serial.print(" | ");
  Serial.println(s);
}
