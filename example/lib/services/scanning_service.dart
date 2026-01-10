import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../services/settings_service.dart';
import '../services/rtl_tcp_service.dart';
import 'database_service.dart';

enum ScanningState {
  idle,
  searching,
  locked,
  error,
}

class ScanningService extends ChangeNotifier {
  final DsdFlutter _dsdPlugin;
  final SettingsService _settingsService;
  final DatabaseService _db = DatabaseService();
  final VoidCallback _onStart;
  final VoidCallback _onStop;
  
  ScanningState _state = ScanningState.idle;
  int? _currentSiteId;
  String? _currentSiteName;
  double? _currentFrequency;
  int _currentChannelIndex = 0;
  List<Map<String, dynamic>> _controlChannels = [];
  Timer? _lockCheckTimer;
  StreamSubscription? _outputSubscription;
  
  bool _hasLock = false;
  DateTime? _lastActivityTime;
  
  ScanningState get state => _state;
  int? get currentSiteId => _currentSiteId;
  String? get currentSiteName => _currentSiteName;
  double? get currentFrequency => _currentFrequency;
  int get currentChannelIndex => _currentChannelIndex;
  int get totalChannels => _controlChannels.length;
  bool get hasLock => _hasLock;

  ScanningService(
    this._dsdPlugin,
    this._settingsService,
    this._onStart,
    this._onStop,
  ) {
    _listenToOutput();
  }

  void _listenToOutput() {
    _outputSubscription = _dsdPlugin.outputStream.listen((line) {
      // Check for control channel activity indicators
      if (line.contains('Sync: +P25') || 
          line.contains('TSBK') || 
          line.contains('NET STS') ||
          line.contains('rfss')) {
        _hasLock = true;
        _lastActivityTime = DateTime.now();
        
        if (_state == ScanningState.searching) {
          _setState(ScanningState.locked);
          if (kDebugMode) {
            print('Control channel LOCKED at ${_currentFrequency} MHz');
          }
        }
      }
    });
  }

  Future<void> startScanning(int siteId, String siteName) async {
    if (_state != ScanningState.idle) {
      await stopScanning();
    }

    try {
      _currentSiteId = siteId;
      _currentSiteName = siteName;
      _currentChannelIndex = 0;
      _hasLock = false;
      _lastActivityTime = null;
      
      // Load control channels for this site
      _controlChannels = await _db.getControlChannels(siteId);
      
      if (_controlChannels.isEmpty) {
        _setState(ScanningState.error);
        if (kDebugMode) {
          print('No control channels found for site $siteId');
        }
        return;
      }
      
      if (kDebugMode) {
        print('Starting scan for site $siteName with ${_controlChannels.length} control channels');
      }
      
      _setState(ScanningState.searching);
      await _tryNextControlChannel();
      
      // Start lock check timer
      _lockCheckTimer = Timer.periodic(const Duration(seconds: 5), _checkLockStatus);
      
    } catch (e) {
      _setState(ScanningState.error);
      if (kDebugMode) {
        print('Error starting scan: $e');
      }
    }
  }

  Future<void> _tryNextControlChannel() async {
    if (_currentChannelIndex >= _controlChannels.length) {
      // Tried all channels, restart from beginning
      if (kDebugMode) {
        print('All channels tried, restarting from first channel');
      }
      _currentChannelIndex = 0;
    }

    final channel = _controlChannels[_currentChannelIndex];
    _currentFrequency = channel['frequency'] as double;
    _hasLock = false;
    _lastActivityTime = DateTime.now(); // Set initial time
    
    if (kDebugMode) {
      print('Trying control channel ${_currentChannelIndex + 1}/${_controlChannels.length}: ${_currentFrequency} MHz');
    }
    
    try {
      // Stop current scan if running
      _onStop();
      await Future.delayed(const Duration(milliseconds: 500));
      
      // Update frequency in settings
      _settingsService.updateFrequency(_currentFrequency!);
      
      // If using local RTL-SDR, restart the driver with new frequency
      // (Necessary to avoid rtl_tcp timeouts when changing frequencies)
      if (_settingsService.rtlSource == RtlSource.local) {
        if (kDebugMode) {
          print('Restarting RTL-TCP driver with frequency: ${_settingsService.frequencyHz} Hz');
        }
        
        final started = await RtlTcpService.startDriver(
          port: _settingsService.localPort,
          sampleRate: _settingsService.sampleRate,
          frequency: _settingsService.frequencyHz,
          gain: _settingsService.gain,
          ppm: _settingsService.ppm,
        );
        
        if (!started) {
          throw Exception('Failed to restart RTL-SDR driver');
        }
        
        // Give driver time to initialize
        await Future.delayed(const Duration(milliseconds: 500));
      }
      
      // Reconnect DSD plugin with new frequency
      await _dsdPlugin.connect(
        _settingsService.effectiveHost,
        _settingsService.effectivePort,
        _settingsService.frequencyHz,
      );
      
      // Start scanning with new frequency
      _onStart();
      
      notifyListeners();
    } catch (e) {
      if (kDebugMode) {
        print('Error changing frequency: $e');
      }
      _setState(ScanningState.error);
    }
  }

  void _checkLockStatus(Timer timer) {
    if (_state != ScanningState.searching && _state != ScanningState.locked) {
      timer.cancel();
      return;
    }

    final now = DateTime.now();
    
    if (_state == ScanningState.locked) {
      // Check if we've lost lock (no activity for 10 seconds)
      if (_lastActivityTime != null) {
        final timeSinceActivity = now.difference(_lastActivityTime!);
        if (timeSinceActivity.inSeconds > 10) {
          if (kDebugMode) {
            print('Lost lock on ${_currentFrequency} MHz, trying next channel');
          }
          _hasLock = false;
          _currentChannelIndex++;
          _setState(ScanningState.searching);
          _tryNextControlChannel();
        }
      }
    } else if (_state == ScanningState.searching) {
      // If still searching after 8 seconds, try next channel
      if (_lastActivityTime != null && now.difference(_lastActivityTime!).inSeconds > 8) {
        if (kDebugMode) {
          print('No lock after 8 seconds, trying next channel');
        }
        _currentChannelIndex++;
        _tryNextControlChannel();
      }
    }
  }

  Future<void> stopScanning() async {
    _lockCheckTimer?.cancel();
    _lockCheckTimer = null;
    _onStop();
    _currentSiteId = null;
    _currentSiteName = null;
    _currentFrequency = null;
    _currentChannelIndex = 0;
    _controlChannels = [];
    _hasLock = false;
    _lastActivityTime = null;
    _setState(ScanningState.idle);
    
    if (kDebugMode) {
      print('Scanning stopped');
    }
  }

  void _setState(ScanningState newState) {
    _state = newState;
    notifyListeners();
  }

  @override
  void dispose() {
    _lockCheckTimer?.cancel();
    _outputSubscription?.cancel();
    super.dispose();
  }
}
