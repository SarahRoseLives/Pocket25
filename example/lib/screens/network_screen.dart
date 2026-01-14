import 'package:flutter/material.dart';
import '../services/scanning_service.dart';

class NetworkScreen extends StatefulWidget {
  final ScanningService? scanningService;
  
  const NetworkScreen({
    super.key,
    this.scanningService,
  });

  @override
  State<NetworkScreen> createState() => _NetworkScreenState();
}

class _NetworkScreenState extends State<NetworkScreen> with SingleTickerProviderStateMixin {
  late TabController _tabController;

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 4, vsync: this);
  }

  @override
  void dispose() {
    _tabController.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Network'),
        bottom: TabBar(
          controller: _tabController,
          isScrollable: true,
          tabs: const [
            Tab(text: 'Neighbor Sites'),
            Tab(text: 'Patches'),
            Tab(text: 'Group Attachments'),
            Tab(text: 'Affiliations'),
          ],
        ),
      ),
      body: TabBarView(
        controller: _tabController,
        children: [
          _NeighborSitesTab(scanningService: widget.scanningService),
          _PatchesTab(scanningService: widget.scanningService),
          _GroupAttachmentsTab(scanningService: widget.scanningService),
          _AffiliationsTab(scanningService: widget.scanningService),
        ],
      ),
    );
  }
}

// ============================================================================
// Neighbor Sites Tab
// ============================================================================

class _NeighborSitesTab extends StatelessWidget {
  final ScanningService? scanningService;
  
  const _NeighborSitesTab({this.scanningService});

  @override
  Widget build(BuildContext context) {
    if (scanningService == null) {
      return const Center(child: CircularProgressIndicator());
    }
    
    return AnimatedBuilder(
      animation: scanningService!,
      builder: (context, _) {
        final neighborFreqs = scanningService!.neighborFreqs;
        final neighborLastSeen = scanningService!.neighborLastSeen;
        
        if (neighborFreqs.isEmpty) {
          return const Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.cell_tower, size: 64, color: Colors.grey),
                SizedBox(height: 16),
                Text(
                  'No neighbor sites detected',
                  style: TextStyle(color: Colors.grey, fontSize: 16),
                ),
              ],
            ),
          );
        }

        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: neighborFreqs.length,
          itemBuilder: (context, index) {
            final freqHz = neighborFreqs[index];
            final freqMHz = freqHz / 1000000.0;
            final lastSeen = neighborLastSeen.isNotEmpty && index < neighborLastSeen.length
                ? DateTime.fromMillisecondsSinceEpoch(neighborLastSeen[index] * 1000)
                : null;
            
            final now = DateTime.now();
            final ageSeconds = lastSeen != null ? now.difference(lastSeen).inSeconds : null;
            
            return Card(
              color: Colors.grey[850],
              margin: const EdgeInsets.only(bottom: 12),
              child: ListTile(
                leading: CircleAvatar(
                  backgroundColor: Colors.blue[700],
                  child: Text('${index + 1}', style: const TextStyle(color: Colors.white)),
                ),
                title: Text(
                  '${freqMHz.toStringAsFixed(6)} MHz',
                  style: const TextStyle(
                    color: Colors.white,
                    fontWeight: FontWeight.bold,
                    fontSize: 16,
                  ),
                ),
                subtitle: Text(
                  lastSeen != null
                      ? 'Last seen: ${_formatAge(ageSeconds!)} ago'
                      : 'Active',
                  style: TextStyle(color: Colors.grey[400], fontSize: 14),
                ),
                trailing: Icon(
                  Icons.radio_button_checked,
                  color: ageSeconds != null && ageSeconds < 30 ? Colors.green : Colors.orange,
                ),
              ),
            );
          },
        );
      },
    );
  }

  String _formatAge(int seconds) {
    if (seconds < 60) return '${seconds}s';
    if (seconds < 3600) return '${seconds ~/ 60}m';
    return '${seconds ~/ 3600}h';
  }
}

// ============================================================================
// Patches Tab
// ============================================================================

class _PatchesTab extends StatelessWidget {
  final ScanningService? scanningService;
  
  const _PatchesTab({this.scanningService});

