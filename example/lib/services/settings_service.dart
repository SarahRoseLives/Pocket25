import 'package:flutter/foundation.dart';
import 'package:shared_preferences/shared_preferences.dart';

enum RtlSource {
  nativeUsb, // Native USB RTL-SDR (built-in, no external app needed)
  hackrf, // Native USB HackRF (built-in, no external app needed)
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
  int _sampleRate = 1536000;  // 1.536 MSPS - matches DSD rtl_tcp default
  
  // Native USB state (RTL-SDR)
  int _nativeUsbFd = -1;
  String _nativeUsbPath = '';
  
  // HackRF specific settings
  int _hackrfLnaGain = 16; // LNA gain 0-40 dB (8 dB steps)
  int _hackrfVgaGain = 16; // VGA gain 0-62 dB (2 dB steps)
  int _hackrfBandwidth = 1750000; // Baseband filter bandwidth in Hz
  bool _hackrfAmpEnable = false; // RF amplifier enable

  SettingsService() {
    _loadSettings();
  }

  Future<void> _loadSettings() async {
    final prefs = await SharedPreferences.getInstance();
    
    final rtlSourceIndex = prefs.getInt('sdr_rtl_source');
    if (rtlSourceIndex != null && rtlSourceIndex < RtlSource.values.length) {
      _rtlSource = RtlSource.values[rtlSourceIndex];
    }
    
    _remoteHost = prefs.getString('sdr_remote_host') ?? _remoteHost;
    _remotePort = prefs.getInt('sdr_remote_port') ?? _remotePort;
    _gain = prefs.getInt('sdr_gain') ?? _gain;
    _ppm = prefs.getInt('sdr_ppm') ?? _ppm;
    _hackrfLnaGain = prefs.getInt('sdr_hackrf_lna_gain') ?? _hackrfLnaGain;
    _hackrfVgaGain = prefs.getInt('sdr_hackrf_vga_gain') ?? _hackrfVgaGain;
    
    notifyListeners();
    
    if (kDebugMode) {
      print('Loaded SDR settings from storage');
    }
  }

  Future<void> _saveSettings() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setInt('sdr_rtl_source', _rtlSource.index);
    await prefs.setString('sdr_remote_host', _remoteHost);
    await prefs.setInt('sdr_remote_port', _remotePort);
    await prefs.setInt('sdr_gain', _gain);
    await prefs.setInt('sdr_ppm', _ppm);
    await prefs.setInt('sdr_hackrf_lna_gain', _hackrfLnaGain);
    await prefs.setInt('sdr_hackrf_vga_gain', _hackrfVgaGain);
    
    if (kDebugMode) {
      print('Saved SDR settings to storage');
    }
  }

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
  
  // HackRF getters
  int get hackrfLnaGain => _hackrfLnaGain;
  int get hackrfVgaGain => _hackrfVgaGain;
  int get hackrfBandwidth => _hackrfBandwidth;
  bool get hackrfAmpEnable => _hackrfAmpEnable;
  
  int get frequencyHz => (_frequency * 1000000).round();
  
  // Get effective host/port based on source (for remote mode)
  String get effectiveHost => _remoteHost;
  int get effectivePort => _remotePort;

  void setRtlSource(RtlSource value) {
    _rtlSource = value;
    _saveSettings();
    notifyListeners();
  }

  void updateRemoteHost(String value) {
    _remoteHost = value;
    _saveSettings();
    notifyListeners();
  }

  void updateRemotePort(int value) {
    _remotePort = value;
    _saveSettings();
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
    _saveSettings();
    notifyListeners();
  }

  void updatePpm(int value) {
    _ppm = value;
    _saveSettings();
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
  
  // HackRF setters
  void updateHackrfLnaGain(int value) {
    _hackrfLnaGain = value;
    _saveSettings();
    notifyListeners();
  }
  
  void updateHackrfVgaGain(int value) {
    _hackrfVgaGain = value;
    _saveSettings();
    notifyListeners();
  }
  
  void updateHackrfBandwidth(int value) {
    _hackrfBandwidth = value;
    notifyListeners();
  }
  
  void setHackrfAmpEnable(bool value) {
    _hackrfAmpEnable = value;
    notifyListeners();
  }
}
