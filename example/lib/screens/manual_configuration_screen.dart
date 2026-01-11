import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../services/settings_service.dart';
import '../services/native_rtlsdr_service.dart';

class ManualConfigurationScreen extends StatefulWidget {
  final SettingsService settings;
  final DsdFlutter dsdPlugin;
  final bool isRunning;
  final VoidCallback onStart;
  final VoidCallback onStop;
  final Function(String) onStatusUpdate;

  const ManualConfigurationScreen({
    super.key,
    required this.settings,
    required this.dsdPlugin,
    required this.isRunning,
    required this.onStart,
    required this.onStop,
    required this.onStatusUpdate,
  });

  @override
  State<ManualConfigurationScreen> createState() => _ManualConfigurationScreenState();
}

class _ManualConfigurationScreenState extends State<ManualConfigurationScreen> {
  late TextEditingController _remoteHostController;
  late TextEditingController _remotePortController;
  late TextEditingController _freqController;
  late TextEditingController _gainController;
  late TextEditingController _ppmController;
  bool _isConfiguring = false;
  bool _nativeRtlSdrSupported = false;
  List<RtlSdrUsbDevice> _nativeUsbDevices = [];
  bool _isRunning = false;

  @override
  void initState() {
    super.initState();
    _remoteHostController = TextEditingController(text: widget.settings.remoteHost);
    _remotePortController = TextEditingController(text: widget.settings.remotePort.toString());
    _freqController = TextEditingController(text: widget.settings.frequency.toString());
    _gainController = TextEditingController(text: widget.settings.gain.toString());
    _ppmController = TextEditingController(text: widget.settings.ppm.toString());
    _isRunning = widget.isRunning;
    _checkNativeRtlSdrSupport();
  }
  
