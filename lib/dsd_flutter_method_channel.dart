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
  
  Stream<String>? _outputStream;
  Stream<Map<String, dynamic>>? _callEventStream;

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
}
