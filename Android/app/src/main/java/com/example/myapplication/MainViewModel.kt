package com.example.myapplication

import androidx.lifecycle.*
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.JsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonArray
import kotlinx.coroutines.*
import kotlinx.serialization.json.booleanOrNull
import kotlinx.serialization.json.contentOrNull

class MainViewModel(private val repo: MqttRepository) : ViewModel() {

    val isConnected = repo.isConnected

    private val _isWaiting = MutableLiveData(false)
    val isWaiting: LiveData<Boolean> = _isWaiting

    private val _statusText = MutableLiveData("未連線")
    val statusText: LiveData<String> = _statusText

    private val _snackbar = MutableLiveData<Event<String>>()
    val snackbar: LiveData<Event<String>> = _snackbar

    private val _wifiList = MutableLiveData<List<String>>(emptyList())
    val wifiList: LiveData<List<String>> = _wifiList

    private var waitJob: Job? = null

    init {
        // 連線狀態字串
        isConnected.observeForever { ok ->
            _statusText.postValue(if (ok) "已連線" else "未連線")
            if (!ok) _isWaiting.postValue(false)
        }

        // 統一處理 ACK → 結束等待、精簡顯示
        repo.incoming.observeForever { (topic, payload) ->
            if (topic == Topics.ACK_PUB) {
                _isWaiting.postValue(false)
                waitJob?.cancel()

                // 解析精簡訊息
                val brief = ackBrief(payload)
                _snackbar.postValue(Event(brief))

                // 若是 Wi-Fi list，另外更新清單
                try {
                    val parsed = Json { ignoreUnknownKeys = true }
                        .decodeFromString(AckMsg.serializer(WifiList.serializer()), payload)
                    parsed.msg?.let { wl ->
                        _wifiList.postValue(wl.networks.map { it.ssid })
                    }
                } catch (_: Exception) { /* 不是 list 就略過 */ }
            }
        }
    }

    /** 把 {"ok":..., "msg":...} 縮成短句；msg 若是物件或陣列就只顯示類型 */
    private fun ackBrief(raw: String): String {
        return try {
            val el = Json.parseToJsonElement(raw)
            val ok = el.jsonObject["ok"]?.jsonPrimitive?.booleanOrNull ?: false
            val msgEl = el.jsonObject["msg"]
            val msg: String = when (msgEl) {
                null -> ""
                is JsonObject -> "(資料)"
                else -> if (msgEl.toString().startsWith("[")) "(清單)" else msgEl.jsonPrimitive.contentOrNull ?: ""
            }
            if (ok) "成功${if (msg.isNotBlank()) "：$msg" else ""}"
            else "失敗${if (msg.isNotBlank()) "：$msg" else ""}"
        } catch (_: Exception) {
            // 不是標準 JSON 就原樣做為提示
            raw.take(120)
        }
    }

    /** 包一層「等待 ACK + 逾時」 */
    private fun waitAck(timeoutMs: Long = 5000) {
        _isWaiting.postValue(true)
        waitJob?.cancel()
        waitJob = viewModelScope.launch {
            delay(timeoutMs)
            if (isActive) {
                _isWaiting.postValue(false)
                _snackbar.postValue(Event("未收到 ACK（逾時）"))
            }
        }
    }

    fun connect(host: String, user: String?, pass: String?, prefix: String) {
        Topics.prefix = prefix.ifBlank { "ShangHuYun/DEoP" }
        val cfg = MqttService.Config(
            host = host.ifBlank { "tcp://broker.mqttgo.io:1883" },
            username = user?.ifBlank { null },
            password = pass?.ifBlank { null }
        )
        repo.connect(cfg) { ok, err ->
            if (!ok) _snackbar.postValue(Event("連線失敗：${err ?: "未知錯誤"}"))
        }
    }

    // ---- 下列方法：送出指令前先進入等待狀態 ----
    fun wifiList() { waitAck(); repo.wifiList() }
    fun wifiAdd(ssid: String, pwd: String) { waitAck(); repo.wifiAdd(ssid, pwd) }
    fun wifiSet(ssid: String, pwd: String) { waitAck(); repo.wifiSet(ssid, pwd) }
    fun wifiDel(ssid: String) { waitAck(); repo.wifiDel(ssid) }
    fun wifiClear() { waitAck(); repo.wifiClear() }
    fun wifiApply() { waitAck(); repo.wifiApply() }

    fun ledSetColor(r: Int, g: Int, b: Int) { waitAck(); repo.ledSetColor(r, g, b) }
    fun ledSetMode(mode: String) { waitAck(); repo.ledSetMode(mode) }

    fun recRecord(start: Boolean) { waitAck(); repo.recRecord(start) }
    fun recPlaybackStart(count: Int?, intervalMs: Int?) { waitAck(); repo.recPlaybackStart(count, intervalMs) }
    fun recPlaybackStop() { waitAck(); repo.recPlaybackStop() }
}
