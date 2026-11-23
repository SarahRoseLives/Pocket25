import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart';
import 'dart:async';
import 'package:package_info_plus/package_info_plus.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import 'screens/scanner_screen.dart';
import 'screens/log_screen.dart';
import 'screens/settings_screen.dart';
import 'screens/network_screen.dart';
import 'screens/scan_grid_screen.dart';
import 'screens/conventional_channels_screen.dart';
import 'screens/advanced_scan_screen.dart';
import 'services/settings_service.dart';
import 'services/scanning_service.dart';
import 'services/database_service.dart';
import 'services/update_service.dart';
import 'models/scanner_activity.dart';
import 'models/site_details.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // Force landscape orientation
  SystemChrome.setPreferredOrientations([
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]);
  // Enable wakelock to keep screen on
  WakelockPlus.enable();
  runApp(const Pocket25App());
}

class Pocket25App extends StatelessWidget {
  const Pocket25App({super.key});

  @override
  Widget build(BuildContext context) {
    return MaterialApp(
      title: 'Pocket25',
      theme: ThemeData.dark().copyWith(
        colorScheme: ColorScheme.dark(
          primary: Colors.cyan,
          secondary: Colors.blue,
          surface: Colors.blueGrey[900]!,
        ),
        appBarTheme: AppBarTheme(
          backgroundColor: Colors.blueGrey[900],
          elevation: 2,
        ),
      ),
      home: const MainScreen(),
      debugShowCheckedModeBanner: false,
    );
  }
}

class MainScreen extends StatefulWidget {
  const MainScreen({super.key});

  @override
  State<MainScreen> createState() => _MainScreenState();
}

class _MainScreenState extends State<MainScreen> {
  final _dsdFlutterPlugin = DsdFlutter();
  final _settingsService = SettingsService();
  final _db = DatabaseService();
  final _updateService = UpdateService();
  late final ScanningService _scanningService;
  final List<String> _logLines = [];
  final List<CallEvent> _recentCalls = [];
  final ScrollController _logScrollController = ScrollController();
  
  // Muted talkgroups tracking (blacklist mode)
  final Set<int> _mutedTalkgroups = {};
  
  CallEvent? _currentCall;
  SiteDetails? _currentSiteDetails;
  bool _isRunning = false;
  int _currentIndex = 0;
  StreamSubscription<String>? _outputSubscription;
  StreamSubscription<Map<String, dynamic>>? _callEventSubscription;
  StreamSubscription<Map<String, dynamic>>? _siteEventSubscription;
  Timer? _callTimeoutTimer;
  Timer? _filterSyncTimer;
  bool _filterSyncPending = false;

  @override
  void initState() {
    super.initState();
    _scanningService = ScanningService(
      _dsdFlutterPlugin,
      _settingsService,
      _start,
      _stop,
    );
    _listenToOutput();
    _listenToCallEvents();
    _listenToSiteEvents();
    _startCallTimeoutTimer();
    _initializeFilters();
    // Repeat-visit cooldown applies immediately when toggled in SDR Settings
    _settingsService.addListener(() {
      _dsdFlutterPlugin.setRevisitCooldown(_settingsService.revisitCooldownMs).catchError((_) {});
    });
    _scanningService.addListener(_onScanningServiceChanged);
    _scanningService.onLog = (line) {
      if (!mounted) return;
      final t = DateTime.now();
      setState(() {
        _logLines.add('$line (${_two(t.hour)}:${_two(t.minute)}:${_two(t.second)})');
        _trimLog();
      });
    };
    PackageInfo.fromPlatform().then((p) {
      _appVersion = '${p.version}+${p.buildNumber} (${p.packageName})';
    }).catchError((_) {});
    _checkForUpdates();
  }
  
  Future<void> _checkForUpdates() async {
    // Wait a bit before checking (let app fully load)
    await Future.delayed(const Duration(seconds: 3));
    
    final updateInfo = await _updateService.checkForUpdates();
    if (updateInfo != null && mounted) {
      _showUpdateDialog(updateInfo);
    }
  }
  
