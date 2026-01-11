package com.example.dsd_flutter_example

import android.app.Activity
import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.hardware.usb.UsbDevice
import android.hardware.usb.UsbDeviceConnection
import android.hardware.usb.UsbManager
import android.net.Uri
import android.os.Build
import android.util.Log
import io.flutter.embedding.android.FlutterActivity
import io.flutter.embedding.engine.FlutterEngine
import io.flutter.plugin.common.MethodChannel

class MainActivity : FlutterActivity() {
    private val TAG = "Pocket25-USB"
    private val RTL_TCP_CHANNEL = "pocket25/rtl_tcp"
    private val NATIVE_USB_CHANNEL = "pocket25/native_usb"
    private val RTL_TCP_REQUEST_CODE = 1234
    private val ACTION_USB_PERMISSION = "com.example.dsd_flutter_example.USB_PERMISSION"
    
    private var pendingResult: MethodChannel.Result? = null
    private var usbPermissionResult: MethodChannel.Result? = null
    private var pendingUsbDevice: UsbDevice? = null
    private var currentConnection: UsbDeviceConnection? = null
    
    // Known RTL-SDR Vendor/Product IDs
    private val rtlSdrDevices = listOf(
        Pair(0x0bda, 0x2832), // Generic RTL2832U
        Pair(0x0bda, 0x2838), // RTL-SDR Blog V3/V4, most common
        Pair(0x1f4d, 0xb803), // Afatech
        Pair(0x1f4d, 0xc803), // Afatech
        Pair(0x1b80, 0xd3a4), // Various
        Pair(0x1d19, 0x1101), // Dexatek
        Pair(0x1d19, 0x1102), // Dexatek
        Pair(0x1d19, 0x1103), // Dexatek
    )
    
    private val usbPermissionReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            if (ACTION_USB_PERMISSION == intent.action) {
                synchronized(this) {
                    val device: UsbDevice? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE, UsbDevice::class.java)
                    } else {
                        @Suppress("DEPRECATION")
                        intent.getParcelableExtra(UsbManager.EXTRA_DEVICE)
                    }
                    
