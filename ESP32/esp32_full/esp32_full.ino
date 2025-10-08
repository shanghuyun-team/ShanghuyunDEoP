#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>

// =============== Wi-Fi initialization ==================
const char* ssid_seed     = "wenwen 的 S24 Ultra";
const char* password_seed = "********";

// ================== MQTT Settings ==================
const char* MQTTServer   = "broker.MQTTGO.io";
const int   MQTTPort     = 1883;
const char* MQTTUser     = "";
const char* MQTTPassword = "";

// MQTT Topics Configuration
const char* MQTT_WIFI_CMD = "ShangHuYun/DEoP/Sub/wifi/control";
const char* MQTT_LED_CMD  = "ShangHuYun/DEoP/Sub/led/control";
const char* MQTT_REC_CMD  = "ShangHuYun/DEoP/Sub/rec/control";

// Acknowledgment Topic
const char* MQTT_PUB_ACK  = "ShangHuYun/DEoP/Pub/ack";

// -- LED (NeoPixel) --
#define LED_PIN     6
#define NUM_PIXELS 16   // LED number

// -- REC (ISD1820) --
#define REC_PIN   3
#define PLAYL_PIN 7

// ================== Objects ==================
WiFiClient   WifiClient;
PubSubClient MQTTClient(WifiClient);
Preferences  prefs;
Adafruit_NeoPixel pixels(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ================== 全域變數與狀態 ==================
bool needReconnectApply = false;

// -- LED initialization state --
/*
    led_mode:
      "off"     - LED off
      "solid"  - Solid color (set by command)
      "rainbow" - Rainbow cycle
*/
String led_mode = "off"; 
unsigned long led_last_update = 0;
int rainbow_hue = 0;

// -- Wi-Fi NVS --
const char* PREF_NAMESPACE = "wifi_cfg";
const char* PREF_KEY       = "networks";
const uint8_t WIFI_MAX = 5;
const size_t JSON_CAPACITY =
  JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(WIFI_MAX) + WIFI_MAX * JSON_OBJECT_SIZE(2) + 256;


// ================== Declarations ==================
// -- MQTT & Wi-Fi --
void MQTTCallback(char* topic, byte* payload, unsigned int length);
void WifiConnecte();
void MQTTConnecte();
void publishAck(bool ok, const String& msg);

// -- Wi-Fi Tools --
void ensureSeedIfEmpty();
bool loadNetworks(String& jsonOut);
bool saveNetworks(const String& jsonIn);
bool addOrUpdateNetwork(const String& ssid, const String& password, String& reason);
bool deleteNetwork(const String& ssid);
bool clearNetworks();
void tryConnectSaved(uint32_t perAttemptTimeoutMs = 12000);
bool tryConnectOpenAP(uint32_t perAttemptTimeoutMs = 10000);

// -- LED Tools --
void led_update();
void rainbow_cycle(int wait_ms);


// ================== setup ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  // -- LED initialization --
  pixels.begin();
  pixels.clear();
  pixels.show();

  // -- REC initialization --
  pinMode(REC_PIN, OUTPUT);
  pinMode(PLAYL_PIN, OUTPUT);
  digitalWrite(REC_PIN, LOW);
  digitalWrite(PLAYL_PIN, LOW);

  // -- Wi-Fi initialization --
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  randomSeed(esp_random());
  prefs.begin(PREF_NAMESPACE, false);
  ensureSeedIfEmpty();
  WifiConnecte();

  // -- MQTT initialization --
  MQTTClient.setServer(MQTTServer, MQTTPort);
  MQTTClient.setCallback(MQTTCallback);
  MQTTConnecte();
}

// ================== loop ==================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    WifiConnecte();
  }
  if (!MQTTClient.connected()) {
    MQTTConnecte();
  }
  MQTTClient.loop();

  // -- 處理 Wi-Fi 清單重新套用指令 --
  if (needReconnectApply) {
    needReconnectApply = false;
    Serial.println("[APPLY] 重新套用 Wi-Fi 清單並嘗試重連");
    WiFi.disconnect(true, true);
    delay(300);
    WifiConnecte();
  }

  // -- 處理 LED 動態效果 --
  led_update();
}


