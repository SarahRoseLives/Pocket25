import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'dart:async';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'screens/scanner_screen.dart';
import 'screens/log_screen.dart';
import 'screens/settings_screen.dart';
import 'screens/site_details_screen.dart';
import 'services/settings_service.dart';
import 'models/scanner_activity.dart';
import 'models/site_details.dart';

void main() {
  WidgetsFlutterBinding.ensureInitialized();
  // Force landscape orientation
  SystemChrome.setPreferredOrientations([
    DeviceOrientation.landscapeLeft,
    DeviceOrientation.landscapeRight,
  ]);
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
  final List<String> _logLines = [];
  final List<CallEvent> _recentCalls = [];
  final ScrollController _logScrollController = ScrollController();
  
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
    _listenToOutput();
    _listenToCallEvents();
    _listenToSiteEvents();
    _startCallTimeoutTimer();
  }

  @override
  void dispose() {
    _outputSubscription?.cancel();
    _callEventSubscription?.cancel();
    _siteEventSubscription?.cancel();
    _callTimeoutTimer?.cancel();
    _logScrollController.dispose();
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
      final callEvent = CallEvent.fromMap(eventMap);
      
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

  Future<void> _stop() async {
    try {
      await _dsdFlutterPlugin.stop();

      setState(() {
        _isRunning = false;
        _currentCall = null;
      });
    } catch (e) {
      // Handle error
    }
  }

  Widget _buildCurrentScreen() {
    switch (_currentIndex) {
      case 0:
        return ScannerScreen(
          currentCall: _currentCall,
          recentCalls: _recentCalls,
          isRunning: _isRunning,
        );
      case 1:
        return LogScreen(
          logLines: _logLines,
          scrollController: _logScrollController,
        );
      case 2:
        return SiteDetailsScreen(
          siteDetails: _currentSiteDetails,
        );
      case 3:
        return SettingsScreen(
          settings: _settingsService,
          dsdPlugin: _dsdFlutterPlugin,
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
            icon: Icon(Icons.settings),
            label: 'Settings',
          ),
        ],
      ),
    );
  }
}
