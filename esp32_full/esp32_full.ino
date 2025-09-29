#include <WiFi.h>
#include <PubSubClient.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <algorithm> // Required for std::sort

// ================== Initial Wi-Fi Credentials (Seed) ==================
const char* ssid_seed     = "wenwen 的 S24 Ultra"; // Fallback Wi-Fi SSID
const char* password_seed = "********";            // Fallback Wi-Fi Password

// ================== MQTT Settings ==================
const char* MQTTServer   = "broker.MQTTGO.io";
const int   MQTTPort     = 1883;
const char* MQTTUser     = "";
const char* MQTTPassword = "";

// Command Topics
const char* MQTT_WIFI_CMD = "ShangHuYun/DEoP/Sub/wifi/control";
const char* MQTT_LED_CMD  = "ShangHuYun/DEoP/Sub/led/control";
const char* MQTT_REC_CMD  = "ShangHuYun/DEoP/Sub/rec/control";

// Response Topic
const char* MQTT_PUB_ACK  = "ShangHuYun/DEoP/Pub/ack";

// ================== Hardware Definitions ==================
// -- LED (NeoPixel) --
#define LED_PIN 6
#define NUM_PIXELS 16

// -- REC (ISD1820) --
#define REC_PIN   3
#define PLAYE_PIN 7 // Connected to PLAYE (edge-trigger)

#define PLAYBACK_INTERVAL_MS 10000 // Default 10 seconds interval

// ================== Objects ==================
WiFiClient   WifiClient;
PubSubClient MQTTClient(WifiClient);
Preferences  prefs;
Adafruit_NeoPixel pixels(NUM_PIXELS, LED_PIN, NEO_GRB + NEO_KHZ800);

// ================== Global Variables & States ==================
bool needReconnectApply = false;

// -- LED State --
String led_mode = "off";
unsigned long led_last_update = 0;
int rainbow_hue = 0;

// -- REC Software Loop State (count-controlled or infinite) --
bool isPlaybackLooping = false;
bool playbackInfinite = false;
uint32_t playbackRemaining = 0;           // >0 means remaining plays for finite mode
unsigned long lastPlaybackTriggerTime = 0;
unsigned long playbackIntervalMs = 10000; // default 10s; can be overridden via MQTT

// -- Wi-Fi NVS --
const char* PREF_NAMESPACE = "wifi_cfg";
const char* PREF_KEY       = "networks";
const uint8_t WIFI_MAX = 5; // Max number of Wi-Fi networks to store
const size_t JSON_CAPACITY =
  JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(WIFI_MAX) + WIFI_MAX * JSON_OBJECT_SIZE(2) + 256;

// ================== Function Declarations ==================
// -- MQTT & Wi-Fi --
void MQTTCallback(char* topic, byte* payload, unsigned int length);
void WifiConnecte();
void MQTTConnecte();
void publishAck(bool ok, const String& msg);

// -- Wi-Fi Tools --
void ensureSeedIfEmpty();
bool loadNetworks(String& jsonOut);
bool saveNetworks(const String& jsonIn);
bool addOrUpdateNetwork(const String& ssid, const String& password, bool updateOnly, String& reason);
bool deleteNetwork(const String& ssid);
bool clearNetworks();
void tryConnectSaved(uint32_t perAttemptTimeoutMs = 12000);
bool tryConnectOpenAP(uint32_t perAttemptTimeoutMs = 10000);

// -- LED Tools --
void led_update();
void rainbow_cycle(int wait_ms);
uint32_t ColorWheel(byte WheelPos);

