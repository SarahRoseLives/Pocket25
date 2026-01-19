import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../services/settings_service.dart';
import '../services/scanning_service.dart';
import '../services/native_rtlsdr_service.dart';

class AdvancedScanScreen extends StatefulWidget {
  final SettingsService settings;
  final DsdFlutter dsdPlugin;
  final ScanningService scanningService;
  final bool isRunning;
  final VoidCallback onStart;
  final VoidCallback onStop;

  const AdvancedScanScreen({
    super.key,
    required this.settings,
    required this.dsdPlugin,
    required this.scanningService,
    required this.isRunning,
    required this.onStart,
    required this.onStop,
  });

  @override
  State<AdvancedScanScreen> createState() => _AdvancedScanScreenState();
}

class _AdvancedScanScreenState extends State<AdvancedScanScreen> {
  late TextEditingController _commandController;
  bool _isRunning = false;

  // Common DSD-Neo command examples
  static const List<Map<String, String>> _examples = [
    {
      'name': 'RTL-SDR P25 All Phases',
      'command': '-fp -fx -i rtl:0:851.375M:22:-2:24:0:2',
    },
    {
      'name': 'RTL-TCP P25',
      'command': '-fp -fx -i rtltcp:127.0.0.1:1234:851.375M:22:-2:24:0:2',
    },
    {
      'name': 'RTL-SDR DMR with AES Key',
      'command': "-fd -i rtl:0:450.5M:26:0:8 -H '736B9A9C5645288B243AD5CB8701EF8A'",
    },
    {
      'name': 'RTL-TCP NXDN',
      'command': '-fn -i rtltcp:localhost:1234:168.575M:22:-1:8:0:1',
    },
    {
      'name': 'P25 with Hex Key',
      'command': '-fp -fx -H 5a4c574738 -4 -i rtl:0:771.18125M:22:0:24',
    },
    {
      'name': 'DMR Simplex Auto Detect',
      'command': '-fs -ma -i rtl:0:446.0M:26:0:8',
    },
    {
      'name': 'P25 Phase 1 Only',
      'command': '-f1 -i rtltcp:192.168.1.100:1234:851.8M:22:0:48',
    },
    {
      'name': 'NXDN with Privacy Key',
      'command': '-fn -R 12345 -i rtl:0:450M:26:0:8',
    },
  ];

  @override
  void initState() {
    super.initState();
    _commandController = TextEditingController(text: '-fp -fx -i rtl:0:851.375M:22:-2:24:0:2');
    _isRunning = widget.isRunning;
  }

