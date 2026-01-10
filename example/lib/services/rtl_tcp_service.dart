import 'package:flutter/services.dart';

class RtlTcpService {
  static const _channel = MethodChannel('pocket25/rtl_tcp');
  
  /// Check if rtl_tcp_andro driver is installed
  static Future<bool> isDriverInstalled() async {
    try {
      final result = await _channel.invokeMethod<bool>('isDriverInstalled');
      return result ?? false;
    } catch (e) {
      return false;
    }
  }
  
  /// Start the rtl_tcp_andro driver
  /// Returns true if started successfully
  static Future<bool> startDriver({
    required int port,
    required int sampleRate,
    required int frequency,
    int gain = 48,
    int ppm = 0,
  }) async {
    try {
      final result = await _channel.invokeMethod<bool>('startDriver', {
        'port': port,
        'sampleRate': sampleRate,
        'frequency': frequency,
        'gain': gain,
        'ppm': ppm,
      });
      return result ?? false;
    } catch (e) {
      return false;
    }
  }
  
  /// Stop the rtl_tcp_andro driver (closes the activity)
  static Future<void> stopDriver() async {
    try {
      await _channel.invokeMethod('stopDriver');
    } catch (e) {
      // Ignore errors when stopping
    }
  }
  
  /// Get the Play Store URL for the driver
  static String get playStoreUrl => 
      'https://play.google.com/store/apps/details?id=marto.rtl_tcp_andro';
}