// ================== 主要功能區 ==================

// ---------- MQTT Callback: 指令處理核心 ----------
void MQTTCallback(char* topic, byte* payload, unsigned int length) {
  String s; s.reserve(length);
  for (unsigned int i = 0; i < length; i++) s += (char)payload[i];

  Serial.print("收到: "); Serial.print(topic);
  Serial.print(" | "); Serial.println(s);

  DynamicJsonDocument d(512);
  auto err = deserializeJson(d, s);
  if (err) {
    publishAck(false, String("json_error: ") + err.c_str());
    return;
  }

  // ---------- Wi-Fi 指令處理 ----------
  if (strcmp(topic, MQTT_WIFI_CMD) == 0) {
    String action = d["action"] | "";
    action.toLowerCase();

    if (action == "add" || action == "set") {
      String ssid = d["ssid"] | "";
      String pwd  = d["password"] | "";
      String reason;
      bool ok = addOrUpdateNetwork(ssid, pwd, reason);
      publishAck(ok, reason);
      return;
    }
    if (action == "del") {
      String ssid = d["ssid"] | "";
      bool ok = deleteNetwork(ssid);
      publishAck(ok, ok ? "deleted" : "delete_failed");
      return;
    }
    if (action == "clear") {
      bool ok = clearNetworks();
      publishAck(ok, ok ? "cleared" : "clear_failed");
      return;
    }
    if (action == "list") {
      String json; loadNetworks(json);
      DynamicJsonDocument src(JSON_CAPACITY);
      deserializeJson(src, json);
      DynamicJsonDocument final_doc(JSON_CAPACITY + 128);
      final_doc["ok"] = true;
      JsonObject msg_obj = final_doc.createNestedObject("msg");
      msg_obj["networks"] = src["networks"];
      String final_out; serializeJson(final_doc, final_out);
      MQTTClient.publish(MQTT_PUB_ACK, final_out.c_str(), false);
      return;
    }
    if (action == "apply") {
      needReconnectApply = true;
      publishAck(true, "apply_requested");
      return;
    }
    publishAck(false, "wifi_unknown_action");
    return;
  }

  // ---------- LED 指令處理 ----------
  if (strcmp(topic, MQTT_LED_CMD) == 0) {
    String action = d["action"] | "";
    action.toLowerCase();

    if (action == "set_mode") {
      led_mode = d["mode"].as<String>();
      if (led_mode == "off") {
          pixels.clear();
          pixels.show();
      }
      publishAck(true, "led_mode_set_to_" + led_mode);
    } else if (action == "set_color") {
      led_mode = "solid";
      uint8_t r = d["r"] | 0;
      uint8_t g = d["g"] | 0;
      uint8_t b = d["b"] | 0;
      pixels.fill(pixels.Color(r, g, b));
      pixels.show();
      publishAck(true, "led_color_set");
    } else {
      publishAck(false, "led_unknown_action");
    }
    return;
  }

  // ---------- REC 指令處理 ----------
  if (strcmp(topic, MQTT_REC_CMD) == 0) {
    String action = d["action"] | "";
    String status = d["status"] | "";
    action.toLowerCase();
    status.toLowerCase();

    bool success = true;
    String msg = "";

    if (action == "record") {
      digitalWrite(REC_PIN, (status == "start") ? HIGH : LOW);
      msg = (status == "start") ? "record_started" : "record_stopped";
    } else if (action == "playback") {
      digitalWrite(PLAYL_PIN, (status == "start") ? HIGH : LOW);
      msg = (status == "start") ? "playback_started" : "playback_stopped";
    } else {
      success = false;
      msg = "rec_unknown_action";
    }
    publishAck(success, msg);
    return;
  }
}

// ---------- LED 動態效果 ----------