  void _showUpdateDialog(UpdateInfo updateInfo) {
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) => AlertDialog(
        title: const Row(
          children: [
            Icon(Icons.system_update, color: Colors.cyan),
            SizedBox(width: 12),
            Text('Update Available'),
          ],
        ),
        content: SingleChildScrollView(
          child: Column(
            mainAxisSize: MainAxisSize.min,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                'Version ${updateInfo.version}',
                style: const TextStyle(
                  fontSize: 18,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 4),
              Text(
                'Released: ${updateInfo.releaseDate}',
                style: TextStyle(
                  fontSize: 12,
                  color: Colors.grey[500],
                ),
              ),
              if (updateInfo.changelog.isNotEmpty) ...[
                const SizedBox(height: 16),
                Text(
                  'What\'s New:',
                  style: TextStyle(
                    fontSize: 14,
                    fontWeight: FontWeight.bold,
                    color: Colors.cyan[300],
                  ),
                ),
                const SizedBox(height: 8),
                ...updateInfo.changelog.map((change) => Padding(
                  padding: const EdgeInsets.only(bottom: 6),
                  child: Row(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      Text('• ', style: TextStyle(color: Colors.cyan[300])),
                      Expanded(
                        child: Text(
                          change,
                          style: const TextStyle(fontSize: 14),
                        ),
                      ),
                    ],
                  ),
                )),
              ],
              const SizedBox(height: 16),
              const Text(
                'Download the latest version from pocket25.com',
                style: TextStyle(fontSize: 12, fontStyle: FontStyle.italic),
              ),
            ],
          ),
        ),
        actions: [
          TextButton(
            onPressed: () {
              _updateService.dismissVersion(updateInfo.version);
              Navigator.of(context).pop();
            },
            child: const Text('Later'),
          ),
          ElevatedButton(
            onPressed: () {
              Navigator.of(context).pop();
            },
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.cyan,
              foregroundColor: Colors.black,
            ),
            child: const Text('OK'),
          ),
        ],
      ),
    );
  }
  
  Future<void> _initializeFilters() async {
    // Set blacklist mode by default and sync muted TGs to native layer
    await _dsdFlutterPlugin.setFilterMode(2); // Blacklist mode
    await _syncMutedTalkgroupsToNative();
    
    // Load locked sites for GPS hopping
    await _scanningService.loadLockedSites();
  }
  
  Future<void> _syncMutedTalkgroupsToNative() async {
    // Debounce: Cancel pending sync and schedule new one
    _filterSyncTimer?.cancel();
    _filterSyncPending = true;
    
    _filterSyncTimer = Timer(const Duration(milliseconds: 300), () async {
      if (_filterSyncPending) {
        _filterSyncPending = false;
        await _applyCallFollowingFilter();
      }
    });
  }

  /// Push the talkgroup filter to native. The same filter decides both which
  /// grants are followed and which audio is muted.
  /// - Default (original behavior): blacklist = talkgroups muted in Scan Grid.
  /// - "Follow my talkgroups only" during a system scan: whitelist = this
  ///   system's talkgroups minus muted ones. If that list would be empty, fall
  ///   back to the blacklist so the scanner never goes silent by accident.
  /// Applies the filter and priority talkgroups, and notes any change in the log.
  Future<void> _applyCallFollowingFilter() async {
    final before = _filterDesc;
    await _applyTalkgroupFilter();
    await _applyPriorityTalkgroups();
    if (_logSessionOpen && before != 'not yet applied' && before != _filterDesc && mounted) {
      setState(() => _logLines.add('[cfg] Talkgroup filter changed: $_filterDesc'));
    }
  }

  // ---- Priority talkgroups (★ in Scan Grid) --------------------------------
  Set<int> _priorityTalkgroups = {};

  String get _priorityDesc => _priorityTalkgroups.isEmpty
      ? 'none'
      : (_priorityTalkgroups.toList()..sort()).join(', ');

  /// Loads the current system's ★ talkgroups from the database and sends them
  /// to the decoder. Priority applies only during a system scan.
  Future<void> _applyPriorityTalkgroups() async {
    try {
      final systemId = _scanningService.currentSystemId;
      final tgs = systemId != null ? await _db.getPriorityTalkgroups(systemId) : <int>{};
      _priorityTalkgroups = tgs;
      await _dsdFlutterPlugin.setPriorityTalkgroups(tgs.toList());
    } catch (e) {
      debugPrint('Error syncing priority talkgroups: $e');
    }
  }

  /// Scan Grid long-press changed the ★ set: apply now and note it in the log.
  Future<void> _onPriorityTalkgroupsChanged(Set<int> tgs) async {
    _priorityTalkgroups = Set<int>.from(tgs);
    try {
      await _dsdFlutterPlugin.setPriorityTalkgroups(_priorityTalkgroups.toList());
    } catch (e) {
      debugPrint('Error syncing priority talkgroups: $e');
    }
    if (_logSessionOpen && mounted) {
      setState(() => _logLines.add('[cfg] Priority talkgroups changed: $_priorityDesc'));
    }
  }

  Future<void> _applyTalkgroupFilter() async {
    try {
      final systemId = _scanningService.currentSystemId;
      if (_settingsService.followSelectedOnly && systemId != null) {
        final rows = await _db.getTalkgroups(systemId);
        final allowed = rows
            .map((r) => r['tg_decimal'])
            .whereType<int>()
            .where((tg) => !_mutedTalkgroups.contains(tg))
            .toList();
        if (allowed.isNotEmpty) {
          await _dsdFlutterPlugin.setFilterMode(1); // whitelist
          await _dsdFlutterPlugin.setFilterTalkgroups(allowed);
          _filterDesc = 'allow list ${allowed.length} TGs: ${allowed.take(30).join(', ')}'
              '${allowed.length > 30 ? ', …' : ''}';
          return;
        }
      }
      await _dsdFlutterPlugin.setFilterMode(2); // blacklist (original)
      await _dsdFlutterPlugin.setFilterTalkgroups(_mutedTalkgroups.toList());
      _filterDesc = _mutedTalkgroups.isEmpty
          ? 'none (follow all talkgroups)'
          : 'mute list ${_mutedTalkgroups.length} TGs: ${_mutedTalkgroups.take(30).join(', ')}';
    } catch (e) {
      debugPrint('Error syncing filter to native: $e');
    }
  }

  ScanningState? _lastScanState;
  int? _lastFilterSystemId;

  /// Re-apply call-following settings whenever a scan starts or the system
  /// changes (e.g. GPS site hopping to another system).
  void _onScanningServiceChanged() {
    final st = _scanningService.state;
    final sys = _scanningService.currentSystemId;
    final started = st == ScanningState.searching && _lastScanState != ScanningState.searching &&
        _lastScanState != ScanningState.locked;
    final systemChanged = sys != _lastFilterSystemId;
    final wasSystemActive = _lastScanState == ScanningState.searching ||
        _lastScanState == ScanningState.locked ||
        _lastScanState == ScanningState.stopping;
    if (st == ScanningState.idle && wasSystemActive && _logSessionIsSystem) {
      _endLogSession('stopped');
    } else if (st == ScanningState.error && _logSessionIsSystem && _logSessionOpen) {
      _endLogSession('error');
    }
    // Redraw the Scanner screen when the scan state changes (e.g. to show
    // "No radio device detected" when nothing else would trigger a rebuild).
    if (st != _lastScanState && mounted) {
      setState(() {});
    }
    _lastScanState = st;
    _lastFilterSystemId = sys;
    if (started || systemChanged) {
      _dsdFlutterPlugin.setTuneEncryptedCalls(!_settingsService.skipEncryptedCalls);
      _applyCallFollowingFilter();
    }
  }
  
  Future<void> _toggleMute(int talkgroup) async {
    // Update state immediately for responsive UI
    setState(() {
      if (_mutedTalkgroups.contains(talkgroup)) {
        _mutedTalkgroups.remove(talkgroup);
      } else {
        _mutedTalkgroups.add(talkgroup);
      }
      
      // Update current call's muted status if it matches
      if (_currentCall != null && _currentCall!.talkgroup == talkgroup) {
        _currentCall = _currentCall!.copyWithMuted(_mutedTalkgroups.contains(talkgroup));
      }
      
      // Update recent calls
      for (int i = 0; i < _recentCalls.length; i++) {
        if (_recentCalls[i].talkgroup == talkgroup) {
          _recentCalls[i] = _recentCalls[i].copyWithMuted(_mutedTalkgroups.contains(talkgroup));
        }
      }
    });
    
    // Sync to native layer with debouncing to prevent blocking during rapid toggles
    _syncMutedTalkgroupsToNative();
  }
  
  bool _isTalkgroupMuted(int talkgroup) {
    return _mutedTalkgroups.contains(talkgroup);
  }

  @override
  void dispose() {
    _outputSubscription?.cancel();
    _callEventSubscription?.cancel();
    _siteEventSubscription?.cancel();
    _callTimeoutTimer?.cancel();
    _filterSyncTimer?.cancel();
    _logScrollController.dispose();
    _scanningService.removeListener(_onScanningServiceChanged);
    _scanningService.dispose();
    super.dispose();
  }

  void _listenToOutput() {
    _outputSubscription = _dsdFlutterPlugin.outputStream.listen((line) {
      _watchForUsbErrors(line);
      setState(() {
        _logLines.add(line);
        _trimLog();
      });

      // Auto-scroll log when on log tab
      WidgetsBinding.instance.addPostFrameCallback((_) {
        if (_logScrollController.hasClients && _currentIndex == 1) {
          _logScrollController.animateTo(
            _logScrollController.position.maxScrollExtent,
            duration: const Duration(milliseconds: 100),
            curve: Curves.easeOut,
          );
        }
      });
    });
  }

  void _listenToCallEvents() {
    _callEventSubscription = _dsdFlutterPlugin.callEventStream.listen((eventMap) {
      _handleCallEvent(eventMap);
    });
  }

  Future<void> _handleCallEvent(Map<String, dynamic> eventMap) async {
    // Look up talkgroup name if we have a current site
    if (_scanningService.currentSiteId != null && eventMap['talkgroup'] != null) {
      final talkgroup = eventMap['talkgroup'] as int;
      if (talkgroup > 0 && (eventMap['groupName'] == null || (eventMap['groupName'] as String).isEmpty)) {
        if (kDebugMode) {
          print('Looking up talkgroup name for TG $talkgroup, site ${_scanningService.currentSiteId}');
        }
        final systemId = await _db.getSystemIdForSite(_scanningService.currentSiteId!);
        if (kDebugMode) {
          print('System ID: $systemId');
        }
        if (systemId != null) {
          final tgName = await _db.getTalkgroupName(systemId, talkgroup);
          if (kDebugMode) {
            print('Talkgroup name lookup result: $tgName');
          }
          if (tgName != null && tgName.isNotEmpty) {
            eventMap['groupName'] = tgName;
            if (kDebugMode) {
              print('Set groupName to: $tgName');
            }
          }
        }
      }
    }
    
    // Check if this talkgroup is muted
    final talkgroup = eventMap['talkgroup'] as int? ?? 0;
    final isMuted = _isTalkgroupMuted(talkgroup);
    final callEvent = CallEvent.fromMap(eventMap, isMuted: isMuted);
    
    if (!mounted) return;
    
    setState(() {
      switch (callEvent.eventType) {
        case CallEventType.callStart:
        case CallEventType.callUpdate:
          // Update current call
          _currentCall = callEvent;
          
          // Add to recent calls (avoid duplicates for same TG within short time)
          final existingIdx = _recentCalls.indexWhere(
            (c) => c.talkgroup == callEvent.talkgroup && 
                   DateTime.now().difference(c.timestamp).inSeconds < 5
          );
          if (existingIdx < 0) {
            _recentCalls.insert(0, callEvent);
            // Keep only last 50 calls
            if (_recentCalls.length > 50) {
              _recentCalls.removeLast();
            }
          }
          break;
          
        case CallEventType.callEnd:
          // Clear current call if it matches
          if (_currentCall?.talkgroup == callEvent.talkgroup) {
            _currentCall = null;
          }
          break;
      }
    });
  }

  void _listenToSiteEvents() {
    _siteEventSubscription = _dsdFlutterPlugin.siteEventStream.listen((eventMap) {
      setState(() {
        _currentSiteDetails = SiteDetails.fromMap(eventMap);
      });
    });
  }

  void _startCallTimeoutTimer() {
    // Clear current call if no update in 10 seconds
    _callTimeoutTimer = Timer.periodic(const Duration(seconds: 2), (_) {
      if (_currentCall != null) {
        final diff = DateTime.now().difference(_currentCall!.timestamp);
        if (diff.inSeconds > 10) {
          setState(() {
            _currentCall = null;
          });
        }
      }
    });
  }

  void _updateStatus(String status) {
    // Status updates can be logged or displayed elsewhere if needed
  }

  // ---- Radio-not-responding detection (Pocket25 LSM patch) -----------------
  // When the phone loses USB contact with the dongle (loose OTG adapter,
  // overheating, USB power cut) librtlsdr prints a burst of register/I2C
  // failures. 5 or more within 5 s during a system scan = radio not responding.
  static final RegExp _usbErrorPattern =
      RegExp(r'rtlsdr_(demod_)?(read|write)_reg failed|i2c (wr|rd) failed');
  final List<DateTime> _usbErrorTimes = [];

  void _watchForUsbErrors(String line) {
    if (!_usbErrorPattern.hasMatch(line)) return;
    final now = DateTime.now();
    _usbErrorTimes.add(now);
    _usbErrorTimes.removeWhere((t) => now.difference(t).inSeconds >= 5);
    if (_usbErrorTimes.length >= 5 && _scanningService.isSystemScanActive) {
      _usbErrorTimes.clear();
      if (mounted) {
        setState(() => _logLines.add('[cfg] Radio not responding (repeated USB errors): scan stopped'));
      }
      _scanningService.reportRadioNotResponding();
    }
  }

  // ---- Log session header/footer -------------------------------------------
  String _appVersion = '';
  int _loggedSessionId = -1;
  bool _logSessionOpen = false;
  bool _logSessionIsSystem = false;
  DateTime? _logSessionStart;
  String _filterDesc = 'not yet applied';

  String _two(int v) => v.toString().padLeft(2, '0');
  String _stamp(DateTime t) =>
      '${t.year}-${_two(t.month)}-${_two(t.day)} ${_two(t.hour)}:${_two(t.minute)}:${_two(t.second)}';

  /// Configuration lines, prefixed "[cfg]" so they're easy to find.
  String? _cfgCc;
  String? _cfgSystemId;
  String? _cfgSite;

  List<String> _configLines() {
    final ss = _scanningService;
    final st = _settingsService;
    final lines = <String>[];
    lines.add('[cfg] App: Pocket25 $_appVersion');
    if (_logSessionIsSystem) {
      // Stop clears the site details before the footer is written, so keep
      // the last known values from this session.
      final ccNow = ss.controlChannelFrequencies.map((f) => f.toStringAsFixed(4)).join(', ');
      if (ccNow.isNotEmpty) _cfgCc = ccNow;
      if (ss.currentSystemId != null) _cfgSystemId = '${ss.currentSystemId}';
      final siteNow = ss.currentSiteName ?? ss.lastSiteName;
      if (siteNow != null) _cfgSite = siteNow;
      lines.add('[cfg] Mode: system scan | site: ${_cfgSite ?? '?'}'
          ' | system id: ${_cfgSystemId ?? '?'}');
      lines.add('[cfg] Modulation: ${ss.currentModulation} | control channels: ${_cfgCc ?? '?'}');
      lines.add('[cfg] GPS site hopping: ${ss.gpsHoppingEnabled ? 'on' : 'off'}');
    } else {
      final adv = AdvancedScanScreen.lastStartedCommand;
      if (ss.currentChannelName != null) {
        lines.add('[cfg] Mode: ${ss.currentChannelName}'
            '${ss.currentFrequency != null ? ' | ${ss.currentFrequency} MHz' : ''}');
      } else {
        lines.add('[cfg] Mode: Advanced Scan / manual${adv != null ? ' | command: $adv' : ''}');
      }
    }
    lines.add('[cfg] SDR: source=${st.rtlSource.name} gain=${st.gain} ppm=${st.ppm}'
        ' bias-T=${st.biasTee ? 'on' : 'off'} sample rate=${st.sampleRate}');
    lines.add('[cfg] Call following: skip encrypted=${st.skipEncryptedCalls ? 'ON' : 'off'}'
        ' | my talkgroups only=${st.followSelectedOnly ? 'ON' : 'off'}'
        ' | muted in Scan Grid: ${_mutedTalkgroups.length}');
    lines.add('[cfg] Talkgroup filter: $_filterDesc');
    lines.add('[cfg] Priority talkgroups: $_priorityDesc');
    lines.add('[cfg] Repeat-visit cooldown: ${_settingsService.revisitCooldown ? 'ON (1.5 s)' : 'off'}'
        ' | restart radio after 3 min silence: ${_settingsService.noSignalRestart ? 'ON' : 'off'}');
    lines.add('[cfg] Receive: auto PPM=${_settingsService.rxAutoPpm ? 'ON' : 'off'}'
        ' | auto gain=${_settingsService.rxAutoGain ? 'ON' : 'off'}'
        ' | FLL=${_settingsService.rxFll ? 'ON' : 'off'} | TED=${_settingsService.rxTed ? 'ON' : 'off'}'
        ' | both TDMA slots=${_settingsService.rxBothSlots ? 'ON' : 'off'}'
        ' | P25-only sync=${_settingsService.p25OnlySync ? 'ON' : 'off'}');
    return lines;
  }

  void _beginLogSession({required bool system}) {
    final now = DateTime.now();
    _logLines.clear();
    _logSessionOpen = true;
    _logSessionIsSystem = system;
    _logSessionStart = now;
    _loggedSessionId = system ? _scanningService.sessionCounter : -1;
    _cfgCc = null;
    _cfgSystemId = null;
    _cfgSite = null;
    _logLines.add('[cfg] ===== SESSION START ${_stamp(now)} =====');
    _logLines.addAll(_configLines());
  }

  /// Log tab keeps the most recent [_maxLogLines] lines (was 2000).
  static const int _maxLogLines = 5000;
  void _trimLog() {
    final extra = _logLines.length - _maxLogLines;
    if (extra > 0) _logLines.removeRange(0, extra);
  }

  /// Appends the config again at the end so long sessions still document it.
  void _endLogSession(String reason) {
    if (!_logSessionOpen) return;
    _logSessionOpen = false;
    final now = DateTime.now();
    final start = _logSessionStart;
    String dur = '';
    if (start != null) {
      final d = now.difference(start);
      dur = ' (duration ${d.inHours > 0 ? '${d.inHours}:' : ''}${_two(d.inMinutes % 60)}:${_two(d.inSeconds % 60)})';
    }
    setState(() {
      _logLines.add('[cfg] ===== SESSION END ${_stamp(now)}$dur — $reason =====');
      _logLines.addAll(_configLines());
      _trimLog();
    });
  }

  /// Stop requested by the user from a screen (not internal engine restarts).
  Future<void> _stopByUser() async {
    if (!_scanningService.isSystemScanActive) {
      _endLogSession('stopped');
    }
    await _stop();
  }

  Future<void> _start() async {
    // Receive options (auto PPM, auto gain, FLL, TED, both TDMA slots) apply at
    // every engine start, in every mode.
    try {
      await _dsdFlutterPlugin.setReceiveOptions(
        autoPpm: _settingsService.rxAutoPpm,
        fll: _settingsService.rxFll,
        ted: _settingsService.rxTed,
        autoGain: _settingsService.rxAutoGain,
        bothSlots: _settingsService.rxBothSlots,
      );
      await _dsdFlutterPlugin.setRevisitCooldown(_settingsService.revisitCooldownMs);
    } catch (_) {}
    try {
      setState(() {
        _isRunning = true;
        // Log = one scan session: cleared and given a config header when a new
        // session starts. Control-channel cycling restarts the engine inside the
        // same system-scan session, so that must not clear the log.
        if (_scanningService.isSystemScanActive) {
          if (_scanningService.sessionCounter != _loggedSessionId) {
            _beginLogSession(system: true);
          }
        } else {
          _beginLogSession(system: false);
        }
        _recentCalls.clear();
        _currentCall = null;
      });

      await _dsdFlutterPlugin.start();
    } catch (e) {
      setState(() {
        _isRunning = false;
      });
    }
  }

  Future<void> _stop() async {
    try {
      await _dsdFlutterPlugin.stop();
      if (mounted) {
        setState(() {
          _isRunning = false;
          _currentCall = null;
        });
      }
    } catch (e) {
      if (kDebugMode) {
        print('Error stopping DSP: $e');
      }
    }
  }

  /// Stop everything from the Scanner screen. A system scan must be stopped
  /// through ScanningService so its lock-check timer doesn't restart it.
  Future<void> _stopAnyScan() async {
    if (_scanningService.isSystemScanActive) {
      await _scanningService.stopScanning(); // footer written by _onScanningServiceChanged
    } else {
      await _stopByUser();
    }
  }

  Widget _buildCurrentScreen() {
    switch (_currentIndex) {
      case 0:
        return ScannerScreen(
          currentCall: _currentCall,
          recentCalls: _recentCalls,
          isRunning: _isRunning,
          scanningService: _scanningService,
          onToggleMute: _toggleMute,
          onStopScan: _stopAnyScan,
          onResumeScan: _scanningService.resumeLastSite,
        );
      case 1:
        return LogScreen(
          logLines: _logLines,
          scrollController: _logScrollController,
        );
      case 2:
        return ScanGridScreen(
          scanningService: _scanningService,
          initialMutedTalkgroups: _mutedTalkgroups,
          onPriorityTalkgroupsChanged: _onPriorityTalkgroupsChanged,
          onMutedTalkgroupsChanged: (mutedTalkgroups) {
            // Update our local muted set and sync to native
            _mutedTalkgroups.clear();
            _mutedTalkgroups.addAll(mutedTalkgroups);
            _syncMutedTalkgroupsToNative();
            
            // Update current and recent calls muted status
            if (_currentCall != null) {
              setState(() {
                _currentCall = _currentCall!.copyWithMuted(_mutedTalkgroups.contains(_currentCall!.talkgroup));
              });
            }
            
            // Update recent calls
            setState(() {
              for (int i = 0; i < _recentCalls.length; i++) {
                _recentCalls[i] = _recentCalls[i].copyWithMuted(_mutedTalkgroups.contains(_recentCalls[i].talkgroup));
              }
            });
          },
        );
      case 3:
        return ConventionalChannelsScreen(
          scanningService: _scanningService,
          settings: _settingsService,
          dsdPlugin: _dsdFlutterPlugin,
          isRunning: _isRunning,
          onStart: _start,
          onStop: _stopByUser,
        );
      case 4:
        return NetworkScreen(
          scanningService: _scanningService,
        );
      case 5:
        return SettingsScreen(
          settings: _settingsService,
          dsdPlugin: _dsdFlutterPlugin,
          scanningService: _scanningService,
          isRunning: _isRunning,
          onStart: _start,
          onStop: _stopByUser,
          onStatusUpdate: _updateStatus,
          onNavigateToScanner: () {
            setState(() {
              _currentIndex = 0; // Switch to scanner tab
            });
          },
        );
      default:
        return const SizedBox.shrink();
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      body: _buildCurrentScreen(),
      bottomNavigationBar: BottomNavigationBar(
        currentIndex: _currentIndex,
        onTap: (index) {
          setState(() {
            _currentIndex = index;
          });
        },
        backgroundColor: Colors.blueGrey[900],
        selectedItemColor: Colors.cyan,
        unselectedItemColor: Colors.grey,
        selectedFontSize: 12,
        unselectedFontSize: 12,
        iconSize: 20,
        type: BottomNavigationBarType.fixed,
        showSelectedLabels: true,
        showUnselectedLabels: true,
        items: const [
          BottomNavigationBarItem(
            icon: Icon(Icons.radio),
            label: 'Scanner',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.terminal),
            label: 'Log',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.grid_on),
            label: 'Scan Grid',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.list),
            label: 'Channels',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.lan),
            label: 'Network',
          ),
          BottomNavigationBarItem(
            icon: Icon(Icons.settings),
            label: 'Settings',
          ),
        ],
      ),
    );
  }
}
