# 🧠 ESP32 Full-Function MQTT Controller

這個專案將多項 ESP32 功能整合於一體，並透過 MQTT 進行遠端控制。  
它示範了如何結合 **Wi‑Fi 管理**、**周邊設備控制（LED、ISD1820 錄音模組）**，以及 **結構化的 MQTT 指令集**。

---

## ✨ 主要功能

- ✅ **統一 MQTT 控制**：所有功能皆透過一致的 MQTT 指令格式操作。  
- 📡 **動態 Wi‑Fi 管理**：可遠端新增、刪除、清除、列出 Wi‑Fi，不需重新燒錄；若沒有可用 Wi‑Fi，會自動搜尋開放熱點連線。  
- 🌈 **RGB LED 控制**（NeoPixel）：  
  - 設定單一顏色  
  - 啟用彩虹特效模式  
  - 關閉 LED  
- 🎙 **ISD1820 語音模組控制**：  
  - 開始／停止錄音  
  - **播放一次、播放多次或無限循環播放錄音（支援 `count` / `interval_ms`）**  
- 🔁 **統一回應**：所有指令都會回傳一則 ACK JSON 到固定主題  
- 🧪 **Python 測試腳本**：可快速驗證所有功能（`mqtt_test.py`）

---

## 📡 MQTT Topic 一覽表

| 功能模組         | 訂閱指令 Topic                             | 發布回應 Topic             |
|------------------|---------------------------------------------|----------------------------|
| Wi‑Fi 控制       | `ShangHuYun/DEoP/Sub/wifi/control`          | `ShangHuYun/DEoP/Pub/ack` |
| LED 控制         | `ShangHuYun/DEoP/Sub/led/control`           | `ShangHuYun/DEoP/Pub/ack` |
| 錄音 / 播放控制  | `ShangHuYun/DEoP/Sub/rec/control`           | `ShangHuYun/DEoP/Pub/ack` |

所有指令執行結果（成功 / 失敗）都會以 JSON 格式回傳到 `ShangHuYun/DEoP/Pub/ack`：

```json
{
  "ok": true,
  "msg": "some_status_message"
}
```

---

## 📶 Wi‑Fi 控制指令

**Topic**：`ShangHuYun/DEoP/Sub/wifi/control`

### `list` ‑ 取得已存 Wi‑Fi 列表

**Payload**
```json
{ "action": "list" }
```

**ACK 成功回應**
```json
{
  "ok": true,
  "msg": {
    "networks": [
      { "ssid": "MyHomeWiFi" },
      { "ssid": "OfficeWiFi" }
    ]
  }
}
```

---

### `add` / `set` ‑ 新增或更新 Wi‑Fi

- `add`：新增 Wi‑Fi。若 SSID 已存在則更新密碼。  
- `set`：只更新現有 Wi‑Fi 的密碼，若不存在則失敗。

**Payload**
```json
{
  "action": "add",
  "ssid": "MyNewWiFi",
  "password": "new_password_123"
}
```

**ACK 回應**
- `{"ok": true, "msg": "added"}`  
- `{"ok": true, "msg": "updated"}`  
- `{"ok": false, "msg": "full"}`  
- `{"ok": false, "msg": "not_found"}`（僅 `set` 可能發生）

---

### `del` ‑ 刪除 Wi‑Fi 設定

**Payload**
```json
{ "action": "del", "ssid": "WiFi_to_Delete" }
```

**ACK 回應**：`{"ok": true, "msg": "deleted"}`

---

### `clear` ‑ 清除所有 Wi‑Fi 設定

**Payload**
```json
{ "action": "clear" }
```

**ACK 回應**：`{"ok": true, "msg": "cleared"}`

---

### `apply` ‑ 套用 Wi‑Fi 設定（立即重連）

**Payload**
```json
{ "action": "apply" }
```

**ACK 回應**：`{"ok": true, "msg": "apply_requested"}`

---

## 💡 LED 控制指令

**Topic**：`ShangHuYun/DEoP/Sub/led/control`

### `set_color` ‑ 設定純色
```json
{ "action": "set_color", "r": 255, "g": 100, "b": 0 }
```

**ACK**：`{"ok": true, "msg": "led_color_set"}`

### `set_mode` ‑ 設定模式
```json
{ "action": "set_mode", "mode": "rainbow" }
```
**可用模式**：`"solid"`, `"rainbow"`, `"off"`

**ACK**：`{"ok": true, "msg": "led_mode_set_to_rainbow"}`

---

## 🔊 錄音 / 播放控制指令

**Topic**：`ShangHuYun/DEoP/Sub/rec/control`

### `record` ‑ 錄音控制

開始錄音：
```json
{ "action": "record", "status": "start" }
```

停止錄音：
```json
{ "action": "record", "status": "stop" }
```

**ACK**
- `{"ok": true, "msg": "record_started"}`  
- `{"ok": true, "msg": "record_stopped"}`

---

### `playback` ‑ 播放控制（支援次數與間隔）

| 欄位           | 型別 | 意義 |
|----------------|------|------|
| `count`        | int  | `0` 或省略＝無限循環；`1`＝播一次；`N>1`＝播 N 次 |
| `interval_ms`  | int  | 兩次播放的間隔，預設 `10000` ms，最小 `250` ms |

**只播一次**
```json
{ "action": "playback", "status": "start", "count": 1 }
```

**播放 5 次，每次間隔 3 秒**
```json
{ "action": "playback", "status": "start", "count": 5, "interval_ms": 3000 }
```

**無限循環播放**
```json
{ "action": "playback", "status": "start" }
```

**停止播放**
```json
{ "action": "playback", "status": "stop" }
```

**ACK 範例**
```json
{ "ok": true, "msg": "playback_loop_started_infinite" }
```
```json
{ "ok": true, "msg": "playback_completed" }
```

---

## 🔧 連線設定

- **MQTT Broker**：`broker.MQTTGO.io`（Port `1883`）  
- **Topics**：  
  - Wi‑Fi 指令：`ShangHuYun/DEoP/Sub/wifi/control`  
  - LED 指令：`ShangHuYun/DEoP/Sub/led/control`  
  - 錄音/播放指令：`ShangHuYun/DEoP/Sub/rec/control`  
  - ACK 回應：`ShangHuYun/DEoP/Pub/ack`  

> 你可依環境變數或編譯常數更換 Broker、Topic 前綴。

---

## 🧪 測試腳本（選用）

提供 `mqtt_test.py` 範例，用於快速測試各功能（以 `paho-mqtt` 為例）。  
若需要我幫你自動產生測試腳本，請告訴我你的 Broker 帳密與欲測試的 Topic 前綴。

---

## 🛠️ 硬體腳位（預設）

- **NeoPixel**：`LED_PIN = 6`，`NUM_PIXELS = 16`  
- **ISD1820**：`REC_PIN = 3`，`PLAYE_PIN = 7`（沿觸發，高→低短脈衝）

> 如需變更，請同步更新程式中對應的 `#define` 常數。

---

## ⚠️ 注意事項

- `interval_ms` 設下限 `250ms`，避免過短造成觸發不穩或音訊黏連。  
- `PLAYE_PIN` 觸發脈衝預設 `50ms`，若你的模組需要不同脈寬可自行調整。  
- 無線環境不佳時，請適度放寬 Wi‑Fi 連線逾時或重試間隔。

---

## 📜 授權

MIT License
