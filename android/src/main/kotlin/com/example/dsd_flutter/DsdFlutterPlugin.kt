package com.example.dsd_flutter

import android.os.Handler
import android.os.Looper
import io.flutter.embedding.engine.plugins.FlutterPlugin
import io.flutter.plugin.common.EventChannel
import io.flutter.plugin.common.MethodCall
import io.flutter.plugin.common.MethodChannel
import io.flutter.plugin.common.MethodChannel.MethodCallHandler
import io.flutter.plugin.common.MethodChannel.Result

/** DsdFlutterPlugin */
class DsdFlutterPlugin :
    FlutterPlugin,
    MethodCallHandler,
    EventChannel.StreamHandler {
    
    private lateinit var channel: MethodChannel
    private lateinit var eventChannel: EventChannel
    private lateinit var callEventChannel: EventChannel
    private lateinit var siteEventChannel: EventChannel
    private var eventSink: EventChannel.EventSink? = null
    private var callEventSink: EventChannel.EventSink? = null
    private var siteEventSink: EventChannel.EventSink? = null
    private val mainHandler = Handler(Looper.getMainLooper())

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
    }

    // Native method declarations
    private external fun nativeInit()
    private external fun nativeConnect(host: String, port: Int, freqHz: Long)
    private external fun nativeStart()
    private external fun nativeStop()
    private external fun nativeCleanup()
    private external fun nativeSetAudioEnabled(enabled: Boolean)
    private external fun nativeIsRtlSdrSupported(): Boolean
    private external fun nativeOpenRtlSdrUsb(fd: Int, devicePath: String, frequency: Long, sampleRate: Int, gain: Int, ppm: Int): Boolean
    private external fun nativeCloseRtlSdrUsb()
    private external fun nativeSetRtlSdrFrequency(frequency: Long): Boolean
    private external fun nativeSetRtlSdrGain(gain: Int): Boolean
    
    // Talkgroup filter native methods
    private external fun nativeSetFilterMode(mode: Int)
    private external fun nativeSetFilterTalkgroups(talkgroups: IntArray?)
    private external fun nativeAddFilterTalkgroup(talkgroup: Int)
    private external fun nativeRemoveFilterTalkgroup(talkgroup: Int)
    private external fun nativeClearFilterTalkgroups()
    private external fun nativeGetFilterMode(): Int

    override fun onAttachedToEngine(flutterPluginBinding: FlutterPlugin.FlutterPluginBinding) {
        instance = this
        channel = MethodChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter")
        channel.setMethodCallHandler(this)
        
        eventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/output")
        eventChannel.setStreamHandler(this)
        
        callEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/call_events")
        callEventChannel.setStreamHandler(CallEventStreamHandler())
        
        siteEventChannel = EventChannel(flutterPluginBinding.binaryMessenger, "dsd_flutter/site_events")
        siteEventChannel.setStreamHandler(SiteEventStreamHandler())
        
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
                nativeConnect(host, port, freqHz)
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
                val success = nativeOpenRtlSdrUsb(fd, devicePath, freqHz, sampleRate, gain, ppm)
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
}
