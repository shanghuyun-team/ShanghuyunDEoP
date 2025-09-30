import paho.mqtt.client as mqtt
import json
import time
import sys

# --- 組態設定 ---
MQTT_BROKER = "broker.MQTTGO.io"
MQTT_PORT = 1883

# ESP32 程式碼中定義的 Topics
TOPIC_WIFI_CONTROL = "ShangHuYun/DEoP/Sub/wifi/control"
TOPIC_LED_CONTROL = "ShangHuYun/DEoP/Sub/led/control"
TOPIC_REC_CONTROL = "ShangHuYun/DEoP/Sub/rec/control"
TOPIC_ACK = "ShangHuYun/DEoP/Pub/ack"

# --- MQTT 客戶端回呼函式 ---

def on_connect(client, userdata, flags, rc):
    """當客戶端連線到 broker 時的回呼函式"""
    if rc == 0:
        print("成功連線到 MQTT Broker!")
        # 訂閱 ACK 主題以接收 ESP32 的回應
        client.subscribe(TOPIC_ACK)
        print(f"已訂閱 ACK 主題: {TOPIC_ACK}")
    else:
        print(f"連線失敗, return code {rc}\n")
        sys.exit(1)

def on_message(client, userdata, msg):
    """當從 broker 收到訊息時的回呼函式"""
    print(f"\n收到 ACK on topic '{msg.topic}':")
    try:
        # 嘗試將 payload 解析為 JSON 以便美化輸出
        payload = json.loads(msg.payload.decode())
        # 檢查 msg 是否為巢狀 JSON 字串 (針對舊版 list 的回傳格式)
        if isinstance(payload.get('msg'), str):
            try:
                # 嘗試再次解析 msg 欄位
                payload['msg'] = json.loads(payload['msg'])
            except (json.JSONDecodeError, TypeError):
                pass # 如果無法解析，則保留原樣
        print(json.dumps(payload, indent=2, ensure_ascii=False))
    except json.JSONDecodeError:
        # 如果不是 JSON，直接印出原始 payload
        print(msg.payload.decode())
    print("-" * 20)

def publish_command(client, topic, payload):
    """發布指令並印出的輔助函式"""
    payload_str = json.dumps(payload)
    print(f"\n>>> 發布到 '{topic}':")
    print(payload_str)
    client.publish(topic, payload_str)
    time.sleep(3) # 等待設備回應

# --- 主測試邏輯 ---

if __name__ == "__main__":
    # 建立並設定客戶端
    client = mqtt.Client()
    client.on_connect = on_connect
    client.on_message = on_message

    # 連線到 broker
    try:
        client.connect(MQTT_BROKER, MQTT_PORT, 60)
    except Exception as e:
        print(f"連線 MQTT broker 時發生錯誤: {e}")
        sys.exit(1)

    # 在背景執行緒中啟動網路迴圈
    client.loop_start()

    # 給客戶端一點時間連線和訂閱
    time.sleep(2)

    print("\n======= 開始 ESP32 全功能測試 =======\n")

    # --- 1. LED 控制測試 ---
    print("\n--- 測試 LED 控制 ---")
    publish_command(client, TOPIC_LED_CONTROL, {"action": "set_color", "r": 255, "g": 0, "b": 0})
    publish_command(client, TOPIC_LED_CONTROL, {"action": "set_color", "r": 0, "g": 255, "b": 0})
    publish_command(client, TOPIC_LED_CONTROL, {"action": "set_mode", "mode": "rainbow"})
    print("...彩虹模式運行 5 秒...")
    time.sleep(5)
    publish_command(client, TOPIC_LED_CONTROL, {"action": "set_mode", "mode": "off"})

    # --- 2. REC 控制測試 ---
    print("\n--- 測試 REC 控制 ---")
    publish_command(client, TOPIC_REC_CONTROL, {"action": "record", "status": "start"})
    print("...錄音 5 秒...")
    time.sleep(5)
    publish_command(client, TOPIC_REC_CONTROL, {"action": "record", "status": "stop"})
    time.sleep(1) # 等待錄音結束
    publish_command(client, TOPIC_REC_CONTROL, {"action": "playback", "status": "start"})
    print("...播放 5 秒...")
    time.sleep(5)
    publish_command(client, TOPIC_REC_CONTROL, {"action": "playback", "status": "stop"})

    # --- 3. Wi-Fi 控制測試 ---
    print("\n--- 測試 Wi-Fi 控制 ---")
    publish_command(client, TOPIC_WIFI_CONTROL, {"action": "list"})
    publish_command(client, TOPIC_WIFI_CONTROL, {"action": "add", "ssid": "test_wifi", "password": "test_password"})
    publish_command(client, TOPIC_WIFI_CONTROL, {"action": "list"})
    publish_command(client, TOPIC_WIFI_CONTROL, {"action": "del", "ssid": "test_wifi"})
    publish_command(client, TOPIC_WIFI_CONTROL, {"action": "list"})

    print("\n======= 測試序列完成 =======")