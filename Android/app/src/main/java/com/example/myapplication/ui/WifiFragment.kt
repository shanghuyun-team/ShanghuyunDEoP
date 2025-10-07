package com.example.myapplication.ui

import android.os.Bundle
import android.view.View
import android.widget.*
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.myapplication.MainViewModel
import com.example.myapplication.R
import com.example.myapplication.Prefs
import com.example.myapplication.MqttUiConfig

class WifiFragment : Fragment(R.layout.fragment_wifi) {

    // 共用 MainViewModel（由 MainActivity 提供）
    private val vm: MainViewModel by activityViewModels()

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val etHost = view.findViewById<EditText>(R.id.etHost)
        val etUser = view.findViewById<EditText>(R.id.etUser)
        val etPass = view.findViewById<EditText>(R.id.etPass)
        val etPrefix = view.findViewById<EditText>(R.id.etPrefix)
        val btnConnect = view.findViewById<Button>(R.id.btnConnect)

        val etSsid = view.findViewById<EditText>(R.id.etSsid)
        val etPwd = view.findViewById<EditText>(R.id.etPwd)
        val etDelSsid = view.findViewById<EditText>(R.id.etDelSsid)

        val btnList = view.findViewById<Button>(R.id.btnList)
        val btnSet = view.findViewById<Button>(R.id.btnSet)
        val btnDel = view.findViewById<Button>(R.id.btnDel)
        val btnClear = view.findViewById<Button>(R.id.btnClear)
        val tvAck = view.findViewById<TextView>(R.id.tvAck)
        val tvList = view.findViewById<TextView>(R.id.tvList)
        val tvStatus = view.findViewById<TextView>(R.id.tvStatus)
        val progressAck = view.findViewById<ProgressBar>(R.id.progressAck)

        val prefs = Prefs(requireContext())
        val saved = prefs.load()
        etHost.setText(saved.host)
        etUser.setText(saved.user ?: "")
        etPass.setText(saved.pass ?: "")
        etPrefix.setText(saved.prefix)

        // 連線設定
        btnConnect.setOnClickListener {
            val host = etHost.text.toString()
            val user = etUser.text.toString()
            val pass = etPass.text.toString()
            val prefix = etPrefix.text.toString()

            vm.connect(host = host, user = user, pass = pass, prefix = prefix)

            prefs.save(MqttUiConfig(host, user, pass, prefix))
        }

        // Wi-Fi 指令操作
        btnList.setOnClickListener { vm.wifiList() }
        btnSet.setOnClickListener { vm.wifiSet(etSsid.text.toString(), etPwd.text.toString()) }
        btnDel.setOnClickListener { vm.wifiDel(etDelSsid.text.toString()) }
        btnClear.setOnClickListener { vm.wifiClear() }

        // 觀察 ACK 與 Wi-Fi 列表
        // 狀態列
        vm.statusText.observe(viewLifecycleOwner) { tvStatus.text = it }

        // 等待圈圈
        vm.isWaiting.observe(viewLifecycleOwner) { waiting ->
            progressAck.visibility = if (waiting) View.VISIBLE else View.GONE
            if (waiting) tvAck.text = "等待 ACK…" else tvAck.text = ""
        }

        // 收到 ACK
        vm.snackbar.observe(viewLifecycleOwner) { event ->
            event.getIfNotHandled()?.let { msg ->
                tvAck.text = msg
                // 或顯示 Snackbar：Snackbar.make(view, msg, Snackbar.LENGTH_SHORT).show()
            }
        }

        vm.wifiList.observe(viewLifecycleOwner) { list ->
            tvList.text = list.joinToString("\n") { "• $it" }
        }
    }
}
