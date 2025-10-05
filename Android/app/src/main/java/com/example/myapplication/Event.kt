package com.example.myapplication

/** 一次性事件容器，避免 LiveData 反覆重放 */
class Event<out T>(private val content: T) {
    private var handled = false
    fun getIfNotHandled(): T? = if (handled) null else { handled = true; content }
}
