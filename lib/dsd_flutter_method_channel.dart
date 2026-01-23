import 'dart:typed_data';
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
  
  /// The event channel for receiving signal quality metrics.
  final _signalEventChannel = const EventChannel('dsd_flutter/signal_events');
  
  /// The event channel for receiving network topology updates.
  final _networkEventChannel = const EventChannel('dsd_flutter/network_events');
  
  /// The event channel for receiving patch updates.
  final _patchEventChannel = const EventChannel('dsd_flutter/patch_events');
  
  /// The event channel for receiving group attachment updates.
  final _groupAttachmentEventChannel = const EventChannel('dsd_flutter/group_attachment_events');
  
  /// The event channel for receiving affiliation updates.
  final _affiliationEventChannel = const EventChannel('dsd_flutter/affiliation_events');
  
  Stream<String>? _outputStream;
  Stream<Map<String, dynamic>>? _callEventStream;
  Stream<Map<String, dynamic>>? _siteEventStream;
  Stream<Map<String, dynamic>>? _signalEventStream;
  Stream<Map<String, dynamic>>? _networkEventStream;
  Stream<Map<String, dynamic>>? _patchEventStream;
  Stream<Map<String, dynamic>>? _groupAttachmentEventStream;
  Stream<Map<String, dynamic>>? _affiliationEventStream;

  @override
  Future<String?> getPlatformVersion() async {
    final version = await methodChannel.invokeMethod<String>('getPlatformVersion');
    return version;
  }

  @override
  Future<void> connect(String host, int port, int freqHz, {int gain = 48, int ppm = 0, bool biasTee = false}) async {
    await methodChannel.invokeMethod('connect', {
      'host': host,
      'port': port,
      'freqHz': freqHz,
      'gain': gain,
      'ppm': ppm,
      'biasTee': biasTee,
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
    bool biasTee = false,
  }) async {
    final result = await methodChannel.invokeMethod<bool>('connectNativeUsb', {
      'fd': fd,
      'devicePath': devicePath,
      'freqHz': freqHz,
      'sampleRate': sampleRate,
      'gain': gain,
      'ppm': ppm,
      'biasTee': biasTee,
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
  Future<List<Map<String, dynamic>>> hackrfListDevices() async {
    final result = await methodChannel.invokeMethod<List<dynamic>>('hackrfListDevices');
    if (result == null) return [];
    return result.map((e) => Map<String, dynamic>.from(e as Map)).toList();
  }
  
  @override
  Future<bool> startHackRfMode(int freqHz, int sampleRate) async {
    final result = await methodChannel.invokeMethod<bool>('startHackRfMode', {
      'freqHz': freqHz,
      'sampleRate': sampleRate,
    });
    return result ?? false;
  }
  
  @override
  Future<bool> hackrfSetFrequency(int freqHz) async {
    final result = await methodChannel.invokeMethod<bool>('hackrfSetFrequency', {
      'freqHz': freqHz,
    });
    return result ?? false;
  }
  
  @override
  Future<bool> hackrfSetSampleRate(int sampleRate) async {
    final result = await methodChannel.invokeMethod<bool>('hackrfSetSampleRate', {
      'sampleRate': sampleRate,
    });
    return result ?? false;
  }
  
  @override
  Future<bool> hackrfSetLnaGain(int gain) async {
    final result = await methodChannel.invokeMethod<bool>('hackrfSetLnaGain', {
      'gain': gain,
    });
    return result ?? false;
  }
  
  @override
  Future<bool> hackrfSetVgaGain(int gain) async {
    final result = await methodChannel.invokeMethod<bool>('hackrfSetVgaGain', {
      'gain': gain,
    });
    return result ?? false;
  }
  
  @override
  Future<bool> hackrfStartRx() async {
    final result = await methodChannel.invokeMethod<bool>('hackrfStartRx');
    return result ?? false;
  }
  
  @override
  Future<bool> hackrfStopRx() async {
    final result = await methodChannel.invokeMethod<bool>('hackrfStopRx');
    return result ?? false;
  }
  
  @override
  Future<int> getHackRfPipeFd() async {
    final result = await methodChannel.invokeMethod<int>('getHackRfPipeFd');
    return result ?? -1;
  }
  
  @override
  Future<bool> feedHackRfSamples(Uint8List samples) async {
    final result = await methodChannel.invokeMethod<bool>('feedHackRfSamples', {
      'samples': samples,
    });
    return result ?? false;
  }
  
  @override
  Future<void> stopHackRfMode() async {
    await methodChannel.invokeMethod('stopHackRfMode');
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
  
  @override
  Stream<Map<String, dynamic>> get signalEventStream {
    _signalEventStream ??= _signalEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _signalEventStream!;
  }
  
  @override
  Stream<Map<String, dynamic>> get networkEventStream {
    _networkEventStream ??= _networkEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _networkEventStream!;
  }
  
  @override
  Stream<Map<String, dynamic>> get patchEventStream {
    _patchEventStream ??= _patchEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _patchEventStream!;
  }
  
  @override
  Stream<Map<String, dynamic>> get groupAttachmentEventStream {
    _groupAttachmentEventStream ??= _groupAttachmentEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _groupAttachmentEventStream!;
  }
  
  @override
  Stream<Map<String, dynamic>> get affiliationEventStream {
    _affiliationEventStream ??= _affiliationEventChannel
        .receiveBroadcastStream()
        .map((event) => Map<String, dynamic>.from(event as Map));
    return _affiliationEventStream!;
  }
  
  @override
  Future<void> setFilterMode(int mode) async {
    await methodChannel.invokeMethod('setFilterMode', {
      'mode': mode,
    });
  }
  
  @override
  Future<void> setFilterTalkgroups(List<int> talkgroups) async {
    await methodChannel.invokeMethod('setFilterTalkgroups', {
      'talkgroups': talkgroups,
    });
  }
  
  @override
  Future<void> addFilterTalkgroup(int talkgroup) async {
    await methodChannel.invokeMethod('addFilterTalkgroup', {
      'talkgroup': talkgroup,
    });
  }
  
  @override
  Future<void> removeFilterTalkgroup(int talkgroup) async {
    await methodChannel.invokeMethod('removeFilterTalkgroup', {
      'talkgroup': talkgroup,
    });
  }
  
  @override
  Future<void> clearFilterTalkgroups() async {
    await methodChannel.invokeMethod('clearFilterTalkgroups');
  }
  
  @override
  Future<int> getFilterMode() async {
    final result = await methodChannel.invokeMethod<int>('getFilterMode');
    return result ?? 0;
  }
  
  @override
  Future<void> setCustomArgs(String args) async {
    await methodChannel.invokeMethod('setCustomArgs', {
      'args': args,
    });
  }
  
  @override
  Future<void> setRetuneFrozen(bool frozen) async {
    await methodChannel.invokeMethod('setRetuneFrozen', {
      'frozen': frozen,
    });
  }
  
  @override
  Future<bool> retune(int freqHz) async {
    final result = await methodChannel.invokeMethod<bool>('retune', {
      'freqHz': freqHz,
    });
    return result ?? false;
  }
  
  @override
  Future<void> resetP25State() async {
    await methodChannel.invokeMethod('resetP25State');
  }
  
  @override
  Future<bool> setBiasTee(bool enabled) async {
    final result = await methodChannel.invokeMethod<bool>('setBiasTee', {
      'enabled': enabled,
    });
    return result ?? false;
  }
}
