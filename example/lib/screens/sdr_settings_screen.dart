import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../services/settings_service.dart';
import '../services/native_rtlsdr_service.dart';

class SdrSettingsScreen extends StatefulWidget {
  final SettingsService settings;
  final DsdFlutter dsdPlugin;

  const SdrSettingsScreen({
    super.key,
    required this.settings,
    required this.dsdPlugin,
  });

  @override
  State<SdrSettingsScreen> createState() => _SdrSettingsScreenState();
}

class _SdrSettingsScreenState extends State<SdrSettingsScreen> {
  late TextEditingController _remoteHostController;
  late TextEditingController _remotePortController;
  late TextEditingController _gainController;
  late TextEditingController _ppmController;
  bool _nativeRtlSdrSupported = false;
  List<RtlSdrUsbDevice> _nativeUsbDevices = [];
  bool _hackrfSupported = false;
  List<Map<String, dynamic>> _hackrfDevices = [];
  late bool _biasTeeEnabled;

  @override
  void initState() {
    super.initState();
    _remoteHostController = TextEditingController(text: widget.settings.remoteHost);
    _remotePortController = TextEditingController(text: widget.settings.remotePort.toString());
    _gainController = TextEditingController(text: widget.settings.gain.toString());
    _ppmController = TextEditingController(text: widget.settings.ppm.toString());
    _biasTeeEnabled = widget.settings.biasTee;
    _checkNativeRtlSdrSupport();
    _checkHackRfSupport();
  }

  Future<void> _checkNativeRtlSdrSupport() async {
    final supported = await widget.dsdPlugin.isNativeRtlSdrSupported();
    if (mounted) {
      setState(() {
        _nativeRtlSdrSupported = supported;
      });
    }
    if (supported) {
      await _refreshNativeDevices();
    }
  }

  Future<void> _refreshNativeDevices() async {
    final devices = await NativeRtlSdrService.listDevices();
    if (mounted) {
      setState(() {
        _nativeUsbDevices = devices;
      });
    }
  }

  Future<void> _checkHackRfSupport() async {
    if (kDebugMode) {
      if (mounted) {
        setState(() {
          _hackrfSupported = true;
        });
      }
      await _refreshHackRfDevices();
    } else {
      if (mounted) {
        setState(() {
          _hackrfSupported = false;
        });
      }
    }
  }

  Future<void> _refreshHackRfDevices() async {
    try {
      final devices = await widget.dsdPlugin.hackrfListDevices();
      if (mounted) {
        setState(() {
          _hackrfDevices = devices;
        });
      }
    } catch (e) {
      if (kDebugMode) {
        print('Error listing HackRF devices: $e');
      }
      if (mounted) {
        setState(() {
          _hackrfDevices = [];
        });
      }
    }
  }

  @override
  void dispose() {
    _remoteHostController.dispose();
    _remotePortController.dispose();
    _gainController.dispose();
    _ppmController.dispose();
    super.dispose();
  }

  void _saveSettings() {
    widget.settings.updateRemoteHost(_remoteHostController.text);
    widget.settings.updateRemotePort(int.tryParse(_remotePortController.text) ?? 8081);
    widget.settings.updateGain(int.tryParse(_gainController.text) ?? 30);
    widget.settings.updatePpm(int.tryParse(_ppmController.text) ?? 0);
    
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text('SDR settings saved'),
        duration: Duration(seconds: 2),
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('SDR Settings'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
        actions: [
          IconButton(
            icon: const Icon(Icons.save),
            onPressed: _saveSettings,
            tooltip: 'Save Settings',
          ),
        ],
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            // SDR Source Selection
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'SDR Source',
                      style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 12),
                    RadioListTile<RtlSource>(
                      title: const Text('RTL-TCP Server (Remote)', style: TextStyle(fontSize: 14)),
                      value: RtlSource.remote,
                      groupValue: widget.settings.rtlSource,
                      onChanged: (value) {
                        setState(() {
                          widget.settings.setRtlSource(value!);
                        });
                      },
                    ),
                    if (_nativeRtlSdrSupported) ...[
                      RadioListTile<RtlSource>(
                        title: Row(
                          children: [
                            const Text('Native USB RTL-SDR', style: TextStyle(fontSize: 14)),
                            const SizedBox(width: 8),
                            Container(
                              padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                              decoration: BoxDecoration(
                                color: Colors.green[700],
                                borderRadius: BorderRadius.circular(4),
                              ),
                              child: const Text(
                                'RECOMMENDED',
                                style: TextStyle(fontSize: 10, fontWeight: FontWeight.bold, color: Colors.white),
                              ),
                            ),
                          ],
                        ),
                        subtitle: Text(
                          _nativeUsbDevices.isEmpty ? 'No devices found' : '${_nativeUsbDevices.length} device(s) found',
                          style: TextStyle(fontSize: 12, color: Colors.grey[400]),
                        ),
                        value: RtlSource.nativeUsb,
                        groupValue: widget.settings.rtlSource,
                        onChanged: (value) {
                          setState(() {
                            widget.settings.setRtlSource(value!);
                          });
                        },
                      ),
                      if (_nativeUsbDevices.isNotEmpty)
                        Padding(
                          padding: const EdgeInsets.only(left: 56, top: 4),
                          child: ElevatedButton.icon(
                            icon: const Icon(Icons.refresh, size: 16),
                            label: const Text('Refresh Devices', style: TextStyle(fontSize: 12)),
                            onPressed: _refreshNativeDevices,
                            style: ElevatedButton.styleFrom(
                              padding: const EdgeInsets.symmetric(horizontal: 12, vertical: 8),
                            ),
                          ),
                        ),
                    ],
                    if (_hackrfSupported) ...[
                      RadioListTile<RtlSource>(
                        title: const Text('Native USB HackRF', style: TextStyle(fontSize: 14)),
                        subtitle: Text(
                          _hackrfDevices.isEmpty ? 'No devices found' : '${_hackrfDevices.length} device(s) found',
                          style: TextStyle(fontSize: 12, color: Colors.grey[400]),
                        ),
                        value: RtlSource.hackrf,
                        groupValue: widget.settings.rtlSource,
                        onChanged: (value) {
                          setState(() {
                            widget.settings.setRtlSource(value!);
                          });
                        },
                      ),
                    ],
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),
            