// ================== setup ==================
void setup() {
  Serial.begin(115200);
  delay(200);

  // -- LED Initialization --
  pixels.begin();
  pixels.clear();
  pixels.show();

  // -- REC Initialization --
  pinMode(REC_PIN, OUTPUT);
  pinMode(PLAYE_PIN, OUTPUT);
  digitalWrite(REC_PIN, LOW);
  digitalWrite(PLAYE_PIN, LOW);

  // -- Wi-Fi Initialization --
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  
  
  randomSeed(esp_random());
  prefs.begin(PREF_NAMESPACE, false);
  ensureSeedIfEmpty();
  WifiConnecte();

  // -- MQTT Initialization --
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

  // -- Handle Wi-Fi reconnect/apply command --
  if (needReconnectApply) {
    needReconnectApply = false;
    Serial.println("[APPLY] Re-applying Wi-Fi list and attempting to reconnect...");
    WiFi.disconnect(true, true);
    delay(300);
    WifiConnecte();
  }

  // -- Handle dynamic LED effects --
  led_update();

  // -- Handle REC software loop playback (count-controlled / infinite) --
  if (isPlaybackLooping) {
    if (millis() - lastPlaybackTriggerTime > playbackIntervalMs) {
      // Trigger one playback pulse on PLAYE (edge-triggered)
      Serial.println("[REC] Trigger playback (loop)...");
      digitalWrite(PLAYE_PIN, HIGH);
      delay(50); // Short pulse is enough
      digitalWrite(PLAYE_PIN, LOW);
      lastPlaybackTriggerTime = millis();

      if (!playbackInfinite) {
        if (playbackRemaining > 0) {
          playbackRemaining--;
        }
        if (playbackRemaining == 0) {
          // Finished finite sequence
          isPlaybackLooping = false;
          publishAck(true, "playback_completed");
          Serial.println("[REC] Playback finished (count reached).");
        }
      }
    }
  }
}

// ================== Main Functionality ==================

// ---------- MQTT Callback: Core Command Handler ----------
void MQTTCallback(char* topic, byte* payload, unsigned int length) {
  String s; s.reserve(length);
  for (unsigned int i = 0; i < length; i++) s += (char)payload[i];

  Serial.print("Received on topic: "); Serial.print(topic);
  Serial.print(" | Payload: "); Serial.println(s);

  DynamicJsonDocument d(512);
  auto err = deserializeJson(d, s);
  if (err) {
    publishAck(false, String("json_error: ") + err.c_str());
    return;
  }

  // ---------- Wi-Fi Command Handling ----------
  if (strcmp(topic, MQTT_WIFI_CMD) == 0) {
    String action = d["action"] | "";
    action.toLowerCase();

    if (action == "add" || action == "set") {
      String ssid = d["ssid"] | "";
      String pwd  = d["password"] | "";
      bool updateOnly = (action == "set");
      String reason;
      bool ok = addOrUpdateNetwork(ssid, pwd, updateOnly, reason);
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

  // ---------- LED Command Handling ----------
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

  // ---------- REC Command Handling ----------
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
      if (status == "start") {
        // Optional parameters: count, interval_ms
        // count: 0 or omitted -> infinite; 1 -> once; N>1 -> N times
        uint32_t count = d["count"] | 0;
        uint32_t interval = d["interval_ms"] | playbackIntervalMs;
        if (interval < 250) interval = 250; // safety lower bound

        playbackIntervalMs = interval;

        if (count == 0) {
          playbackInfinite = true;
          playbackRemaining = 0;
        } else {
          playbackInfinite = false;
          playbackRemaining = count; // will decrement after first immediate trigger
        }

        // Start loop and trigger first playback immediately
        isPlaybackLooping = true;

        Serial.printf("[REC] Start playback: %s, interval=%lu ms, count=%lu\n",
                      playbackInfinite ? "infinite" : "finite",
                      (unsigned long)playbackIntervalMs,
                      (unsigned long)(playbackInfinite ? 0 : playbackRemaining));

        // First immediate trigger
        digitalWrite(PLAYE_PIN, HIGH);
        delay(50);
        digitalWrite(PLAYE_PIN, LOW);
        lastPlaybackTriggerTime = millis();

        if (!playbackInfinite) {
          if (playbackRemaining > 0) playbackRemaining--;
          if (playbackRemaining == 0) {
            // count == 1 -> single shot
            isPlaybackLooping = false;
            msg = "playback_once_done";
            publishAck(true, msg);
            return;
          }
        }

        msg = playbackInfinite ? "playback_loop_started_infinite" : "playback_loop_started";
      } else if (status == "stop") {
        isPlaybackLooping = false;
        playbackInfinite = false;
        playbackRemaining = 0;
        msg = "playback_loop_stopped";
      } else {
        success = false;
        msg = "rec_unknown_status";
      }
    } else {
      success = false;
      msg = "rec_unknown_action";
    }
    publishAck(success, msg);
    return;
  }
}

