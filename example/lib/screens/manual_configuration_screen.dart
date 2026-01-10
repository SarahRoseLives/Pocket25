import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:url_launcher/url_launcher.dart';
import '../services/settings_service.dart';
import '../services/rtl_tcp_service.dart';

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
  late TextEditingController _localPortController;
  late TextEditingController _freqController;
  late TextEditingController _gainController;
  late TextEditingController _ppmController;
  bool _isConfiguring = false;
  bool _driverInstalled = false;

  @override
  void initState() {
    super.initState();
    _remoteHostController = TextEditingController(text: widget.settings.remoteHost);
    _remotePortController = TextEditingController(text: widget.settings.remotePort.toString());
    _localPortController = TextEditingController(text: widget.settings.localPort.toString());
    _freqController = TextEditingController(text: widget.settings.frequency.toString());
    _gainController = TextEditingController(text: widget.settings.gain.toString());
    _ppmController = TextEditingController(text: widget.settings.ppm.toString());
    _checkDriverInstalled();
  }

  Future<void> _checkDriverInstalled() async {
    final installed = await RtlTcpService.isDriverInstalled();
    if (mounted) {
      setState(() {
        _driverInstalled = installed;
      });
    }
  }

  @override
  void dispose() {
    _remoteHostController.dispose();
    _remotePortController.dispose();
    _localPortController.dispose();
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
      
      if (widget.settings.rtlSource == RtlSource.local) {
        final port = int.parse(_localPortController.text);
        widget.settings.updateLocalPort(port);
        
        widget.onStatusUpdate('Starting RTL-SDR driver...');
        
        final started = await RtlTcpService.startDriver(
          port: port,
          sampleRate: widget.settings.sampleRate,
          frequency: widget.settings.frequencyHz,
          gain: gain,
          ppm: ppm,
        );
        
        if (!started) {
          throw Exception('Failed to start RTL-SDR driver. Make sure it is installed.');
        }
        
        await Future.delayed(const Duration(milliseconds: 500));
        
        widget.onStatusUpdate('Connecting to local RTL-SDR...');
      } else {
        final host = _remoteHostController.text;
        final port = int.parse(_remotePortController.text);
        widget.settings.updateRemoteHost(host);
        widget.settings.updateRemotePort(port);
        widget.onStatusUpdate('Connecting to $host:$port...');
      }
      
      await widget.dsdPlugin.connect(
        widget.settings.effectiveHost,
        widget.settings.effectivePort,
        widget.settings.frequencyHz,
      );
      
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
          onChanged: widget.isRunning ? (RtlSource? value) {} : (RtlSource? value) {
            if (value != null) {
              widget.settings.setRtlSource(value);
              setState(() {});
            }
          },
          child: Column(
            children: [
              RadioListTile<RtlSource>(
                title: const Text('Local USB RTL-SDR', style: TextStyle(fontSize: 14)),
                subtitle: Text(
                  _driverInstalled ? 'Driver installed' : 'Driver not found',
                  style: TextStyle(
                    fontSize: 11,
                    color: _driverInstalled ? Colors.green : Colors.orange,
                  ),
                ),
                value: RtlSource.local,
                dense: true,
                contentPadding: EdgeInsets.zero,
              ),
              if (!_driverInstalled)
                Padding(
                  padding: const EdgeInsets.only(left: 40),
                  child: TextButton.icon(
                    icon: const Icon(Icons.download, size: 16),
                    label: const Text('Install Driver', style: TextStyle(fontSize: 12)),
                    onPressed: () async {
                      final uri = Uri.parse(RtlTcpService.playStoreUrl);
                      if (await canLaunchUrl(uri)) {
                        await launchUrl(uri, mode: LaunchMode.externalApplication);
                      }
                    },
                  ),
                ),
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
    final isLocal = widget.settings.rtlSource == RtlSource.local;
    
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      children: [
        Text(
          isLocal ? 'Local Connection' : 'Remote Connection',
          style: const TextStyle(fontSize: 14, fontWeight: FontWeight.w600),
        ),
        const SizedBox(height: 8),
        if (!isLocal) ...[
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
            enabled: !widget.isRunning,
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
            enabled: !widget.isRunning,
          ),
        ] else ...[
          TextField(
            controller: _localPortController,
            decoration: const InputDecoration(
              labelText: 'Local Port',
              hintText: '1234',
              border: OutlineInputBorder(),
              isDense: true,
              contentPadding: EdgeInsets.symmetric(horizontal: 12, vertical: 10),
            ),
            style: const TextStyle(fontSize: 14),
            keyboardType: TextInputType.number,
            enabled: !widget.isRunning,
          ),
          const SizedBox(height: 8),
          Text(
            'Will connect to 127.0.0.1:\${_localPortController.text}',
            style: TextStyle(fontSize: 11, color: Colors.grey[500]),
          ),
        ],
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
          enabled: !widget.isRunning,
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
                enabled: !widget.isRunning,
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
                enabled: !widget.isRunning,
              ),
            ),
          ],
        ),
        const SizedBox(height: 10),
        SwitchListTile(
          title: const Text('Audio Output', style: TextStyle(fontSize: 14)),
          value: widget.settings.audioEnabled,
          onChanged: widget.isRunning ? null : (value) {
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
            onPressed: widget.isRunning || _isConfiguring ? null : _applySettings,
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
                onPressed: widget.isRunning ? null : widget.onStart,
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
                onPressed: widget.isRunning ? widget.onStop : null,
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
                widget.isRunning ? Icons.radio : Icons.radio_button_unchecked,
                size: 16,
                color: widget.isRunning ? Colors.green : Colors.grey,
              ),
              const SizedBox(width: 8),
              Expanded(
                child: Text(
                  widget.isRunning 
                      ? 'Scanner running - \${widget.settings.effectiveHost}:\${widget.settings.effectivePort}'
                      : 'Scanner stopped',
                  style: TextStyle(
                    fontSize: 11,
                    color: widget.isRunning ? Colors.green[300] : Colors.grey,
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
