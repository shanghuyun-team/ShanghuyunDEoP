package com.example.myapplication.ui

import android.os.Bundle
import android.view.View
import android.widget.Button
import android.widget.ProgressBar
import android.widget.SeekBar
import android.widget.TextView
import androidx.fragment.app.Fragment
import androidx.fragment.app.activityViewModels
import com.example.myapplication.MainViewModel
import com.example.myapplication.R

class LedFragment : Fragment(R.layout.fragment_led) {

    private val vm: MainViewModel by activityViewModels()

    override fun onViewCreated(view: View, savedInstanceState: Bundle?) {
        super.onViewCreated(view, savedInstanceState)

        val sbR = view.findViewById<SeekBar>(R.id.sbR)
        val sbG = view.findViewById<SeekBar>(R.id.sbG)
        val sbB = view.findViewById<SeekBar>(R.id.sbB)
        val btnSetColor = view.findViewById<Button>(R.id.btnSetColor)
        val btnRainbow = view.findViewById<Button>(R.id.btnRainbow)
        val btnOff = view.findViewById<Button>(R.id.btnOff)
        val tvAck = view.findViewById<TextView>(R.id.tvAck)
        val tvStatus = view.findViewById<TextView>(R.id.tvStatus)
        val progressAck = view.findViewById<ProgressBar>(R.id.progressAck)

        val colorPreview = view.findViewById<View>(R.id.viewColorPreview)

        // 即時更新顏色預覽
        val listener = object : SeekBar.OnSeekBarChangeListener {
            override fun onProgressChanged(seekBar: SeekBar?, progress: Int, fromUser: Boolean) {
                val r = sbR.progress
                val g = sbG.progress
                val b = sbB.progress
                val color = android.graphics.Color.rgb(r, g, b)
                colorPreview.setBackgroundColor(color)
            }

            override fun onStartTrackingTouch(seekBar: SeekBar?) {}
            override fun onStopTrackingTouch(seekBar: SeekBar?) {}
        }

        sbR.setOnSeekBarChangeListener(listener)
        sbG.setOnSeekBarChangeListener(listener)
        sbB.setOnSeekBarChangeListener(listener)

        btnSetColor.setOnClickListener {
            vm.ledSetColor(sbR.progress, sbG.progress, sbB.progress)
        }
        btnRainbow.setOnClickListener { vm.ledSetMode("rainbow") }
        btnOff.setOnClickListener { vm.ledSetMode("off") }

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
