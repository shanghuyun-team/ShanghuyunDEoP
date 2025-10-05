package com.example.myapplication

import kotlinx.serialization.json.Json
import kotlinx.serialization.json.buildJsonObject
import kotlinx.serialization.json.put

/**
 * 把你的 MQTT 指令（Wi-Fi / LED / REC）封裝成簡單方法，內部統一 publish JSON。
 */
class MqttRepository(private val svc: MqttService) {

    val isConnected get() = svc.isConnected
    val incoming get() = svc.incoming

    fun connect(cfg: MqttService.Config, cb: (Boolean, String?) -> Unit) {
        svc.applyConfig(cfg)
        svc.connect(cb)
    }

    // ---------- Wi-Fi ----------
    fun wifiList() = publishJson(Topics.WIFI_CTRL) { put("action", "list") }

    fun wifiAdd(ssid: String, pwd: String) = publishJson(Topics.WIFI_CTRL) {
        put("action", "add"); put("ssid", ssid); put("password", pwd)
    }

    fun wifiSet(ssid: String, pwd: String) = publishJson(Topics.WIFI_CTRL) {
        put("action", "set"); put("ssid", ssid); put("password", pwd)
    }

    fun wifiDel(ssid: String) = publishJson(Topics.WIFI_CTRL) {
        put("action", "del"); put("ssid", ssid)
    }

    fun wifiClear() = publishJson(Topics.WIFI_CTRL) { put("action", "clear") }
    fun wifiApply() = publishJson(Topics.WIFI_CTRL) { put("action", "apply") }

    // ---------- LED ----------
    fun ledSetColor(r: Int, g: Int, b: Int) = publishJson(Topics.LED_CTRL) {
        put("action", "set_color"); put("r", r); put("g", g); put("b", b)
    }

    fun ledSetMode(mode: String) = publishJson(Topics.LED_CTRL) {
        put("action", "set_mode"); put("mode", mode) // solid / rainbow / off
    }

    // ---------- REC ----------
    fun recRecord(start: Boolean) = publishJson(Topics.REC_CTRL) {
        put("action", "record"); put("status", if (start) "start" else "stop")
    }

    fun recPlaybackStart(count: Int? = null, intervalMs: Int? = null) = publishJson(Topics.REC_CTRL) {
        put("action", "playback"); put("status", "start")
        count?.let { put("count", it) }
        intervalMs?.let { put("interval_ms", it) }
    }

    fun recPlaybackStop() = publishJson(Topics.REC_CTRL) {
        put("action", "playback"); put("status", "stop")
    }

    // ---------- 共用 ----------
    private fun publishJson(topic: String, builder: kotlinx.serialization.json.JsonObjectBuilder.() -> Unit) {
        val payload = buildJsonObject(builder).toString()
        svc.publish(topic, payload)
    }
}
