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
  stopping, // Added to prevent UI interaction during stop
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
  StreamSubscription? _signalSubscription;
  StreamSubscription? _networkSubscription;
  StreamSubscription? _patchSubscription;
  StreamSubscription? _gaSubscription;
  StreamSubscription? _affSubscription;
  StreamSubscription<Position>? _positionSubscription;
  
  bool _hasLock = false;
  DateTime? _lastActivityTime;
  bool _gpsHoppingEnabled = false;
  Position? _lastPosition;
  
  // Signal quality tracking
  int _tsbkCount = 0;
  int _parityMismatches = 0;
  DateTime? _lastTsbkTime;
  
  // Network information
  List<int> _neighborFreqs = []; // Neighbor site frequencies in Hz
  List<int> _neighborLastSeen = []; // Last seen timestamps for neighbors
  List<Map<String, dynamic>> _patches = []; // Active patches
  List<Map<String, dynamic>> _groupAttachments = []; // Group attachments
  List<Map<String, dynamic>> _affiliations = []; // Affiliated radios
  double? _downlinkFreq;
  double? _uplinkFreq;
  
  ScanningState get state => _state;
  int? get currentSiteId => _currentSiteId;
  String? get currentSiteName => _currentSiteName;
  int? get currentSystemId => _currentSystemId;
  double? get currentFrequency => _currentFrequency;
  int get currentChannelIndex => _currentChannelIndex;
  int get totalChannels => _controlChannels.length;
  bool get hasLock => _hasLock;
  bool get gpsHoppingEnabled => _gpsHoppingEnabled;
  Position? get lastPosition => _lastPosition;
  int get tsbkCount => _tsbkCount;
  int get parityMismatches => _parityMismatches;
  DateTime? get lastTsbkTime => _lastTsbkTime;
  List<int> get neighborFreqs => _neighborFreqs;
  List<int> get neighborLastSeen => _neighborLastSeen;
  List<Map<String, dynamic>> get patches => _patches;
  List<Map<String, dynamic>> get groupAttachments => _groupAttachments;
  List<Map<String, dynamic>> get affiliations => _affiliations;
  double? get downlinkFreq => _downlinkFreq;
  double? get uplinkFreq => _uplinkFreq;

  ScanningService(
    this._dsdPlugin,
    this._settingsService,
    this._onStart,
    this._onStop,
  ) {
    _listenToOutput();
    _listenToSignal();
    _listenToNetwork();
    _listenToPatches();
    _listenToGroupAttachments();
    _listenToAffiliations();
  }

  void _listenToOutput() {
    _outputSubscription = _dsdPlugin.outputStream.listen((line) {
      // Parse frequency information from P25 FREQ lines
      // Example: "  P25 FREQ: map ch=0x15BC -> 771.181250 MHz"
      if (line.contains('P25 FREQ:') && line.contains('MHz')) {
        if (kDebugMode) {
          print('DEBUG: Found P25 FREQ line: $line');
        }
        final freqMatch = RegExp(r'([0-9.]+)\s*MHz').firstMatch(line);
        
        if (freqMatch != null) {
          final freq = double.tryParse(freqMatch.group(1) ?? '');
          
          if (freq != null) {
            if (kDebugMode) {
              print('DEBUG: Parsed frequency: $freq MHz');
            }
            
            // Determine if downlink or uplink based on frequency range
            if (freq >= 851 && freq <= 870) {
              // 800 MHz band downlink
              _downlinkFreq = freq;
              if (kDebugMode) print('DEBUG: Set as 800 MHz downlink');
            } else if (freq >= 806 && freq <= 825) {
              // 800 MHz band uplink
              _uplinkFreq = freq;
              if (kDebugMode) print('DEBUG: Set as 800 MHz uplink');
            } else if (freq >= 762 && freq <= 776) {
              // 700 MHz band downlink
              _downlinkFreq = freq;
              if (kDebugMode) print('DEBUG: Set as 700 MHz downlink');
            } else if (freq >= 792 && freq <= 806) {
              // 700 MHz band uplink
              _uplinkFreq = freq;
              if (kDebugMode) print('DEBUG: Set as 700 MHz uplink');
            } else {
              if (kDebugMode) print('DEBUG: Frequency $freq MHz not in known band ranges');
            }
            
            notifyListeners();
          }
        }
      }
      
      // Note: Control channel lock detection now handled by _listenToSignal()
      // which uses DSD state fields instead of parsing logs
    });
  }
  
  void _listenToSignal() {
    _signalSubscription = _dsdPlugin.signalEventStream.listen((event) {
      // Update TSBK counts from DSD state (more reliable than parsing)
      final tsbkOk = event['tsbkOk'] as int;
      final tsbkErr = event['tsbkErr'] as int;
      final hasSync = event['hasSync'] as bool;
      
      // Update counters
      if (tsbkOk > _tsbkCount) {
        _tsbkCount = tsbkOk;
        _lastTsbkTime = DateTime.now();
      }
      _parityMismatches = tsbkErr;
      
      // Update lock status based on sync
      if (hasSync) {
        _hasLock = true;
        _lastActivityTime = DateTime.now();
        
        if (_state == ScanningState.searching) {
          _setState(ScanningState.locked);
          if (kDebugMode) {
            print('Control channel LOCKED at $_currentFrequency MHz');
          }
        }
      }
      
      notifyListeners();
    });
  }
  
  void _listenToNetwork() {
    _networkSubscription = _dsdPlugin.networkEventStream.listen((event) {
      // Update neighbor sites from DSD state
      final neighborCount = event['neighborCount'] as int;
      final neighborFreqList = event['neighborFreqs'] as List<dynamic>;
      final neighborLastSeenList = event['neighborLastSeen'] as List<dynamic>;
      
      // Convert to List<int>
      _neighborFreqs = neighborFreqList.map((freq) => freq as int).toList();
      _neighborLastSeen = neighborLastSeenList.map((ts) => ts as int).toList();
      
      if (kDebugMode) {
        print('Network update: $neighborCount neighbors');
        for (int i = 0; i < _neighborFreqs.length && i < 5; i++) {
          print('  Neighbor ${i+1}: ${(_neighborFreqs[i] / 1000000).toStringAsFixed(6)} MHz');
        }
      }
      
      notifyListeners();
    });
  }
  
  void _listenToPatches() {
    _patchSubscription = _dsdPlugin.patchEventStream.listen((event) {
      final patchCount = event['patchCount'] as int;
      final patchList = event['patches'] as List<dynamic>;
      
      _patches = patchList.map((p) => Map<String, dynamic>.from(p as Map)).toList();
      
      if (kDebugMode) {
        print('Patch update: $patchCount patches');
        for (var patch in _patches) {
          print('  Patch SGID ${patch['sgid']}: ${patch['wgidCount']} WGIDs, '
                '${patch['wuidCount']} WUIDs, active=${patch['active']}');
          // Print actual WGID values
          final wgids = patch['wgids'] as List<dynamic>;
          final wgidCount = patch['wgidCount'] as int;
          print('    WGIDs: ${wgids.take(wgidCount).join(", ")}');
          if (wgids.length > wgidCount) {
            print('    (Full array has ${wgids.length} slots, only $wgidCount are valid)');
          }
        }
      }
      
      notifyListeners();
    });
  }
  
  void _listenToGroupAttachments() {
    _gaSubscription = _dsdPlugin.groupAttachmentEventStream.listen((event) {
      final gaCount = event['gaCount'] as int;
      final attachmentList = event['attachments'] as List<dynamic>;
      
      _groupAttachments = attachmentList.map((a) => Map<String, dynamic>.from(a as Map)).toList();
      
      if (kDebugMode) {
        print('Group attachment update: $gaCount attachments');
        // Only log first few to avoid spam
        for (int i = 0; i < _groupAttachments.length && i < 5; i++) {
          final ga = _groupAttachments[i];
          print('  RID ${ga['rid']} on TG ${ga['tg']}');
        }
      }
      
      notifyListeners();
    });
  }
  
  void _listenToAffiliations() {
    _affSubscription = _dsdPlugin.affiliationEventStream.listen((event) {
      final affCount = event['affCount'] as int;
      final affList = event['affiliations'] as List<dynamic>;
      
      _affiliations = affList.map((a) => Map<String, dynamic>.from(a as Map)).toList();
      
      if (kDebugMode) {
        print('Affiliation update: $affCount affiliated radios');
      }
      
      notifyListeners();
    });
  }

  Future<void> startScanning(int siteId, String siteName, {int? systemId}) async {
    if (_state == ScanningState.stopping) {
      if (kDebugMode) print('Cannot start scanning while stopping');
      return;
    }
    
    if (_state != ScanningState.idle) {
      await stopScanning();
    }

    try {
      _currentSiteId = siteId;
      _currentSiteName = siteName;
      _currentChannelIndex = 0;
      _hasLock = false;
      _lastActivityTime = null;
      
      // Reset signal quality tracking
      _tsbkCount = 0;
      _parityMismatches = 0;
      _lastTsbkTime = null;
      
      // Reset network information
      _neighborFreqs.clear();
      _neighborLastSeen.clear();
      _patches.clear();
      _groupAttachments.clear();
      _affiliations.clear();
      _downlinkFreq = null;
      _uplinkFreq = null;
      
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
          // Device already open - need to stop engine, let it clean up USB, then restart with new frequency
          if (kDebugMode) {
            print('Retuning native USB RTL-SDR to ${_settingsService.frequencyHz} Hz');
          }
          
          // Clear our tracking of the USB device - engine will close it during stop
          _settingsService.clearNativeUsbDevice();
          
          // Stop the engine - this will close the USB device internally
          // Note: _onStop() calls async stop but we can't await it (blocks UI thread)
          // So we fire it and wait longer to ensure it completes
          _onStop();
          
          // Wait longer for engine to fully stop and release USB (native stop takes 3+ seconds)
          await Future.delayed(const Duration(milliseconds: 3500));
          
          // Re-open USB device with new frequency
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
          
          if (kDebugMode) {
            print('Reopened native USB RTL-SDR: fd=${result['fd']}, path=${result['devicePath']}');
          }
          
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
          
          // Start engine with new configuration
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
    _setState(ScanningState.stopping);
    
    _lockCheckTimer?.cancel();
    _lockCheckTimer = null;
    _stopGpsTracking();
    
    // Clear device tracking before stopping - engine will close USB during cleanup
    if (_settingsService.rtlSource == RtlSource.nativeUsb && _settingsService.hasNativeUsbDevice) {
      _settingsService.clearNativeUsbDevice();
    }
    
    // Stop engine - it will handle closing USB device internally
    // Note: Can't await stop as it blocks UI thread, so fire and wait
    _onStop();
    
    // Wait longer for engine to fully stop (native USB stop takes 3+ seconds)
    await Future.delayed(const Duration(milliseconds: 3500));
    
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
    _signalSubscription?.cancel();
    _networkSubscription?.cancel();
    _patchSubscription?.cancel();
    _gaSubscription?.cancel();
    _affSubscription?.cancel();
    _positionSubscription?.cancel();
    super.dispose();
  }
}
