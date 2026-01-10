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
