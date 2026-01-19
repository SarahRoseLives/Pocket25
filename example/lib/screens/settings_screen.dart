import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import 'package:package_info_plus/package_info_plus.dart';
import '../services/settings_service.dart';
import '../services/scanning_service.dart';
import 'system_selection_screen.dart';
import 'import_manage_screen.dart';
import 'sdr_settings_screen.dart';
import 'quick_scan_screen.dart';

class SettingsScreen extends StatefulWidget {
  final SettingsService settings;
  final DsdFlutter dsdPlugin;
  final ScanningService scanningService;
  final bool isRunning;
  final VoidCallback onStart;
  final VoidCallback onStop;
  final Function(String) onStatusUpdate;

  const SettingsScreen({
    super.key,
    required this.settings,
    required this.dsdPlugin,
    required this.scanningService,
    required this.isRunning,
    required this.onStart,
    required this.onStop,
    required this.onStatusUpdate,
  });

  @override
  State<SettingsScreen> createState() => _SettingsScreenState();
}

class _SettingsScreenState extends State<SettingsScreen> {
  String _version = '...';

  @override
  void initState() {
    super.initState();
    _loadVersion();
  }

  Future<void> _loadVersion() async {
    final packageInfo = await PackageInfo.fromPlatform();
    setState(() {
      _version = '${packageInfo.version}+${packageInfo.buildNumber}';
    });
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Settings'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(12),
          children: [
            _buildMenuTile(
              context,
              title: 'Systems',
              subtitle: 'View and manage systems, select site to scan',
              icon: Icons.cell_tower,
              iconColor: Colors.cyan[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => SystemSelectionScreen(
                      onSystemSelected: (siteId, siteName) async {
                        // Start scanning in background to avoid blocking UI thread
                        Future.microtask(() async {
                          await widget.scanningService.startScanning(siteId, siteName);
                        });
                        if (context.mounted) {
                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(
                              content: Text('Starting scan: $siteName...'),
                              duration: const Duration(seconds: 2),
                            ),
                          );
                        }
                      },
                      scanningService: widget.scanningService,
                    ),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
            _buildMenuTile(
              context,
              title: 'Import & Manage',
              subtitle: 'Import from Radio Reference, Web Programmer',
              icon: Icons.cloud_download,
              iconColor: Colors.purple[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => const ImportManageScreen(),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
            _buildMenuTile(
              context,
              title: 'SDR Settings',
              subtitle: 'Configure RTL-SDR, HackRF, or RTL-TCP server',
              icon: Icons.settings_input_antenna,
              iconColor: Colors.blue[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => SdrSettingsScreen(
                      settings: widget.settings,
                      dsdPlugin: widget.dsdPlugin,
                    ),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
            _buildMenuTile(
              context,
              title: 'Quick Scan',
              subtitle: 'Scan a frequency without creating a system',
              icon: Icons.radio,
              iconColor: Colors.orange[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => QuickScanScreen(
                      settings: widget.settings,
                      dsdPlugin: widget.dsdPlugin,
                      scanningService: widget.scanningService,
                      isRunning: widget.isRunning,
                      onStart: widget.onStart,
                      onStop: widget.onStop,
                    ),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
            _buildMenuTile(
              context,
              title: 'About',
              subtitle: 'App version and information',
              icon: Icons.info_outline,
              iconColor: Colors.grey[300]!,
              onTap: () {
                showAboutDialog(
                  context: context,
                  applicationName: 'Pocket25',
                  applicationVersion: _version,
                  applicationLegalese: 'Licensed under GNU GPLv3',
                  children: const [
                    SizedBox(height: 16),
                    Text(
                      'Digital Voice Decoder for Android',
                      style: TextStyle(fontWeight: FontWeight.bold),
                    ),
                    SizedBox(height: 16),
                    Text(
                      'Developed by Sarah Rose',
                      style: TextStyle(fontSize: 12),
                    ),
                    SizedBox(height: 4),
                    Text(
                      'DSD integration supported by GitHub Copilot AI',
                      style: TextStyle(fontSize: 11, fontStyle: FontStyle.italic),
                    ),
                    SizedBox(height: 16),
                    Divider(),
                    SizedBox(height: 8),
                    Text(
                      'Credits:',
                      style: TextStyle(fontWeight: FontWeight.bold),
                    ),
                    SizedBox(height: 8),
                    Text(
                      'This application embeds DSD-Neo, a digital speech decoder capable of decoding multiple digital voice protocols.',
                      style: TextStyle(fontSize: 12),
                    ),
                    SizedBox(height: 8),
                    Text(
                      'DSD-Neo is based on DSD-FME (Digital Speech Decoder - Florida Man Edition), which in turn is based on the original DSD (Digital Speech Decoder) project.',
                      style: TextStyle(fontSize: 12),
                    ),
                  ],
                );
              },
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildMenuTile(
    BuildContext context, {
    required String title,
    required String subtitle,
    required IconData icon,
    required Color iconColor,
    required VoidCallback onTap,
  }) {
    return Card(
      child: ListTile(
        leading: Icon(icon, color: iconColor, size: 32),
        title: Text(
          title,
          style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
        ),
        subtitle: Text(
          subtitle,
          style: const TextStyle(fontSize: 12),
        ),
        trailing: const Icon(Icons.chevron_right),
        onTap: onTap,
        contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
      ),
    );
  }
}

