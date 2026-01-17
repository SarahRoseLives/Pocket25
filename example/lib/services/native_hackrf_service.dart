import 'package:hackrf_flutter/hackrf_flutter.dart';

/// Service for native USB HackRF access (no external driver needed)
class NativeHackRfService {
  static HackrfFlutter? _hackrf;
  static bool _isInitialized = false;
  static bool _isStreaming = false;

  /// Initialize the HackRF library
  static Future<bool> initialize() async {
    if (_isInitialized) return true;
    
    try {
      _hackrf = HackrfFlutter();
      _isInitialized = true;
      return true;
    } catch (e) {
      print('Failed to initialize HackRF: $e');
      return false;
    }
  }

  /// Check if USB host mode is supported on this device
  static Future<bool> isUsbHostSupported() async {
    try {
      if (!_isInitialized) {
        await initialize();
      }
      // If we can initialize, USB host is likely supported
      return _isInitialized;
    } catch (e) {
      return false;
    }
  }

  /// List all connected HackRF devices
  static Future<List<Map<String, dynamic>>> listDevices() async {
    try {
      if (!_isInitialized) {
        await initialize();
      }
      
      if (_hackrf == null) return [];
      
      final devices = await _hackrf!.listDevices();
      return devices;
    } catch (e) {
      print('Failed to list HackRF devices: $e');
      return [];
    }
  }

  /// Open a HackRF device by its index
  /// Returns true on success
  static Future<bool> openDevice(int deviceIndex) async {
    try {
      if (!_isInitialized) {
        await initialize();
      }
      
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.open(deviceIndex);
      return result;
    } catch (e) {
      print('Failed to open HackRF device: $e');
      return false;
    }
  }

  /// Close the currently open HackRF device
  static Future<void> closeDevice() async {
    try {
      if (_hackrf == null) return;
      
      if (_isStreaming) {
        await stopRx();
      }
      
      await _hackrf!.close();
    } catch (e) {
      print('Failed to close HackRF device: $e');
    }
  }

  /// Set the center frequency in Hz
  static Future<bool> setFrequency(int freqHz) async {
    try {
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.setFreq(freqHz);
      return result;
    } catch (e) {
      print('Failed to set HackRF frequency: $e');
      return false;
    }
  }

  /// Set the sample rate in Hz
  static Future<bool> setSampleRate(int sampleRate) async {
    try {
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.setSampleRate(sampleRate);
      return result;
    } catch (e) {
      print('Failed to set HackRF sample rate: $e');
      return false;
    }
  }

  /// Set the LNA (Low Noise Amplifier) gain in dB
  /// Valid range: 0-40 dB in 8 dB steps
  static Future<bool> setLnaGain(int gainDb) async {
    try {
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.setLnaGain(gainDb);
      return result;
    } catch (e) {
      print('Failed to set HackRF LNA gain: $e');
      return false;
    }
  }

  /// Set the VGA (Variable Gain Amplifier) gain in dB
  /// Valid range: 0-62 dB in 2 dB steps
  static Future<bool> setVgaGain(int gainDb) async {
    try {
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.setVgaGain(gainDb);
      return result;
    } catch (e) {
      print('Failed to set HackRF VGA gain: $e');
      return false;
    }
  }

  /// Set the baseband filter bandwidth in Hz
  static Future<bool> setBasebandFilterBandwidth(int bandwidthHz) async {
    try {
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.setBasebandFilterBandwidth(bandwidthHz);
      return result;
    } catch (e) {
      print('Failed to set HackRF bandwidth: $e');
      return false;
    }
  }

  /// Enable or disable the RF amplifier
  static Future<bool> setAmpEnable(bool enable) async {
    try {
      if (_hackrf == null) return false;
      
      final result = await _hackrf!.setAmpEnable(enable);
      return result;
    } catch (e) {
      print('Failed to set HackRF amp enable: $e');
      return false;
    }
  }

  /// Start receiving samples
  /// Callback is called with IQ sample data
  static Future<bool> startRx(Function(List<int>) callback) async {
    try {
      if (_hackrf == null) return false;
      if (_isStreaming) return false;
      
      final result = await _hackrf!.startRx(callback);
      _isStreaming = result;
      return result;
    } catch (e) {
      print('Failed to start HackRF RX: $e');
      return false;
    }
  }

  /// Stop receiving samples
  static Future<bool> stopRx() async {
    try {
      if (_hackrf == null) return false;
      if (!_isStreaming) return false;
      
      final result = await _hackrf!.stopRx();
      _isStreaming = !result;
      return result;
    } catch (e) {
      print('Failed to stop HackRF RX: $e');
      return false;
    }
  }

  /// Check if currently streaming
  static bool get isStreaming => _isStreaming;

  /// Get the HackRF instance for advanced operations
  static HackrfFlutter? get instance => _hackrf;
}
