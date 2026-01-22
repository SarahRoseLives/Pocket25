import 'dart:async';
import 'dart:math' as math;
import 'package:flutter/foundation.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:geolocator/geolocator.dart';
import 'package:shared_preferences/shared_preferences.dart';
import '../services/settings_service.dart';
import '../services/native_rtlsdr_service.dart';
import 'database_service.dart';

/// Helper class for computing nearest site in isolate
class _NearestSiteParams {
  final List<Map<String, dynamic>> sites;
  final double lat;
  final double lon;
  final int? currentSiteId;
  final Set<String> lockedSiteKeys; // Add locked sites filter
  
  _NearestSiteParams(this.sites, this.lat, this.lon, this.currentSiteId, this.lockedSiteKeys);
}

/// Result from nearest site computation
class _NearestSiteResult {
  final int? siteId;
  final String? siteName;
  final double distance;
  
  _NearestSiteResult(this.siteId, this.siteName, this.distance);
}

/// Haversine distance calculation (doesn't require Geolocator)
double _haversineDistance(double lat1, double lon1, double lat2, double lon2) {
  const earthRadius = 6371000.0; // meters
  final dLat = (lat2 - lat1) * (math.pi / 180);
  final dLon = (lon2 - lon1) * (math.pi / 180);
  final a = math.sin(dLat / 2) * math.sin(dLat / 2) +
      math.cos(lat1 * (math.pi / 180)) * math.cos(lat2 * (math.pi / 180)) *
      math.sin(dLon / 2) * math.sin(dLon / 2);
  final c = 2 * math.atan2(math.sqrt(a), math.sqrt(1 - a));
  return earthRadius * c;
}

