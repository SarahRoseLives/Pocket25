package com.example.dsd_flutter_example

import android.app.Activity
import android.content.Intent
import android.content.pm.PackageManager
import android.net.Uri
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val RTL_TCP_CHANNEL = "pocket25/rtl_tcp"
    private val RTL_TCP_REQUEST_CODE = 1234
    private var pendingResult: MethodChannel.Result? = null

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, RTL_TCP_CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "isDriverInstalled" -> {
                    result.success(isRtlTcpDriverInstalled())
                }
                "startDriver" -> {
                    val port = call.argument<Int>("port") ?: 1234
                    val sampleRate = call.argument<Int>("sampleRate") ?: 2400000
                    val frequency = call.argument<Int>("frequency") ?: 771181250
                    val gain = call.argument<Int>("gain") ?: 48
                    val ppm = call.argument<Int>("ppm") ?: 0
                    
                    pendingResult = result
                    startRtlTcpDriver(port, sampleRate, frequency, gain, ppm)
                }
                "stopDriver" -> {
                    // The driver stops when we disconnect or when we send a close command
                    result.success(null)
                }
                else -> {
                    result.notImplemented()
                }
            }
        }
    }

    private fun isRtlTcpDriverInstalled(): Boolean {
        return try {
            // Check if the rtl_tcp_andro package is installed
            packageManager.getPackageInfo("marto.rtl_tcp_andro", 0)
            true
        } catch (e: Exception) {
            false
        }
    }

    private fun startRtlTcpDriver(port: Int, sampleRate: Int, frequency: Int, gain: Int, ppm: Int) {
        try {
            // Build the iqsrc URI with rtl_tcp compatible arguments
            // -a address, -p port, -s sample rate, -f frequency, -g gain, -P ppm
            val uri = "iqsrc://-a 127.0.0.1 -p $port -s $sampleRate -f $frequency -g $gain -P $ppm"
            val intent = Intent(Intent.ACTION_VIEW).setData(Uri.parse(uri))
            startActivityForResult(intent, RTL_TCP_REQUEST_CODE)
        } catch (e: Exception) {
            pendingResult?.error("DRIVER_ERROR", "Failed to start RTL-TCP driver: ${e.message}", null)
            pendingResult = null
        }
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        
        if (requestCode == RTL_TCP_REQUEST_CODE) {
            if (resultCode == Activity.RESULT_OK) {
                // Driver started successfully
                val supportedCommands = data?.getIntArrayExtra("supportedTcpCommands")
                pendingResult?.success(true)
            } else {
                // Driver failed to start
                val errorMsg = data?.getStringExtra("detailed_exception_message") ?: "Unknown error"
                pendingResult?.error("DRIVER_ERROR", errorMsg, null)
            }
            pendingResult = null
        }
    }
}
