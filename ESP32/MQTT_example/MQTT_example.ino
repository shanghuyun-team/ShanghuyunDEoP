#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>

// ================== 初始 Wi-Fi（種子） ==================
const char* ssid_seed     = "wenwen 的 S24 Ultra";
const char* password_seed = "********";

// ================== MQTT ==================
const char* MQTTServer   = "broker.MQTTGO.io";
const int   MQTTPort     = 1883;
const char* MQTTUser     = "";
const char* MQTTPassword = "";

// 節流示範（原樣保留）
const char* MQTTPubTopic1 = "ShangHuYun/DEoP/Pub/test1";
const char* MQTTPubTopic2 = "ShangHuYun/DEoP/Pub/test2";

// ★ 單一 Sub Topic + action
const char* MQTT_WIFI_CMD = "ShangHuYun/DEoP/Sub/wifi";

// ★ Wi-Fi 回覆統一前綴
const char* MQTT_PUB_WIFI_ACK  = "ShangHuYun/DEoP/Pub/wifi/ack";

// ================== 物件 ==================
WiFiClient   WifiClient;
PubSubClient MQTTClient(WifiClient);
Preferences  prefs;

// ================== 常數與狀態 ==================
unsigned long lastPub = 0;
const unsigned long PUB_INTERVAL_MS = 3000;

const char* PREF_NAMESPACE = "wifi_cfg";
const char* PREF_KEY       = "networks";

// ★ 最多 5 組
const uint8_t WIFI_MAX = 5;

// JSON 容量：最多 5 組、每組 2 欄位 + 邊際
const size_t JSON_CAPACITY =
  JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(WIFI_MAX) + WIFI_MAX * JSON_OBJECT_SIZE(2) + 256;

bool needReconnectApply = false;

// ---------- 前置宣告 ----------
void MQTTCallback(char* topic, byte* payload, unsigned int length);
void ensureSeedIfEmpty();
bool loadNetworks(String& jsonOut);
bool saveNetworks(const String& jsonIn);
uint8_t countNetworks();
bool addOrUpdateNetwork(const String& ssid, const String& password, bool updateOnly, String& reason);
bool deleteNetwork(const String& ssid);
bool clearNetworks();
void publishAck(bool ok, const String& msg);
void WifiConnecte();
void tryDelayDots(uint32_t ms);
void tryConnectSaved(uint32_t perAttemptTimeoutMs = 12000);
bool tryConnectOpenAP(uint32_t perAttemptTimeoutMs = 10000);
void MQTTConnecte();

// ================== setup ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_8_5dBm); // 如需更強可改 WIFI_POWER_19_5dBm（注意功耗/規範）

  randomSeed(esp_random());

  prefs.begin(PREF_NAMESPACE, false);
  ensureSeedIfEmpty();

  WifiConnecte();

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

  if (needReconnectApply) {
    needReconnectApply = false;
    Serial.println("[APPLY] 重新套用 Wi-Fi 清單並嘗試重連");
    WiFi.disconnect(true, true);
    delay(300);
    WifiConnecte();
  }

  unsigned long now = millis();
  if (now - lastPub >= PUB_INTERVAL_MS && MQTTClient.connected()) {
    lastPub = now;
    MQTTClient.publish(MQTTPubTopic1, "132113");
    MQTTClient.publish(MQTTPubTopic2, "132155");
  }
}

// ================== Wi-Fi 連線主流程 ==================
void WifiConnecte() {
  // 先逐一嘗試儲存的 Wi-Fi
  tryConnectSaved(12000);
  if (WiFi.status() == WL_CONNECTED) return;

  // 全失敗 → 先確定退出連線狀態，再掃描開放式 AP
  WiFi.disconnect(true, false);  // 斷線但保留 STA config
  delay(150);

  if (tryConnectOpenAP(10000)) return;

  Serial.println("[WiFi] 無法連線已儲存或開放式網路");
}

void tryDelayDots(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
}

void tryConnectSaved(uint32_t perAttemptTimeoutMs) {
  String json;
  if (!loadNetworks(json)) {
    Serial.println("[WiFi] 載入清單失敗，改用種子");
    WiFi.begin(ssid_seed, password_seed);
    tryDelayDots(perAttemptTimeoutMs);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 種子連線成功，IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  DynamicJsonDocument doc(JSON_CAPACITY);
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("[WiFi] JSON 解析失敗: %s，改用種子\n", err.c_str());
    WiFi.begin(ssid_seed, password_seed);
    tryDelayDots(perAttemptTimeoutMs);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 種子連線成功，IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    Serial.println("[WiFi] 清單為空，改用種子");
    WiFi.begin(ssid_seed, password_seed);
    tryDelayDots(perAttemptTimeoutMs);
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 種子連線成功，IP: ");
      Serial.println(WiFi.localIP());
    }
    return;
  }

  Serial.printf("[WiFi] 將依序嘗試 %u 組（上限 5）\n", arr.size());
  uint8_t tried = 0;
  for (JsonObject net : arr) {
    if (tried >= WIFI_MAX) break;
    const char* s = net["ssid"] | "";
    const char* p = net["password"] | "";
    if (!s || strlen(s) == 0) continue;

    Serial.print("[WiFi] 連線到: ");
    Serial.println(s);

    WiFi.begin(s, p);
    tryDelayDots(perAttemptTimeoutMs);

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 連線成功，IP: ");
      Serial.println(WiFi.localIP());
      return;
    } else {
      Serial.println("[WiFi] 逾時，嘗試下一組");
    }
    tried++;
  }
  Serial.println("[WiFi] 所有已儲存網路皆無法連線");
}