/// Top-level function for compute() - finds nearest site
_NearestSiteResult _findNearestSite(_NearestSiteParams params) {
  Map<String, dynamic>? nearestSite;
  double nearestDistance = double.infinity;
  
  for (final site in params.sites) {
    final lat = site['latitude'] as double?;
    final lon = site['longitude'] as double?;
    final siteId = site['site_id'] as int?;
    final systemId = site['system_id'] as int?;
    
    // Skip locked sites
    if (systemId != null && siteId != null) {
      final siteKey = '${systemId}_$siteId';
      if (params.lockedSiteKeys.contains(siteKey)) {
        continue; // Skip this site
      }
    }
    
    if (lat != null && lon != null) {
      final distance = _haversineDistance(params.lat, params.lon, lat, lon);
      
      if (distance < nearestDistance) {
        nearestDistance = distance;
        nearestSite = site;
      }
    }
  }
  
  if (nearestSite != null) {
    return _NearestSiteResult(
      nearestSite['site_id'] as int,
      nearestSite['site_name'] as String,
      nearestDistance,
    );
  }
  
  return _NearestSiteResult(null, null, double.infinity);
}

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
  final Future<void> Function() _onStop;
  
  ScanningState _state = ScanningState.idle;
  int? _currentSiteId;
  int? _previousSiteId; // Track previous site for detecting site switches
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
  DateTime? _rtlTcpReconnectTime; // Track when we reconnected for buffer flush delay
  bool _gpsHoppingEnabled = false;
  Position? _lastPosition;
  
  // Signal quality tracking
  int _tsbkCount = 0;
  int _parityMismatches = 0;
  DateTime? _lastTsbkTime;
  
  // Retune freeze tracking - unfreeze when new CC locks
  bool _pendingRetuneUnfreeze = false;
  
  // Network information
  List<int> _neighborFreqs = []; // Neighbor site frequencies in Hz
  List<int> _neighborLastSeen = []; // Last seen timestamps for neighbors
  List<Map<String, dynamic>> _patches = []; // Active patches
  List<Map<String, dynamic>> _groupAttachments = []; // Group attachments
  List<Map<String, dynamic>> _affiliations = []; // Affiliated radios
  double? _downlinkFreq;
  double? _uplinkFreq;
  
  // HackRF state
  bool _hackrfStreaming = false;
  
  // RTL TCP state
  bool _rtlTcpConnected = false;
  
  // Locked sites for GPS hopping
  Set<String> _lockedSiteKeys = {}; // Format: "systemId_siteId"
  
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
              _updateChannelIndexFromFrequency(freq);
            } else if (freq >= 806 && freq <= 825) {
              // 800 MHz band uplink
              _uplinkFreq = freq;
              if (kDebugMode) print('DEBUG: Set as 800 MHz uplink');
            } else if (freq >= 762 && freq <= 776) {
              // 700 MHz band downlink
              _downlinkFreq = freq;
              if (kDebugMode) print('DEBUG: Set as 700 MHz downlink');
              _updateChannelIndexFromFrequency(freq);
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
      
      // For rtl_tcp: ignore lock for 8 seconds after reconnect to let buffer flush
      // Buffered samples from old frequency will cause false locks and auto-retune
      if (_settingsService.rtlSource == RtlSource.remote && _rtlTcpReconnectTime != null) {
        final timeSinceReconnect = DateTime.now().difference(_rtlTcpReconnectTime!);
        if (timeSinceReconnect.inSeconds < 8) {
          if (kDebugMode && hasSync) {
            print('Ignoring lock during rtl_tcp buffer flush (${timeSinceReconnect.inSeconds}s/${8}s)');
          }
          return; // Skip lock processing during flush period
        } else {
          // Flush period complete - reset P25 state if this was a site switch
          // to clear frequency tables learned from old buffered data
          if (_pendingRetuneUnfreeze) {
            if (kDebugMode) {
              print('Buffer flush complete - resetting P25 state (keeping retunes frozen for site switch)');
            }
            _dsdPlugin.resetP25State();
            _pendingRetuneUnfreeze = false;
            // NOTE: We intentionally do NOT unfreeze retunes here for site switches
            // Voice grant following would retune to the old site which still has signal
            // Explicit CC hop retunes bypass the freeze, so scanning still works
          }
          _rtlTcpReconnectTime = null;
        }
      }
      
      // For native USB site switches: reset P25 state when we first get sync
      // This clears frequency tables from old site before DSD can use them for grants
      if (_settingsService.rtlSource == RtlSource.nativeUsb && _pendingRetuneUnfreeze && hasSync) {
        if (kDebugMode) {
          print('Native USB site switch - resetting P25 state (keeping retunes frozen)');
        }
        _dsdPlugin.resetP25State();
        _pendingRetuneUnfreeze = false;
        // Keep retunes frozen - explicit CC hop retunes bypass the freeze
      }
      
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
      
      // Update activity time when we receive network data (indicates valid P25 signal)
      _lastActivityTime = DateTime.now();
      
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
    
    // Detect if this is a site switch (not just restarting same site)
    // Must capture current site BEFORE stopScanning() clears it
    final int? oldSiteId = _currentSiteId;
    final bool isSiteSwitch = _state != ScanningState.idle && oldSiteId != null && oldSiteId != siteId;
    
    if (_state != ScanningState.idle) {
      await stopScanning();
    }

    try {
      _previousSiteId = oldSiteId;
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
      
      // Load control channels for this site first (needed immediately)
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
        if (isSiteSwitch) {
          print('Site switch detected ($_previousSiteId -> $siteId), will freeze retunes');
        }
      }
      
      _setState(ScanningState.searching);
      await _tryNextControlChannel(freezeRetunes: isSiteSwitch);
      
      // Start lock check timer
      _lockCheckTimer = Timer.periodic(const Duration(seconds: 5), _checkLockStatus);
      
      // Load all sites for GPS hopping in background (deferred - not blocking UI)
      if (_currentSystemId != null) {
        // Use unawaited to load sites asynchronously without blocking
        _loadSitesForGpsHopping(_currentSystemId!);
      }
      
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

  Future<void> _tryNextControlChannel({bool freezeRetunes = false}) async {
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
      print('Current RTL Source: ${_settingsService.rtlSource}');
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
          
          // Freeze retunes during initial startup to prevent old buffered data issues
          if (kDebugMode) {
            print('Freezing retunes during native USB startup');
          }
          await _dsdPlugin.setRetuneFrozen(true);
          
          // For site switches, mark pending unfreeze so the timer below won't unfreeze
          if (freezeRetunes) {
            _pendingRetuneUnfreeze = true;
            if (kDebugMode) {
              print('Site switch - retunes will stay frozen');
            }
          }
          
          // Start the engine
          _onStart();
          
          // Unfreeze retunes after buffer settles (5 seconds for native USB)
          // But only if this is NOT a site switch (site switches keep freeze until stop)
          Future.delayed(const Duration(seconds: 5), () async {
            // Don't unfreeze if a site switch set _pendingRetuneUnfreeze
            if (!_pendingRetuneUnfreeze) {
              if (kDebugMode) {
                print('Unfreezing retunes after native USB startup');
              }
              await _dsdPlugin.setRetuneFrozen(false);
            } else {
              if (kDebugMode) {
                print('Skipping unfreeze - site switch in progress');
              }
            }
          });
        } else {
          // Device already open - need to stop engine, let it clean up USB, then restart with new frequency
          if (kDebugMode) {
            print('Retuning native USB RTL-SDR to ${_settingsService.frequencyHz} Hz');
          }
          
          // Only freeze retunes during site switch, not control channel hopping
          if (freezeRetunes) {
            if (kDebugMode) {
              print('Freezing retunes during native USB site switch');
            }
            await _dsdPlugin.setRetuneFrozen(true);
          }
          
          // Clear our tracking of the USB device - engine will close it during stop
          _settingsService.clearNativeUsbDevice();
          
          // Stop the engine - must await to ensure USB is released
          await _onStop();
          
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
          
          // Mark that we need to unfreeze when CC locks (only if we froze)
          if (freezeRetunes) {
            _pendingRetuneUnfreeze = true;
            if (kDebugMode) {
              print('Waiting for CC lock to unfreeze retunes');
            }
          }
        }
      } else if (_settingsService.rtlSource == RtlSource.hackrf) {
        // HackRF mode - use direct dsd_flutter HackRF support
        if (!_hackrfStreaming) {
          // First time - initialize HackRF and start DSD
          if (kDebugMode) {
            print('Initializing HackRF mode...');
          }
          
          // Start DSD in HackRF mode (this also initializes HackRF)
          final dsdSuccess = await _dsdPlugin.startHackRfMode(
            _settingsService.frequencyHz,
            _settingsService.sampleRate,
          );
          
          if (!dsdSuccess) {
            throw Exception('Failed to start DSD in HackRF mode');
          }
          
          if (kDebugMode) {
            print('DSD started in HackRF mode');
          }
          
          // Configure HackRF via dsd_flutter
          await _dsdPlugin.hackrfSetFrequency(_settingsService.frequencyHz);
          await _dsdPlugin.hackrfSetSampleRate(_settingsService.sampleRate);
          await _dsdPlugin.hackrfSetLnaGain(_settingsService.hackrfLnaGain);
          await _dsdPlugin.hackrfSetVgaGain(_settingsService.hackrfVgaGain);
          
          if (kDebugMode) {
            print('Configured HackRF: freq=${_settingsService.frequencyHz} Hz, '
                  'sampleRate=${_settingsService.sampleRate} Hz, '
                  'lnaGain=${_settingsService.hackrfLnaGain} dB, '
                  'vgaGain=${_settingsService.hackrfVgaGain} dB');
          }
          
          // Start RX - samples go directly to DSD pipe via native thread
          final rxSuccess = await _dsdPlugin.hackrfStartRx();
          if (!rxSuccess) {
            throw Exception('Failed to start HackRF RX');
          }
          
          _hackrfStreaming = true;
          
          // Start the DSD engine
          _onStart();
        } else {
          // Device already open - retune to new frequency
          if (kDebugMode) {
            print('Retuning HackRF to ${_settingsService.frequencyHz} Hz');
          }
          
          await _dsdPlugin.hackrfSetFrequency(_settingsService.frequencyHz);
          
          // No need to restart engine for frequency change
          notifyListeners();
        }
      } else {
        // Remote rtl_tcp mode
        if (!_rtlTcpConnected || freezeRetunes) {
          // First connection or site switch - need full stop/reconnect/start
          
          // Only freeze retunes during site switch to prevent old buffered
          // P25 grants from retuning back to old site frequencies
          if (freezeRetunes) {
            if (kDebugMode) {
              print('Freezing retunes during rtl_tcp site switch');
            }
            await _dsdPlugin.setRetuneFrozen(true);
          }
          
          if (kDebugMode) {
            print('Reconnecting rtl_tcp at ${_settingsService.effectiveHost}:${_settingsService.effectivePort} freq ${_settingsService.frequencyHz} Hz');
          }
          
          // Reconnect with new frequency (DSD is already stopped from stopScanning())
          await _dsdPlugin.connect(
            _settingsService.effectiveHost,
            _settingsService.effectivePort,
            _settingsService.frequencyHz,
          );
          
          _rtlTcpConnected = true;
          
          if (kDebugMode) {
            if (freezeRetunes) {
              print('Starting DSD with rtl_tcp at ${_settingsService.frequencyHz} Hz (retunes frozen until CC lock)');
            } else {
              print('Starting DSD with rtl_tcp at ${_settingsService.frequencyHz} Hz');
            }
          }
          
          // Start DSD
          _onStart();
          
          // For site switches, explicitly retune to ensure rtl_tcp server changes frequency
          // The connect() only configures DSD's input string, not the rtl_tcp server
          if (freezeRetunes) {
            if (kDebugMode) {
              print('Sending explicit retune command to rtl_tcp server');
            }
            // Brief delay to let engine initialize
            await Future.delayed(const Duration(milliseconds: 100));
            await _dsdPlugin.retune(_settingsService.frequencyHz);
            
            // Keep retunes frozen until we lock on the new site
            // The freeze prevents DSD from auto-retuning back to old site frequencies
            // that it learns from buffered data. We'll unfreeze when CC lock is detected.
            _pendingRetuneUnfreeze = true;
            if (kDebugMode) {
              print('Retunes frozen until CC lock on new site');
            }
          }
          
          // Set reconnect time to enable buffer flush period
          _rtlTcpReconnectTime = DateTime.now();
        } else {
          // Already connected - just retune without restarting DSD
          // This preserves P25 state machine for control channel hopping
          if (kDebugMode) {
            print('Retuning rtl_tcp to ${_settingsService.frequencyHz} Hz (fast retune, no restart)');
          }
          
          final success = await _dsdPlugin.retune(_settingsService.frequencyHz);
          
          if (!success) {
            if (kDebugMode) {
              print('Retune failed, will try reconnect on next hop');
            }
            _rtlTcpConnected = false;
          } else {
            // Set reconnect time to enable buffer flush period
            // Even with fast retune, rtl_tcp server still has buffered samples
            _rtlTcpReconnectTime = DateTime.now();
          }
        }
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
      // Activity is updated by network/patch/GA/AFF events (valid P25 data)
      if (_lastActivityTime != null) {
        final timeSinceActivity = now.difference(_lastActivityTime!);
        if (timeSinceActivity.inSeconds > 10) {
          if (kDebugMode) {
            print('Lost lock on ${_currentFrequency} MHz (no data for 10s), trying next channel');
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
    
    // Ensure retunes are unfrozen when stopping
    _pendingRetuneUnfreeze = false;
    await _dsdPlugin.setRetuneFrozen(false);
    
    // Clear device tracking before stopping - engine will close USB during cleanup
    if (_settingsService.rtlSource == RtlSource.nativeUsb && _settingsService.hasNativeUsbDevice) {
      _settingsService.clearNativeUsbDevice();
    }
    
    // Stop HackRF if running
    if (_settingsService.rtlSource == RtlSource.hackrf && _hackrfStreaming) {
      await _dsdPlugin.hackrfStopRx();
      await _dsdPlugin.stopHackRfMode();
      _hackrfStreaming = false;
      if (kDebugMode) {
        print('HackRF stopped and DSD HackRF mode stopped');
      }
    }
    
    // Reset rtl_tcp connection tracking
    _rtlTcpConnected = false;
    
    // Stop engine - it will handle closing USB device internally
    // Must await to ensure DSP is fully stopped before starting new scan
    await _onStop();
    
    // Clear state after DSP stops
    _clearSystemState();
    _setState(ScanningState.idle);
    
    if (kDebugMode) {
      print('Scanning stopped');
    }
  }
  
  /// Clear system/site state (used when switching to Quick Scan mode)
  void _clearSystemState() {
    _currentSiteId = null;
    _currentSiteName = null;
    _currentSystemId = null;
    _currentFrequency = null;
    _currentChannelIndex = 0;
    _controlChannels = [];
    _allSystemSites = [];
    _hasLock = false;
    _lastActivityTime = null;
    _pendingRetuneUnfreeze = false;
    _rtlTcpConnected = false; // Reset connection tracking
    notifyListeners();
  }
  
  /// Clear current system selection (for Quick Scan mode)
  void clearCurrentSystem() {
    _clearSystemState();
  }

  void _setState(ScanningState newState) {
    _state = newState;
    notifyListeners();
  }
  
  /// Update the current channel index based on the actual frequency we're tuned to
  void _updateChannelIndexFromFrequency(double freq) {
    // Only update if this frequency matches what we think we're tuned to
    // P25 systems broadcast info about ALL channels, not just the one we're on
    if (_currentFrequency != null && ((_currentFrequency! - freq).abs() > 0.001)) {
      // This is a different channel being advertised, not the one we're on
      return;
    }
    
    // Find which control channel matches this frequency (within 0.001 MHz tolerance)
    for (int i = 0; i < _controlChannels.length; i++) {
      final channelFreq = _controlChannels[i]['frequency'] as double;
      if ((channelFreq - freq).abs() < 0.001) {
        if (_currentChannelIndex != i) {
          _currentChannelIndex = i;
          if (kDebugMode) {
            print('Updated channel index to ${i + 1}/${_controlChannels.length} based on frequency $freq MHz');
          }
          notifyListeners();
        }
        return;
      }
    }
    
    // If we didn't find a match, log it
    if (kDebugMode) {
      print('Warning: Could not find channel index for frequency $freq MHz');
    }
  }
  
  /// Load sites for GPS hopping in background (non-blocking)
  Future<void> _loadSitesForGpsHopping(int systemId) async {
    try {
      _allSystemSites = await _db.getSitesBySystem(systemId);
      if (kDebugMode) {
        print('Loaded ${_allSystemSites.length} sites for system $systemId (background)');
      }
    } catch (e) {
      if (kDebugMode) {
        print('Error loading sites for GPS hopping: $e');
      }
    }
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
      // Use compute() to find nearest site off the main thread
      final result = await compute(
        _findNearestSite,
        _NearestSiteParams(
          _allSystemSites,
          position.latitude,
          position.longitude,
          _currentSiteId,
          _lockedSiteKeys, // Pass locked sites to isolate
        ),
      );
      
      // Switch to nearest site if different from current and within reasonable range
      if (result.siteId != null && result.siteName != null) {
        // Only switch if:
        // 1. It's a different site
        // 2. Distance is reasonable (< 100km)
        if (result.siteId != _currentSiteId && result.distance < 100000) {
          if (kDebugMode) {
            print('GPS Hopping: Switching from $_currentSiteName to ${result.siteName} (${(result.distance / 1000).toStringAsFixed(1)} km away)');
          }
          
          await startScanning(result.siteId!, result.siteName!, systemId: _currentSystemId);
        }
      }
    } catch (e) {
      if (kDebugMode) {
        print('Error checking nearest site: $e');
      }
    }
  }
  
  // Site lockout methods
  
  /// Load locked sites from SharedPreferences
  Future<void> loadLockedSites() async {
    final prefs = await SharedPreferences.getInstance();
    final locked = prefs.getStringList('locked_sites') ?? [];
    _lockedSiteKeys = locked.toSet();
    if (kDebugMode) {
      print('Loaded ${_lockedSiteKeys.length} locked sites');
    }
  }
  
  /// Save locked sites to SharedPreferences
  Future<void> _saveLockedSites() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.setStringList('locked_sites', _lockedSiteKeys.toList());
  }
  
  /// Check if a site is locked
  bool isSiteLocked(int systemId, int siteId) {
    return _lockedSiteKeys.contains('${systemId}_$siteId');
  }
  
  /// Check if current site is locked
  bool get isCurrentSiteLocked {
    if (_currentSystemId == null || _currentSiteId == null) return false;
    return isSiteLocked(_currentSystemId!, _currentSiteId!);
  }
  
  /// Toggle lock state of a site
  Future<void> toggleSiteLock(int systemId, int siteId) async {
    final key = '${systemId}_$siteId';
    final wasLocked = _lockedSiteKeys.contains(key);
    
    if (wasLocked) {
      _lockedSiteKeys.remove(key);
      if (kDebugMode) {
        print('Unlocked site: $key');
      }
    } else {
      _lockedSiteKeys.add(key);
      if (kDebugMode) {
        print('Locked site: $key');
      }
      
      // If we just locked the current site and GPS hopping is enabled,
      // immediately check for next nearest site
      if (_gpsHoppingEnabled && 
          systemId == _currentSystemId && 
          siteId == _currentSiteId &&
          _lastPosition != null) {
        if (kDebugMode) {
          print('Locked current site - searching for next nearest site...');
        }
        // Trigger immediate site check
        await _checkNearestSite(_lastPosition!);
      }
    }
    await _saveLockedSites();
    notifyListeners();
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