// ---------- LED Dynamic Effects ----------
uint32_t ColorWheel(byte WheelPos) {
  WheelPos = 255 - WheelPos;
  if (WheelPos < 85) {
    return pixels.Color(255 - WheelPos * 3, 0, WheelPos * 3);
  }
  if (WheelPos < 170) {
    WheelPos -= 85;
    return pixels.Color(0, WheelPos * 3, 255 - WheelPos * 3);
  }
  WheelPos -= 170;
  return pixels.Color(WheelPos * 3, 255 - WheelPos * 3, 0);
}

void led_update() {
  if (led_mode == "rainbow") {
    if (millis() - led_last_update > 50) { // Update every 50ms
      led_last_update = millis();
      rainbow_cycle(20);
    }
  }
}

void rainbow_cycle(int wait_ms) {
  for (int i = 0; i < NUM_PIXELS; i++) {
    pixels.setPixelColor(i, ColorWheel(((i * 256 / NUM_PIXELS) + rainbow_hue) & 255));
  }
  pixels.show();
  rainbow_hue++;
  if (rainbow_hue >= 256 * 5) { // Let the rainbow cycle a few times
    rainbow_hue = 0;
  }
}

// ================== Connection Management (Wi-Fi & MQTT) ==================
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
    Serial.print("Connecting to MQTT... clientId=");
    Serial.println(clientId);

    if (MQTTClient.connect(clientId.c_str(), MQTTUser, MQTTPassword)) {
      Serial.println("MQTT Connected.");
      MQTTClient.subscribe(MQTT_WIFI_CMD);
      MQTTClient.subscribe(MQTT_LED_CMD);
      MQTTClient.subscribe(MQTT_REC_CMD);
      publishAck(true, "mqtt_connected");
      break;
    } else {
      Serial.print("MQTT connection failed, rc=");
      Serial.println(MQTTClient.state());
      uint32_t backoff = 1000UL << std::min<uint8_t>(2, attempt++);
      Serial.printf("Retrying in %lu ms\n", backoff);
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
  Serial.println("[WiFi] Could not connect to any saved or open networks.");
}

void tryConnectSaved(uint32_t perAttemptTimeoutMs) {
  String json;
  if (!loadNetworks(json)) {
    Serial.println("[WiFi] Failed to load network list, trying seed credentials...");
    WiFi.begin(ssid_seed, password_seed);
    uint32_t start = millis(); while (millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) { Serial.print("\n[WiFi] Connected with seed credentials. IP: "); Serial.println(WiFi.localIP()); }
    return;
  }

  DynamicJsonDocument doc(JSON_CAPACITY);
  if (deserializeJson(doc, json)) {
    Serial.println("[WiFi] JSON parse failed, trying seed credentials...");
    WiFi.begin(ssid_seed, password_seed);
    uint32_t start = millis(); while (millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) { Serial.print("\n[WiFi] Connected with seed credentials. IP: "); Serial.println(WiFi.localIP()); }
    return;
  }

  JsonArray arr = doc["networks"].as<JsonArray>();
  if (arr.isNull() || arr.size() == 0) {
    Serial.println("[WiFi] Network list is empty, trying seed credentials...");
    WiFi.begin(ssid_seed, password_seed);
    uint32_t start = millis(); while (millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    if (WiFi.status() == WL_CONNECTED) { Serial.print("\n[WiFi] Connected with seed credentials. IP: "); Serial.println(WiFi.localIP()); }
    return;
  }

  Serial.printf("[WiFi] Trying %u saved network(s)...\n", arr.size());
  for (JsonObject net : arr) {
    const char* s = net["ssid"] | "";
    const char* p = net["password"] | "";
    if (!s || strlen(s) == 0) continue;

    Serial.print("[WiFi] Connecting to: "); Serial.println(s);
    WiFi.begin(s, p);
    uint32_t start = millis(); while (millis() - start < perAttemptTimeoutMs && WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] Connection successful. IP: "); Serial.println(WiFi.localIP());
      return;
    } else {
      Serial.println("[WiFi] Timeout. Trying next network...");
    }
  }
  Serial.println("[WiFi] All saved networks failed to connect.");
}

