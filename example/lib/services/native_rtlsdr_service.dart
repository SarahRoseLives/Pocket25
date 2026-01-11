import 'package:flutter/services.dart';

/// Represents an RTL-SDR USB device
class RtlSdrUsbDevice {
  final String deviceName;
  final int vendorId;
  final int productId;
  final String productName;
  final String manufacturerName;
  final bool hasPermission;

  RtlSdrUsbDevice({
    required this.deviceName,
    required this.vendorId,
    required this.productId,
    required this.productName,
    required this.manufacturerName,
    required this.hasPermission,
  });

  factory RtlSdrUsbDevice.fromMap(Map<dynamic, dynamic> map) {
    return RtlSdrUsbDevice(
      deviceName: map['deviceName'] as String,
      vendorId: map['vendorId'] as int,
      productId: map['productId'] as int,
      productName: map['productName'] as String? ?? 'RTL-SDR',
      manufacturerName: map['manufacturerName'] as String? ?? 'Unknown',
      hasPermission: map['hasPermission'] as bool? ?? false,
    );
  }

  @override
  String toString() =>
      'RtlSdrUsbDevice($productName at $deviceName, VID:0x${vendorId.toRadixString(16)}, PID:0x${productId.toRadixString(16)})';
}

/// Service for native USB RTL-SDR access (no external driver needed)
class NativeRtlSdrService {
  static const _channel = MethodChannel('pocket25/native_usb');

  /// Check if USB host mode is supported on this device
  static Future<bool> isUsbHostSupported() async {
    try {
      final result = await _channel.invokeMethod<bool>('isUsbHostSupported');
      return result ?? false;
    } catch (e) {
      return false;
    }
  }

  /// List all connected RTL-SDR devices
  static Future<List<RtlSdrUsbDevice>> listDevices() async {
    try {
      final result = await _channel.invokeMethod<List<dynamic>>('listRtlSdrDevices');
      if (result == null) return [];
      
      return result
          .map((item) => RtlSdrUsbDevice.fromMap(item as Map<dynamic, dynamic>))
          .toList();
    } catch (e) {
      return [];
    }
  }

  /// Open an RTL-SDR device by its device name
  /// Returns a map with 'fd' (file descriptor) and 'devicePath'
  /// Will request USB permission if not already granted
  static Future<Map<String, dynamic>?> openDevice(String deviceName) async {
    try {
      final result = await _channel.invokeMethod<Map<dynamic, dynamic>>(
        'openDevice',
        {'deviceName': deviceName},
      );
      if (result == null) return null;
      
      return {
        'fd': result['fd'] as int,
        'devicePath': result['devicePath'] as String,
      };
    } catch (e) {
      return null;
    }
  }

  /// Close the currently open USB device
  static Future<void> closeDevice() async {
    try {
      await _channel.invokeMethod('closeDevice');
    } catch (e) {
      // Ignore errors
    }
  }

  /// Get the file descriptor of the currently open device
  static Future<int> getDeviceFd() async {
    try {
      final result = await _channel.invokeMethod<int>('getDeviceFd');
      return result ?? -1;
    } catch (e) {
      return -1;
    }
  }
}
