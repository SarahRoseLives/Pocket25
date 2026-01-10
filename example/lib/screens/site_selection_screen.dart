import 'package:flutter/material.dart';
import 'package:flutter/foundation.dart';
import 'package:geolocator/geolocator.dart';
import 'system_selection_screen.dart';

class SiteSelectionScreen extends StatefulWidget {
  final String systemId;
  final String systemName;
  final List<SiteInfo> sites;
  final Function(int siteId, String siteName) onSiteSelected;

  const SiteSelectionScreen({
    super.key,
    required this.systemId,
    required this.systemName,
    required this.sites,
    required this.onSiteSelected,
  });

  @override
  State<SiteSelectionScreen> createState() => _SiteSelectionScreenState();
}

class _SiteSelectionScreenState extends State<SiteSelectionScreen> {
  List<SiteInfo> _sortedSites = [];
  Position? _currentPosition;
  bool _isLoadingLocation = false;

  @override
  void initState() {
    super.initState();
    _sortedSites = List.from(widget.sites);
    _getCurrentLocation();
  }

  Future<void> _getCurrentLocation() async {
    setState(() {
      _isLoadingLocation = true;
    });

    try {
      bool serviceEnabled = await Geolocator.isLocationServiceEnabled();
      if (!serviceEnabled) {
        if (kDebugMode) {
          print('Location service not enabled');
        }
        setState(() {
          _isLoadingLocation = false;
        });
        return;
      }

      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          if (kDebugMode) {
            print('Location permission denied');
          }
          setState(() {
            _isLoadingLocation = false;
          });
          return;
        }
      }

      if (permission == LocationPermission.deniedForever) {
        if (kDebugMode) {
          print('Location permission denied forever');
        }
        setState(() {
          _isLoadingLocation = false;
        });
        return;
      }

      final position = await Geolocator.getCurrentPosition();
      if (kDebugMode) {
        print('Got current position: ${position.latitude}, ${position.longitude}');
      }
      setState(() {
        _currentPosition = position;
        _sortSitesByDistance();
        _isLoadingLocation = false;
      });
    } catch (e) {
      if (kDebugMode) {
        print('Error getting location: $e');
      }
      setState(() {
        _isLoadingLocation = false;
      });
    }
  }

  void _sortSitesByDistance() {
    if (_currentPosition == null) return;

    _sortedSites.sort((a, b) {
      if (a.latitude == null || a.longitude == null) return 1;
      if (b.latitude == null || b.longitude == null) return -1;

      final distanceA = Geolocator.distanceBetween(
        _currentPosition!.latitude,
        _currentPosition!.longitude,
        a.latitude!,
        a.longitude!,
      );

      final distanceB = Geolocator.distanceBetween(
        _currentPosition!.latitude,
        _currentPosition!.longitude,
        b.latitude!,
        b.longitude!,
      );

      return distanceA.compareTo(distanceB);
    });
  }

  String _formatDistance(double distanceInMeters) {
    if (distanceInMeters < 1000) {
      return '${distanceInMeters.round()} m';
    } else if (distanceInMeters < 10000) {
      return '${(distanceInMeters / 1000).toStringAsFixed(1)} km';
    } else {
      return '${(distanceInMeters / 1000).round()} km';
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text('Sites - ${widget.systemName}'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: ListView.builder(
          padding: const EdgeInsets.all(12),
          itemCount: _sortedSites.length,
          itemBuilder: (context, index) {
            final site = _sortedSites[index];
            
            String? distanceText;
            if (_currentPosition != null && site.latitude != null && site.longitude != null) {
              final distance = Geolocator.distanceBetween(
                _currentPosition!.latitude,
                _currentPosition!.longitude,
                site.latitude!,
                site.longitude!,
              );
              distanceText = _formatDistance(distance);
            }
            
            return Card(
              margin: const EdgeInsets.only(bottom: 12),
              child: ListTile(
                leading: const Icon(Icons.router, color: Colors.green, size: 32),
                title: Text(
                  site.siteName,
                  style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                ),
                subtitle: Text(
                  'Site ID: ${site.siteId}\n${site.controlChannels.isNotEmpty ? "${site.controlChannels.length} control channel(s)" : "No control channels"}${distanceText != null ? " • $distanceText away" : ""}',
                  style: const TextStyle(fontSize: 12),
                ),
                trailing: const Icon(Icons.chevron_right),
                onTap: () {
                  final siteId = int.parse(site.siteId);
                  widget.onSiteSelected(siteId, site.siteName);
                  Navigator.pop(context);
                  ScaffoldMessenger.of(context).showSnackBar(
                    SnackBar(
                      content: Text('Starting scan: ${site.siteName}'),
                    ),
                  );
                },
                contentPadding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              ),
            );
          },
        ),
      ),
    );
  }
}
