package com.example.myapplication

import android.content.Context
import androidx.lifecycle.LiveData
import androidx.lifecycle.MutableLiveData
import info.mqtt.android.service.MqttAndroidClient
import org.eclipse.paho.client.mqttv3.DisconnectedBufferOptions
import org.eclipse.paho.client.mqttv3.IMqttActionListener
import org.eclipse.paho.client.mqttv3.IMqttDeliveryToken
import org.eclipse.paho.client.mqttv3.IMqttToken
import org.eclipse.paho.client.mqttv3.MqttCallbackExtended
import org.eclipse.paho.client.mqttv3.MqttConnectOptions
import org.eclipse.paho.client.mqttv3.MqttMessage
import java.util.UUID

class MqttService(private val context: Context) {

    data class Config(
        val host: String = "tcp://broker.mqttgo.io:1883", // 或 ssl://...:8883
        val clientId: String = "AndroidClient-" + UUID.randomUUID().toString().takeLast(8),
        val username: String? = null,
        val password: String? = null,
        val cleanSession: Boolean = true,
        val keepAliveSec: Int = 30
    )

    private var client: MqttAndroidClient? = null
    private var cfg: Config = Config()

    private val _isConnected = MutableLiveData(false)
    val isConnected: LiveData<Boolean> = _isConnected

    /** 任一訊息抵達都會丟 (topic to payload) */
    private val _incoming = MutableLiveData<Pair<String, String>>()
    val incoming: LiveData<Pair<String, String>> = _incoming

    fun applyConfig(newCfg: Config) { cfg = newCfg }

    fun connect(onResult: (Boolean, String?) -> Unit = { _, _ -> }) {
        // 若已有 client，先清掉
        try { client?.unregisterResources() } catch (_: Exception) {}
        try { client?.close() } catch (_: Exception) {}

        // ✅ 三參數建構子（沒有 Ack）
        client = MqttAndroidClient(context, cfg.host, cfg.clientId)

        // ✅ 你這版的 registerResources() 不收參數
        client?.registerResources()

        val options = MqttConnectOptions().apply {
            isAutomaticReconnect = true
            isCleanSession = cfg.cleanSession
            keepAliveInterval = cfg.keepAliveSec
            cfg.username?.let { userName = it }
            cfg.password?.let { password = it.toCharArray() }
        }

        // ✅ 用 MqttCallbackExtended 拿 connectComplete（含自動重連）
        client?.setCallback(object : MqttCallbackExtended {
            override fun connectComplete(reconnect: Boolean, serverURI: String?) {
                _isConnected.postValue(true)
                // 重連或首次連上都重新訂閱（只訂 ACK_PUB）
                subscribe(Topics.ACK_PUB) { _, _ -> }
            }
            override fun messageArrived(topic: String?, message: MqttMessage?) {
                if (topic != null) _incoming.postValue(topic to (message?.toString() ?: ""))
            }
            override fun connectionLost(cause: Throwable?) { _isConnected.postValue(false) }
            override fun deliveryComplete(token: IMqttDeliveryToken?) { /* no-op */ }
        })

        client?.connect(options, null, object : IMqttActionListener {
            override fun onSuccess(asyncActionToken: IMqttToken?) {
                _isConnected.postValue(true)

                // ✅ 離線緩衝，避免暫斷時 publish 直接丟失
                val bufferOpts = DisconnectedBufferOptions().apply {
                    isBufferEnabled = true
                    bufferSize = 100
                    isPersistBuffer = false
                    isDeleteOldestMessages = true
                }
                client?.setBufferOpts(bufferOpts)

                // 首次連線成功先訂一次（與 connectComplete 互為保險）
                subscribe(Topics.ACK_PUB) { ok, err -> onResult(ok, err) }
            }
            override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                _isConnected.postValue(false)
                onResult(false, exception?.message)
            }
        })
    }

    fun subscribe(
        topic: String,
        qos: Int = 1,
        onResult: (Boolean, String?) -> Unit = { _, _ -> }
    ) {
        val c = client ?: return onResult(false, "client 尚未建立")
        c.subscribe(topic, qos, null, object : IMqttActionListener {
            override fun onSuccess(asyncActionToken: IMqttToken?) { onResult(true, null) }
            override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                onResult(false, exception?.message)
            }
        })
    }

    fun publish(
        topic: String,
        payload: String,
        qos: Int = 1,
        retained: Boolean = false,
        onResult: (Boolean, String?) -> Unit = { _, _ -> }
    ) {
        val c = client ?: return onResult(false, "client 尚未建立")
        // 只在連線時才發送（你也可改成允許離線緩衝）
        if (c.isConnected != true) return onResult(false, "尚未連線")

        val msg = MqttMessage(payload.toByteArray()).apply {
            this.qos = qos
            isRetained = retained
        }

        c.publish(topic, msg, null, object : IMqttActionListener {
            override fun onSuccess(asyncActionToken: IMqttToken?) {
                onResult(true, null)
            }

            override fun onFailure(asyncActionToken: IMqttToken?, exception: Throwable?) {
                onResult(false, exception?.message)
            }
        })
    }


    fun disconnect() {
        try { client?.disconnectForcibly(500, 200) } catch (_: Exception) {}
        _isConnected.postValue(false)
    }
}
