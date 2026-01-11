import 'package:plugin_platform_interface/plugin_platform_interface.dart';

import 'dsd_flutter_method_channel.dart';

abstract class DsdFlutterPlatform extends PlatformInterface {
  /// Constructs a DsdFlutterPlatform.
  DsdFlutterPlatform() : super(token: _token);

  static final Object _token = Object();

  static DsdFlutterPlatform _instance = MethodChannelDsdFlutter();

  /// The default instance of [DsdFlutterPlatform] to use.
  ///
  /// Defaults to [MethodChannelDsdFlutter].
  static DsdFlutterPlatform get instance => _instance;

  /// Platform-specific implementations should set this with their own
  /// platform-specific class that extends [DsdFlutterPlatform] when
  /// they register themselves.
  static set instance(DsdFlutterPlatform instance) {
    PlatformInterface.verifyToken(instance, _token);
    _instance = instance;
  }

  Future<String?> getPlatformVersion() {
    throw UnimplementedError('platformVersion() has not been implemented.');
  }

  Future<void> connect(String host, int port, int freqHz) {
    throw UnimplementedError('connect() has not been implemented.');
  }

  Future<void> start() {
    throw UnimplementedError('start() has not been implemented.');
  }

  Future<void> stop() {
    throw UnimplementedError('stop() has not been implemented.');
  }
  
  Future<void> setAudioEnabled(bool enabled) {
    throw UnimplementedError('setAudioEnabled() has not been implemented.');
  }
  
  /// Check if native RTL-SDR USB support is available
  Future<bool> isNativeRtlSdrSupported() {
    throw UnimplementedError('isNativeRtlSdrSupported() has not been implemented.');
  }
  
  /// Connect using native USB RTL-SDR (Android only)
  /// Returns true on success
  Future<bool> connectNativeUsb({
    required int fd,
    required String devicePath,
    required int freqHz,
    int sampleRate = 2400000,
    int gain = 0,
    int ppm = 0,
  }) {
    throw UnimplementedError('connectNativeUsb() has not been implemented.');
  }
  
  /// Disconnect native USB RTL-SDR
  Future<void> disconnectNativeUsb() {
    throw UnimplementedError('disconnectNativeUsb() has not been implemented.');
  }
  
  /// Set frequency on native RTL-SDR
  Future<bool> setNativeRtlFrequency(int freqHz) {
    throw UnimplementedError('setNativeRtlFrequency() has not been implemented.');
  }
  
  /// Set gain on native RTL-SDR (in tenths of dB)
  Future<bool> setNativeRtlGain(int gain) {
    throw UnimplementedError('setNativeRtlGain() has not been implemented.');
  }
  
  /// Stream of log output strings from DSD
  Stream<String> get outputStream {
    throw UnimplementedError('outputStream has not been implemented.');
  }
  
  /// Stream of structured call events from DSD
  Stream<Map<String, dynamic>> get callEventStream {
    throw UnimplementedError('callEventStream has not been implemented.');
  }
  
  /// Stream of site/system detail updates from DSD
  Stream<Map<String, dynamic>> get siteEventStream {
    throw UnimplementedError('siteEventStream has not been implemented.');
  }
}
