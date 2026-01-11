import 'package:flutter/foundation.dart';

enum RtlSource {
  nativeUsb, // Native USB RTL-SDR (built-in, no external app needed)
  remote, // Connect to remote rtl_tcp server
}

class SettingsService extends ChangeNotifier {
  RtlSource _rtlSource = RtlSource.nativeUsb;
  String _remoteHost = '192.168.1.240';
  int _remotePort = 1234;
  double _frequency = 771.18125;
  bool _audioEnabled = true;
  int _gain = 48;
  int _ppm = 0;
  int _sampleRate = 2400000;  // 2.4 MSPS default
  
  // Native USB state
  int _nativeUsbFd = -1;
  String _nativeUsbPath = '';

  RtlSource get rtlSource => _rtlSource;
  String get remoteHost => _remoteHost;
  int get remotePort => _remotePort;
  double get frequency => _frequency;
  bool get audioEnabled => _audioEnabled;
  int get gain => _gain;
  int get ppm => _ppm;
  int get sampleRate => _sampleRate;
  int get nativeUsbFd => _nativeUsbFd;
  String get nativeUsbPath => _nativeUsbPath;
  bool get hasNativeUsbDevice => _nativeUsbFd >= 0;
  
  int get frequencyHz => (_frequency * 1000000).round();
  
  // Get effective host/port based on source (for remote mode)
  String get effectiveHost => _remoteHost;
  int get effectivePort => _remotePort;

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
  
  void setNativeUsbDevice(int fd, String path) {
    _nativeUsbFd = fd;
    _nativeUsbPath = path;
    notifyListeners();
  }
  
  void clearNativeUsbDevice() {
    _nativeUsbFd = -1;
    _nativeUsbPath = '';
    notifyListeners();
  }
}
