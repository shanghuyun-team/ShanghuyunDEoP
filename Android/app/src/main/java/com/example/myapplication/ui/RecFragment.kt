package com.example.myapplication.ui

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.myapplication.MainViewModel
import com.example.myapplication.R

class RecFragment : Fragment(R.layout.fragment_rec) {

    private val vm: MainViewModel by activityViewModels()

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val btnRecStart = view.findViewById<Button>(R.id.btnRecStart)
        val btnRecStop = view.findViewById<Button>(R.id.btnRecStop)
        val etCount = view.findViewById<EditText>(R.id.etCount)
        val etInterval = view.findViewById<EditText>(R.id.etInterval)
        val btnPlayStart = view.findViewById<Button>(R.id.btnPlayStart)
        val btnPlayStop = view.findViewById<Button>(R.id.btnPlayStop)
        val tvAck = view.findViewById<TextView>(R.id.tvAck)
        val tvStatus = view.findViewById<TextView>(R.id.tvStatus)
        val progressAck = view.findViewById<ProgressBar>(R.id.progressAck)

        btnRecStart.setOnClickListener { vm.recRecord(true) }
        btnRecStop.setOnClickListener { vm.recRecord(false) }

        btnPlayStart.setOnClickListener {
            val count = etCount.text.toString().toIntOrNull()
            val interval = etInterval.text.toString().toIntOrNull()
            val safeInterval = interval?.let { maxOf(250, it) } // 下限 250ms
            vm.recPlaybackStart(count, safeInterval)
        }
        btnPlayStop.setOnClickListener { vm.recPlaybackStop() }

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

    }
}
