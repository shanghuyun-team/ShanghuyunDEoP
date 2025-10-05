package com.example.myapplication

object Topics {
    var prefix: String = "ShangHuYun/DEoP"

    val WIFI_CTRL: String get() = "$prefix/Sub/wifi/control"
    val LED_CTRL:  String get() = "$prefix/Sub/led/control"
    val REC_CTRL:  String get() = "$prefix/Sub/rec/control"
    val ACK_PUB:   String get() = "$prefix/Pub/ack"
}