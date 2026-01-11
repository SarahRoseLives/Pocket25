import 'package:flutter_test/flutter_test.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:dsd_flutter/dsd_flutter_platform_interface.dart';
import 'package:dsd_flutter/dsd_flutter_method_channel.dart';
import 'package:plugin_platform_interface/plugin_platform_interface.dart';

class MockDsdFlutterPlatform
    with MockPlatformInterfaceMixin
    implements DsdFlutterPlatform {

  @override
  Future<String?> getPlatformVersion() => Future.value('42');
  
  @override
  Future<void> connect(String host, int port, int freqHz) async {}
  
  @override
  Future<void> start() async {}
  
  @override
  Future<void> stop() async {}
  
  @override
  Future<void> setAudioEnabled(bool enabled) async {}
  
  @override
  Stream<String> get outputStream => const Stream.empty();
  
  @override
  Stream<Map<String, dynamic>> get callEventStream => const Stream.empty();
  
  @override
  Stream<Map<String, dynamic>> get siteEventStream => const Stream.empty();
  
  @override
  Future<bool> isNativeRtlSdrSupported() async => false;
  
  @override
  Future<bool> connectNativeUsb({
    required int fd,
    required String devicePath,
    required int freqHz,
    int sampleRate = 2400000,
    int gain = 0,
    int ppm = 0,
  }) async => false;
  
  @override
  Future<void> disconnectNativeUsb() async {}
  
  @override
  Future<bool> setNativeRtlFrequency(int freqHz) async => false;
  
  @override
  Future<bool> setNativeRtlGain(int gain) async => false;
  
  @override
  Future<void> setFilterMode(int mode) async {}
  
  @override
  Future<void> setFilterTalkgroups(List<int> talkgroups) async {}
  
  @override
  Future<void> addFilterTalkgroup(int talkgroup) async {}
  
  @override
  Future<void> removeFilterTalkgroup(int talkgroup) async {}
  
  @override
  Future<void> clearFilterTalkgroups() async {}
  
  @override
  Future<int> getFilterMode() async => 0;
}

void main() {
  final DsdFlutterPlatform initialPlatform = DsdFlutterPlatform.instance;

  test('$MethodChannelDsdFlutter is the default instance', () {
    expect(initialPlatform, isInstanceOf<MethodChannelDsdFlutter>());
  });

  test('getPlatformVersion', () async {
    DsdFlutter dsdFlutterPlugin = DsdFlutter();
    MockDsdFlutterPlatform fakePlatform = MockDsdFlutterPlatform();
    DsdFlutterPlatform.instance = fakePlatform;

    expect(await dsdFlutterPlugin.getPlatformVersion(), '42');
  });
}