// ================== 開放式（無密碼）AP 掃描 + 連線（穩定版） ==================
bool tryConnectOpenAP(uint32_t perAttemptTimeoutMs) {
  // 1) 確保掃描前完全停止連線流程並清理舊結果
  WiFi.disconnect(true, true);   // 斷線且擦除 STA config（避免殘留）
  delay(200);
  WiFi.scanDelete();             // 刪掉上次掃描結果
  delay(50);

  Serial.println("[WiFi] 掃描可用網路（尋找開放式 AP）...");

  // 2) 改為主動掃描以提高發現率；最多重試 3 次
  int n = -1;
  for (int attempt = 0; attempt < 3; ++attempt) {
    n = WiFi.scanNetworks(false /*async*/, false /*show_hidden*/, false /*passive*/, 300 /*ms/chan*/);
    if (n >= 0) break;
    delay(200);
  }

  if (n <= 0) {
    Serial.printf("[WiFi] 掃描結果異常/為 0（n=%d）\n", n);
    return false;
  }

  // 3) 收集開放式 AP
  struct OpenAP { String ssid; int32_t rssi; };
  OpenAP openAPs[20];
  uint8_t cnt = 0;

  for (int i = 0; i < n; ++i) {
    wifi_auth_mode_t auth = WiFi.encryptionType(i);
    if (auth == WIFI_AUTH_OPEN) {
      if (cnt < 20) {
        openAPs[cnt].ssid = WiFi.SSID(i);
        openAPs[cnt].rssi = WiFi.RSSI(i);
        cnt++;
      }
    }
  }

  if (cnt == 0) {
    Serial.println("[WiFi] 沒有開放式 AP");
    return false;
  }

  // 4) 依 RSSI 由強到弱排序
  for (uint8_t i = 0; i + 1 < cnt; ++i) {
    for (uint8_t j = i + 1; j < cnt; ++j) {
      if (openAPs[j].rssi > openAPs[i].rssi) {
        OpenAP t = openAPs[i]; openAPs[i] = openAPs[j]; openAPs[j] = t;
      }
    }
  }

  // 5) 逐一嘗試連線開放式 AP
  for (uint8_t i = 0; i < cnt; ++i) {
    Serial.printf("[WiFi] 嘗試開放式 AP: %s (RSSI=%d)\n", openAPs[i].ssid.c_str(), openAPs[i].rssi);
    WiFi.begin(openAPs[i].ssid.c_str()); // 無密碼
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < perAttemptTimeoutMs) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] 已連線開放式 AP，IP: ");
      Serial.println(WiFi.localIP());
      return true;
    }
  }

  Serial.println("[WiFi] 開放式 AP 連線失敗");
  return false;
}

