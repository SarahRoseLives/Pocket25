import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:flutter/foundation.dart';
import 'dart:async';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:wakelock_plus/wakelock_plus.dart';
import 'screens/scanner_screen.dart';
import 'screens/log_screen.dart';
import 'screens/settings_screen.dart';
import 'screens/site_details_screen.dart';
import 'screens/network_screen.dart';
import 'services/settings_service.dart';
import 'services/scanning_service.dart';
import 'services/database_service.dart';
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
  }
  
  Future<void> _initializeFilters() async {
    // Set blacklist mode by default and sync muted TGs to native layer
    await _dsdFlutterPlugin.setFilterMode(2); // Blacklist mode
    await _syncMutedTalkgroupsToNative();
  }
  
  Future<void> _syncMutedTalkgroupsToNative() async {
    await _dsdFlutterPlugin.setFilterTalkgroups(_mutedTalkgroups.toList());
  }
  
  Future<void> _toggleMute(int talkgroup) async {
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
    
    // Sync to native layer
    await _syncMutedTalkgroupsToNative();
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
    _logScrollController.dispose();
    _scanningService.dispose();
    super.dispose();
  }

  void _listenToOutput() {
    _outputSubscription = _dsdFlutterPlugin.outputStream.listen((line) {
      setState(() {
        _logLines.add(line);
        if (_logLines.length > 500) {
          _logLines.removeAt(0);
        }
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

  Future<void> _start() async {
    try {
      setState(() {
        _isRunning = true;
        _logLines.clear();
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

  void _stop() {
    // Defer the stop call to next event loop cycle to prevent blocking current operation
    Timer.run(() async {
      try {
        await _dsdFlutterPlugin.stop();
        setState(() {
          _isRunning = false;
          _currentCall = null;
        });
      } catch (e) {
        // Handle error silently
      }
    });
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
        );
      case 1:
        return LogScreen(
          logLines: _logLines,
          scrollController: _logScrollController,
        );
      case 2:
        return SiteDetailsScreen(
          siteDetails: _currentSiteDetails,
          scanningService: _scanningService,
        );
      case 3:
        return NetworkScreen(
          scanningService: _scanningService,
        );
      case 4:
        return SettingsScreen(
          settings: _settingsService,
          dsdPlugin: _dsdFlutterPlugin,
          scanningService: _scanningService,
          isRunning: _isRunning,
          onStart: _start,
          onStop: _stop,
          onStatusUpdate: _updateStatus,
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
            icon: Icon(Icons.cell_tower),
            label: 'Site',
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