  @override
  void didUpdateWidget(AdvancedScanScreen oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.isRunning != widget.isRunning) {
      setState(() {
        _isRunning = widget.isRunning;
      });
    }
  }

  @override
  void dispose() {
    _commandController.dispose();
    super.dispose();
  }

  void _loadExample(String command) {
    setState(() {
      _commandController.text = command;
    });
  }

  void _startAdvancedScanning() async {
    final command = _commandController.text.trim();
    if (command.isEmpty) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Please enter a DSD command string'),
          backgroundColor: Colors.red,
        ),
      );
      return;
    }

    // Show loading indicator
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Row(
            children: [
              SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
              SizedBox(width: 16),
              Text('Starting DSD with custom command...'),
            ],
          ),
          duration: Duration(seconds: 10),
        ),
      );
    }

    try {
      // Set custom command args - this will be parsed by DSD on start
      await widget.dsdPlugin.setCustomArgs(command);
      
      // Clear any current system selection (Advanced Scan mode = no system)
      widget.scanningService.clearCurrentSystem();

      // Start DSD engine - it will parse the custom args
      widget.onStart();
      
      if (mounted) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        Navigator.pop(context);
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Error: $e'),
            backgroundColor: Colors.red,
            duration: const Duration(seconds: 3),
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Advanced Scan'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: SingleChildScrollView(
          padding: const EdgeInsets.all(16),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.stretch,
            children: [
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Custom DSD Command Args',
                        style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        'Enter full DSD-Neo command line',
                        style: TextStyle(fontSize: 13, color: Colors.grey[400]),
                      ),
                      const SizedBox(height: 4),
                      Text(
                        'Include -i for input source (rtl, rtltcp, etc.)',
                        style: TextStyle(fontSize: 12, color: Colors.grey[500]),
                      ),
                      const SizedBox(height: 16),
                      TextField(
                        controller: _commandController,
                        decoration: const InputDecoration(
                          labelText: 'DSD-Neo Full Command Line',
                          hintText: '-fp -fx -i rtl:0:851.375M:22:-2:24',
                          border: OutlineInputBorder(),
                          prefixIcon: Icon(Icons.terminal),
                          helperText: 'Full DSD-Neo command with -i for input source',
                          helperMaxLines: 2,
                        ),
                        maxLines: 4,
                        keyboardType: TextInputType.text,
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'Quick Examples',
                        style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 8),
                      ..._examples.map((example) => Padding(
                        padding: const EdgeInsets.only(bottom: 8),
                        child: OutlinedButton(
                          onPressed: () => _loadExample(example['command']!),
                          style: OutlinedButton.styleFrom(
                            alignment: Alignment.centerLeft,
                          ),
                          child: Column(
                            crossAxisAlignment: CrossAxisAlignment.start,
                            children: [
                              Text(
                                example['name']!,
                                style: const TextStyle(fontWeight: FontWeight.bold),
                              ),
                              Text(
                                example['command']!,
                                style: TextStyle(fontSize: 11, color: Colors.grey[400]),
                              ),
                            ],
                          ),
                        ),
                      )),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
              Card(
                color: Colors.orange[900]?.withOpacity(0.3),
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Row(
                    children: [
                      Icon(Icons.warning_amber, color: Colors.orange[300], size: 20),
                      const SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          'Advanced users only! Invalid arguments may cause DSD to crash.',
                          style: TextStyle(fontSize: 12, color: Colors.orange[200]),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 8),
              Card(
                color: Colors.blue[900]?.withOpacity(0.3),
                child: Padding(
                  padding: const EdgeInsets.all(12),
                  child: Row(
                    children: [
                      Icon(Icons.info_outline, color: Colors.blue[300], size: 20),
                      const SizedBox(width: 12),
                      Expanded(
                        child: Text(
                          'Enter complete DSD-Neo command with -i flag to specify SDR source (rtl, rtltcp, etc.). This bypasses SDR Settings.',
                          style: TextStyle(fontSize: 12, color: Colors.blue[200]),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 24),
              ElevatedButton.icon(
                onPressed: _isRunning ? null : _startAdvancedScanning,
                icon: const Icon(Icons.play_arrow, size: 28),
                label: const Text('Start Advanced Scan', style: TextStyle(fontSize: 18)),
                style: ElevatedButton.styleFrom(
                  padding: const EdgeInsets.symmetric(vertical: 16),
                  backgroundColor: Colors.green[700],
                  foregroundColor: Colors.white,
                  disabledBackgroundColor: Colors.grey[700],
                ),
              ),
              if (_isRunning) ...[
                const SizedBox(height: 12),
                ElevatedButton.icon(
                  onPressed: () async {
                    // Show loading indicator
                    if (mounted) {
                      ScaffoldMessenger.of(context).showSnackBar(
                        const SnackBar(
                          content: Row(
                            children: [
                              SizedBox(
                                width: 20,
                                height: 20,
                                child: CircularProgressIndicator(strokeWidth: 2),
                              ),
                              SizedBox(width: 16),
                              Text('Stopping scanner...'),
                            ],
                          ),
                          duration: Duration(seconds: 5),
                        ),
                      );
                    }
                    
                    // Stop in background to avoid blocking UI
                    await Future.microtask(() {
                      widget.onStop();
                    });
                    
                    if (mounted) {
                      ScaffoldMessenger.of(context).hideCurrentSnackBar();
                      Navigator.pop(context);
                    }
                  },
                  icon: const Icon(Icons.stop, size: 28),
                  label: const Text('Stop Scanning', style: TextStyle(fontSize: 18)),
                  style: ElevatedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                    backgroundColor: Colors.red[700],
                    foregroundColor: Colors.white,
                  ),
                ),
              ],
            ],
          ),
        ),
      ),
    );
  }
}
