import 'package:flutter/material.dart';
import 'radio_reference_import_screen.dart';
import 'system_selection_screen.dart';

class ImportSettingsScreen extends StatelessWidget {
  final Function(int siteId, String siteName) onSiteSelected;
  
  const ImportSettingsScreen({
    super.key,
    required this.onSiteSelected,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Import Settings'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: ListView(
          padding: const EdgeInsets.all(12),
          children: [
            _buildMenuTile(
              context,
              title: 'Radio Reference Import',
              subtitle: 'Import systems from Radio Reference',
              icon: Icons.cloud_download,
              iconColor: Colors.purple[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => const RadioReferenceImportScreen(),
                  ),
                );
              },
            ),
            const SizedBox(height: 12),
            _buildMenuTile(
              context,
              title: 'Systems',
              subtitle: 'Select system and site to scan',
              icon: Icons.cell_tower,
              iconColor: Colors.cyan[300]!,
              onTap: () {
                Navigator.push(
                  context,
                  MaterialPageRoute(
                    builder: (context) => SystemSelectionScreen(
                      onSystemSelected: onSiteSelected,
                    ),
                  ),
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