// Input a value 0 to 255 to get a color value.
// The colours are a transition r - g - b - back to r.
uint32_t ColorWheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if(WheelPos < 85) {
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if(WheelPos < 170) {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void led_update() {
  if (led_mode == "rainbow") {
    if (millis() - led_last_update > 50) { // 每 50ms 更新一次
      led_last_update = millis();
      rainbow_cycle(20);
    }
  }
}

void rainbow_cycle(int wait_ms) {
  for(int i=0; i< NUM_PIXELS; i++) {
    pixels.setPixelColor(i, ColorWheel(((i * 256 / NUM_PIXELS) + rainbow_hue) & 255));
  }
  pixels.show();
  rainbow_hue++;
  if (rainbow_hue >= 256*5) { // 讓彩虹轉幾圈
    rainbow_hue = 0;
  }
}


// ================== 連線管理 (Wi-Fi & MQTT) ==================

void publishAck(bool ok, const String& msg) {
  DynamicJsonDocument doc(256);
  doc["ok"] = ok;
  doc["msg"] = msg;
  String out; serializeJson(doc, out);
  MQTTClient.publish(MQTT_PUB_ACK, out.c_str(), false);
}

void MQTTConnecte() {
  uint8_t attempt = 0;
  while (!MQTTClient.connected() && WiFi.status() == WL_CONNECTED) {
    String clientId = "esp32-full-" + String((uint32_t)esp_random(), HEX);
    Serial.print("連線到 MQTT...clientId=");
    Serial.println(clientId);

    if (MQTTClient.connect(clientId.c_str(), MQTTUser, MQTTPassword)) {
      Serial.println("MQTT 已連線");
      MQTTClient.subscribe(MQTT_WIFI_CMD);
      MQTTClient.subscribe(MQTT_LED_CMD);
      MQTTClient.subscribe(MQTT_REC_CMD);
      publishAck(true, "mqtt_connected");
      break;
    } else {
      Serial.print("MQTT 連線失敗, 狀態碼=");
      Serial.println(MQTTClient.state());
      uint32_t backoff = 1000UL << min((uint8_t)2, attempt++);
      Serial.printf("等待 %lu ms 後重試\n", backoff);
      delay(backoff);
    }
  }
}

void WifiConnecte() {
  tryConnectSaved(12000);
  if (WiFi.status() == WL_CONNECTED) return;
  WiFi.disconnect(true, false);
  delay(150);
  if (tryConnectOpenAP(10000)) return;
  Serial.println("[WiFi] 無法連線已儲存或開放式網路");
}

void tryConnectSaved(uint32_t perAttemptTimeoutMs) {
  String json;
  if (!loadNetworks(json)) {
    Serial.println("[WiFi] 載入清單失敗，改用種子");
    WiFi.begin(ssid_seed, password_seed);
    uint32_t start = millis(); while(millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) { Serial.print("\n[WiFi] 種子連線成功，IP: "); Serial.println(WiFi.localIP()); }
    return;
  }

  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) {
    Serial.println("[WiFi] JSON 解析失敗，改用種子");
    WiFi.begin(ssid_seed, password_seed);
    uint32_t start = millis(); while(millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) { Serial.print("\n[WiFi] 種子連線成功，IP: "); Serial.println(WiFi.localIP()); }
    return;
  }

  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    Serial.println("[WiFi] 清單為空，改用種子");
    WiFi.begin(ssid_seed, password_seed);
    uint32_t start = millis(); while(millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) { Serial.print("\n[WiFi] 種子連線成功，IP: "); Serial.println(WiFi.localIP()); }
    return;
  }

  Serial.printf("[WiFi] 將依序嘗試 %u 組\n", arr.size());
  for (JsonObject net : arr) {
    const char* s = net["ssid"] | "";
    const char* p = net["password"] | "";
    if (!s || strlen(s) == 0) continue;

    Serial.print("[WiFi] 連線到: "); Serial.println(s);
    WiFi.begin(s, p);
    uint32_t start = millis(); while(millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 連線成功，IP: "); Serial.println(WiFi.localIP());
      return;
    } else {
      Serial.println("[WiFi] 逾時，嘗試下一組");
    }
  }
  Serial.println("[WiFi] 所有已儲存網路皆無法連線");
}