            // RTL-TCP Settings (shown when remote source is selected)
            if (widget.settings.rtlSource == RtlSource.remote) ...[
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'RTL-TCP Server Settings',
                        style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 12),
                      TextField(
                        controller: _remoteHostController,
                        decoration: const InputDecoration(
                          labelText: 'Remote Host',
                          hintText: '127.0.0.1',
                          border: OutlineInputBorder(),
                        ),
                      ),
                      const SizedBox(height: 12),
                      TextField(
                        controller: _remotePortController,
                        decoration: const InputDecoration(
                          labelText: 'Remote Port',
                          hintText: '8081',
                          border: OutlineInputBorder(),
                        ),
                        keyboardType: TextInputType.number,
                      ),
                    ],
                  ),
                ),
              ),
              const SizedBox(height: 16),
            ],
            
            // Gain Settings
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    const Text(
                      'Tuner Settings',
                      style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 12),
                    TextField(
                      controller: _gainController,
                      decoration: const InputDecoration(
                        labelText: 'Gain',
                        hintText: '30',
                        border: OutlineInputBorder(),
                        helperText: 'RTL-SDR gain (0-49)',
                      ),
                      keyboardType: TextInputType.number,
                    ),
                    const SizedBox(height: 12),
                    TextField(
                      controller: _ppmController,
                      decoration: const InputDecoration(
                        labelText: 'PPM Correction',
                        hintText: '0',
                        border: OutlineInputBorder(),
                        helperText: 'Frequency correction in PPM',
                      ),
                      keyboardType: TextInputType.number,
                    ),
                    const SizedBox(height: 12),
                    SwitchListTile(
                      title: const Text('Bias-T'),
                      subtitle: const Text('Enable bias tee for external LNA power'),
                      value: _biasTeeEnabled,
                      onChanged: (value) {
                        setState(() {
                          _biasTeeEnabled = value;
                        });
                        widget.settings.updateBiasTee(value);
                      },
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 16),
            
            // HackRF-specific settings
            if (widget.settings.rtlSource == RtlSource.hackrf) ...[
              Card(
                child: Padding(
                  padding: const EdgeInsets.all(16),
                  child: Column(
                    crossAxisAlignment: CrossAxisAlignment.start,
                    children: [
                      const Text(
                        'HackRF Settings',
                        style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                      ),
                      const SizedBox(height: 12),
                      Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text('LNA Gain: ${widget.settings.hackrfLnaGain} dB', style: const TextStyle(fontSize: 14)),
                                Slider(
                                  value: widget.settings.hackrfLnaGain.toDouble(),
                                  min: 0,
                                  max: 40,
                                  divisions: 8,
                                  label: '${widget.settings.hackrfLnaGain} dB',
                                  onChanged: (value) {
                                    setState(() {
                                      widget.settings.updateHackrfLnaGain(value.toInt());
                                    });
                                  },
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                      const SizedBox(height: 8),
                      Row(
                        children: [
                          Expanded(
                            child: Column(
                              crossAxisAlignment: CrossAxisAlignment.start,
                              children: [
                                Text('VGA Gain: ${widget.settings.hackrfVgaGain} dB', style: const TextStyle(fontSize: 14)),
                                Slider(
                                  value: widget.settings.hackrfVgaGain.toDouble(),
                                  min: 0,
                                  max: 62,
                                  divisions: 31,
                                  label: '${widget.settings.hackrfVgaGain} dB',
                                  onChanged: (value) {
                                    setState(() {
                                      widget.settings.updateHackrfVgaGain(value.toInt());
                                    });
                                  },
                                ),
                              ],
                            ),
                          ),
                        ],
                      ),
                    ],
                  ),
                ),
              ),
            ],
          ],
        ),
      ),
    );
  }
}
