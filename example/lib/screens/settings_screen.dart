import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../services/settings_service.dart';
import '../services/scanning_service.dart';
import 'manual_configuration_screen.dart';
import 'import_settings_screen.dart';

class SettingsScreen extends StatelessWidget {
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
              title: 'Manual Configuration',
              subtitle: 'Configure RTL-SDR connection and tuning',
              icon: Icons.settings_input_antenna,
              iconColor: Colors.blue[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => ManualConfigurationScreen(
                      settings: settings,
                      dsdPlugin: dsdPlugin,
                      isRunning: isRunning,
                      onStart: onStart,
                      onStop: onStop,
                      onStatusUpdate: onStatusUpdate,
                    ),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
            _buildMenuTile(
              context,
              title: 'Import Settings',
              subtitle: 'Import from Radio Reference and manage systems',
              icon: Icons.cloud_download,
              iconColor: Colors.purple[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => ImportSettingsScreen(
                      onSiteSelected: (siteId, siteName) async {
                        // Start scanning the selected site
                        await scanningService.startScanning(siteId, siteName);
                        if (context.mounted) {
                          ScaffoldMessenger.of(context).showSnackBar(
                            SnackBar(
                              content: Text('Scanning $siteName...'),
                              duration: const Duration(seconds: 2),
                            ),
                          );
                        }
                      },
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
              iconColor: Colors.cyan[300]!,
              onTap: () {
                // TODO: Navigate to about screen
                showAboutDialog(
                  context: context,
                  applicationName: 'Pocket25',
                  applicationVersion: '1.0.0',
                  applicationLegalese: '© 2025 Pocket25',
                  children: const [
                    SizedBox(height: 16),
                    Text('A P25 Phase 1 Scanner for Android'),
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

