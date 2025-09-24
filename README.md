# ESP32 Full-Function MQTT Controller

This project integrates multiple ESP32 functionalities into a single, remotely-controlled application via MQTT. It serves as a comprehensive example of combining Wi-Fi management, peripheral control (LEDs, Voice Module), and a robust command structure.

## Features

The `esp32_full` version includes the following features:

- **Unified MQTT Control:** All features are controlled via a structured MQTT command system.
- **Dynamic Wi-Fi Management:** Remotely add, delete, list, and manage saved Wi-Fi networks without needing to re-flash the device. The device can also connect to open (password-less) networks if no saved networks are available.
- **RGB LED Control:** Control an attached Adafruit NeoPixel strip.
  - Set any solid color.
  - Activate a dynamic rainbow cycle mode.
  - Turn the LEDs on and off.
- **Voice Module Control:** Control an ISD1820-based voice recording module.
  - Start and stop recording.
  - Start and stop playback of the recorded message.
- **Unified Feedback:** All commands provide a response on a single acknowledgment (`ack`) topic, indicating the success or failure of the operation.
- **Python Test Script:** A companion script (`mqtt_test.py`) is provided to test and verify all MQTT functionalities.

---

# MQTT 指令說明

本文檔記錄 ESP32 的 MQTT 指令格式與行為。

## 通用回應 (ACK)

所有指令執行後，無論成功或失敗，都會在以下 Topic 發布一則回應訊息，用以確認指令已被處理：

- **Topic:** `ShangHuYun/DEoP/Pub/ack`

回應的訊息格式為 JSON，包含兩個欄位：
- `ok` (boolean): `true` 表示指令成功，`false` 表示失敗。
- `msg` (string | object): 包含詳細的狀態訊息或指令結果。

---

## Wi-Fi 控制指令

- **Topic:** `ShangHuYun/DEoP/Sub/wifi/control`

### `list` - 取得已存 Wi-Fi 列表

**指令 Payload:**
```json
{
  "action": "list"
}
```

**回應 (`ack`):**
`msg` 欄位會是一個包含網路列表的 JSON 物件。

**成功範例:**
- **Payload:**
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
### `add` / `set` - 新增或更新 Wi-Fi

- `add`: 新增 Wi-Fi。若 SSID 已存在則更新密碼。
- `set`: 只更新現有 Wi-Fi 的密碼，若不存在則失敗。

**指令 Payload:**
```json
{
  "action": "add",
  "ssid": "MyNewWiFi",
  "password": "new_password_123"
}
```

**回應 (`ack`):**
- `{"ok": true, "msg": "added"}`
- `{"ok": true, "msg": "updated"}`
- `{"ok": false, "msg": "full"}`
- `{"ok": false, "msg": "not_found"}` (僅限 `set`)

---
### `del` - 刪除 Wi-Fi 設定

**指令 Payload:**
```json
{
  "action": "del",
  "ssid": "WiFi_to_Delete"
}
```

**回應 (`ack`):** `{"ok": true, "msg": "deleted"}`

---
### `clear` - 清除所有 Wi-Fi 設定

**指令 Payload:**
```json
{
  "action": "clear"
}
```

**回應 (`ack`):** `{"ok": true, "msg": "cleared"}`

---
### `apply` - 套用 Wi-Fi 設定

讓裝置立即斷線並重新連線。

**指令 Payload:**
```json
{
  "action": "apply"
}
```

**回應 (`ack`):** `{"ok": true, "msg": "apply_requested"}`

---

## LED 控制指令

- **Topic:** `ShangHuYun/DEoP/Sub/led/control`

### `set_color` - 設定純色

**指令 Payload:**
```json
{
  "action": "set_color",
  "r": 255,
  "g": 100,
  "b": 0
}
```

**回應 (`ack`):** `{"ok": true, "msg": "led_color_set"}`

### `set_mode` - 設定模式

**指令 Payload (彩虹模式):**
```json
{
  "action": "set_mode",
  "mode": "rainbow"
}
```

**指令 Payload (關閉):**
```json
{
  "action": "set_mode",
  "mode": "off"
}
```

**可用模式:** `solid`, `rainbow`, `off`

**回應 (`ack`):** `{"ok": true, "msg": "led_mode_set_to_rainbow"}`

---

## 錄音/播放控制指令

- **Topic:** `ShangHuYun/DEoP/Sub/rec/control`

### `record` - 錄音控制

**指令 Payload (開始):**
```json
{
  "action": "record",
  "status": "start"
}
```

**指令 Payload (停止):**
```json
{
  "action": "record",
  "status": "stop"
}
```

**回應 (`ack`):**
- `{"ok": true, "msg": "record_started"}`
- `{"ok": true, "msg": "record_stopped"}`

### `playback` - 播放控制

**指令 Payload (開始):**
```json
{
  "action": "playback",
  "status": "start"
}
```

**指令 Payload (停止):**
```json
{
  "action": "playback",
  "status": "stop"
}
```

**回應 (`ack`):**
- `{"ok": true, "msg": "playback_started"}`
- `{"ok": true, "msg": "playback_stopped"}`