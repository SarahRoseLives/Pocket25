import 'package:flutter/material.dart';
import 'package:flutter/services.dart';
import 'package:network_info_plus/network_info_plus.dart';
import '../services/web_programmer_service.dart';

class WebProgrammerScreen extends StatefulWidget {
  const WebProgrammerScreen({super.key});

  @override
  State<WebProgrammerScreen> createState() => _WebProgrammerScreenState();
}

class _WebProgrammerScreenState extends State<WebProgrammerScreen> {
  final WebProgrammerService _webService = WebProgrammerService();
  final NetworkInfo _networkInfo = NetworkInfo();
  String? _ipAddress;
  bool _isLoading = false;
  String? _errorMessage;

  @override
  void initState() {
    super.initState();
    _loadIpAddress();
  }

  Future<void> _loadIpAddress() async {
    try {
      final ip = await _networkInfo.getWifiIP();
      setState(() {
        _ipAddress = ip;
      });
    } catch (e) {
      setState(() {
        _ipAddress = 'Unable to get IP';
      });
    }
  }

  Future<void> _toggleServer() async {
    setState(() {
      _isLoading = true;
      _errorMessage = null;
    });

    try {
      if (_webService.isRunning) {
        await _webService.stopServer();
      } else {
        await _webService.startServer();
        // Reload IP address in case it changed
        await _loadIpAddress();
      }
      
      setState(() {
        _isLoading = false;
      });

      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(
              _webService.isRunning
                  ? 'Web Programmer started'
                  : 'Web Programmer stopped',
            ),
            duration: const Duration(seconds: 2),
          ),
        );
      }
    } catch (e) {
      setState(() {
        _isLoading = false;
        _errorMessage = 'Error: ${e.toString()}';
      });
    }
  }

  void _copyToClipboard(String text) {
    Clipboard.setData(ClipboardData(text: text));
    ScaffoldMessenger.of(context).showSnackBar(
      const SnackBar(
        content: Text('Copied to clipboard'),
        duration: Duration(seconds: 1),
      ),
    );
  }

  @override
  void dispose() {
    _webService.stopServer();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    final serverUrl = _ipAddress != null && _webService.isRunning
        ? 'http://$_ipAddress:${_webService.port}'
        : null;

    return Scaffold(
      appBar: AppBar(
        title: const Text('Web Programmer'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      children: [
                        Icon(
                          Icons.info_outline,
                          color: Colors.blue[300],
                          size: 24,
                        ),
                        const SizedBox(width: 12),
                        const Expanded(
                          child: Text(
                            'About Web Programmer',
                            style: TextStyle(
                              fontSize: 18,
                              fontWeight: FontWeight.bold,
                            ),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    const Text(
                      'The Web Programmer allows you to manually configure radio systems using a web browser. This is useful for users who don\'t have access to Radio Reference.',
                      style: TextStyle(fontSize: 14),
                    ),
                    const SizedBox(height: 8),
                    const Text(
                      'Enable the web server below and connect from any device on the same network.',
                      style: TextStyle(fontSize: 14, color: Colors.grey),
                    ),
                  ],
                ),
              ),
            ),
            const SizedBox(height: 20),
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Row(
                      mainAxisAlignment: MainAxisAlignment.spaceBetween,
                      children: [
                        Row(
                          children: [
                            Icon(
                              _webService.isRunning
                                  ? Icons.check_circle
                                  : Icons.cloud_off,
                              color: _webService.isRunning
                                  ? Colors.green[300]
                                  : Colors.grey[500],
                              size: 24,
                            ),
                            const SizedBox(width: 12),
                            Text(
                              'Web Server',
                              style: const TextStyle(
                                fontSize: 18,
                                fontWeight: FontWeight.bold,
                              ),
                            ),
                          ],
                        ),
                        Switch(
                          value: _webService.isRunning,
                          onChanged: _isLoading ? null : (_) => _toggleServer(),
                          activeTrackColor: Colors.green[400],
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    if (_isLoading)
                      const Center(
                        child: Padding(
                          padding: EdgeInsets.all(8.0),
                          child: CircularProgressIndicator(),
                        ),
                      ),
                    if (_errorMessage != null)
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: Colors.red.withValues(alpha: 0.2),
                          border: Border.all(color: Colors.red),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Row(
                          children: [
                            Icon(Icons.error_outline, color: Colors.red[300]),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                _errorMessage!,
                                style: TextStyle(color: Colors.red[300]),
                              ),
                            ),
                          ],
                        ),
                      ),
                    if (_webService.isRunning && serverUrl != null) ...[
                      const Divider(height: 24),
                      const Text(
                        'Connection Information',
                        style: TextStyle(
                          fontSize: 16,
                          fontWeight: FontWeight.bold,
                        ),
                      ),
                      const SizedBox(height: 12),
                      _buildInfoRow(
                        'Device IP Address',
                        _ipAddress ?? 'Loading...',
                        Icons.devices,
                      ),
                      const SizedBox(height: 8),
                      _buildInfoRow(
                        'Port',
                        '${_webService.port}',
                        Icons.settings_ethernet,
                      ),
                      const SizedBox(height: 12),
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: Colors.blue.withValues(alpha: 0.1),
                          border: Border.all(color: Colors.blue),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Column(
                          crossAxisAlignment: CrossAxisAlignment.start,
                          children: [
                            Row(
                              children: [
                                Icon(Icons.link, color: Colors.blue[300]),
                                const SizedBox(width: 8),
                                const Text(
                                  'Web URL',
                                  style: TextStyle(
                                    fontWeight: FontWeight.bold,
                                  ),
                                ),
                              ],
                            ),
                            const SizedBox(height: 8),
                            Row(
                              children: [
                                Expanded(
                                  child: SelectableText(
                                    serverUrl,
                                    style: TextStyle(
                                      color: Colors.blue[300],
                                      fontSize: 16,
                                      fontWeight: FontWeight.bold,
                                    ),
                                  ),
                                ),
                                IconButton(
                                  icon: const Icon(Icons.copy),
                                  onPressed: () => _copyToClipboard(serverUrl),
                                  tooltip: 'Copy URL',
                                  color: Colors.blue[300],
                                ),
                              ],
                            ),
                          ],
                        ),
                      ),
                      const SizedBox(height: 12),
                      Container(
                        padding: const EdgeInsets.all(12),
                        decoration: BoxDecoration(
                          color: Colors.orange.withValues(alpha: 0.1),
                          border: Border.all(color: Colors.orange),
                          borderRadius: BorderRadius.circular(8),
                        ),
                        child: Row(
                          children: [
                            Icon(Icons.info_outline, color: Colors.orange[300]),
                            const SizedBox(width: 8),
                            Expanded(
                              child: Text(
                                'Open this URL in a web browser on any device connected to the same network.',
                                style: TextStyle(
                                  color: Colors.orange[300],
                                  fontSize: 13,
                                ),
                              ),
                            ),
                          ],
                        ),
                      ),
                    ] else if (!_webService.isRunning && !_isLoading) ...[
                      const SizedBox(height: 12),
                      Text(
                        'Enable the web server to get connection information.',
                        style: TextStyle(
                          color: Colors.grey[400],
                          fontSize: 14,
                        ),
                      ),
                    ],
                  ],
                ),
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildInfoRow(String label, String value, IconData icon) {
    return Row(
      children: [
        Icon(icon, size: 20, color: Colors.grey[400]),
        const SizedBox(width: 8),
        Text(
          '$label: ',
          style: TextStyle(
            color: Colors.grey[400],
            fontSize: 14,
          ),
        ),
        Text(
          value,
          style: const TextStyle(
            fontWeight: FontWeight.bold,
            fontSize: 14,
          ),
        ),
      ],
    );
  }
}
