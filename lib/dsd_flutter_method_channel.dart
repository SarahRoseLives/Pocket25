import 'package:flutter/foundation.dart';
import 'package:flutter/services.dart';

import 'dsd_flutter_platform_interface.dart';

/// An implementation of [DsdFlutterPlatform] that uses method channels.
class MethodChannelDsdFlutter extends DsdFlutterPlatform {
  /// The method channel used to interact with the native platform.
  @visibleForTesting
  final methodChannel = const MethodChannel('dsd_flutter');
  
  /// The event channel for receiving log output from the decoder.
  final _eventChannel = const EventChannel('dsd_flutter/output');
  
  /// The event channel for receiving structured call events.
  final _callEventChannel = const EventChannel('dsd_flutter/call_events');
  
  /// The event channel for receiving site/system detail updates.
  final _siteEventChannel = const EventChannel('dsd_flutter/site_events');
  
  Stream<String>? _outputStream;
  Stream<Map<String, dynamic>>? _callEventStream;
  Stream<Map<String, dynamic>>? _siteEventStream;

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }

  @override
  Future<void> connect(String host, int port, int freqHz) async {
    await methodChannel.invokeMethod('connect', {
      'host': host,
      'port': port,
      'freqHz': freqHz,
    });
  }

  @override
  Future<void> start() async {
    await methodChannel.invokeMethod('start');
  }

  @override
  Future<void> stop() async {
    await methodChannel.invokeMethod('stop');
  }
  
  @override
  Future<void> setAudioEnabled(bool enabled) async {
    await methodChannel.invokeMethod('setAudioEnabled', {
      'enabled': enabled,
    });
  }
  
  @override
  Future<bool> isNativeRtlSdrSupported() async {
    final result = await methodChannel.invokeMethod<bool>('isNativeRtlSdrSupported');
    return result ?? false;
  }
  
  @override
  Future<bool> connectNativeUsb({
    required int fd,
    required String devicePath,
    required int freqHz,
    int sampleRate = 2400000,
    int gain = 0,
    int ppm = 0,
  }) async {
    final result = await methodChannel.invokeMethod<bool>('connectNativeUsb', {
      'fd': fd,
      'devicePath': devicePath,
      'freqHz': freqHz,
      'sampleRate': sampleRate,
      'gain': gain,
      'ppm': ppm,
    });
    return result ?? false;
  }
  
  @override
  Future<void> disconnectNativeUsb() async {
    await methodChannel.invokeMethod('disconnectNativeUsb');
  }
  
  @override
  Future<bool> setNativeRtlFrequency(int freqHz) async {
    final result = await methodChannel.invokeMethod<bool>('setNativeRtlFrequency', {
      'freqHz': freqHz,
    });
    return result ?? false;
  }
  
  @override
  Future<bool> setNativeRtlGain(int gain) async {
    final result = await methodChannel.invokeMethod<bool>('setNativeRtlGain', {
      'gain': gain,
    });
    return result ?? false;
  }
  
  @override
  Stream<String> get outputStream {
    _outputStream ??= _eventChannel
        .receiveBroadcastStream()
        .map((event) => event.toString());
    return _outputStream!;
  }
  
  @override
  Stream<Map<String, dynamic>> get callEventStream {
    _callEventStream ??= _callEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _callEventStream!;
  }
  
  @override
  Stream<Map<String, dynamic>> get siteEventStream {
    _siteEventStream ??= _siteEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _siteEventStream!;
  }
}
