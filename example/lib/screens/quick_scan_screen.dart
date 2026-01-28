import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../services/settings_service.dart';
import '../services/scanning_service.dart';
import '../services/native_rtlsdr_service.dart';

class QuickScanScreen extends StatefulWidget {
  final SettingsService settings;
  final DsdFlutter dsdPlugin;
  final ScanningService scanningService;
  final bool isRunning;
  final VoidCallback onStart;
  final VoidCallback onStop;

  const QuickScanScreen({
    super.key,
    required this.settings,
    required this.dsdPlugin,
    required this.scanningService,
    required this.isRunning,
    required this.onStart,
    required this.onStop,
  });

  @override
  State<QuickScanScreen> createState() => _QuickScanScreenState();
}

class _QuickScanScreenState extends State<QuickScanScreen> {
  late TextEditingController _freqController;
  bool _isRunning = false;
  bool _enableTrunkFollowing = false;  // Default to conventional/DMR mode

  @override
  void initState() {
    super.initState();
    _freqController = TextEditingController(text: widget.settings.frequency.toString());
    _isRunning = widget.isRunning;
  }

  @override
  void didUpdateWidget(QuickScanScreen oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.isRunning != widget.isRunning) {
      setState(() {
        _isRunning = widget.isRunning;
      });
      // Reset frequency field when scanner stops
      if (!widget.isRunning) {
        _freqController.text = widget.settings.frequency.toString();
      }
    }
  }

  @override
  void dispose() {
    _freqController.dispose();
    super.dispose();
  }

  void _startScanning() async {
    // Save settings
    final freq = double.tryParse(_freqController.text);
    if (freq == null || freq <= 0) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(
          content: Text('Invalid frequency'),
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
              Text('Initializing SDR...'),
            ],
          ),
          duration: Duration(seconds: 10),
        ),
      );
    }

    try {
      // Stop existing scanner if running
      if (widget.isRunning) {
        widget.onStop();
        // Wait for stop to complete
        await Future.delayed(const Duration(milliseconds: 500));
      }
      
      widget.settings.updateFrequency(freq);
      
      // Set trunk following based on user selection
      await widget.dsdPlugin.setTrunkFollowing(_enableTrunkFollowing);
      
      // Freeze retunes to prevent buffered P25 data from causing retunes to old frequencies
      await widget.dsdPlugin.setRetuneFrozen(true);
      
      // Reset P25 state to clear old frequency tables
      await widget.dsdPlugin.resetP25State();
      
      // Run SDR initialization in background to avoid blocking UI
      await Future.microtask(() async {
        // Initialize SDR based on source type
        if (widget.settings.rtlSource == RtlSource.nativeUsb) {
          // Native USB RTL-SDR - need to open device first
          final devices = await NativeRtlSdrService.listDevices();
          if (devices.isEmpty) {
            throw Exception('No RTL-SDR USB devices found. Please connect a device.');
          }
          
          final result = await NativeRtlSdrService.openDevice(devices.first.deviceName);
          if (result == null) {
            throw Exception('Failed to open RTL-SDR USB device. Please grant USB permission.');
          }
          
          widget.settings.setNativeUsbDevice(
            result['fd'] as int,
            result['devicePath'] as String,
          );
          
          final success = await widget.dsdPlugin.connectNativeUsb(
            fd: result['fd'] as int,
            devicePath: result['devicePath'] as String,
            freqHz: widget.settings.frequencyHz,
            sampleRate: widget.settings.sampleRate,
            gain: widget.settings.gain * 10, // Convert to tenths of dB
            ppm: widget.settings.ppm,
          );
          
          if (!success) {
            throw Exception('Failed to configure native RTL-SDR');
          }
        } else if (widget.settings.rtlSource == RtlSource.hackrf) {
          // HackRF mode
          final dsdSuccess = await widget.dsdPlugin.startHackRfMode(
            widget.settings.frequencyHz,
            widget.settings.sampleRate,
          ).timeout(
            const Duration(seconds: 10),
            onTimeout: () => throw Exception('HackRF init timeout'),
          );
          
          if (!dsdSuccess) {
            throw Exception('Failed to initialize HackRF');
          }
          
          await widget.dsdPlugin.hackrfSetFrequency(widget.settings.frequencyHz);
          await widget.dsdPlugin.hackrfSetSampleRate(widget.settings.sampleRate);
          await widget.dsdPlugin.hackrfSetLnaGain(widget.settings.hackrfLnaGain);
          await widget.dsdPlugin.hackrfSetVgaGain(widget.settings.hackrfVgaGain);
          
          final rxSuccess = await widget.dsdPlugin.hackrfStartRx();
          if (!rxSuccess) {
            throw Exception('Failed to start HackRF RX');
          }
        } else {
          // RTL-TCP remote mode
          await widget.dsdPlugin.connect(
            widget.settings.remoteHost,
            widget.settings.remotePort,
            widget.settings.frequencyHz,
            gain: widget.settings.gain,
            ppm: widget.settings.ppm,
            biasTee: widget.settings.biasTee,
          );
        }
      });
      
      // Clear any current system selection (Quick Scan mode = no system)
      widget.scanningService.clearCurrentSystem();

      // Start scanning
      widget.onStart();
      
      // Wait for connection to be established
      await Future.delayed(const Duration(milliseconds: 500));
      
      // Explicitly retune to ensure the frequency is set
      await widget.dsdPlugin.retune(widget.settings.frequencyHz);
      
      // Mark that we need to unfreeze retunes once we get lock
      // The scanning service will detect sync and unfreeze automatically
      widget.scanningService.setPendingRetuneUnfreeze();
      
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
        title: const Text('Quick Scan'),
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
                        'Scan a Frequency',
                        style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 8),
                      Text(
                        'Scan a single frequency without creating a system',
                        style: TextStyle(fontSize: 13, color: Colors.grey[400]),
                      ),
                      const SizedBox(height: 16),
                      TextField(
                        controller: _freqController,
                        decoration: const InputDecoration(
                          labelText: 'Frequency (MHz)',
                          hintText: '851.0375',
                          border: OutlineInputBorder(),
                          prefixIcon: Icon(Icons.radio),
                        ),
                        keyboardType: const TextInputType.numberWithOptions(decimal: true),
                      ),
                      const SizedBox(height: 16),
                      CheckboxListTile(
                        value: _enableTrunkFollowing,
                        onChanged: (value) {
                          setState(() {
                            _enableTrunkFollowing = value ?? false;
                          });
                        },
                        title: const Text('Enable Trunk Following'),
                        subtitle: Text(
                          _enableTrunkFollowing 
                            ? 'Enabled - Follow trunked system control channels'
                            : 'Disabled - Conventional/DMR mode',
                          style: TextStyle(fontSize: 12, color: Colors.grey[400]),
                        ),
                        controlAffinity: ListTileControlAffinity.leading,
                        contentPadding: EdgeInsets.zero,
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
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
                          'This uses the SDR settings configured in SDR Settings.',
                          style: TextStyle(fontSize: 12, color: Colors.blue[200]),
                        ),
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 24),
              ElevatedButton.icon(
                onPressed: _isRunning ? null : _startScanning,
                icon: const Icon(Icons.play_arrow, size: 28),
                label: const Text('Start Scanning', style: TextStyle(fontSize: 18)),
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
