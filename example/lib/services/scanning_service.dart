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
  String _currentModulation = 'auto';
  // Last system scan started by the user, for "Resume" on the Scanner screen
  int? _lastSiteId;
  String? _lastSiteName;
  int? _lastSystemId;
  String? get lastSiteName => _lastSiteName;
  bool get canResumeLastSite => _lastSiteId != null && _lastSiteName != null;
  // Incremented each time a new system scan session starts (user start, Resume,
  // site change). Internal control-channel cycling does NOT change it.
  int _sessionCounter = 0;
  int get sessionCounter => _sessionCounter;
  /// Control channel frequencies (MHz) of the current site, in priority order.
  List<double> get controlChannelFrequencies => _controlChannels
      .map((c) => (c['frequency'] as num?)?.toDouble())
      .whereType<double>()
      .toList();
  /// True while a trunked system scan is active (searching or locked).
  bool get isSystemScanActive =>
      _state == ScanningState.searching || _state == ScanningState.locked;
  String get currentModulation => _currentModulation;
  String? _currentChannelName; // For Quick Scan or Conventional channel name
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
  DateTime? _lastSyncTime; // last signal event with sync (Pocket25 LSM patch)
  // Message shown on the Scanner screen when no radio was found at scan start.
  String? _deviceError;
  String? get deviceError => _deviceError;

  // No-signal watchdog (Pocket25 LSM patch): when a system scan hears no
  // control channel at all for [noSignalSeconds], warn on the Scanner screen
  // and in the log; optionally restart the radio connection once (a software
  // "unplug and replug"), which clears a dongle whose sample stream stalled.
  static const int noSignalSeconds = 180;
  DateTime? _huntSince;            // start of the current scan attempt
  bool _noSignalReported = false;  // warned for the current quiet stretch
  bool _noSignalRestartTried = false;
  bool _watchdogRestarting = false;
  DateTime? _noSignalFirstSeen;    // when the first warning fired
  String? _noSignalWarning;
  String? get noSignalWarning => _noSignalWarning;
  /// Set by the app to receive lines for the Log tab.
  void Function(String line)? onLog;
  DateTime? _rtlTcpReconnectTime; // Track when we reconnected for buffer flush delay
  DateTime? _channelSwitchTime; // Track when we switched channels to ignore stale data
  bool _gpsHoppingEnabled = false;
  Position? _lastPosition;
  
  // Signal quality tracking
  int _tsbkCount = 0;
  int _tsbkErrors = 0;
  int _parityMismatches = 0;
  DateTime? _lastTsbkTime;
  int _consecutiveSyncs = 0; // Track consecutive syncs for reliable lock detection
  bool _isP25 = false; // Track if current protocol is P25 (for BER display)
  
  // Retune freeze tracking - unfreeze when new CC locks
  bool _pendingRetuneUnfreeze = false;
  
  // Channel quality tracking - remember which channels work
  Map<int, int> _channelSuccessCount = {}; // channel index -> success count
  int? _lastKnownGoodChannel; // Last channel that had reliable lock
  
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
  String? get currentChannelName => _currentChannelName;
  int? get currentSystemId => _currentSystemId;
  double? get currentFrequency => _currentFrequency;
  int get currentChannelIndex => _currentChannelIndex;
  int get totalChannels => _controlChannels.length;
  bool get hasLock => _hasLock;
  bool get gpsHoppingEnabled => _gpsHoppingEnabled;
  Position? get lastPosition => _lastPosition;
  bool get isP25 => _isP25;
  int get tsbkCount => _tsbkCount;
  int get tsbkErrors => _tsbkErrors;
  int get parityMismatches => _parityMismatches;
  DateTime? get lastTsbkTime => _lastTsbkTime;
  
  // Calculate BER (Bit Error Rate) as a percentage
  // Note: Only works for P25 - DMR doesn't expose FEC counters, will show 0%
  double get ber {
    final total = _tsbkCount + _tsbkErrors;
    if (total == 0) return 0.0;
    return (_tsbkErrors / total) * 100.0;
  }
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
      // Note: These are output for ALL frequency lookups, not just the channel we're tuned to
      // So we only use these for tracking downlink/uplink reference, not for updating the display
      if (line.contains('P25 FREQ:') && line.contains('MHz')) {
        final freqMatch = RegExp(r'([0-9.]+)\s*MHz').firstMatch(line);
        
        if (freqMatch != null) {
          final freq = double.tryParse(freqMatch.group(1) ?? '');
          
          if (freq != null) {
            // Track downlink/uplink for reference only
            if (freq >= 851 && freq <= 870) {
              _downlinkFreq = freq;
            } else if (freq >= 806 && freq <= 825) {
              _uplinkFreq = freq;
            } else if (freq >= 762 && freq <= 776) {
              _downlinkFreq = freq;
            } else if (freq >= 792 && freq <= 806) {
              _uplinkFreq = freq;
            }
            // Don't call notifyListeners() here - too frequent
          }
        }
      }
      
      // Note: Control channel lock detection now handled by _listenToSignal()
      // which uses DSD state fields instead of parsing logs
    });
  }
  
  void _listenToSignal() {
    _signalSubscription = _dsdPlugin.signalEventStream.listen((event) {
      if (kDebugMode) {
        print('DEBUG Signal Event: $event');
      }
      
      // Update TSBK counts from DSD state (more reliable than parsing)
      // Note: These counters are P25-specific (p25_p1_fec_ok/err)
      // For DMR, these values will remain at 0, so BER will show 0%
      final tsbkOk = event['tsbkOk'] as int? ?? 0;
      final tsbkErr = event['tsbkErr'] as int? ?? 0;
      final hasSync = event['hasSync'] as bool? ?? false;
      final synctype = event['synctype'] as int? ?? -1;
      
      // Detect protocol from synctype integer
      // DMR: 10-13 (BS), 32-34 (MS/RC)
      // P25: 0-1 (Phase 1), 35-36 (Phase 2)
      final isDMR = (synctype >= 10 && synctype <= 13) || (synctype >= 32 && synctype <= 34);
      final isP25 = (synctype >= 0 && synctype <= 1) || (synctype >= 35 && synctype <= 36);
      
      // Update protocol state
      if (hasSync && isP25) {
        _isP25 = true;
      } else if (hasSync && isDMR) {
        _isP25 = false;
      }
      
      if (kDebugMode && hasSync) {
        print('Sync detected: synctype=$synctype (DMR: $isDMR, P25: $isP25)');
      }
      
      // Update counters (only meaningful for P25)
      if (isP25) {
        _recordBerSample(tsbkOk, tsbkErr);
        if (tsbkOk > _tsbkCount) {
          _tsbkCount = tsbkOk;
          _lastTsbkTime = DateTime.now();
        }
        if (tsbkErr > _tsbkErrors) {
          _tsbkErrors = tsbkErr;
        }
      }
      _parityMismatches = tsbkErr;
      
      // Ignore data during channel switch settle time (3 seconds)
      // This prevents stale buffered data from the previous channel causing false locks
      if (_channelSwitchTime != null) {
        final timeSinceSwitch = DateTime.now().difference(_channelSwitchTime!);
        if (timeSinceSwitch.inSeconds < 3) {
          if (kDebugMode && hasSync) {
            print('Ignoring sync during channel settle (${timeSinceSwitch.inMilliseconds}ms/3000ms)');
          }
          return; // Skip lock processing during settle period
        } else {
          _channelSwitchTime = null; // Settle period complete
        }
      }
      
      // For rtl_tcp: ignore lock for 12 seconds after reconnect to let buffer flush
      // Buffered samples from old frequency will cause false locks and auto-retune
      // Increased from 8 to 12 seconds for larger buffers
      if (_settingsService.rtlSource == RtlSource.remote && _rtlTcpReconnectTime != null) {
        final timeSinceReconnect = DateTime.now().difference(_rtlTcpReconnectTime!);
        if (timeSinceReconnect.inSeconds < 12) {
          if (kDebugMode && hasSync) {
            print('Ignoring sync during rtl_tcp buffer flush (${timeSinceReconnect.inSeconds}s/12s)');
          }
          // Also reset consecutive syncs to prevent false lock after flush
          _consecutiveSyncs = 0;
          return; // Skip lock processing during flush period
        } else {
          // Flush period complete - reset protocol state if this was a site switch
          // to clear frequency tables learned from old buffered data
          if (_pendingRetuneUnfreeze) {
            if (kDebugMode) {
              print('Buffer flush complete - resetting protocol state and unfreezing retunes');
            }
            // Reset P25 state for P25 systems
            if (isP25) {
              _dsdPlugin.resetP25State();
            }
            // TODO: Add resetDmrState() when available in plugin
            _pendingRetuneUnfreeze = false;
            // Unfreeze retunes now that old data is flushed - voice grants can now work
            _dsdPlugin.setRetuneFrozen(false);
            // Reset consecutive syncs - require fresh syncs after flush
            _consecutiveSyncs = 0;
          }
          _rtlTcpReconnectTime = null;
        }
      }
      
      // Update lock status based on sync (works for both P25 and DMR)
      // Require multiple consecutive syncs to declare lock (prevents false lock from stale data)
      if (hasSync) {
        _consecutiveSyncs++;
        _lastActivityTime = DateTime.now();
        _lastSyncTime = _lastActivityTime;
        if (_noSignalWarning != null) {
          final first = _noSignalFirstSeen;
          final mins = first == null ? 0 : DateTime.now().difference(first).inMinutes + noSignalSeconds ~/ 60;
          onLog?.call('[watch] Control channel heard again'
              '${mins > 0 ? ' after about $mins min of silence' : ''}');
          _noSignalWarning = null;
          _noSignalFirstSeen = null;
        }
        _noSignalReported = false;
        _noSignalRestartTried = false;

        // Require at least 3 consecutive syncs to declare lock
        if (_consecutiveSyncs >= 3 && !_hasLock) {
          _hasLock = true;
          
          // Track this channel as successful
          _channelSuccessCount[_currentChannelIndex] = 
              (_channelSuccessCount[_currentChannelIndex] ?? 0) + 1;
          _lastKnownGoodChannel = _currentChannelIndex;
          
          // For native USB: unfreeze retunes after confirming reliable lock
          // This ensures we don't unfreeze based on stale buffered data
          if (_settingsService.rtlSource == RtlSource.nativeUsb && _pendingRetuneUnfreeze) {
            if (kDebugMode) {
              print('Native USB confirmed lock - resetting protocol state and unfreezing retunes');
            }
            // Reset P25 state to clear any frequency tables from buffered data
            if (isP25) {
              _dsdPlugin.resetP25State();
            }
            _pendingRetuneUnfreeze = false;
            // Unfreeze retunes now that we have confirmed lock - voice grants can now work
            _dsdPlugin.setRetuneFrozen(false);
          }
          
          if (_state == ScanningState.searching) {
            _setState(ScanningState.locked);
            if (kDebugMode) {
              final protocol = isDMR ? 'DMR' : (isP25 ? 'P25' : 'Unknown');
              print('$protocol Control channel LOCKED at $_currentFrequency MHz (after $_consecutiveSyncs syncs)');
            }
          }
        } else if (_hasLock) {
          // Already locked, just update activity time
        }
      } else {
        // No sync - reset consecutive counter
        _consecutiveSyncs = 0;
      }
      
      notifyListeners();
    });
  }
  
  void _listenToNetwork() {
    _networkSubscription = _dsdPlugin.networkEventStream.listen((event) {
      // Ignore during channel settle period
      if (_channelSwitchTime != null) {
        final timeSinceSwitch = DateTime.now().difference(_channelSwitchTime!);
        if (timeSinceSwitch.inSeconds < 3) {
          return; // Skip during settle period
        }
      }
      
      // Ignore during rtl_tcp buffer flush period
      if (_rtlTcpReconnectTime != null) {
        final timeSinceReconnect = DateTime.now().difference(_rtlTcpReconnectTime!);
        if (timeSinceReconnect.inSeconds < 12) {
          return; // Skip during buffer flush
        }
      }
      
      // Update neighbor sites from DSD state
      final neighborCount = event['neighborCount'] as int;
      final neighborFreqList = event['neighborFreqs'] as List<dynamic>;
      final neighborLastSeenList = event['neighborLastSeen'] as List<dynamic>;
      
      // Convert to List<int>
      _neighborFreqs = neighborFreqList.map((freq) => freq as int).toList();
      _neighborLastSeen = neighborLastSeenList.map((ts) => ts as int).toList();
      
      // Update activity time when we receive network data, but only if we have
      // had sync recently. Neighbor counts also change as old entries age out
      // with no signal at all, which kept a dead channel looking "active".
      if (_lastSyncTime != null &&
          DateTime.now().difference(_lastSyncTime!).inSeconds < 3) {
        _lastActivityTime = DateTime.now();
      }
      
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
    _sessionCounter++;

    // Make sure the selected radio is plugged in before starting.
    _deviceError = await _checkRadioPresent();
    if (_deviceError != null) {
      _lastSiteId = siteId; // so RESUME works once the radio is plugged in
      _lastSiteName = siteName;
      _lastSystemId = systemId;
      _setState(ScanningState.error);
      return;
    }

    try {
      _previousSiteId = oldSiteId;
      _currentSiteId = siteId;
      _currentSiteName = siteName;
      _currentChannelName = null; // Clear channel name when starting system scanning
      _currentChannelIndex = 0;
      _hasLock = false;
      _lastActivityTime = null;
      _consecutiveSyncs = 0; // Reset sync counter
      _channelSwitchTime = null; // Clear any pending settle time
      _rtlTcpReconnectTime = null; // Clear any pending buffer flush
      
      // Reset signal quality tracking
      _tsbkCount = 0;
      _parityMismatches = 0;
      _lastTsbkTime = null;
      
      // Reset channel quality tracking for fresh start
      _channelSuccessCount.clear();
      _lastKnownGoodChannel = null;
      
      // Reset network information
      _neighborFreqs.clear();
      _neighborLastSeen.clear();
      _patches.clear();
      _groupAttachments.clear();
      _affiliations.clear();
      _downlinkFreq = null;
      _uplinkFreq = null;
      
      // Reset P25 protocol state BEFORE starting new scan
      // This clears frequency tables from previous site/session
      await _dsdPlugin.resetP25State();
      if (kDebugMode) {
        print('Reset P25 state before starting new scan');
      }
      
      // Get system ID if not provided
      if (systemId != null) {
        _currentSystemId = systemId;
      } else {
        _currentSystemId = await _db.getSystemIdForSite(siteId);
      }
      
      _lastSiteId = siteId;
      _lastSiteName = siteName;
      _lastSystemId = systemId;
      _saveLastSite();

      // Normal system scan: drop any stale Advanced Scan command so its -i/-m
      // options don't leak into this scan, then apply this site's modulation.
      await _dsdPlugin.setCustomArgs('');
      final siteModulation = await _db.getSiteModulation(siteId);
      _currentModulation = siteModulation;
      await _dsdPlugin.setModulation(DatabaseService.modulationCode(siteModulation));
      if (kDebugMode) {
        print('Site $siteName modulation: $siteModulation');
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
      
      _huntSince = DateTime.now();
      _noSignalReported = false;
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

  /// Before each system-scan engine start: receive options, plus P25-only
  /// sync if enabled (the native side clears P25-only when the engine stops).
  Future<void> _armReceive() async {
    try {
      await _dsdPlugin.setReceiveOptions(
        autoPpm: _settingsService.rxAutoPpm,
        fll: _settingsService.rxFll,
        ted: _settingsService.rxTed,
        autoGain: _settingsService.rxAutoGain,
        bothSlots: _settingsService.rxBothSlots,
      );
      await _dsdPlugin.setP25Only(_settingsService.p25OnlySync);
      await _dsdPlugin.setRevisitCooldown(_settingsService.revisitCooldownMs);
    } catch (e) {
      if (kDebugMode) print('Receive options not applied: $e');
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
    _consecutiveSyncs = 0; // Reset sync counter for new channel
    _channelSwitchTime = DateTime.now(); // Start settle timer
    _lastActivityTime = null; // Don't set until we get real activity
    
    // Note: We do NOT set _rtlTcpReconnectTime here for control channel hopping.
    // The long buffer flush is only set during initial connection in the rtl_tcp
    // startup path below. For control channel hopping, the 3-second channel settle
    // time is sufficient.
    
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
            biasTee: _settingsService.biasTee,
          );
          
          if (!success) {
            throw Exception('Failed to configure native RTL-SDR');
          }
          
          // Enable trunk following for P25 trunked systems
          await _dsdPlugin.setTrunkFollowing(true);
          
          // ALWAYS freeze retunes during initial startup to prevent old buffered data issues
          if (kDebugMode) {
            print('Freezing retunes during native USB startup');
          }
          await _dsdPlugin.setRetuneFrozen(true);
          
          // Mark pending unfreeze - will unfreeze after first sync when buffer settles
          _pendingRetuneUnfreeze = true;
          if (kDebugMode) {
            print('Retunes will unfreeze after first sync (buffer settle)');
          }
          
          // Start the engine
          await _armReceive();
          _onStart();
        } else {
          // Device already open - need to stop engine, let it clean up USB, then restart with new frequency
          if (kDebugMode) {
            print('Retuning native USB RTL-SDR to ${_settingsService.frequencyHz} Hz');
          }
          
          // ALWAYS freeze retunes during restart to prevent auto-retune based on stale data
          if (kDebugMode) {
            print('Freezing retunes during native USB restart');
          }
          await _dsdPlugin.setRetuneFrozen(true);
          
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
            biasTee: _settingsService.biasTee,
          );
          
          if (!configSuccess) {
            throw Exception('Failed to configure native RTL-SDR');
          }
          
          // Enable trunk following for P25 trunked systems
          await _dsdPlugin.setTrunkFollowing(true);
          
          // Start engine with new configuration
          await _armReceive();
          _onStart();
          
          // Mark that we need to unfreeze when first sync arrives
          _pendingRetuneUnfreeze = true;
          if (kDebugMode) {
            print('Retunes will unfreeze after first sync');
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
          
          // Enable trunk following for P25 trunked systems
          await _dsdPlugin.setTrunkFollowing(true);
          
          // Start the DSD engine
          await _armReceive();
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
          
          // ALWAYS freeze retunes when starting fresh on rtl_tcp
          // This prevents DSD from auto-retuning based on stale buffered TSBK data
          // which could cause it to tune to a different frequency than we requested
          if (kDebugMode) {
            print('Freezing retunes during rtl_tcp startup');
          }
          await _dsdPlugin.setRetuneFrozen(true);
          
          if (kDebugMode) {
            print('Reconnecting rtl_tcp at ${_settingsService.effectiveHost}:${_settingsService.effectivePort} freq ${_settingsService.frequencyHz} Hz');
          }
          
          // Reconnect with new frequency (DSD is already stopped from stopScanning())
          await _dsdPlugin.connect(
            _settingsService.effectiveHost,
            _settingsService.effectivePort,
            _settingsService.frequencyHz,
            gain: _settingsService.gain * 10, // decoder expects tenths of dB (fix from ClayF/Pocket25)
            ppm: _settingsService.ppm,
            biasTee: _settingsService.biasTee,
          );
          
          _rtlTcpConnected = true;
          
          // Enable trunk following for P25 trunked systems
          await _dsdPlugin.setTrunkFollowing(true);
          
          if (kDebugMode) {
            print('Starting DSD with rtl_tcp at ${_settingsService.frequencyHz} Hz (retunes frozen until buffer flush)');
          }
          
          // Set reconnect time BEFORE starting DSD to enable buffer flush period
          // This ensures sync events that arrive during startup are properly filtered
          _rtlTcpReconnectTime = DateTime.now();
          
          // Start DSD
          await _armReceive();
          _onStart();
          
          // Wait for rtl_tcp connection to be established
          // The engine thread needs time to connect to the rtl_tcp server
          await Future.delayed(const Duration(milliseconds: 500));
          
          // Note: bias-tee is applied during native initialization from opts->rtl_bias_tee
          // set by the connection string parsing. We don't need to re-apply here.
          // The native code at rtl_sdr_fm.cpp:2532 handles it.
          
          // For all fresh starts, explicitly retune to ensure rtl_tcp server changes frequency
          // The connect() only configures DSD's input string, not the rtl_tcp server
          if (kDebugMode) {
            print('Sending explicit retune command to rtl_tcp server');
          }
          await _dsdPlugin.retune(_settingsService.frequencyHz);
          
          // Keep retunes frozen until buffer flush completes
          // The freeze prevents DSD from auto-retuning based on stale buffered data
          // which would cause display to be out of sync with actual tuned frequency
          _pendingRetuneUnfreeze = true;
          if (kDebugMode) {
            print('Retunes frozen until buffer flush completes');
          }
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
          }
          // Note: We do NOT set _rtlTcpReconnectTime for fast retunes during control
          // channel hopping. The long buffer flush is only needed for full reconnects
          // when switching from a different frequency/mode. Fast retunes have minimal
          // latency and the 3-second channel settle time is sufficient.
        }
      }
      
      notifyListeners();
    } catch (e) {
      if (kDebugMode) {
        print('Error changing frequency: $e');
      }
      if (e.toString().contains('No RTL-SDR USB devices found')) {
        _deviceError = 'Radio device disconnected. Plug the RTL-SDR back in and tap RESUME.';
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
    _checkNoSignal(now);
    
    // Don't check during channel settle period
    if (_channelSwitchTime != null) {
      final timeSinceSwitch = now.difference(_channelSwitchTime!);
      if (timeSinceSwitch.inSeconds < 3) {
        return; // Still settling
      }
    }
    
    if (_state == ScanningState.locked) {
      // Check if we've lost lock (no activity for 15 seconds)
      // Increased from 10 to 15 to allow for brief quiet periods
      if (_lastActivityTime != null) {
        final timeSinceActivity = now.difference(_lastActivityTime!);
        if (timeSinceActivity.inSeconds > 15) {
          if (kDebugMode) {
            print('Lost lock on ${_currentFrequency} MHz (no data for 15s), trying next channel');
          }
          _hasLock = false;
          _consecutiveSyncs = 0;
          
          // If we have a known good channel, try it first before cycling
          if (_lastKnownGoodChannel != null && 
              _lastKnownGoodChannel != _currentChannelIndex &&
              _controlChannels.length > 1) {
            if (kDebugMode) {
              print('Switching back to last known good channel: ${_lastKnownGoodChannel! + 1}');
            }
            _currentChannelIndex = _lastKnownGoodChannel!;
          } else {
            _currentChannelIndex++;
          }
          
          _setState(ScanningState.searching);
          _tryNextControlChannel();
        }
      }
    } else if (_state == ScanningState.searching) {
      // If still searching after 10 seconds with no activity, try next channel
      // (increased from 8 to give more time for settle + lock)
      if (_lastActivityTime == null) {
        // No activity at all yet - use channel switch time
        if (_channelSwitchTime != null) {
          final timeSinceSwitch = now.difference(_channelSwitchTime!);
          if (timeSinceSwitch.inSeconds > 10) {
            if (kDebugMode) {
              print('No activity after 10 seconds, trying next channel');
            }
            _currentChannelIndex++;
            _tryNextControlChannel();
          }
        }
      } else {
        final timeSinceActivity = now.difference(_lastActivityTime!);
        if (timeSinceActivity.inSeconds > 10) {
          if (kDebugMode) {
            print('No lock after 10 seconds, trying next channel');
          }
          _currentChannelIndex++;
          _tryNextControlChannel();
        }
      }
    }
  }

  /// No-signal watchdog. Quiet time counts from the later of the scan start
  /// and the last sync, so it covers every control channel in the list.
  void _checkNoSignal(DateTime now) {
    final start = _huntSince;
    if (start == null || _noSignalReported || _watchdogRestarting) return;
    final last = _lastSyncTime;
    final quietFrom = (last != null && last.isAfter(start)) ? last : start;
    if (now.difference(quietFrom).inSeconds < noSignalSeconds) return;

    _noSignalReported = true;
    final n = _controlChannels.length;
    final chans = '$n control channel${n == 1 ? '' : 's'}';
    if (!_noSignalRestartTried && _settingsService.noSignalRestart) {
      _noSignalRestartTried = true;
      _noSignalFirstSeen ??= now;
      _noSignalWarning = 'No control channel heard for 3 min. Restarting the radio connection...';
      onLog?.call('[watch] No control channel heard for 3 min on any of $chans '
          '- restarting the radio connection (auto-recovery)');
      notifyListeners();
      _restartForNoSignal();
      return;
    }
    final afterRestart = _noSignalRestartTried;
    _noSignalFirstSeen ??= now;
    _noSignalWarning = afterRestart
        ? 'Still no control channel heard after restarting the radio. Check the '
          'antenna and connector; if the dongle is hot, unplug it, let it cool, '
          'plug it back in and tap RESUME. Still searching.'
        : 'No control channel heard for 3 min. Check the antenna and connector. '
          'If the dongle is hot or this keeps happening, tap STOP, unplug and '
          'replug the dongle, then RESUME. Still searching.';
    onLog?.call(afterRestart
        ? '[watch] Still no control channel heard 3 min after the restart '
          '(antenna/signal, or the dongle needs a replug) - still searching'
        : '[watch] No control channel heard for 3 min on any of $chans - still searching');
    notifyListeners();
  }

  Future<void> _restartForNoSignal() async {
    final id = _currentSiteId;
    final name = _currentSiteName;
    if (id == null || name == null) return;
    _watchdogRestarting = true;
    try {
      await startScanning(id, name, systemId: _currentSystemId);
    } finally {
      _watchdogRestarting = false;
    }
  }

  /// Returns a message if the radio selected in SDR Settings isn't connected,
  /// or null if it is (or if it can't be checked, e.g. a remote rtl_tcp server).
  Future<String?> _checkRadioPresent() async {
    try {
      switch (_settingsService.rtlSource) {
        case RtlSource.nativeUsb:
          final devices = await NativeRtlSdrService.listDevices();
          if (devices.isEmpty) {
            return 'No radio device detected. Plug in the RTL-SDR '
                '(with a USB-C OTG adapter) and tap RESUME.';
          }
          return null;
        case RtlSource.hackrf:
          final devices = await _dsdPlugin.hackrfListDevices();
          if (devices.isEmpty) {
            return 'No HackRF detected. Plug it in and tap RESUME.';
          }
          return null;
        default:
          return null; // remote rtl_tcp: nothing local to check
      }
    } catch (_) {
      return null; // if the check itself fails, don't block scanning
    }
  }

  /// Remember the last site across app restarts (for RESUME / auto-start).
  Future<void> _saveLastSite() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      if (_lastSiteId != null) await prefs.setInt('last_site_id', _lastSiteId!);
      if (_lastSiteName != null) await prefs.setString('last_site_name', _lastSiteName!);
      if (_lastSystemId != null) {
        await prefs.setInt('last_system_id', _lastSystemId!);
      } else {
        await prefs.remove('last_system_id');
      }
    } catch (_) {}
  }

  /// Load the last site saved by a previous run (does not start scanning).
  Future<void> loadLastSite() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      _lastSiteId ??= prefs.getInt('last_site_id');
      _lastSiteName ??= prefs.getString('last_site_name');
      _lastSystemId ??= prefs.getInt('last_system_id');
      notifyListeners();
    } catch (_) {}
  }

  /// Scan quality over roughly the last 10 seconds (Pocket25 LSM patch;
  /// idea from ClayF/Pocket25). Percent of control-channel messages that
  /// failed error correction. Null when there isn't enough recent data.
  final List<List<int>> _berSamples = []; // [epoch ms, ok total, err total]

  void _recordBerSample(int ok, int err) {
    final now = DateTime.now().millisecondsSinceEpoch;
    _berSamples.add([now, ok, err]);
    _berSamples.removeWhere((s) => now - s[0] > 10000);
  }

  double? get recentBer {
    if (_berSamples.length < 2) return null;
    final first = _berSamples.first, last = _berSamples.last;
    final ok = last[1] - first[1], err = last[2] - first[2];
    if (ok < 0 || err < 0 || ok + err < 5) return null;
    return err * 100.0 / (ok + err);
  }

  /// Restart the most recent system scan (site) started from Systems.
  Future<void> resumeLastSite() async {
    if (!canResumeLastSite) return;
    await startScanning(_lastSiteId!, _lastSiteName!, systemId: _lastSystemId);
  }

  /// The dongle stopped answering USB commands (seen as repeated
  /// "rtlsdr_*_reg failed" / "i2c ... failed" lines from the decoder). Stop the
  /// scan so it doesn't keep restarting a dead radio, and tell the user.
  Future<void> reportRadioNotResponding() async {
    if (_state != ScanningState.searching && _state != ScanningState.locked) return;
    await stopScanning();
    _deviceError = 'Radio not responding. Check the USB-C OTG adapter and cable '
        '(reseat them), and that the dongle isn\'t overheating, then tap RESUME.';
    _setState(ScanningState.error);
  }

  Future<void> stopScanning() async {
    _deviceError = null;
    if (!_watchdogRestarting) {
      _noSignalWarning = null;
      _noSignalFirstSeen = null;
      _noSignalReported = false;
      _noSignalRestartTried = false;
    }
    _huntSince = null;
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
    _currentChannelName = null;
    _currentSystemId = null;
    _currentFrequency = null;
    _currentChannelIndex = 0;
    _controlChannels = [];
    _allSystemSites = [];
    _hasLock = false;
    _consecutiveSyncs = 0;
    _channelSwitchTime = null;
    _lastActivityTime = null;
    _pendingRetuneUnfreeze = false;
    _rtlTcpConnected = false; // Reset connection tracking
    _channelSuccessCount.clear(); // Clear channel quality tracking
    _lastKnownGoodChannel = null;
    notifyListeners();
  }
  
  /// Clear current system selection (for Quick Scan mode)
  void clearCurrentSystem() {
    _clearSystemState();
  }
  
  /// Set channel name (for Quick Scan or Conventional mode)
  void setChannelName(String? name) {
    _currentChannelName = name;
    notifyListeners();
  }
  
  /// Set frequency manually (for Quick Scan or Conventional mode)
  void setCurrentFrequency(double? frequency) {
    _currentFrequency = frequency;
    notifyListeners();
  }
  
  /// Set pending retune unfreeze flag (for Quick Scan frequency changes)
  void setPendingRetuneUnfreeze() {
    _pendingRetuneUnfreeze = true;
  }

  void _setState(ScanningState newState) {
    _state = newState;
    notifyListeners();
  }
  
  /// Track which control channel a frequency corresponds to (for quality tracking)
  /// This does NOT update the display - display is updated when we request a channel switch
  void _updateChannelIndexFromFrequency(double freq) {
    // Find which control channel matches this frequency (within 0.001 MHz tolerance)
    for (int i = 0; i < _controlChannels.length; i++) {
      final channelFreq = _controlChannels[i]['frequency'] as double;
      if ((channelFreq - freq).abs() < 0.001) {
        // This frequency is a known control channel - track it as successful
        if (_hasLock && _currentChannelIndex == i) {
          _channelSuccessCount[i] = (_channelSuccessCount[i] ?? 0) + 1;
          _lastKnownGoodChannel = i;
        }
        return;
      }
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
