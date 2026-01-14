import 'package:flutter/material.dart';
import '../models/scanner_activity.dart';
import '../services/scanning_service.dart';

class ScannerScreen extends StatelessWidget {
  final CallEvent? currentCall;
  final List<CallEvent> recentCalls;
  final bool isRunning;
  final ScanningService scanningService;
  final Function(int talkgroup)? onToggleMute;

  const ScannerScreen({
    super.key,
    this.currentCall,
    required this.recentCalls,
    required this.isRunning,
    required this.scanningService,
    this.onToggleMute,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      backgroundColor: Colors.black,
      body: SafeArea(
        child: Row(
          children: [
            // Left side - Current call display (main focus)
            Expanded(
              flex: 2,
              child: _buildCurrentCallPanel(),
            ),
            // Right side - Recent calls list
            Expanded(
              flex: 1,
              child: _buildRecentCallsPanel(),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildCurrentCallPanel() {
    if (currentCall == null) {
      return Container(
        color: Colors.grey[900],
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            // Status header
            Row(
              children: [
                Icon(
                  isRunning ? Icons.radio : Icons.radio_button_off,
                  size: 28,
                  color: isRunning ? Colors.green : Colors.grey[700],
                ),
                const SizedBox(width: 10),
                Text(
                  isRunning ? 'SCANNING' : 'SCANNER IDLE',
                  style: TextStyle(
                    fontSize: 22,
                    color: isRunning ? Colors.green : Colors.grey[500],
                    fontWeight: FontWeight.bold,
                    letterSpacing: 1.2,
                  ),
                ),
                if (isRunning) ...[
                  const SizedBox(width: 10),
                  SizedBox(
                    width: 18,
                    height: 18,
                    child: CircularProgressIndicator(
                      strokeWidth: 2,
                      valueColor: AlwaysStoppedAnimation<Color>(Colors.green),
                    ),
                  ),
                ],
              ],
            ),
            const SizedBox(height: 24),
            
            // Site information
            if (isRunning && scanningService.currentSiteName != null) ...[
              _buildInfoRow(
                'Site',
                scanningService.currentSiteName!,
                Icons.cell_tower,
                Colors.cyan,
              ),
              const SizedBox(height: 12),
            ],
            
            // Control Channel
            if (isRunning && scanningService.currentFrequency != null) ...[
              _buildInfoRow(
                'Control Channel',
                '${scanningService.currentFrequency!.toStringAsFixed(4)} MHz',
                Icons.settings_input_antenna,
                Colors.blue,
              ),
              if (scanningService.totalChannels > 1) ...[
                const SizedBox(height: 6),
                Padding(
                  padding: const EdgeInsets.only(left: 36),
                  child: Text(
                    'Channel ${scanningService.currentChannelIndex + 1} of ${scanningService.totalChannels}',
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.grey[600],
                    ),
                  ),
                ),
              ],
              const SizedBox(height: 12),
            ],
            
            // Sync Status
            if (isRunning) ...[
              _buildInfoRow(
                'Sync Status',
                scanningService.hasLock ? 'LOCKED' : 'SEARCHING',
                scanningService.hasLock ? Icons.lock : Icons.search,
                scanningService.hasLock ? Colors.green : Colors.orange,
              ),
              const SizedBox(height: 12),
            ],
            
            // GPS Hopping Status
            if (isRunning && scanningService.gpsHoppingEnabled) ...[
              _buildInfoRow(
                'GPS Site Hopping',
                'ENABLED',
                Icons.my_location,
                Colors.purple,
              ),
            ],
          ],
        ),
      );
    }

    final call = currentCall!;
    
    // Wrap with GestureDetector for long-press to mute/unmute
    return GestureDetector(
      onLongPress: () {
        if (onToggleMute != null) {
          onToggleMute!(call.talkgroup);
        }
      },
      child: Container(
        color: call.isMuted 
            ? Colors.grey[850] 
            : (call.isEmergency ? Colors.red[900] : Colors.grey[900]),
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          // Status bar - fixed at top
          Row(
            children: [
              _buildStatusChip(
                isRunning ? 'ACTIVE' : 'IDLE',
                isRunning ? Colors.green : Colors.grey,
              ),
              const SizedBox(width: 6),
              if (call.isMuted)
                Row(
                  children: [
                    _buildStatusChip('MUTED', Colors.grey[700]!),
                    const SizedBox(width: 6),
                  ],
                ),
              if (scanningService.gpsHoppingEnabled)
                Row(
                  children: [
                    _buildStatusChip('GPS', Colors.blue),
                    const SizedBox(width: 6),
                  ],
                ),
              if (call.isEmergency)
                _buildStatusChip('EMERGENCY', Colors.red),
              if (call.isEncrypted)
                Padding(
                  padding: const EdgeInsets.only(left: 6),
                  child: _buildStatusChip('ENC', Colors.orange),
                ),
              const Spacer(),
              Text(
                call.durationDisplay,
                style: const TextStyle(
                  fontSize: 16,
                  color: Colors.white70,
                  fontFamily: 'monospace',
                ),
              ),
            ],
          ),
          const SizedBox(height: 12),
          // Main content - flexible
          Expanded(
            child: SingleChildScrollView(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  // Call type label
                  Text(
                    call.callType.toUpperCase(),
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.cyan[300],
                      letterSpacing: 2,
                    ),
                  ),
                  const SizedBox(height: 4),
                  // Main talkgroup display
                  FittedBox(
                    fit: BoxFit.scaleDown,
                    alignment: Alignment.centerLeft,
                    child: Text(
                      call.talkgroupDisplay,
                      style: const TextStyle(
                        fontSize: 48,
                        fontWeight: FontWeight.bold,
                        color: Colors.white,
                      ),
                    ),
                  ),
                  if (call.groupName.isEmpty && call.talkgroup > 0)
                    Text(
                      'TG ${call.talkgroup}',
                      style: TextStyle(
                        fontSize: 14,
                        color: Colors.grey[500],
                      ),
                    ),
                  const SizedBox(height: 16),
                  // Source info
                  if (call.sourceDisplay.isNotEmpty) ...[
                    Text(
                      'SOURCE',
                      style: TextStyle(
                        fontSize: 10,
                        color: Colors.grey[500],
                        letterSpacing: 2,
                      ),
                    ),
                    const SizedBox(height: 2),
                    FittedBox(
                      fit: BoxFit.scaleDown,
                      alignment: Alignment.centerLeft,
                      child: Text(
                        call.sourceDisplay,
                        style: const TextStyle(
                          fontSize: 24,
                          color: Colors.white,
                        ),
                      ),
                    ),
                    if (call.sourceName.isEmpty && call.sourceId > 0)
                      Text(
                        'ID ${call.sourceId}',
                        style: TextStyle(
                          fontSize: 12,
                          color: Colors.grey[500],
                        ),
                      ),
                  ],
                ],
              ),
            ),
          ),
          // System info row - fixed at bottom
          const SizedBox(height: 8),
          Wrap(
            spacing: 12,
            runSpacing: 4,
            children: [
              if (call.nacDisplay.isNotEmpty)
                _buildInfoChip('NAC', call.nacDisplay),
              if (call.systemName.isNotEmpty)
                _buildInfoChip('SYS', call.systemName),
              if (call.slot > 0)
                _buildInfoChip('SLOT', call.slot.toString()),
            ],
          ),
        ],
      ),
      ),
    );
  }

  Widget _buildRecentCallsPanel() {
    return Container(
      color: Colors.grey[850],
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Container(
            padding: const EdgeInsets.symmetric(horizontal: 10, vertical: 8),
            color: Colors.blueGrey[900],
            child: Row(
              children: [
                const Icon(Icons.history, size: 16, color: Colors.white70),
                const SizedBox(width: 6),
                const Expanded(
                  child: Text(
                    'Recent',
                    style: TextStyle(
                      fontWeight: FontWeight.bold,
                      fontSize: 13,
                      color: Colors.white70,
                    ),
                    overflow: TextOverflow.ellipsis,
                  ),
                ),
                Container(
                  padding: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
                  decoration: BoxDecoration(
                    color: isRunning ? Colors.green : Colors.grey,
                    borderRadius: BorderRadius.circular(8),
                  ),
                  child: Text(
                    isRunning ? 'ON' : 'OFF',
                    style: const TextStyle(
                      fontSize: 9,
                      fontWeight: FontWeight.bold,
                    ),
                  ),
                ),
              ],
            ),
          ),
          Expanded(
            child: recentCalls.isEmpty
                ? Center(
                    child: Text(
                      'No recent calls',
                      style: TextStyle(color: Colors.grey[600], fontSize: 12),
                    ),
                  )
                : ListView.builder(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    itemCount: recentCalls.length,
                    itemBuilder: (context, index) {
                      final call = recentCalls[index];
                      return _buildRecentCallTile(call);
                    },
                  ),
          ),
        ],
      ),
    );
  }

  Widget _buildRecentCallTile(CallEvent call) {
    return GestureDetector(
      onLongPress: () {
        if (onToggleMute != null) {
          onToggleMute!(call.talkgroup);
        }
      },
      child: Container(
        margin: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
        padding: const EdgeInsets.all(6),
        decoration: BoxDecoration(
          color: call.isMuted
              ? Colors.grey[900]
              : (call.isEmergency 
                  ? Colors.red[900]!.withValues(alpha: 0.5)
                  : Colors.grey[800]),
          borderRadius: BorderRadius.circular(4),
          border: call.isMuted
              ? Border.all(color: Colors.grey[700]!, width: 1)
              : (call.isEncrypted 
                  ? Border.all(color: Colors.orange, width: 1)
                  : null),
        ),
        child: Row(
          children: [
            // Muted indicator icon
            if (call.isMuted)
              Padding(
                padding: const EdgeInsets.only(right: 4),
                child: Icon(
                  Icons.volume_off,
                  size: 12,
                  color: Colors.grey[500],
                ),
              ),
            Expanded(
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                mainAxisSize: MainAxisSize.min,
                children: [
                  Text(
                    call.talkgroupDisplay,
                    style: TextStyle(
                      fontWeight: FontWeight.bold,
                      fontSize: 12,
                      color: call.isMuted ? Colors.grey[500] : Colors.white,
                    ),
                    overflow: TextOverflow.ellipsis,
                  ),
                  if (call.sourceDisplay.isNotEmpty)
                    Text(
                      call.sourceDisplay,
                      style: TextStyle(
                        fontSize: 10,
                        color: call.isMuted ? Colors.grey[600] : Colors.grey[400],
                      ),
                      overflow: TextOverflow.ellipsis,
                    ),
                ],
              ),
            ),
            const SizedBox(width: 4),
            Text(
              call.timeDisplay,
              style: TextStyle(
                fontSize: 9,
                color: Colors.grey[500],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusChip(String label, Color color) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(
        label,
        style: const TextStyle(
          fontSize: 11,
          fontWeight: FontWeight.bold,
          color: Colors.white,
        ),
      ),
    );
  }

  Widget _buildInfoRow(String label, String value, IconData icon, Color color) {
    return Row(
      children: [
        Container(
          padding: const EdgeInsets.all(6),
          decoration: BoxDecoration(
            color: color.withOpacity(0.2),
            borderRadius: BorderRadius.circular(6),
          ),
          child: Icon(
            icon,
            size: 20,
            color: color,
          ),
        ),
        const SizedBox(width: 10),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                label,
                style: TextStyle(
                  fontSize: 11,
                  color: Colors.grey[500],
                  letterSpacing: 0.5,
                ),
              ),
              const SizedBox(height: 1),
              Text(
                value,
                style: TextStyle(
                  fontSize: 16,
                  color: Colors.white,
                  fontWeight: FontWeight.w500,
                  fontFamily: 'monospace',
                ),
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildInfoChip(String label, String value) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          label,
          style: TextStyle(
            fontSize: 9,
            color: Colors.grey[500],
            letterSpacing: 1,
          ),
        ),
        Text(
          value,
          style: const TextStyle(
            fontSize: 12,
            color: Colors.white70,
            fontFamily: 'monospace',
          ),
        ),
      ],
    );
  }
}
