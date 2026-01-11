
import 'dsd_flutter_platform_interface.dart';

class DsdFlutter {
  Future<String?> getPlatformVersion() {
    return DsdFlutterPlatform.instance.getPlatformVersion();
  }

  Future<void> connect(String host, int port, int freqHz) {
    return DsdFlutterPlatform.instance.connect(host, port, freqHz);
  }

  Future<void> start() {
    return DsdFlutterPlatform.instance.start();
  }

  Future<void> stop() {
    return DsdFlutterPlatform.instance.stop();
  }
  
  Future<void> setAudioEnabled(bool enabled) {
    return DsdFlutterPlatform.instance.setAudioEnabled(enabled);
  }
  
  /// Check if native RTL-SDR USB support is available
  Future<bool> isNativeRtlSdrSupported() {
    return DsdFlutterPlatform.instance.isNativeRtlSdrSupported();
  }
  
  /// Connect using native USB RTL-SDR (Android only)
  /// fd: USB file descriptor from Android UsbDeviceConnection
  /// devicePath: USB device path
  /// Returns true on success
  Future<bool> connectNativeUsb({
    required int fd,
    required String devicePath,
    required int freqHz,
    int sampleRate = 2400000,
    int gain = 0,
    int ppm = 0,
  }) {
    return DsdFlutterPlatform.instance.connectNativeUsb(
      fd: fd,
      devicePath: devicePath,
      freqHz: freqHz,
      sampleRate: sampleRate,
      gain: gain,
      ppm: ppm,
    );
  }
  
  /// Disconnect native USB RTL-SDR
  Future<void> disconnectNativeUsb() {
    return DsdFlutterPlatform.instance.disconnectNativeUsb();
  }
  
  /// Set frequency on native RTL-SDR
  Future<bool> setNativeRtlFrequency(int freqHz) {
    return DsdFlutterPlatform.instance.setNativeRtlFrequency(freqHz);
  }
  
  /// Set gain on native RTL-SDR (in tenths of dB, e.g. 480 = 48.0 dB)
  Future<bool> setNativeRtlGain(int gain) {
    return DsdFlutterPlatform.instance.setNativeRtlGain(gain);
  }
  
  /// Stream of log output strings from DSD
  Stream<String> get outputStream {
    return DsdFlutterPlatform.instance.outputStream;
  }
  
  /// Stream of structured call events from DSD
  /// 
  /// Each event is a Map containing:
  /// - eventType: int (0=call_start, 1=call_update, 2=call_end)
  /// - talkgroup: int
  /// - sourceId: int
  /// - nac: int
  /// - callType: String ("Group", "Private", "Voice")
  /// - isEncrypted: bool
  /// - isEmergency: bool
  /// - algName: String (encryption algorithm if encrypted)
  /// - slot: int (timeslot for TDMA)
  /// - frequency: double
  /// - systemName: String
  /// - groupName: String (from CSV import)
  /// - sourceName: String (from CSV import)
  Stream<Map<String, dynamic>> get callEventStream {
    return DsdFlutterPlatform.instance.callEventStream;
  }
  
  /// Stream of site/system detail updates from DSD
  /// 
  /// Each event is a Map containing:
  /// - wacn: int (Wide Area Communications Network ID)
  /// - siteId: int (Site identifier)
  /// - rfssId: int (Radio Frequency Subsystem ID)
  /// - systemId: int (System identifier)
  /// - nac: int (Network Access Code)
  Stream<Map<String, dynamic>> get siteEventStream {
    return DsdFlutterPlatform.instance.siteEventStream;
  }
}