  @override
  Widget build(BuildContext context) {
    if (scanningService == null) {
      return const Center(child: CircularProgressIndicator());
    }
    
    return AnimatedBuilder(
      animation: scanningService!,
      builder: (context, _) {
        final patches = scanningService!.patches;
        
        if (patches.isEmpty) {
          return const Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.link, size: 64, color: Colors.grey),
                SizedBox(height: 16),
                Text(
                  'No active patches',
                  style: TextStyle(color: Colors.grey, fontSize: 16),
                ),
              ],
            ),
          );
        }

        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: patches.length,
          itemBuilder: (context, index) {
            final patch = patches[index];
            return _buildPatchCard(patch);
          },
        );
      },
    );
  }

  Widget _buildPatchCard(Map<String, dynamic> patch) {
    final sgid = patch['sgid'] as int;
    final isPatch = patch['isPatch'] as bool;
    final active = patch['active'] as bool;
    final wgidCount = patch['wgidCount'] as int;
    final wgids = (patch['wgids'] as List<dynamic>).map((e) => e as int).toList();
    final wuidCount = patch['wuidCount'] as int;
    final wuids = (patch['wuids'] as List<dynamic>).map((e) => e as int).toList();
    final keyValid = patch['keyValid'] as bool;
    final key = patch['key'] as int;
    final alg = patch['alg'] as int;
    
    return Card(
      color: Colors.grey[850],
      margin: const EdgeInsets.only(bottom: 16),
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(
                  isPatch ? Icons.link : Icons.call_split,
                  color: active ? Colors.green : Colors.grey,
                ),
                const SizedBox(width: 8),
                Text(
                  'Super Group $sgid',
                  style: const TextStyle(
                    color: Colors.white,
                    fontWeight: FontWeight.bold,
                    fontSize: 18,
                  ),
                ),
                const Spacer(),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                  decoration: BoxDecoration(
                    color: active ? Colors.green[700] : Colors.grey[700],
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Text(
                    active ? 'ACTIVE' : 'INACTIVE',
                    style: const TextStyle(color: Colors.white, fontSize: 12),
                  ),
                ),
              ],
            ),
            const SizedBox(height: 12),
            Text(
              'Type: ${isPatch ? "Two-Way Patch" : "Simulselect"}',
              style: TextStyle(color: Colors.grey[300], fontSize: 14),
            ),
            if (wgidCount > 0) ...[
              const SizedBox(height: 8),
              Text(
                'Talkgroups (WGIDs):',
                style: TextStyle(color: Colors.grey[400], fontSize: 13),
              ),
              const SizedBox(height: 4),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: wgids.take(wgidCount).map((wgid) {
                  return Chip(
                    label: Text('$wgid', style: const TextStyle(color: Colors.white)),
                    backgroundColor: Colors.blue[700],
                  );
                }).toList(),
              ),
            ],
            if (wuidCount > 0) ...[
              const SizedBox(height: 8),
              Text(
                'Radio IDs (WUIDs):',
                style: TextStyle(color: Colors.grey[400], fontSize: 13),
              ),
              const SizedBox(height: 4),
              Wrap(
                spacing: 8,
                runSpacing: 8,
                children: wuids.take(wuidCount).map((wuid) {
                  return Chip(
                    label: Text('$wuid', style: const TextStyle(color: Colors.white)),
                    backgroundColor: Colors.purple[700],
                  );
                }).toList(),
              ),
            ],
            if (keyValid) ...[
              const SizedBox(height: 12),
              Row(
                children: [
                  Icon(Icons.lock, size: 16, color: Colors.orange[300]),
                  const SizedBox(width: 4),
                  Text(
                    'Encrypted: Key $key, Alg $alg',
                    style: TextStyle(color: Colors.orange[300], fontSize: 13),
                  ),
                ],
              ),
            ],
          ],
        ),
      ),
    );
  }
}

// ============================================================================
// Group Attachments Tab
// ============================================================================

class _GroupAttachmentsTab extends StatelessWidget {
  final ScanningService? scanningService;
  
  const _GroupAttachmentsTab({this.scanningService});

