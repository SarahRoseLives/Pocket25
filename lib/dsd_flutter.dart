
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
}
