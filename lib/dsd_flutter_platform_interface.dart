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