  @override
  Widget build(BuildContext context) {
    if (scanningService == null) {
      return const Center(child: CircularProgressIndicator());
    }
    
    return AnimatedBuilder(
      animation: scanningService!,
      builder: (context, _) {
        final attachments = scanningService!.groupAttachments;
        
        if (attachments.isEmpty) {
          return const Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.person_pin, size: 64, color: Colors.grey),
                SizedBox(height: 16),
                Text(
                  'No group attachments detected',
                  style: TextStyle(color: Colors.grey, fontSize: 16),
                ),
              ],
            ),
          );
        }

        // Group by talkgroup
        final byTalkgroup = <int, List<Map<String, dynamic>>>{};
        for (var attachment in attachments) {
          final tg = attachment['tg'] as int;
          byTalkgroup.putIfAbsent(tg, () => []);
          byTalkgroup[tg]!.add(attachment);
        }

        final sortedTGs = byTalkgroup.keys.toList()..sort();

        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: sortedTGs.length,
          itemBuilder: (context, index) {
            final tg = sortedTGs[index];
            final tgAttachments = byTalkgroup[tg]!;
            return _buildTalkgroupCard(tg, tgAttachments);
          },
        );
      },
    );
  }

  Widget _buildTalkgroupCard(int tg, List<Map<String, dynamic>> attachments) {
    return Card(
      color: Colors.grey[850],
      margin: const EdgeInsets.only(bottom: 16),
      child: ExpansionTile(
        title: Text(
          'Talkgroup $tg',
          style: const TextStyle(
            color: Colors.white,
            fontWeight: FontWeight.bold,
            fontSize: 16,
          ),
        ),
        subtitle: Text(
          '${attachments.length} radio${attachments.length != 1 ? "s" : ""} attached',
          style: TextStyle(color: Colors.grey[400], fontSize: 14),
        ),
        leading: CircleAvatar(
          backgroundColor: Colors.green[700],
          child: Text(
            '${attachments.length}',
            style: const TextStyle(color: Colors.white, fontWeight: FontWeight.bold),
          ),
        ),
        children: attachments.map((attachment) {
          final rid = attachment['rid'] as int;
          final lastSeenTs = attachment['lastSeen'] as int;
          final lastSeen = DateTime.fromMillisecondsSinceEpoch(lastSeenTs * 1000);
          final ageSeconds = DateTime.now().difference(lastSeen).inSeconds;
          
          return ListTile(
            leading: Icon(Icons.radio, color: Colors.blue[300], size: 20),
            title: Text(
              'RID: $rid',
              style: const TextStyle(color: Colors.white, fontSize: 14),
            ),
            subtitle: Text(
              'Last seen: ${_formatAge(ageSeconds)} ago',
              style: TextStyle(color: Colors.grey[400], fontSize: 12),
            ),
            trailing: Icon(
              Icons.circle,
              color: ageSeconds < 60 ? Colors.green : Colors.orange,
              size: 12,
            ),
          );
        }).toList(),
      ),
    );
  }

  String _formatAge(int seconds) {
    if (seconds < 60) return '${seconds}s';
    if (seconds < 3600) return '${seconds ~/ 60}m';
    return '${seconds ~/ 3600}h';
  }
}

// ============================================================================
// Affiliations Tab
// ============================================================================

class _AffiliationsTab extends StatelessWidget {
  final ScanningService? scanningService;
  
  const _AffiliationsTab({this.scanningService});

  @override
  Widget build(BuildContext context) {
    if (scanningService == null) {
      return const Center(child: CircularProgressIndicator());
    }
    
    return AnimatedBuilder(
      animation: scanningService!,
      builder: (context, _) {
        final affiliations = scanningService!.affiliations;
        
        if (affiliations.isEmpty) {
          return const Center(
            child: Column(
              mainAxisAlignment: MainAxisAlignment.center,
              children: [
                Icon(Icons.people, size: 64, color: Colors.grey),
                SizedBox(height: 16),
                Text(
                  'No affiliated radios',
                  style: TextStyle(color: Colors.grey, fontSize: 16),
                ),
              ],
            ),
          );
        }

        // Sort by last seen (most recent first)
        final sortedAffiliations = List<Map<String, dynamic>>.from(affiliations);
        sortedAffiliations.sort((a, b) {
          final aTime = a['lastSeen'] as int;
          final bTime = b['lastSeen'] as int;
          return bTime.compareTo(aTime);
        });

        return ListView.builder(
          padding: const EdgeInsets.all(16),
          itemCount: sortedAffiliations.length,
          itemBuilder: (context, index) {
            final affiliation = sortedAffiliations[index];
            final rid = affiliation['rid'] as int;
            final lastSeenTs = affiliation['lastSeen'] as int;
            final lastSeen = DateTime.fromMillisecondsSinceEpoch(lastSeenTs * 1000);
            final ageSeconds = DateTime.now().difference(lastSeen).inSeconds;
            
            return Card(
              color: Colors.grey[850],
              margin: const EdgeInsets.only(bottom: 12),
              child: ListTile(
                leading: CircleAvatar(
                  backgroundColor: ageSeconds < 60 ? Colors.green[700] : Colors.orange[700],
                  child: Icon(
                    Icons.person,
                    color: Colors.white,
                    size: 20,
                  ),
                ),
                title: Text(
                  'Radio ID: $rid',
                  style: const TextStyle(
                    color: Colors.white,
                    fontWeight: FontWeight.bold,
                    fontSize: 16,
                  ),
                ),
                subtitle: Text(
                  'Last seen: ${_formatAge(ageSeconds)} ago',
                  style: TextStyle(color: Colors.grey[400], fontSize: 14),
                ),
                trailing: Container(
                  width: 60,
                  padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                  decoration: BoxDecoration(
                    color: ageSeconds < 60 ? Colors.green[700] : Colors.grey[700],
                    borderRadius: BorderRadius.circular(12),
                  ),
                  child: Center(
                    child: Text(
                      ageSeconds < 60 ? 'ACTIVE' : 'IDLE',
                      style: const TextStyle(color: Colors.white, fontSize: 10),
                    ),
                  ),
                ),
              ),
            );
          },
        );
      },
    );
  }

  String _formatAge(int seconds) {
    if (seconds < 60) return '${seconds}s';
    if (seconds < 3600) return '${seconds ~/ 60}m';
    return '${seconds ~/ 3600}h';
  }
}
