import 'dart:async';
import 'package:flutter/foundation.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:geolocator/geolocator.dart';
import '../services/settings_service.dart';
import '../services/native_rtlsdr_service.dart';
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
  int? _currentSystemId;
  double? _currentFrequency;
  int _currentChannelIndex = 0;
  List<Map<String, dynamic>> _controlChannels = [];
  List<Map<String, dynamic>> _allSystemSites = [];
  Timer? _lockCheckTimer;
  StreamSubscription? _outputSubscription;
  StreamSubscription<Position>? _positionSubscription;
  
  bool _hasLock = false;
  DateTime? _lastActivityTime;
  bool _gpsHoppingEnabled = false;
  Position? _lastPosition;
  
  ScanningState get state => _state;
  int? get currentSiteId => _currentSiteId;
  String? get currentSiteName => _currentSiteName;
  double? get currentFrequency => _currentFrequency;
  int get currentChannelIndex => _currentChannelIndex;
  int get totalChannels => _controlChannels.length;
  bool get hasLock => _hasLock;
  bool get gpsHoppingEnabled => _gpsHoppingEnabled;
  Position? get lastPosition => _lastPosition;

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

  Future<void> startScanning(int siteId, String siteName, {int? systemId}) async {
    if (_state != ScanningState.idle) {
      await stopScanning();
    }

    try {
      _currentSiteId = siteId;
      _currentSiteName = siteName;
      _currentChannelIndex = 0;
      _hasLock = false;
      _lastActivityTime = null;
      
      // Get system ID if not provided
      if (systemId != null) {
        _currentSystemId = systemId;
      } else {
        _currentSystemId = await _db.getSystemIdForSite(siteId);
      }
      
      // Load all sites for GPS hopping
      if (_currentSystemId != null) {
        _allSystemSites = await _db.getSitesBySystem(_currentSystemId!);
        if (kDebugMode) {
          print('Loaded ${_allSystemSites.length} sites for system $_currentSystemId');
        }
      }
      
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
      
      // Start GPS tracking if hopping is enabled
      if (_gpsHoppingEnabled) {
        _startGpsTracking();
      }
      
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
      // Update frequency in settings
      _settingsService.updateFrequency(_currentFrequency!);
      
      if (_settingsService.rtlSource == RtlSource.nativeUsb) {
        // Native USB mode - use built-in RTL-SDR support
        if (!_settingsService.hasNativeUsbDevice) {
          // First time - need to open device and start engine
          final devices = await NativeRtlSdrService.listDevices();
          if (devices.isEmpty) {
            throw Exception('No RTL-SDR USB devices found');
          }
          
          final result = await NativeRtlSdrService.openDevice(devices.first.deviceName);
          if (result == null) {
            throw Exception('Failed to open RTL-SDR USB device');
          }
          
          _settingsService.setNativeUsbDevice(
            result['fd'] as int,
            result['devicePath'] as String,
          );
          
          if (kDebugMode) {
            print('Opened native USB RTL-SDR: fd=${result['fd']}, path=${result['devicePath']}');
          }
          
          // Configure native USB connection
          final success = await _dsdPlugin.connectNativeUsb(
            fd: result['fd'] as int,
            devicePath: result['devicePath'] as String,
            freqHz: _settingsService.frequencyHz,
            sampleRate: _settingsService.sampleRate,
            gain: _settingsService.gain * 10, // Convert to tenths of dB
            ppm: _settingsService.ppm,
          );
          
          if (!success) {
            throw Exception('Failed to configure native RTL-SDR');
          }
          
          // Start the engine
          _onStart();
        } else {
          // Device already open - just change frequency without restart
          if (kDebugMode) {
            print('Retuning native USB RTL-SDR to ${_settingsService.frequencyHz} Hz');
          }
          
          // Update frequency in opts for next engine run
          final success = await _dsdPlugin.setNativeRtlFrequency(_settingsService.frequencyHz);
          if (!success) {
            if (kDebugMode) {
              print('Warning: Failed to set frequency via setNativeRtlFrequency');
            }
          }
          
          // For now, we need to stop and restart to pick up the new frequency
          // TODO: Implement live retuning in the engine
          _onStop();
          await Future.delayed(const Duration(milliseconds: 300));
          
          // Re-open USB device since stopping closes it
          final devices = await NativeRtlSdrService.listDevices();
          if (devices.isEmpty) {
            throw Exception('No RTL-SDR USB devices found');
          }
          
          final result = await NativeRtlSdrService.openDevice(devices.first.deviceName);
          if (result == null) {
            throw Exception('Failed to re-open RTL-SDR USB device');
          }
          
          _settingsService.setNativeUsbDevice(
            result['fd'] as int,
            result['devicePath'] as String,
          );
          
          // Configure with new frequency
          final configSuccess = await _dsdPlugin.connectNativeUsb(
            fd: result['fd'] as int,
            devicePath: result['devicePath'] as String,
            freqHz: _settingsService.frequencyHz,
            sampleRate: _settingsService.sampleRate,
            gain: _settingsService.gain * 10,
            ppm: _settingsService.ppm,
          );
          
          if (!configSuccess) {
            throw Exception('Failed to configure native RTL-SDR');
          }
          
          _onStart();
        }
      } else {
        // Remote rtl_tcp mode - stop/start approach
        _onStop();
        await Future.delayed(const Duration(milliseconds: 500));
        
        await _dsdPlugin.connect(
          _settingsService.effectiveHost,
          _settingsService.effectivePort,
          _settingsService.frequencyHz,
        );
        
        _onStart();
      }
      
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
    _stopGpsTracking();
    _onStop();
    
    // Clean up native USB if used
    if (_settingsService.rtlSource == RtlSource.nativeUsb && _settingsService.hasNativeUsbDevice) {
      await _dsdPlugin.disconnectNativeUsb();
      await NativeRtlSdrService.closeDevice();
      _settingsService.clearNativeUsbDevice();
    }
    
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

  void enableGpsHopping(bool enabled) {
    _gpsHoppingEnabled = enabled;
    
    if (enabled && _state != ScanningState.idle) {
      _startGpsTracking();
    } else {
      _stopGpsTracking();
    }
    
    notifyListeners();
    
    if (kDebugMode) {
      print('GPS hopping ${enabled ? "enabled" : "disabled"}');
    }
  }

  void _startGpsTracking() {
    _stopGpsTracking(); // Cancel existing subscription
    
    if (kDebugMode) {
      print('Starting GPS tracking for site hopping');
    }
    
    const locationSettings = LocationSettings(
      accuracy: LocationAccuracy.high,
      distanceFilter: 1000, // Update every 1km
    );
    
    _positionSubscription = Geolocator.getPositionStream(
      locationSettings: locationSettings,
    ).listen((Position position) {
      _lastPosition = position;
      _checkNearestSite(position);
      notifyListeners();
    });
  }

  void _stopGpsTracking() {
    _positionSubscription?.cancel();
    _positionSubscription = null;
  }

  Future<void> _checkNearestSite(Position position) async {
    if (_allSystemSites.isEmpty || _currentSiteId == null) return;
    
    try {
      // Find nearest site
      Map<String, dynamic>? nearestSite;
      double nearestDistance = double.infinity;
      
      for (final site in _allSystemSites) {
        final lat = site['latitude'] as double?;
        final lon = site['longitude'] as double?;
        
        if (lat != null && lon != null) {
          final distance = Geolocator.distanceBetween(
            position.latitude,
            position.longitude,
            lat,
            lon,
          );
          
          if (distance < nearestDistance) {
            nearestDistance = distance;
            nearestSite = site;
          }
        }
      }
      
      // Switch to nearest site if different from current and within reasonable range
      if (nearestSite != null) {
        final nearestSiteId = nearestSite['site_id'] as int;
        final nearestSiteName = nearestSite['site_name'] as String;
        
        // Only switch if:
        // 1. It's a different site
        // 2. Distance is reasonable (< 100km)
        if (nearestSiteId != _currentSiteId && nearestDistance < 100000) {
          if (kDebugMode) {
            print('GPS Hopping: Switching from $_currentSiteName to $nearestSiteName (${(nearestDistance / 1000).toStringAsFixed(1)} km away)');
          }
          
          await startScanning(nearestSiteId, nearestSiteName, systemId: _currentSystemId);
        }
      }
    } catch (e) {
      if (kDebugMode) {
        print('Error checking nearest site: $e');
      }
    }
  }

  @override
  void dispose() {
    _lockCheckTimer?.cancel();
    _outputSubscription?.cancel();
    _positionSubscription?.cancel();
    super.dispose();
  }
}
