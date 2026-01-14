
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
  
  /// Stream of signal quality metrics from DSD
  /// 
  /// Each event is a Map containing:
  /// - tsbkOk: int (successful TSBK/frame decodes)
  /// - tsbkErr: int (failed TSBK/frame decodes due to FEC errors)
  /// - synctype: int (P25 sync type: 0/1 for P25 P1, 35/36 for P25 P2)
  /// - hasCarrier: bool (carrier detected)
  /// - hasSync: bool (P25 sync detected)
  Stream<Map<String, dynamic>> get signalEventStream {
    return DsdFlutterPlatform.instance.signalEventStream;
  }
  
  /// Set the talkgroup filter mode
  /// 
  /// [mode] values:
  /// - 0: Disabled (hear all calls)
  /// - 1: Whitelist (only hear talkgroups in the filter list)
  /// - 2: Blacklist (mute talkgroups in the filter list)
  Future<void> setFilterMode(int mode) {
    return DsdFlutterPlatform.instance.setFilterMode(mode);
  }
  
  /// Set the complete list of talkgroups for filtering
  /// 
  /// Replaces any existing filter list with the provided talkgroups.
  Future<void> setFilterTalkgroups(List<int> talkgroups) {
    return DsdFlutterPlatform.instance.setFilterTalkgroups(talkgroups);
  }
  
  /// Add a single talkgroup to the filter list
  Future<void> addFilterTalkgroup(int talkgroup) {
    return DsdFlutterPlatform.instance.addFilterTalkgroup(talkgroup);
  }
  
  /// Remove a single talkgroup from the filter list
  Future<void> removeFilterTalkgroup(int talkgroup) {
    return DsdFlutterPlatform.instance.removeFilterTalkgroup(talkgroup);
  }
  
  /// Clear all talkgroups from the filter list
  Future<void> clearFilterTalkgroups() {
    return DsdFlutterPlatform.instance.clearFilterTalkgroups();
  }
  
  /// Get the current filter mode
  /// 
  /// Returns:
  /// - 0: Disabled
  /// - 1: Whitelist
  /// - 2: Blacklist
  Future<int> getFilterMode() {
    return DsdFlutterPlatform.instance.getFilterMode();
  }
}