// ================== MQTT 連線 ==================
void MQTTConnecte() {
  uint8_t attempt = 0;
  while (!MQTTClient.connected() && WiFi.status() == WL_CONNECTED) {
    String clientId = "esp32-" + String((uint32_t)esp_random(), HEX);
    Serial.print("連線到 MQTT... clientId=");
    Serial.println(clientId);

    if (MQTTClient.connect(clientId.c_str(), MQTTUser, MQTTPassword)) {
      Serial.println("MQTT 已連線");

      // ★ 單一 Wi-Fi 指令 topic
      MQTTClient.subscribe(MQTT_WIFI_CMD);

      // 你的測試 topic（保留）
      MQTTClient.subscribe("ShangHuYun/DEoP/Sub/printtest");

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

// ================== NVS / JSON 工具 ==================
void ensureSeedIfEmpty() {
  String raw = prefs.getString(PREF_KEY, "");
  bool needSeed = true;

  if (raw.length()) {
    DynamicJsonDocument d(JSON_CAPACITY);
    if (!deserializeJson(d, raw)) {
      JsonArray arr = d["networks"].as<JsonArray>();
      if (!arr.isNull() && arr.size() > 0) {
        needSeed = false;
      }
    }
  }

  if (needSeed) {
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
  if (!prefs.isKey(PREF_KEY)) {
    jsonOut = "{\"networks\":[]}";
    return true;
  }
  jsonOut = prefs.getString(PREF_KEY, "{\"networks\":[]}");
  if (jsonOut.length() == 0) jsonOut = "{\"networks\":[]}";
  return true;
}

bool saveNetworks(const String& jsonIn) {
  return prefs.putString(PREF_KEY, jsonIn) > 0;
}

uint8_t countNetworks() {
  String json; loadNetworks(json);
  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) return 0;
  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull()) return 0;
  return (uint8_t)arr.size();
}

// updateOnly=true 時若不存在就失敗；false 則允許新增（但不超過 5 組）
bool addOrUpdateNetwork(const String& ssid, const String& password, bool updateOnly, String& reason) {
  if (ssid.length() == 0) { reason = "empty_ssid"; return false; }

  String json; loadNetworks(json);
  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) { reason = "json_parse_failed"; return false; }

  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull()) arr = doc.createNestedArray("networks");

  // 先找是否已存在 → 更新密碼
  for (JsonObject net : arr) {
    const char* s = net["ssid"] | "";
    if (ssid.equals(s)) {
      net["password"] = password;
      String out; serializeJson(doc, out);
      bool ok = saveNetworks(out);
      reason = ok ? "updated" : "save_failed";
      return ok;
    }
  }

  // 僅允許更新 & 不存在 → 失敗
  if (updateOnly) { reason = "not_found"; return false; }

  // 新增但超過上限 → 失敗
  if (arr.size() >= WIFI_MAX) { reason = "full"; return false; }

  JsonObject n = arr.createNestedObject();
  n["ssid"] = ssid;
  n["password"] = password;

  String out; serializeJson(doc, out);
  bool ok = saveNetworks(out);
  reason = ok ? "added" : "save_failed";
  return ok;
}

bool deleteNetwork(const String& ssid) {
  if (ssid.length() == 0) return false;
  String json; loadNetworks(json);
  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) return false;

  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull()) return false;

  for (size_t i = 0; i < arr.size(); ++i) {
    const char* s = arr[i]["ssid"] | "";
    if (ssid.equals(s)) {
      arr.remove(i);
      String out; serializeJson(doc, out);
      return saveNetworks(out);
    }
  }
  return false;
}

bool clearNetworks() {
  DynamicJsonDocument doc(JSON_CAPACITY);
  JsonArray arr = doc.createNestedArray("networks");
  (void)arr;
  String out; serializeJson(doc, out);
  return saveNetworks(out);
}

void publishAck(bool ok, const String& msg) {
  DynamicJsonDocument doc(256);
  doc["ok"] = ok;
  doc["msg"] = msg;
  String out; serializeJson(doc, out);
  MQTTClient.publish(MQTT_PUB_WIFI_ACK, out.c_str(), false);
}

// ================== MQTT Callback ==================
void MQTTCallback(char* topic, byte* payload, unsigned int length) {
  String s; s.reserve(length);
  for (unsigned int i = 0; i < length; i++) s += (char)payload[i];

  Serial.print("收到: "); Serial.print(topic);
  Serial.print(" | "); Serial.println(s);

  // 保留測試
  if (strcmp(topic, "ShangHuYun/DEoP/Sub/printtest") == 0) {
    Serial.println("→ printtest 指令已處理");
    return;
  }

  if (strcmp(topic, MQTT_WIFI_CMD) == 0) {
    DynamicJsonDocument d(384);
    auto err = deserializeJson(d, s);
    if (err) { publishAck(false, String("json_error: ") + err.c_str()); return; }

    String action = d["action"] | "";
    action.toLowerCase();

    if (action == "add" || action == "set") {
      String ssid = d["ssid"] | "";
      String pwd  = d["password"] | "";
      bool updateOnly = (action == "set");
      String reason;
      bool ok = addOrUpdateNetwork(ssid, pwd, updateOnly, reason);
      publishAck(ok, reason); // reason: added/updated/full/not_found/save_failed/empty_ssid/...
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
      if (deserializeJson(src, json)) {
        publishAck(false, "list_parse_fail");
        return;
      }

      DynamicJsonDocument final_doc(JSON_CAPACITY + 128);
      final_doc["ok"] = true;

      JsonArray arr = src["networks"].as<JsonArray>();
      JsonObject msg_obj = final_doc.createNestedObject("msg");
      JsonArray outArr = msg_obj.createNestedArray("networks");

      if (!arr.isNull()) {
        uint8_t cnt = 0;
        for (JsonObject net : arr) {
          if (cnt >= WIFI_MAX) break;
          JsonObject o = outArr.createNestedObject();
          o["ssid"] = net["ssid"].as<const char*>();
          cnt++;
        }
      }
      String final_out; serializeJson(final_doc, final_out);
      MQTTClient.publish(MQTT_PUB_WIFI_ACK, final_out.c_str(), false);
      return;
    }

    if (action == "apply") {
      needReconnectApply = true;
      publishAck(true, "apply_requested");
      return;
    }

    publishAck(false, "unknown_action");
    return;
  }
}
