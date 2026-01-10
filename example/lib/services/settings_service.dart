import 'package:flutter/foundation.dart';

enum RtlSource {
  local,  // Use rtl_tcp_andro app on device
  remote, // Connect to remote rtl_tcp server
}

class SettingsService extends ChangeNotifier {
  RtlSource _rtlSource = RtlSource.local;
  String _remoteHost = '192.168.1.240';
  int _remotePort = 1234;
  int _localPort = 1234;  // Port for local rtl_tcp_andro
  double _frequency = 771.18125;
  bool _audioEnabled = true;
  int _gain = 48;
  int _ppm = 0;
  int _sampleRate = 2400000;  // 2.4 MSPS default

  RtlSource get rtlSource => _rtlSource;
  String get remoteHost => _remoteHost;
  int get remotePort => _remotePort;
  int get localPort => _localPort;
  double get frequency => _frequency;
  bool get audioEnabled => _audioEnabled;
  int get gain => _gain;
  int get ppm => _ppm;
  int get sampleRate => _sampleRate;
  
  int get frequencyHz => (_frequency * 1000000).round();
  
  // Get effective host/port based on source
  String get effectiveHost => _rtlSource == RtlSource.local ? '127.0.0.1' : _remoteHost;
  int get effectivePort => _rtlSource == RtlSource.local ? _localPort : _remotePort;

  void setRtlSource(RtlSource value) {
    _rtlSource = value;
    notifyListeners();
  }

  void updateRemoteHost(String value) {
    _remoteHost = value;
    notifyListeners();
  }

  void updateRemotePort(int value) {
    _remotePort = value;
    notifyListeners();
  }

  void updateLocalPort(int value) {
    _localPort = value;
    notifyListeners();
  }

  void updateFrequency(double value) {
    _frequency = value;
    notifyListeners();
  }

  void setAudioEnabled(bool value) {
    _audioEnabled = value;
    notifyListeners();
  }

  void updateGain(int value) {
    _gain = value;
    notifyListeners();
  }

  void updatePpm(int value) {
    _ppm = value;
    notifyListeners();
  }

  void updateSampleRate(int value) {
    _sampleRate = value;
    notifyListeners();
  }
}
