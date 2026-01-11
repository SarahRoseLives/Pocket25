import 'dart:io';
import 'package:flutter/material.dart';
import '../services/database_service.dart';
import '../services/scanning_service.dart';
import 'site_selection_screen.dart';

class SystemSelectionScreen extends StatefulWidget {
  final Function(int siteId, String siteName) onSystemSelected;
  final ScanningService scanningService;
  
  const SystemSelectionScreen({
    super.key,
    required this.onSystemSelected,
    required this.scanningService,
  });

  @override
  State<SystemSelectionScreen> createState() => _SystemSelectionScreenState();
}

class _SystemSelectionScreenState extends State<SystemSelectionScreen> {
  List<SystemInfo>? _systems;
  bool _isLoading = true;
  String? _error;
  final DatabaseService _db = DatabaseService();

  @override
  void initState() {
    super.initState();
    _loadSystems();
  }

  Future<void> _loadSystems() async {
    setState(() {
      _isLoading = true;
      _error = null;
    });

    try {
      final systemsData = await _db.getSystems();
      final systems = <SystemInfo>[];
      
      for (final systemData in systemsData) {
        final systemId = systemData['system_id'] as int;
        final systemName = systemData['system_name'] as String;
        
        final sitesData = await _db.getSitesBySystem(systemId);
        final sites = <SiteInfo>[];
        
        for (final siteData in sitesData) {
          final siteId = siteData['site_id'] as int;
          final controlChannelsData = await _db.getControlChannels(siteId);
          final controlChannels = controlChannelsData
              .map((cc) => cc['frequency'].toString())
              .toList();
          
          sites.add(SiteInfo(
            siteId: siteId.toString(),
            siteName: siteData['site_name'] as String,
            filePath: '', // Not used anymore
            controlChannels: controlChannels,
            latitude: siteData['latitude'] as double?,
            longitude: siteData['longitude'] as double?,
          ));
        }
        
        if (sites.isNotEmpty) {
          systems.add(SystemInfo(
            systemId: systemId.toString(),
            systemName: systemName,
            sites: sites,
          ));
        }
      }

      setState(() {
        _systems = systems;
        _isLoading = false;
      });
    } catch (e) {
      setState(() {
        _error = e.toString();
        _isLoading = false;
      });
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Select System'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: _buildBody(),
      ),
    );
  }

  Widget _buildBody() {
    if (_isLoading) {
      return const Center(
        child: CircularProgressIndicator(),
      );
    }

    if (_error != null) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const Icon(Icons.error_outline, size: 64, color: Colors.red),
              const SizedBox(height: 16),
              Text(
                'Error loading systems',
                style: const TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 8),
              Text(_error!),
              const SizedBox(height: 16),
              ElevatedButton(
                onPressed: _loadSystems,
                child: const Text('Retry'),
              ),
            ],
          ),
        ),
      );
    }

    if (_systems == null || _systems!.isEmpty) {
      return Center(
        child: Padding(
          padding: const EdgeInsets.all(24),
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              const Icon(Icons.inbox, size: 64, color: Colors.grey),
              const SizedBox(height: 16),
              const Text(
                'No Systems Found',
                style: TextStyle(fontSize: 18, fontWeight: FontWeight.bold),
              ),
              const SizedBox(height: 8),
              const Text(
                'Import a system from Radio Reference to get started',
                textAlign: TextAlign.center,
                style: TextStyle(color: Colors.grey),
              ),
            ],
          ),
        ),
      );
    }

    return ListView.builder(
      padding: const EdgeInsets.all(12),
      itemCount: _systems!.length,
      itemBuilder: (context, index) {
        final system = _systems![index];
        return Card(
          margin: const EdgeInsets.only(bottom: 12),
          child: ListTile(
            leading: const Icon(Icons.cell_tower, color: Colors.cyan, size: 32),
            title: Text(
              system.systemName,
              style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
            ),
            subtitle: Text(
              'System ID: ${system.systemId}\n${system.sites.length} site(s)',
              style: const TextStyle(fontSize: 12),
            ),
            trailing: const Icon(Icons.chevron_right),
            onTap: () {
              Navigator.push(
                context,
                MaterialPageRoute(
                  builder: (context) => SiteSelectionScreen(
                    systemId: system.systemId,
                    systemName: system.systemName,
                    sites: system.sites,
                    onSiteSelected: widget.onSystemSelected,
                    scanningService: widget.scanningService,
                  ),
                ),
              );
            },
            contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
          ),
        );
      },
    );
  }
}

class SystemInfo {
  final String systemId;
  final String systemName;
  final List<SiteInfo> sites;

  SystemInfo({
    required this.systemId,
    required this.systemName,
    required this.sites,
  });
}

class SiteInfo {
  final String siteId;
  final String siteName;
  final String filePath;
  final List<String> controlChannels;
  final double? latitude;
  final double? longitude;

  SiteInfo({
    required this.siteId,
    required this.siteName,
    required this.filePath,
    required this.controlChannels,
    this.latitude,
    this.longitude,
  });
}
