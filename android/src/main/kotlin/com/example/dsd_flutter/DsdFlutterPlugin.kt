package com.example.dsd_flutter

import android.content.Context
import android.hardware.usb.UsbManager
import android.os.Handler
import android.os.Looper
import android.os.ParcelFileDescriptor
import android.util.Log
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result
import com.mantz_it.hackrf_android.Hackrf
import com.mantz_it.hackrf_android.HackrfCallbackInterface
import com.mantz_it.hackrf_android.HackrfUsbException
import java.io.FileOutputStream
import java.util.concurrent.ArrayBlockingQueue

// HackRF USB IDs
private const val HACKRF_VENDOR_ID = 0x1d50
private const val HACKRF_PRODUCT_ID = 0x6089

/** DsdFlutterPlugin */
class DsdFlutterPlugin :
    FlutterPlugin,
    MethodCallHandler,
    EventChannel.StreamHandler {
    
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private lateinit var callEventChannel: EventChannel
    private lateinit var siteEventChannel: EventChannel
    private lateinit var signalEventChannel: EventChannel
    private lateinit var networkEventChannel: EventChannel
    private lateinit var patchEventChannel: EventChannel
    private lateinit var groupAttachmentEventChannel: EventChannel
    private lateinit var affiliationEventChannel: EventChannel
    private var eventSink: EventChannel.EventSink? = null
    private var callEventSink: EventChannel.EventSink? = null
    private var siteEventSink: EventChannel.EventSink? = null
    private var signalEventSink: EventChannel.EventSink? = null
    private var networkEventSink: EventChannel.EventSink? = null
    private var patchEventSink: EventChannel.EventSink? = null
    private var groupAttachmentEventSink: EventChannel.EventSink? = null
    private var affiliationEventSink: EventChannel.EventSink? = null
    private val mainHandler = Handler(Looper.getMainLooper())
    
    // HackRF support
    private var appContext: Context? = null
    private var hackrf: Hackrf? = null
    private var hackrfRxThread: Thread? = null
    private var hackrfRxQueue: ArrayBlockingQueue<ByteArray>? = null
    @Volatile private var hackrfRxRunning = false
    private var pendingHackrfResult: Result? = null
    
    // HackRF callback for initialization
    private val hackrfCallback = object : HackrfCallbackInterface {
        override fun onHackrfReady(hackrfInstance: Hackrf) {
            hackrf = hackrfInstance
            Log.i("DSD-HackRF", "HackRF initialized successfully")
            mainHandler.post {
                pendingHackrfResult?.success(true)
                pendingHackrfResult = null
            }
        }
        
        override fun onHackrfError(message: String) {
            Log.e("DSD-HackRF", "HackRF init error: $message")
            mainHandler.post {
                pendingHackrfResult?.error("HACKRF_ERROR", message, null)
                pendingHackrfResult = null
            }
        }
    }

    companion object {
        private var instance: DsdFlutterPlugin? = null
        
        init {
            System.loadLibrary("dsd_flutter")
        }
        
        // Called from JNI to send log output to Flutter
        @JvmStatic
        fun sendOutput(text: String) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    plugin.eventSink?.success(text)
                }
            }
        }
        
        // Called from JNI to send structured call events to Flutter
        @JvmStatic
        fun sendCallEvent(
            eventType: Int,
            talkgroup: Int,
            sourceId: Int,
            nac: Int,
            callType: String,
            isEncrypted: Boolean,
            isEmergency: Boolean,
            algName: String,
            slot: Int,
            frequency: Double,
            systemName: String,
            groupName: String,
            sourceName: String
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val eventMap = mapOf(
                        "eventType" to eventType,
                        "talkgroup" to talkgroup,
                        "sourceId" to sourceId,
                        "nac" to nac,
                        "callType" to callType,
                        "isEncrypted" to isEncrypted,
                        "isEmergency" to isEmergency,
                        "algName" to algName,
                        "slot" to slot,
                        "frequency" to frequency,
                        "systemName" to systemName,
                        "groupName" to groupName,
                        "sourceName" to sourceName
                    )
                    plugin.callEventSink?.success(eventMap)
                }
            }
        }
        
        // Called from JNI to send site/system details to Flutter
        @JvmStatic
        fun sendSiteEvent(
            wacn: Long,
            siteId: Long,
            rfssId: Long,
            systemId: Long,
            nac: Int
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val eventMap = mapOf(
                        "wacn" to wacn,
                        "siteId" to siteId,
                        "rfssId" to rfssId,
                        "systemId" to systemId,
                        "nac" to nac
                    )
                    plugin.siteEventSink?.success(eventMap)
                }
            }
        }
        
        // Called from JNI to send signal quality metrics to Flutter
        @JvmStatic
        fun sendSignalEvent(
            tsbkOk: Int,
            tsbkErr: Int,
            synctype: Int,
            hasCarrier: Boolean,
            hasSync: Boolean
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val eventMap = mapOf(
                        "tsbkOk" to tsbkOk,
                        "tsbkErr" to tsbkErr,
                        "synctype" to synctype,
                        "hasCarrier" to hasCarrier,
                        "hasSync" to hasSync
                    )
                    plugin.signalEventSink?.success(eventMap)
                }
            }
        }
        
        // Called from JNI to send network topology updates to Flutter (neighbor sites)
        @JvmStatic
        fun sendNetworkEvent(
            neighborCount: Int,
            neighborFreqs: LongArray?,
            neighborLastSeen: LongArray?
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val freqList = neighborFreqs?.toList() ?: emptyList()
                    val lastSeenList = neighborLastSeen?.toList() ?: emptyList()
                    val eventMap = mapOf(
                        "neighborCount" to neighborCount,
                        "neighborFreqs" to freqList,
                        "neighborLastSeen" to lastSeenList
                    )
                    plugin.networkEventSink?.success(eventMap)
                }
            }
        }
        
        // Called from JNI to send patch events to Flutter
        @JvmStatic
        fun sendPatchEvent(
            patchCount: Int,
            sgids: IntArray?,
            isPatch: BooleanArray?,
            active: BooleanArray?,
            lastUpdate: LongArray?,
            wgidCounts: IntArray?,
            wgids: IntArray?,
            wuidCounts: IntArray?,
            wuids: IntArray?,
            keys: IntArray?,
            algs: IntArray?,
            keyValid: BooleanArray?
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val patches = mutableListOf<Map<String, Any?>>()
                    
                    for (i in 0 until patchCount) {
                        val wgidCount = wgidCounts?.get(i) ?: 0
                        val wuidCount = wuidCounts?.get(i) ?: 0
                        
                        val patchWgids = mutableListOf<Int>()
                        val patchWuids = mutableListOf<Long>()
                        
                        for (j in 0 until wgidCount) {
                            wgids?.get(i * 8 + j)?.let { patchWgids.add(it) }
                        }
                        
                        for (j in 0 until wuidCount) {
                            wuids?.get(i * 8 + j)?.let { patchWuids.add(it.toLong()) }
                        }
                        
                        val patch = mapOf(
                            "sgid" to (sgids?.get(i) ?: 0),
                            "isPatch" to (isPatch?.get(i) ?: false),
                            "active" to (active?.get(i) ?: false),
                            "lastUpdate" to (lastUpdate?.get(i) ?: 0L),
                            "wgidCount" to wgidCount,
                            "wgids" to patchWgids,
                            "wuidCount" to wuidCount,
                            "wuids" to patchWuids,
                            "key" to (keys?.get(i) ?: 0),
                            "alg" to (algs?.get(i) ?: 0),
                            "keyValid" to (keyValid?.get(i) ?: false)
                        )
                        patches.add(patch)
                    }
                    
                    val eventMap = mapOf(
                        "patchCount" to patchCount,
                        "patches" to patches
                    )
                    plugin.patchEventSink?.success(eventMap)
                }
            }
        }
        
        // Called from JNI to send group attachment events to Flutter
        @JvmStatic
        fun sendGroupAttachmentEvent(
            gaCount: Int,
            rids: LongArray?,
            tgs: IntArray?,
            lastSeen: LongArray?
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val attachments = mutableListOf<Map<String, Any?>>()
                    
                    for (i in 0 until gaCount) {
                        val attachment = mapOf(
                            "rid" to (rids?.get(i) ?: 0L),
                            "tg" to (tgs?.get(i) ?: 0),
                            "lastSeen" to (lastSeen?.get(i) ?: 0L)
                        )
                        attachments.add(attachment)
                    }
                    
                    val eventMap = mapOf(
                        "gaCount" to gaCount,
                        "attachments" to attachments
                    )
                    plugin.groupAttachmentEventSink?.success(eventMap)
                }
            }
        }
        
        // Called from JNI to send affiliation events to Flutter
        @JvmStatic
        fun sendAffiliationEvent(
            affCount: Int,
            rids: LongArray?,
            lastSeen: LongArray?
        ) {
            instance?.let { plugin ->
                plugin.mainHandler.post {
                    val affiliations = mutableListOf<Map<String, Any?>>()
                    
                    for (i in 0 until affCount) {
                        val affiliation = mapOf(
                            "rid" to (rids?.get(i) ?: 0L),
                            "lastSeen" to (lastSeen?.get(i) ?: 0L)
                        )
                        affiliations.add(affiliation)
                    }
                    
                    val eventMap = mapOf(
                        "affCount" to affCount,
                        "affiliations" to affiliations
                    )
                    plugin.affiliationEventSink?.success(eventMap)
                }
            }
        }
    }

    // Native method declarations
    private external fun nativeInit()
    private external fun nativeConnect(host: String, port: Int, freqHz: Long, gain: Int, ppm: Int, biasTee: Int)
    private external fun nativeStart()
    private external fun nativeStop()
    private external fun nativeCleanup()
    private external fun nativeSetAudioEnabled(enabled: Boolean)
    private external fun nativeIsRtlSdrSupported(): Boolean
    private external fun nativeOpenRtlSdrUsb(fd: Int, devicePath: String, frequency: Long, sampleRate: Int, gain: Int, ppm: Int, biasTee: Int): Boolean
    private external fun nativeCloseRtlSdrUsb()
    private external fun nativeSetRtlSdrFrequency(frequency: Long): Boolean
    private external fun nativeSetRtlSdrGain(gain: Int): Boolean
    
    // HackRF native methods
    private external fun nativeStartHackRfMode(frequency: Long, sampleRate: Int): Boolean
    private external fun nativeGetHackRfPipeFd(): Int
    private external fun nativeFeedHackRfSamples(samples: ByteArray): Boolean
    private external fun nativeStopHackRfMode()
    
    // Talkgroup filter native methods
    private external fun nativeSetFilterMode(mode: Int)
    private external fun nativeSetFilterTalkgroups(talkgroups: IntArray?)
    private external fun nativeAddFilterTalkgroup(talkgroup: Int)
    private external fun nativeRemoveFilterTalkgroup(talkgroup: Int)
    private external fun nativeClearFilterTalkgroups()
    private external fun nativeGetFilterMode(): Int
    private external fun nativeSetCustomArgs(args: String)
    private external fun nativeSetRetuneFrozen(frozen: Boolean)
    private external fun nativeRetune(freqHz: Int): Boolean
    private external fun nativeResetP25State()
    private external fun nativeSetBiasTee(enabled: Boolean): Boolean

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        instance = this
        appContext = flutterPluginBinding.applicationContext
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter")
        channel.setMethodCallHandler(this)
        
        eventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/output")
        eventChannel.setStreamHandler(this)
        
        callEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/call_events")
        callEventChannel.setStreamHandler(CallEventStreamHandler())
        
        siteEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/site_events")
        siteEventChannel.setStreamHandler(SiteEventStreamHandler())
        
        signalEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/signal_events")
        signalEventChannel.setStreamHandler(SignalEventStreamHandler())
        
        networkEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/network_events")
        networkEventChannel.setStreamHandler(NetworkEventStreamHandler())
        
        patchEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/patch_events")
        patchEventChannel.setStreamHandler(PatchEventStreamHandler())
        
        groupAttachmentEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/group_attachment_events")
        groupAttachmentEventChannel.setStreamHandler(GroupAttachmentEventStreamHandler())
        
        affiliationEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/affiliation_events")
        affiliationEventChannel.setStreamHandler(AffiliationEventStreamHandler())
        
        nativeInit()
    }

    override fun onMethodCall(
        call: MethodCall,
        result: Result
    ) {
        when (call.method) {
            "getPlatformVersion" -> {
                result.success("Android ${android.os.Build.VERSION.RELEASE}")
            }
            "connect" -> {
                val host = call.argument<String>("host") ?: "127.0.0.1"
                val port = call.argument<Int>("port") ?: 1234
                val freqHz = call.argument<Number>("freqHz")?.toLong() ?: 771181250L
                val gain = call.argument<Int>("gain") ?: 48
                val ppm = call.argument<Int>("ppm") ?: 0
                val biasTee = call.argument<Boolean>("biasTee") ?: false
                nativeConnect(host, port, freqHz, gain, ppm, if (biasTee) 1 else 0)
                result.success(null)
            }
            "start" -> {
                nativeStart()
                result.success(null)
            }
            "stop" -> {
                nativeStop()
                result.success(null)
            }
            "setAudioEnabled" -> {
                val enabled = call.argument<Boolean>("enabled") ?: true
                nativeSetAudioEnabled(enabled)
                result.success(null)
            }
            "isNativeRtlSdrSupported" -> {
                result.success(nativeIsRtlSdrSupported())
            }
            "connectNativeUsb" -> {
                val fd = call.argument<Int>("fd") ?: -1
                val devicePath = call.argument<String>("devicePath") ?: ""
                val freqHz = call.argument<Number>("freqHz")?.toLong() ?: 771181250L
                val sampleRate = call.argument<Int>("sampleRate") ?: 2400000
                val gain = call.argument<Int>("gain") ?: 0
                val ppm = call.argument<Int>("ppm") ?: 0
                val biasTee = call.argument<Boolean>("biasTee") ?: false
                val success = nativeOpenRtlSdrUsb(fd, devicePath, freqHz, sampleRate, gain, ppm, if (biasTee) 1 else 0)
                result.success(success)
            }
            "disconnectNativeUsb" -> {
                nativeCloseRtlSdrUsb()
                result.success(null)
            }
            "setNativeRtlFrequency" -> {
                val freqHz = call.argument<Number>("freqHz")?.toLong() ?: 0L
                result.success(nativeSetRtlSdrFrequency(freqHz))
            }
            "setNativeRtlGain" -> {
                val gain = call.argument<Int>("gain") ?: 0
                result.success(nativeSetRtlSdrGain(gain))
            }
            "hackrfListDevices" -> {
                val devices = mutableListOf<Map<String, Any>>()
                try {
                    val usbManager = appContext?.getSystemService(Context.USB_SERVICE) as? UsbManager
                    Log.i("DSD-HackRF", "hackrfListDevices: usbManager=$usbManager, appContext=$appContext")
                    if (usbManager != null) {
                        Log.i("DSD-HackRF", "USB device count: ${usbManager.deviceList.size}")
                        for (device in usbManager.deviceList.values) {
                            Log.i("DSD-HackRF", "Found USB device: vid=${device.vendorId} (0x${device.vendorId.toString(16)}), pid=${device.productId} (0x${device.productId.toString(16)})")
                            if (device.vendorId == HACKRF_VENDOR_ID && device.productId == HACKRF_PRODUCT_ID) {
                                Log.i("DSD-HackRF", "HackRF detected at ${device.deviceName}")
                                // Don't access productName/manufacturerName/serialNumber - requires permission
                                // Just return what we can without permission
                                devices.add(mapOf(
                                    "index" to devices.size,
                                    "name" to "HackRF One",
                                    "manufacturer" to "Great Scott Gadgets",
                                    "serial" to "",
                                    "deviceName" to device.deviceName,
                                    "hasPermission" to usbManager.hasPermission(device)
                                ))
                            }
                        }
                    } else {
                        Log.e("DSD-HackRF", "UsbManager is null!")
                    }
                } catch (e: Exception) {
                    Log.e("DSD-HackRF", "Error listing devices: ${e.message}")
                    e.printStackTrace()
                }
                Log.i("DSD-HackRF", "Returning ${devices.size} HackRF devices")
                result.success(devices)
            }
            "startHackRfMode" -> {
                val freqHz = call.argument<Number>("freqHz")?.toLong() ?: 0L
                val sampleRate = call.argument<Int>("sampleRate") ?: 2400000
                
                // First start the native pipe mode
                val pipeSuccess = nativeStartHackRfMode(freqHz, sampleRate)
                if (!pipeSuccess) {
                    result.error("PIPE_ERROR", "Failed to create HackRF pipe", null)
                    return
                }
                
                // Initialize HackRF if not already done
                if (hackrf == null) {
                    if (appContext == null) {
                        result.error("CONTEXT_ERROR", "Application context not available", null)
                        return
                    }
                    pendingHackrfResult = result
                    try {
                        Hackrf.initHackrf(appContext, hackrfCallback, 15)
                        // Result will be sent via callback
                    } catch (e: Exception) {
                        pendingHackrfResult = null
                        result.error("INIT_ERROR", "Failed to init HackRF: ${e.message}", null)
                    }
                } else {
                    result.success(true)
                }
            }
            "hackrfSetFrequency" -> {
                val freqHz = call.argument<Number>("freqHz")?.toLong() ?: 0L
                try {
                    hackrf?.setFrequency(freqHz)
                    result.success(true)
                } catch (e: Exception) {
                    result.error("HACKRF_ERROR", "Failed to set frequency: ${e.message}", null)
                }
            }
            "hackrfSetSampleRate" -> {
                val sampleRate = call.argument<Int>("sampleRate") ?: 2400000
                try {
                    // Use divider=1 for standard rates
                    hackrf?.setSampleRate(sampleRate, 1)
                    result.success(true)
                } catch (e: Exception) {
                    result.error("HACKRF_ERROR", "Failed to set sample rate: ${e.message}", null)
                }
            }
            "hackrfSetLnaGain" -> {
                val gain = call.argument<Int>("gain") ?: 16
                try {
                    hackrf?.setRxLNAGain(gain)
                    result.success(true)
                } catch (e: Exception) {
                    result.error("HACKRF_ERROR", "Failed to set LNA gain: ${e.message}", null)
                }
            }
            "hackrfSetVgaGain" -> {
                val gain = call.argument<Int>("gain") ?: 16
                try {
                    hackrf?.setRxVGAGain(gain)
                    result.success(true)
                } catch (e: Exception) {
                    result.error("HACKRF_ERROR", "Failed to set VGA gain: ${e.message}", null)
                }
            }
            "hackrfStartRx" -> {
                try {
                    // Start RX and get the queue
                    hackrfRxQueue = hackrf?.startRX()
                    if (hackrfRxQueue == null) {
                        result.error("HACKRF_ERROR", "Failed to start RX", null)
                        return
                    }
                    
                    // Start a thread to read from queue and send via JNI to TCP socket
                    hackrfRxRunning = true
                    hackrfRxThread = Thread {
                        Log.i("DSD-HackRF", "RX thread started, sending samples via JNI")
                        try {
                            while (hackrfRxRunning) {
                                val packet = hackrfRxQueue?.poll(100, java.util.concurrent.TimeUnit.MILLISECONDS)
                                if (packet != null) {
                                    // Send samples to native TCP socket via JNI
                                    nativeFeedHackRfSamples(packet)
                                    // Return buffer to pool for reuse
                                    hackrf?.returnBufferToBufferPool(packet)
                                }
                            }
                            
                            Log.i("DSD-HackRF", "RX thread stopping")
                        } catch (e: Exception) {
                            Log.e("DSD-HackRF", "RX thread error: ${e.message}")
                        }
                    }
                    hackrfRxThread?.start()
                    
                    result.success(true)
                } catch (e: Exception) {
                    result.error("HACKRF_ERROR", "Failed to start RX: ${e.message}", null)
                }
            }
            "hackrfStopRx" -> {
                hackrfRxRunning = false
                try {
                    hackrf?.stop()
                    hackrfRxThread?.join(1000)
                    hackrfRxThread = null
                    result.success(true)
                } catch (e: Exception) {
                    result.error("HACKRF_ERROR", "Failed to stop RX: ${e.message}", null)
                }
            }
            "getHackRfPipeFd" -> {
                result.success(nativeGetHackRfPipeFd())
            }
            "feedHackRfSamples" -> {
                val samples = call.argument<ByteArray>("samples")
                if (samples != null) {
                    result.success(nativeFeedHackRfSamples(samples))
                } else {
                    result.success(false)
                }
            }
            "stopHackRfMode" -> {
                hackrfRxRunning = false
                try {
                    hackrf?.stop()
                    hackrfRxThread?.join(1000)
                } catch (e: Exception) {
                    Log.e("DSD-HackRF", "Error stopping HackRF: ${e.message}")
                }
                hackrfRxThread = null
                nativeStopHackRfMode()
                result.success(null)
            }
            "setFilterMode" -> {
                val mode = call.argument<Int>("mode") ?: 0
                nativeSetFilterMode(mode)
                result.success(null)
            }
            "setFilterTalkgroups" -> {
                val talkgroups = call.argument<List<Int>>("talkgroups")
                nativeSetFilterTalkgroups(talkgroups?.toIntArray())
                result.success(null)
            }
            "addFilterTalkgroup" -> {
                val talkgroup = call.argument<Int>("talkgroup") ?: 0
                nativeAddFilterTalkgroup(talkgroup)
                result.success(null)
            }
            "removeFilterTalkgroup" -> {
                val talkgroup = call.argument<Int>("talkgroup") ?: 0
                nativeRemoveFilterTalkgroup(talkgroup)
                result.success(null)
            }
            "clearFilterTalkgroups" -> {
                nativeClearFilterTalkgroups()
                result.success(null)
            }
            "getFilterMode" -> {
                result.success(nativeGetFilterMode())
            }
            "setCustomArgs" -> {
                val args = call.argument<String>("args") ?: ""
                nativeSetCustomArgs(args)
                result.success(null)
            }
            "setRetuneFrozen" -> {
                val frozen = call.argument<Boolean>("frozen") ?: false
                nativeSetRetuneFrozen(frozen)
                result.success(null)
            }
            "retune" -> {
                val freqHz = call.argument<Int>("freqHz") ?: 0
                val success = nativeRetune(freqHz)
                result.success(success)
            }
            "resetP25State" -> {
                nativeResetP25State()
                result.success(null)
            }
            "setBiasTee" -> {
                val enabled = call.argument<Boolean>("enabled") ?: false
                val success = nativeSetBiasTee(enabled)
                result.success(success)
            }
            else -> {
                result.notImplemented()
            }
        }
    }

    override fun onDetachedFromEngine(binding: FlutterPlugin.FlutterPluginBinding) {
        nativeCleanup()
        channel.setMethodCallHandler(null)
        eventChannel.setStreamHandler(null)
        callEventChannel.setStreamHandler(null)
        siteEventChannel.setStreamHandler(null)
        signalEventChannel.setStreamHandler(null)
        networkEventChannel.setStreamHandler(null)
        instance = null
    }
    
    // EventChannel.StreamHandler for log output
    override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
        eventSink = events
    }
    
    override fun onCancel(arguments: Any?) {
        eventSink = null
    }
    
    // Inner class for call events stream
    inner class CallEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            callEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            callEventSink = null
        }
    }
    
    // Inner class for site events stream
    inner class SiteEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            siteEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            siteEventSink = null
        }
    }
    
    // Inner class for signal events stream
    inner class SignalEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            signalEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            signalEventSink = null
        }
    }
    
    // Inner class for network events stream
    inner class NetworkEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            networkEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            networkEventSink = null
        }
    }
    
    inner class PatchEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            patchEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            patchEventSink = null
        }
    }
    
    inner class GroupAttachmentEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            groupAttachmentEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            groupAttachmentEventSink = null
        }
    }
    
    inner class AffiliationEventStreamHandler : EventChannel.StreamHandler {
        override fun onListen(arguments: Any?, events: EventChannel.EventSink?) {
            affiliationEventSink = events
        }
        
        override fun onCancel(arguments: Any?) {
            affiliationEventSink = null
        }
    }
}