  @override
  void didUpdateWidget(ManualConfigurationScreen oldWidget) {
    super.didUpdateWidget(oldWidget);
    if (oldWidget.isRunning != widget.isRunning) {
      setState(() {
        _isRunning = widget.isRunning;
      });
    }
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

  @override
  void dispose() {
    _remoteHostController.dispose();
    _remotePortController.dispose();
    _freqController.dispose();
    _gainController.dispose();
    _ppmController.dispose();
    super.dispose();
  }

  Future<void> _applySettings() async {
    try {
      setState(() => _isConfiguring = true);
      
      final freq = double.parse(_freqController.text);
      final gain = int.parse(_gainController.text);
      final ppm = int.parse(_ppmController.text);
      
      widget.settings.updateFrequency(freq);
      widget.settings.updateGain(gain);
      widget.settings.updatePpm(ppm);
      
      if (widget.settings.rtlSource == RtlSource.nativeUsb) {
        // Native USB mode - open device and configure
        widget.onStatusUpdate('Opening native USB RTL-SDR...');
        
        if (_nativeUsbDevices.isEmpty) {
          throw Exception('No RTL-SDR USB devices found. Please connect a device.');
        }
        
        final result = await NativeRtlSdrService.openDevice(_nativeUsbDevices.first.deviceName);
        if (result == null) {
          throw Exception('Failed to open RTL-SDR USB device. Please grant USB permission.');
        }
        
        widget.settings.setNativeUsbDevice(
          result['fd'] as int,
          result['devicePath'] as String,
        );
        
        widget.onStatusUpdate('Configuring native USB RTL-SDR...');
        
        final success = await widget.dsdPlugin.connectNativeUsb(
          fd: result['fd'] as int,
          devicePath: result['devicePath'] as String,
          freqHz: widget.settings.frequencyHz,
          sampleRate: widget.settings.sampleRate,
          gain: gain * 10, // Convert to tenths of dB
          ppm: ppm,
        );
        
        if (!success) {
          throw Exception('Failed to configure native RTL-SDR');
        }
      } else {
        // Remote mode
        final host = _remoteHostController.text;
        final port = int.parse(_remotePortController.text);
        widget.settings.updateRemoteHost(host);
        widget.settings.updateRemotePort(port);
        widget.onStatusUpdate('Connecting to $host:$port...');
        
        await widget.dsdPlugin.connect(
          widget.settings.effectiveHost,
          widget.settings.effectivePort,
          widget.settings.frequencyHz,
        );
      }
      
      await widget.dsdPlugin.setAudioEnabled(widget.settings.audioEnabled);
      
      widget.onStatusUpdate('Configuration applied successfully');
      
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          const SnackBar(
            content: Text('Settings applied successfully'),
            backgroundColor: Colors.green,
          ),
        );
      }
    } catch (e) {
      widget.onStatusUpdate('Configuration error: $e');
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Error: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    } finally {
      setState(() => _isConfiguring = false);
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Manual Configuration'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(12),
          children: [
            _buildExpandableSection(
              title: 'Connection Settings',
              icon: Icons.settings_input_antenna,
              iconColor: Colors.blue[300]!,
              children: [
                _buildSourceCard(),
                const SizedBox(height: 12),
                _buildConnectionCard(),
              ],
            ),
            const SizedBox(height: 12),
            _buildExpandableSection(
              title: 'Tuning & Audio',
              icon: Icons.tune,
              iconColor: Colors.green[300]!,
              children: [
                _buildTuningCard(),
              ],
            ),
            const SizedBox(height: 12),
            _buildExpandableSection(
              title: 'Scanner Control',
              icon: Icons.play_circle,
              iconColor: Colors.amber[300]!,
              children: [
                _buildControlCard(),
              ],
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildExpandableSection({
    required String title,
    required IconData icon,
    required Color iconColor,
    required List<Widget> children,
    bool initiallyExpanded = true,
  }) {
    return Card(
      child: ExpansionTile(
        title: Row(
          children: [
            Icon(icon, color: iconColor, size: 20),
            const SizedBox(width: 8),
            Text(
              title,
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
          ],
        ),
        initiallyExpanded: initiallyExpanded,
        children: [
          Padding(
            padding: const EdgeInsets.all(12),
            child: Column(
              children: children,
            ),
          ),
        ],
      ),
    );
  }

  Widget _buildSourceCard() {
    final nativeDeviceStatus = _nativeUsbDevices.isEmpty 
        ? 'No devices found' 
        : '${_nativeUsbDevices.length} device(s) found';
    
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'RTL-SDR Source',
          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
        ),
        const SizedBox(height: 8),
        RadioGroup<RtlSource>(
          groupValue: widget.settings.rtlSource,
          onChanged: _isRunning ? (RtlSource? value) {} : (RtlSource? value) {
            if (value != null) {
              widget.settings.setRtlSource(value);
              setState(() {});
            }
          },
          child: Column(
            children: [
              // Native USB option (recommended)
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
                          'Recommended',
                          style: TextStyle(fontSize: 9, color: Colors.white),
                        ),
                      ),
                    ],
                  ),
                  subtitle: Text(
                    nativeDeviceStatus,
                    style: TextStyle(
                      fontSize: 11,
                      color: _nativeUsbDevices.isNotEmpty ? Colors.green : Colors.orange,
                    ),
                  ),
                  value: RtlSource.nativeUsb,
                  dense: true,
                  contentPadding: EdgeInsets.zero,
                ),
                if (widget.settings.rtlSource == RtlSource.nativeUsb)
                  Padding(
                    padding: const EdgeInsets.only(left: 40),
                    child: TextButton.icon(
                      icon: const Icon(Icons.refresh, size: 16),
                      label: const Text('Refresh Devices', style: TextStyle(fontSize: 12)),
                      onPressed: _refreshNativeDevices,
                    ),
                  ),
              ],
              RadioListTile<RtlSource>(
                title: const Text('Remote RTL-TCP Server', style: TextStyle(fontSize: 14)),
                subtitle: const Text('Connect over network', style: TextStyle(fontSize: 11)),
                value: RtlSource.remote,
                dense: true,
                contentPadding: EdgeInsets.zero,
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildConnectionCard() {
    final isNativeUsb = widget.settings.rtlSource == RtlSource.nativeUsb;
    
    // For native USB, show device info instead of connection settings
    if (isNativeUsb) {
      return Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          const Text(
            'USB Device',
            style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
          ),
          const SizedBox(height: 8),
          if (_nativeUsbDevices.isEmpty)
            Container(
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: Colors.orange[900]?.withValues(alpha: 0.3),
                borderRadius: BorderRadius.circular(8),
                border: Border.all(color: Colors.orange[700]!),
              ),
              child: const Row(
                children: [
                  Icon(Icons.usb_off, color: Colors.orange, size: 20),
                  SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      'No RTL-SDR devices connected. Please connect a device via USB OTG.',
                      style: TextStyle(fontSize: 12, color: Colors.orange),
                    ),
                  ),
                ],
              ),
            )
          else
            ...List.generate(_nativeUsbDevices.length, (index) {
              final device = _nativeUsbDevices[index];
              return Container(
                margin: EdgeInsets.only(bottom: index < _nativeUsbDevices.length - 1 ? 8 : 0),
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.green[900]?.withValues(alpha: 0.3),
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: Colors.green[700]!),
                ),
                child: Row(
                  children: [
                    const Icon(Icons.usb, color: Colors.green, size: 20),
                    const SizedBox(width: 8),
                    Expanded(
                      child: Column(
                        crossAxisAlignment: CrossAxisAlignment.start,
                        children: [
                          Text(
                            device.productName,
                            style: const TextStyle(fontSize: 13, fontWeight: FontWeight.w500),
                          ),
                          Text(
                            'VID: 0x${device.vendorId.toRadixString(16).padLeft(4, '0')} PID: 0x${device.productId.toRadixString(16).padLeft(4, '0')}',
                            style: TextStyle(fontSize: 11, color: Colors.grey[400]),
                          ),
                        ],
                      ),
                    ),
                    Icon(
                      device.hasPermission ? Icons.check_circle : Icons.warning,
                      color: device.hasPermission ? Colors.green : Colors.orange,
                      size: 18,
                    ),
                  ],
                ),
              );
            }),
        ],
      );
    }
    
    // Remote connection settings
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        const Text(
          'Remote Connection',
          style: TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
        ),
        const SizedBox(height: 8),
        TextField(
          controller: _remoteHostController,
          decoration: const InputDecoration(
            labelText: 'Host',
            hintText: '192.168.1.240',
            border: OutlineInputBorder(),
            isDense: true,
            contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          ),
          style: const TextStyle(fontSize: 14),
          enabled: !_isRunning,
        ),
        const SizedBox(height: 10),
        TextField(
          controller: _remotePortController,
          decoration: const InputDecoration(
            labelText: 'Port',
            hintText: '1234',
            border: OutlineInputBorder(),
            isDense: true,
            contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          ),
          style: const TextStyle(fontSize: 14),
          keyboardType: TextInputType.number,
          enabled: !_isRunning,
        ),
      ],
    );
  }

  Widget _buildTuningCard() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        TextField(
          controller: _freqController,
          decoration: const InputDecoration(
            labelText: 'Frequency (MHz)',
            hintText: '771.18125',
            border: OutlineInputBorder(),
            isDense: true,
            suffixText: 'MHz',
            contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
          ),
          style: const TextStyle(fontSize: 14),
          keyboardType: const TextInputType.numberWithOptions(decimal: true),
          enabled: !_isRunning,
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: TextField(
                controller: _gainController,
                decoration: const InputDecoration(
                  labelText: 'Gain',
                  border: OutlineInputBorder(),
                  isDense: true,
                  contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                ),
                style: const TextStyle(fontSize: 14),
                keyboardType: TextInputType.number,
                enabled: !_isRunning,
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: TextField(
                controller: _ppmController,
                decoration: const InputDecoration(
                  labelText: 'PPM',
                  border: OutlineInputBorder(),
                  isDense: true,
                  contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
                ),
                style: const TextStyle(fontSize: 14),
                keyboardType: TextInputType.number,
                enabled: !_isRunning,
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        SwitchListTile(
          title: const Text('Audio Output', style: TextStyle(fontSize: 14)),
          value: widget.settings.audioEnabled,
          onChanged: _isRunning ? null : (value) {
            widget.settings.setAudioEnabled(value);
            setState(() {});
          },
          dense: true,
          contentPadding: EdgeInsets.zero,
        ),
      ],
    );
  }

  Widget _buildControlCard() {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        SizedBox(
          width: double.infinity,
          child: ElevatedButton.icon(
            onPressed: _isRunning || _isConfiguring ? null : _applySettings,
            icon: _isConfiguring
                ? const SizedBox(
                    width: 16,
                    height: 16,
                    child: CircularProgressIndicator(strokeWidth: 2),
                  )
                : const Icon(Icons.check, size: 18),
            label: Text(_isConfiguring ? 'Applying...' : 'Apply & Connect'),
            style: ElevatedButton.styleFrom(
              backgroundColor: Colors.blue,
              padding: const EdgeInsets.symmetric(vertical: 12),
            ),
          ),
        ),
        const SizedBox(height: 10),
        Row(
          children: [
            Expanded(
              child: ElevatedButton.icon(
                onPressed: _isRunning ? null : () {
                  widget.onStart();
                  setState(() => _isRunning = true);
                },
                icon: const Icon(Icons.play_arrow, size: 18),
                label: const Text('Start'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.green,
                  padding: const EdgeInsets.symmetric(vertical: 10),
                ),
              ),
            ),
            const SizedBox(width: 10),
            Expanded(
              child: ElevatedButton.icon(
                onPressed: _isRunning ? () {
                  widget.onStop();
                  setState(() => _isRunning = false);
                } : null,
                icon: const Icon(Icons.stop, size: 18),
                label: const Text('Stop'),
                style: ElevatedButton.styleFrom(
                  backgroundColor: Colors.red,
                  padding: const EdgeInsets.symmetric(vertical: 10),
                ),
              ),
            ),
          ],
        ),
        const SizedBox(height: 12),
        Container(
          padding: const EdgeInsets.all(8),
          decoration: BoxDecoration(
            color: Colors.blueGrey[800],
            borderRadius: BorderRadius.circular(4),
          ),
          child: Row(
            children: [
              Icon(
                _isRunning ? Icons.radio : Icons.radio_button_unchecked,
                size: 16,
                color: _isRunning ? Colors.green : Colors.grey,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  _isRunning 
                      ? widget.settings.rtlSource == RtlSource.nativeUsb
                          ? 'Scanner running - Native USB'
                          : 'Scanner running - \${widget.settings.effectiveHost}:\${widget.settings.effectivePort}'
                      : 'Scanner stopped',
                  style: TextStyle(
                    fontSize: 11,
                    color: _isRunning ? Colors.green[300] : Colors.grey,
                  ),
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }
}
