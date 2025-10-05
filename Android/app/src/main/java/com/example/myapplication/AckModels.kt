package com.example.myapplication

import kotlinx.serialization.Serializable

@Serializable
data class AckMsg<T>(
    val ok: Boolean,
    val msg: T? = null
)

@Serializable
data class WifiList(
    val networks: List<WifiItem> = emptyList()
)

@Serializable
data class WifiItem(val ssid: String)
