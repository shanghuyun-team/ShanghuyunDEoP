package com.example.myapplication

import android.content.Context

data class MqttUiConfig(
    val host: String,
    val user: String?,
    val pass: String?,
    val prefix: String
)

class Prefs(ctx: Context) {
    private val sp = ctx.getSharedPreferences("mqtt_prefs", Context.MODE_PRIVATE)

    fun save(cfg: MqttUiConfig) {
        sp.edit()
            .putString("host", cfg.host)
            .putString("user", cfg.user)
            .putString("pass", cfg.pass)
            .putString("prefix", cfg.prefix)
            .apply()
    }

    fun load(): MqttUiConfig = MqttUiConfig(
        host   = sp.getString("host", AppDefaults.DEFAULT_HOST)!!,
        user   = sp.getString("user", AppDefaults.DEFAULT_USERNAME),
        pass   = sp.getString("pass", AppDefaults.DEFAULT_PASSWORD),
        prefix = sp.getString("prefix", AppDefaults.DEFAULT_PREFIX)!!
    )
}
