import 'package:flutter/material.dart';
import '../models/scanner_activity.dart';
import '../services/scanning_service.dart';

class ScannerScreen extends StatelessWidget {
  final CallEvent? currentCall;
  final List<CallEvent> recentCalls;
  final bool isRunning;
  final ScanningService scanningService;

  const ScannerScreen({
    super.key,
    this.currentCall,
    required this.recentCalls,
    required this.isRunning,
    required this.scanningService,
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
        child: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            mainAxisSize: MainAxisSize.min,
            children: [
              Icon(
                isRunning ? Icons.hearing : Icons.radio,
                size: 64,
                color: isRunning ? Colors.green[700] : Colors.grey[700],
              ),
              const SizedBox(height: 12),
              Text(
                isRunning ? 'Monitoring...' : 'Scanner Idle',
                style: TextStyle(
                  fontSize: 20,
                  color: Colors.grey[500],
                  fontWeight: FontWeight.w300,
                ),
              ),
              if (isRunning)
                Padding(
                  padding: const EdgeInsets.only(top: 4),
                  child: Text(
                    'Waiting for call activity',
                    style: TextStyle(
                      fontSize: 12,
                      color: Colors.grey[600],
                    ),
                  ),
                ),
            ],
          ),
        ),
      );
    }

    final call = currentCall!;
    
    return Container(
      color: call.isEmergency ? Colors.red[900] : Colors.grey[900],
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
    return Container(
      margin: const EdgeInsets.symmetric(horizontal: 6, vertical: 2),
      padding: const EdgeInsets.all(6),
      decoration: BoxDecoration(
        color: call.isEmergency 
            ? Colors.red[900]!.withValues(alpha: 0.5)
            : Colors.grey[800],
        borderRadius: BorderRadius.circular(4),
        border: call.isEncrypted 
            ? Border.all(color: Colors.orange, width: 1)
            : null,
      ),
      child: Row(
        children: [
          Expanded(
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              mainAxisSize: MainAxisSize.min,
              children: [
                Text(
                  call.talkgroupDisplay,
                  style: const TextStyle(
                    fontWeight: FontWeight.bold,
                    fontSize: 12,
                  ),
                  overflow: TextOverflow.ellipsis,
                ),
                if (call.sourceDisplay.isNotEmpty)
                  Text(
                    call.sourceDisplay,
                    style: TextStyle(
                      fontSize: 10,
                      color: Colors.grey[400],
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
