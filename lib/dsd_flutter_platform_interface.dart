import 'dart:typed_data';
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

  Future<void> connect(String host, int port, int freqHz, {int gain = 48, int ppm = 0, bool biasTee = false}) {
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
    bool biasTee = false,
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
  
  /// List connected HackRF devices
  Future<List<Map<String, dynamic>>> hackrfListDevices() {
    throw UnimplementedError('hackrfListDevices() has not been implemented.');
  }
  
  /// Start HackRF mode with external sample feeding
  Future<bool> startHackRfMode(int freqHz, int sampleRate) {
    throw UnimplementedError('startHackRfMode() has not been implemented.');
  }
  
  /// Set HackRF frequency
  Future<bool> hackrfSetFrequency(int freqHz) {
    throw UnimplementedError('hackrfSetFrequency() has not been implemented.');
  }
  
  /// Set HackRF sample rate
  Future<bool> hackrfSetSampleRate(int sampleRate) {
    throw UnimplementedError('hackrfSetSampleRate() has not been implemented.');
  }
  
  /// Set HackRF LNA gain (0-40 dB, 8 dB steps)
  Future<bool> hackrfSetLnaGain(int gain) {
    throw UnimplementedError('hackrfSetLnaGain() has not been implemented.');
  }
  
  /// Set HackRF VGA gain (0-62 dB, 2 dB steps)
  Future<bool> hackrfSetVgaGain(int gain) {
    throw UnimplementedError('hackrfSetVgaGain() has not been implemented.');
  }
  
  /// Start HackRF RX - samples go directly to DSD pipe
  Future<bool> hackrfStartRx() {
    throw UnimplementedError('hackrfStartRx() has not been implemented.');
  }
  
  /// Stop HackRF RX
  Future<bool> hackrfStopRx() {
    throw UnimplementedError('hackrfStopRx() has not been implemented.');
  }
  
  /// Get the HackRF pipe FD for native sample feeding
  Future<int> getHackRfPipeFd() {
    throw UnimplementedError('getHackRfPipeFd() has not been implemented.');
  }
  
  /// Feed HackRF samples to DSD
  Future<bool> feedHackRfSamples(Uint8List samples) {
    throw UnimplementedError('feedHackRfSamples() has not been implemented.');
  }
  
  /// Stop HackRF mode
  Future<void> stopHackRfMode() {
    throw UnimplementedError('stopHackRfMode() has not been implemented.');
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
  
  /// Stream of signal quality metrics from DSD
  Stream<Map<String, dynamic>> get signalEventStream {
    throw UnimplementedError('signalEventStream has not been implemented.');
  }
  
  /// Stream of network topology updates from DSD
  Stream<Map<String, dynamic>> get networkEventStream {
    throw UnimplementedError('networkEventStream has not been implemented.');
  }
  
  /// Stream of patch updates from DSD
  Stream<Map<String, dynamic>> get patchEventStream {
    throw UnimplementedError('patchEventStream has not been implemented.');
  }
  
  /// Stream of group attachment updates from DSD
  Stream<Map<String, dynamic>> get groupAttachmentEventStream {
    throw UnimplementedError('groupAttachmentEventStream has not been implemented.');
  }
  
  /// Stream of affiliation updates from DSD
  Stream<Map<String, dynamic>> get affiliationEventStream {
    throw UnimplementedError('affiliationEventStream has not been implemented.');
  }
  
  /// Set the talkgroup filter mode
  /// mode: 0=disabled, 1=whitelist (only hear listed TGs), 2=blacklist (mute listed TGs)
  Future<void> setFilterMode(int mode) {
    throw UnimplementedError('setFilterMode() has not been implemented.');
  }
  
  /// Set the list of talkgroups for filtering
  Future<void> setFilterTalkgroups(List<int> talkgroups) {
    throw UnimplementedError('setFilterTalkgroups() has not been implemented.');
  }
  
  /// Add a single talkgroup to the filter list
  Future<void> addFilterTalkgroup(int talkgroup) {
    throw UnimplementedError('addFilterTalkgroup() has not been implemented.');
  }
  
  /// Remove a single talkgroup from the filter list
  Future<void> removeFilterTalkgroup(int talkgroup) {
    throw UnimplementedError('removeFilterTalkgroup() has not been implemented.');
  }
  
  /// Clear all talkgroups from the filter list
  Future<void> clearFilterTalkgroups() {
    throw UnimplementedError('clearFilterTalkgroups() has not been implemented.');
  }
  
  /// Get the current filter mode
  Future<int> getFilterMode() {
    throw UnimplementedError('getFilterMode() has not been implemented.');
  }
  
  /// Set custom DSD command line arguments
  Future<void> setCustomArgs(String args) {
    throw UnimplementedError('setCustomArgs() has not been implemented.');
  }
  
  /// Freeze/unfreeze auto-retune during system switching
  Future<void> setRetuneFrozen(bool frozen) {
    throw UnimplementedError('setRetuneFrozen() has not been implemented.');
  }
  
  /// Retune to a new frequency without restarting DSD
  /// This preserves P25 state machine and is faster than stop/reconnect/start
  Future<bool> retune(int freqHz) {
    throw UnimplementedError('retune() has not been implemented.');
  }
  
  /// Reset P25 state (frequency tables and state machine)
  Future<void> resetP25State() {
    throw UnimplementedError('resetP25State() has not been implemented.');
  }
}