                    if (intent.getBooleanExtra(UsbManager.EXTRA_PERMISSION_GRANTED, false)) {
                        device?.let {
                            Log.i(TAG, "USB permission granted for ${it.deviceName}")
                            openUsbDeviceWithPermission(it)
                        }
                    } else {
                        Log.e(TAG, "USB permission denied")
                        usbPermissionResult?.error("USB_PERMISSION_DENIED", "User denied USB permission", null)
                        usbPermissionResult = null
                        pendingUsbDevice = null
                    }
                }
            }
        }
    }

    override fun configureFlutterEngine(flutterEngine: FlutterEngine) {
        super.configureFlutterEngine(flutterEngine)
        
        // Register USB permission receiver
        val filter = IntentFilter(ACTION_USB_PERMISSION)
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            registerReceiver(usbPermissionReceiver, filter, Context.RECEIVER_NOT_EXPORTED)
        } else {
            registerReceiver(usbPermissionReceiver, filter)
        }
        
        // RTL-TCP driver channel (existing)
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
                    result.success(null)
                }
                else -> {
                    result.notImplemented()
                }
            }
        }
        
        // Native USB RTL-SDR channel (new)
        MethodChannel(flutterEngine.dartExecutor.binaryMessenger, NATIVE_USB_CHANNEL).setMethodCallHandler { call, result ->
            when (call.method) {
                "isUsbHostSupported" -> {
                    result.success(packageManager.hasSystemFeature(android.content.pm.PackageManager.FEATURE_USB_HOST))
                }
                "listRtlSdrDevices" -> {
                    result.success(listRtlSdrDevices())
                }
                "openDevice" -> {
                    val deviceName = call.argument<String>("deviceName")
                    if (deviceName != null) {
                        openRtlSdrDevice(deviceName, result)
                    } else {
                        result.error("INVALID_ARGS", "deviceName required", null)
                    }
                }
                "closeDevice" -> {
                    closeCurrentDevice()
                    result.success(null)
                }
                "getDeviceFd" -> {
                    val fd = currentConnection?.fileDescriptor ?: -1
                    result.success(fd)
                }
                else -> {
                    result.notImplemented()
                }
            }
        }
    }
    
    override fun onDestroy() {
        super.onDestroy()
        try {
            unregisterReceiver(usbPermissionReceiver)
        } catch (e: Exception) {
            // Receiver might not be registered
        }
        closeCurrentDevice()
    }

    private fun isRtlTcpDriverInstalled(): Boolean {
        return try {
            packageManager.getPackageInfo("marto.rtl_tcp_andro", 0)
            true
        } catch (e: Exception) {
            false
        }
    }

    private fun startRtlTcpDriver(port: Int, sampleRate: Int, frequency: Int, gain: Int, ppm: Int) {
        try {
            val uri = "iqsrc://-a 127.0.0.1 -p $port -s $sampleRate -f $frequency -g $gain -P $ppm"
            val intent = Intent(Intent.ACTION_VIEW).setData(Uri.parse(uri))
            startActivityForResult(intent, RTL_TCP_REQUEST_CODE)
        } catch (e: Exception) {
            pendingResult?.error("DRIVER_ERROR", "Failed to start RTL-TCP driver: ${e.message}", null)
            pendingResult = null
        }
    }
    
    private fun listRtlSdrDevices(): List<Map<String, Any>> {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val devices = mutableListOf<Map<String, Any>>()
        
        for (device in usbManager.deviceList.values) {
            if (isRtlSdrDevice(device)) {
                devices.add(mapOf(
                    "deviceName" to device.deviceName,
                    "vendorId" to device.vendorId,
                    "productId" to device.productId,
                    "productName" to (device.productName ?: "RTL-SDR"),
                    "manufacturerName" to (device.manufacturerName ?: "Unknown"),
                    "hasPermission" to usbManager.hasPermission(device)
                ))
            }
        }
        
        return devices
    }
    
    private fun isRtlSdrDevice(device: UsbDevice): Boolean {
        return rtlSdrDevices.any { (vid, pid) ->
            device.vendorId == vid && device.productId == pid
        }
    }
    
    private fun openRtlSdrDevice(deviceName: String, result: MethodChannel.Result) {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        val device = usbManager.deviceList[deviceName]
        
        if (device == null) {
            result.error("DEVICE_NOT_FOUND", "USB device not found: $deviceName", null)
            return
        }
        
        if (!isRtlSdrDevice(device)) {
            result.error("NOT_RTLSDR", "Device is not an RTL-SDR", null)
            return
        }
        
        if (usbManager.hasPermission(device)) {
            openUsbDeviceWithPermission(device)
            result.success(mapOf(
                "fd" to (currentConnection?.fileDescriptor ?: -1),
                "devicePath" to device.deviceName
            ))
        } else {
            // Request permission
            pendingUsbDevice = device
            usbPermissionResult = result
            
            val permissionIntent = PendingIntent.getBroadcast(
                this, 0, Intent(ACTION_USB_PERMISSION),
                PendingIntent.FLAG_MUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
            )
            usbManager.requestPermission(device, permissionIntent)
            Log.i(TAG, "Requesting USB permission for ${device.deviceName}")
        }
    }
    
    private fun openUsbDeviceWithPermission(device: UsbDevice) {
        val usbManager = getSystemService(Context.USB_SERVICE) as UsbManager
        
        closeCurrentDevice()
        
        currentConnection = usbManager.openDevice(device)
        if (currentConnection != null) {
            Log.i(TAG, "Opened USB device: ${device.deviceName}, fd=${currentConnection?.fileDescriptor}")
            
            // If this was a permission callback, send success
            usbPermissionResult?.success(mapOf(
                "fd" to (currentConnection?.fileDescriptor ?: -1),
                "devicePath" to device.deviceName
            ))
            usbPermissionResult = null
            pendingUsbDevice = null
        } else {
            Log.e(TAG, "Failed to open USB device")
            usbPermissionResult?.error("OPEN_FAILED", "Failed to open USB device", null)
            usbPermissionResult = null
            pendingUsbDevice = null
        }
    }
    
    private fun closeCurrentDevice() {
        currentConnection?.close()
        currentConnection = null
    }

    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        
        if (requestCode == RTL_TCP_REQUEST_CODE) {
            if (resultCode == Activity.RESULT_OK) {
                val supportedCommands = data?.getIntArrayExtra("supportedTcpCommands")
                pendingResult?.success(true)
            } else {
                val errorMsg = data?.getStringExtra("detailed_exception_message") ?: "Unknown error"
                pendingResult?.error("DRIVER_ERROR", errorMsg, null)
            }
            pendingResult = null
        }
    }
}