bool tryConnectOpenAP(uint32_t perAttemptTimeoutMs) {
  WiFi.disconnect(true, true); delay(200);
  WiFi.scanDelete(); delay(50);
  Serial.println("[WiFi] 掃描可用網路（尋找開放式 AP）...");
  int n = -1;
  for (int attempt = 0; attempt < 3; ++attempt) {
    n = WiFi.scanNetworks(false, false, false, 300);
    if (n >= 0) break;
    delay(200);
  }
  if (n <= 0) { Serial.printf("[WiFi] 掃描結果異常/為 0（n=%d）\n", n); return false; }

  struct OpenAP { String ssid; int32_t rssi; };
  OpenAP openAPs[20];
  uint8_t cnt = 0;
  for (int i = 0; i < n && cnt < 20; ++i) {
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
      openAPs[cnt++] = {WiFi.SSID(i), WiFi.RSSI(i)};
    }
  }
  if (cnt == 0) { Serial.println("[WiFi] 沒有開放式 AP"); return false; }

  std::sort(openAPs, openAPs + cnt, [](const OpenAP& a, const OpenAP& b){ return a.rssi > b.rssi; });

  for (uint8_t i = 0; i < cnt; ++i) {
    Serial.printf("[WiFi] 嘗試開放式 AP: %s (RSSI=%d)\n", openAPs[i].ssid.c_str(), openAPs[i].rssi);
    WiFi.begin(openAPs[i].ssid.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < perAttemptTimeoutMs) { delay(500); Serial.print("."); }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 已連線開放式 AP，IP: "); Serial.println(WiFi.localIP());
      return true;
    }
  }
  Serial.println("[WiFi] 開放式 AP 連線失敗");
  return false;
}


// ================== NVS / JSON Wi-Fi 工具函式 ==================

void ensureSeedIfEmpty() {
  if (!prefs.isKey(PREF_KEY) || prefs.getString(PREF_KEY, "").length() < 15) {
    DynamicJsonDocument d(JSON_CAPACITY);
    JsonArray arr = d.createNestedArray("networks");
    JsonObject n = arr.createNestedObject();
    n["ssid"] = ssid_seed;
    n["password"] = password_seed;
    String out; serializeJson(d, out);
    prefs.putString(PREF_KEY, out);
    Serial.println("[NVS] 以初始 SSID 建立 Wi-Fi 清單");
  }
}

bool loadNetworks(String& jsonOut) {
  jsonOut = prefs.getString(PREF_KEY, "{\"networks\":[]}");
  return true;
}

bool saveNetworks(const String& jsonIn) {
  bool res = prefs.putString(PREF_KEY, jsonIn) > 0;
  if(res) needReconnectApply = true;
  return res;
}

bool addOrUpdateNetwork(const String& ssid, const String& password, String& reason) {
  if (ssid.length() == 0) { reason = "empty_ssid"; return false; }
  String json; loadNetworks(json);
  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) { reason = "json_parse_failed"; return false; }
  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull()) arr = doc.createNestedArray("networks");

  for (JsonObject net : arr) {
    if (ssid.equals(net["ssid"] | "")) {
      net["password"] = password;
      String out; serializeJson(doc, out);
      reason = saveNetworks(out) ? "updated" : "save_failed";
      return reason == "updated";
    }
  }

  if (arr.size() >= WIFI_MAX) { 
    if(!clearNetworks()) return false;
  }

  JsonObject n = arr.createNestedObject();
  n["ssid"] = ssid;
  n["password"] = password;
  String out; serializeJson(doc, out);
  reason = saveNetworks(out) ? "added" : "save_failed";
  return reason == "added";
}

bool deleteNetwork(const String& ssid) {
  if (ssid.length() == 0) return false;
  String json; loadNetworks(json);
  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) return false;
  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull()) return false;
  for (size_t i = 0; i < arr.size(); ++i) {
    if (ssid.equals(arr[i]["ssid"] | "")) {
      arr.remove(i);
      String out; serializeJson(doc, out);
      return saveNetworks(out);
    }
  }
  return false;
}

bool clearNetworks() {
  return saveNetworks("{\"networks\":[]}");
}