bool tryConnectOpenAP(uint32_t perAttemptTimeoutMs) {
  WiFi.disconnect(true, true); delay(200);
  WiFi.scanDelete(); delay(50);
  Serial.println("[WiFi] Scanning for available networks (looking for open APs)...");
  int n = -1;
  for (int attempt = 0; attempt < 3; ++attempt) {
    n = WiFi.scanNetworks(false, false, false, 300);
    if (n >= 0) break;
    delay(200);
  }
  if (n <= 0) { Serial.printf("[WiFi] Scan failed or returned 0 networks (n=%d)\n", n); return false; }

  struct OpenAP { String ssid; int32_t rssi; };
  OpenAP openAPs[20];
  uint8_t cnt = 0;
  for (int i = 0; i < n && cnt < 20; ++i) {
    if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) {
      openAPs[cnt++] = {WiFi.SSID(i), WiFi.RSSI(i)};
    }
  }
  if (cnt == 0) { Serial.println("[WiFi] No open access points found."); return false; }

  std::sort(openAPs, openAPs + cnt, [](const OpenAP& a, const OpenAP& b){ return a.rssi > b.rssi; });

  for (uint8_t i = 0; i < cnt; ++i) {
    Serial.printf("[WiFi] Trying open AP: %s (RSSI=%d)\n", openAPs[i].ssid.c_str(), openAPs[i].rssi);
    WiFi.begin(openAPs[i].ssid.c_str());
    uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < perAttemptTimeoutMs) { delay(500); Serial.print("."); }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      Serial.print("[WiFi] Connected to open AP. IP: "); Serial.println(WiFi.localIP());
      return true;
    }
  }
  Serial.println("[WiFi] Failed to connect to any open APs.");
  return false;
}

// ================== NVS / JSON Wi-Fi Utility Functions ==================
void ensureSeedIfEmpty() {
  if (!prefs.isKey(PREF_KEY) || prefs.getString(PREF_KEY, "").length() < 15) {
    DynamicJsonDocument d(JSON_CAPACITY);
    JsonArray arr = d.createNestedArray("networks");
    JsonObject n = arr.createNestedObject();
    n["ssid"] = ssid_seed;
    n["password"] = password_seed;
    String out; serializeJson(d, out);
    prefs.putString(PREF_KEY, out);
    Serial.println("[NVS] Wi-Fi list is empty. Initializing with seed credentials.");
  }
}

bool loadNetworks(String& jsonOut) {
  jsonOut = prefs.getString(PREF_KEY, "{\"networks\":[]}");
  return true;
}

bool saveNetworks(const String& jsonIn) {
  return prefs.putString(PREF_KEY, jsonIn) > 0;
}

bool addOrUpdateNetwork(const String& ssid, const String& password, bool updateOnly, String& reason) {
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

  if (updateOnly) { reason = "not_found"; return false; }
  if (arr.size() >= WIFI_MAX) { reason = "full"; return false; }

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
