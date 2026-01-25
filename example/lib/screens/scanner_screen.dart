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
              child: _buildCurrentCallPanel(context),
            ),
            // Right side - Recent calls list
            Expanded(
              flex: 1,
              child: _buildRecentCallsPanel(context),
            ),
          ],
        ),
      ),
    );
  }

  // Calculate responsive font size based on screen width
  // Large tablets (>1000px) get larger text, smaller tablets get base/slightly larger
  double _fontSize(BuildContext context, double baseSize) {
    final width = MediaQuery.of(context).size.width;
    if (width > 1000) {
      return baseSize * 1.3; // 30% larger on large tablets
    } else if (width > 700) {
      return baseSize * 1.1; // 10% larger on medium tablets
    } else {
      return baseSize * 0.7; // 30% smaller on phones
    }
  }

  // Calculate responsive icon size
  double _iconSize(BuildContext context, double baseSize) {
    final width = MediaQuery.of(context).size.width;
    if (width > 1000) {
      return baseSize * 1.3;
    } else if (width > 700) {
      return baseSize * 1.1;
    } else {
      return baseSize * 0.75; // 25% smaller on phones
    }
  }

  // Calculate responsive spacing
  double _spacing(BuildContext context, double baseSpacing) {
    final width = MediaQuery.of(context).size.width;
    if (width > 1000) {
      return baseSpacing * 1.3;
    } else if (width > 700) {
      return baseSpacing * 1.1;
    } else {
      return baseSpacing * 0.5; // 50% smaller on phones
    }
  }

  Widget _buildCurrentCallPanel(BuildContext context) {
    final call = currentCall;
    
    return Column(
      children: [
        // Top section: System Information (always visible)
        Container(
          color: Colors.blueGrey[900],
          padding: EdgeInsets.all(_spacing(context, 12)),
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              // Status header with scanner state
              Row(
                children: [
                  Icon(
                    isRunning ? Icons.radio : Icons.radio_button_off,
                    size: _iconSize(context, 24),
                    color: isRunning ? Colors.green : Colors.grey[700],
                  ),
                  SizedBox(width: _spacing(context, 8)),
                  Text(
                    isRunning ? 'SCANNING' : 'IDLE',
                    style: TextStyle(
                      fontSize: _fontSize(context, 16),
                      color: isRunning ? Colors.green : Colors.grey[500],
                      fontWeight: FontWeight.bold,
                      letterSpacing: 1.2,
                    ),
                  ),
                  if (isRunning) ...[
                    SizedBox(width: _spacing(context, 8)),
                    SizedBox(
                      width: _iconSize(context, 16),
                      height: _iconSize(context, 16),
                      child: CircularProgressIndicator(
                        strokeWidth: 2,
                        valueColor: AlwaysStoppedAnimation<Color>(Colors.green),
                      ),
                    ),
                  ],
                  const Spacer(),
                  // Sync status indicator
                  if (isRunning)
                    Row(
                      children: [
                        Icon(
                          scanningService.hasLock ? Icons.lock : Icons.search,
                          size: _iconSize(context, 16),
                          color: scanningService.hasLock ? Colors.green : Colors.orange,
                        ),
                        SizedBox(width: _spacing(context, 4)),
                        Text(
                          scanningService.hasLock ? 'LOCKED' : 'SEARCHING',
                          style: TextStyle(
                            fontSize: _fontSize(context, 12),
                            color: scanningService.hasLock ? Colors.green : Colors.orange,
                            fontWeight: FontWeight.w500,
                          ),
                        ),
                      ],
                    ),
                ],
              ),
              
              if (isRunning) ...[
                SizedBox(height: _spacing(context, 12)),
                Divider(color: Colors.grey[700], height: 1),
                SizedBox(height: _spacing(context, 12)),
                
                // Site information row
                if (scanningService.currentSiteName != null)
                  Padding(
                    padding: EdgeInsets.only(bottom: _spacing(context, 8)),
                    child: Row(
                      children: [
                        Expanded(
                          child: Builder(
                            builder: (context) => GestureDetector(
                              onLongPress: () => _showSiteLockDialog(context),
                              child: _buildCompactInfoRow(
                                scanningService.isCurrentSiteLocked ? Icons.lock : Icons.cell_tower,
                                scanningService.currentSiteName!,
                                scanningService.isCurrentSiteLocked ? Colors.orange : Colors.cyan,
                                context,
                                extraIcon: scanningService.gpsHoppingEnabled ? Icons.my_location : null,
                                extraIconColor: Colors.purple,
                              ),
                            ),
                          ),
                        ),
                        if (scanningService.tsbkCount > 0) ...[
                          SizedBox(width: _spacing(context, 12)),
                          _buildCompactInfoRow(
                            Icons.swap_vert,
                            'TSBK: ${scanningService.tsbkCount}',
                            Colors.teal,
                            context,
                          ),
                        ],
                      ],
                    ),
                  ),
                
                // Control Channel info
                if (scanningService.currentFrequency != null)
                  _buildCompactInfoRow(
                    Icons.settings_input_antenna,
                    '${scanningService.currentFrequency!.toStringAsFixed(4)} MHz'
                        '${scanningService.totalChannels > 1 ? ' (${scanningService.currentChannelIndex + 1}/${scanningService.totalChannels})' : ''}',
                    Colors.blue,
                    context,
                  ),
              ],
            ],
          ),
        ),
        
        // Bottom section: Call Information (always visible)
        Expanded(
          child: call != null ? _buildActiveCallSection(call, context) : _buildIdleCallSection(context),
        ),
      ],
    );
  }
  
  Widget _buildCompactInfoRow(IconData icon, String text, Color color, BuildContext context, {IconData? extraIcon, Color? extraIconColor}) {
    return Row(
      mainAxisSize: MainAxisSize.min,
      children: [
        Icon(icon, size: _iconSize(context, 18), color: color),
        SizedBox(width: _spacing(context, 6)),
        Flexible(
          child: Text(
            text,
            style: TextStyle(
              fontSize: _fontSize(context, 14),
              color: Colors.white,
              fontWeight: FontWeight.w500,
            ),
            overflow: TextOverflow.ellipsis,
          ),
        ),
        if (extraIcon != null) ...[
          SizedBox(width: _spacing(context, 6)),
          Icon(extraIcon, size: _iconSize(context, 16), color: extraIconColor ?? Colors.white),
        ],
      ],
    );
  }
  
  Widget _buildIdleCallSection(BuildContext context) {
    return Container(
      color: Colors.grey[900],
      child: Center(
        child: Column(
          mainAxisAlignment: MainAxisAlignment.center,
          children: [
            Icon(
              Icons.headset_off,
              size: _iconSize(context, 64),
              color: Colors.grey[700],
            ),
            SizedBox(height: _spacing(context, 16)),
            Text(
              isRunning ? 'No Active Call' : 'Scanner Stopped',
              style: TextStyle(
                fontSize: _fontSize(context, 20),
                color: Colors.grey[600],
                fontWeight: FontWeight.w500,
              ),
            ),
          ],
        ),
      ),
    );
  }
  
  Widget _buildActiveCallSection(CallEvent call, BuildContext context) {
    final width = MediaQuery.of(context).size.width;
    final isPhone = width < 900; // Use larger breakpoint for phones
    
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
        child: Column(
          children: [
            // Top bar with timer/badges - only on tablets
            if (!isPhone)
              Container(
                padding: EdgeInsets.symmetric(
                  horizontal: _spacing(context, 16),
                  vertical: _spacing(context, 10),
                ),
                color: Colors.black.withValues(alpha: 0.3),
                child: Row(
                  children: [
                    if (call.isMuted)
                      Padding(
                        padding: EdgeInsets.only(right: _spacing(context, 6)),
                        child: Icon(Icons.volume_off, 
                          size: _iconSize(context, 18), 
                          color: Colors.grey[400]),
                      ),
                    if (call.isEmergency)
                      Padding(
                        padding: EdgeInsets.only(right: _spacing(context, 6)),
                        child: Icon(Icons.warning, 
                          size: _iconSize(context, 18), 
                          color: Colors.red),
                      ),
                    if (call.isEncrypted)
                      Icon(Icons.lock, 
                        size: _iconSize(context, 18), 
                        color: Colors.orange),
                    const Spacer(),
                    Text(
                      call.durationDisplay,
                      style: TextStyle(
                        fontSize: _fontSize(context, 18),
                        color: Colors.white,
                        fontWeight: FontWeight.bold,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ],
                ),
              ),
            
            // Main content
            Expanded(
              child: isPhone 
                ? Stack(
                    children: [
                      // Phone: Centered with overlaid timer
                      Center(
                        child: Padding(
                          padding: EdgeInsets.symmetric(
                            horizontal: _spacing(context, 16),
                            vertical: _spacing(context, 4),
                          ),
                          child: Column(
                            mainAxisAlignment: MainAxisAlignment.center,
                            crossAxisAlignment: CrossAxisAlignment.center,
                            children: [
                              // Alpha tag (talkgroup name)
                              if (call.groupName.isNotEmpty)
                                Text(
                                  call.groupName,
                                  style: TextStyle(
                                    fontSize: _fontSize(context, 28),
                                    fontWeight: FontWeight.bold,
                                    color: Colors.white,
                                    height: 1.2,
                                  ),
                                  textAlign: TextAlign.center,
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                ),
                              
                              // Talkgroup ID
                              if (call.talkgroup > 0) ...[
                                SizedBox(height: _spacing(context, 4)),
                                Text(
                                  'TG ${call.talkgroup}',
                                  style: TextStyle(
                                    fontSize: _fontSize(context, 16),
                                    color: Colors.grey[400],
                                    fontWeight: FontWeight.w500,
                                  ),
                                ),
                              ],
                              
                              // Source ID
                              if (call.sourceId > 0) ...[
                                SizedBox(height: _spacing(context, 14)),
                                Text(
                                  'ID ${call.sourceId}',
                                  style: TextStyle(
                                    fontSize: _fontSize(context, 20),
                                    color: Colors.white70,
                                    fontWeight: FontWeight.w400,
                                  ),
                                  textAlign: TextAlign.center,
                                ),
                              ],
                            ],
                          ),
                        ),
                      ),
                      
                      // Phone: Timer in top-right
                      Positioned(
                        top: _spacing(context, 12),
                        right: _spacing(context, 12),
                        child: Row(
                          mainAxisSize: MainAxisSize.min,
                          children: [
                            if (call.isMuted)
                              Padding(
                                padding: EdgeInsets.only(right: _spacing(context, 6)),
                                child: Icon(Icons.volume_off, 
                                  size: _iconSize(context, 18), 
                                  color: Colors.grey[400]),
                              ),
                            if (call.isEmergency)
                              Padding(
                                padding: EdgeInsets.only(right: _spacing(context, 6)),
                                child: Icon(Icons.warning, 
                                  size: _iconSize(context, 18), 
                                  color: Colors.red),
                              ),
                            if (call.isEncrypted)
                              Padding(
                                padding: EdgeInsets.only(right: _spacing(context, 6)),
                                child: Icon(Icons.lock, 
                                  size: _iconSize(context, 18), 
                                  color: Colors.orange),
                              ),
                            Text(
                              call.durationDisplay,
                              style: TextStyle(
                                fontSize: _fontSize(context, 16),
                                color: Colors.white,
                                fontWeight: FontWeight.bold,
                                fontFamily: 'monospace',
                              ),
                            ),
                          ],
                        ),
                      ),
                    ],
                  )
                : Center(
                    // Tablet: Simple centered layout
                    child: Padding(
                      padding: EdgeInsets.symmetric(
                        horizontal: _spacing(context, 20),
                      ),
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        crossAxisAlignment: CrossAxisAlignment.center,
                        children: [
                          // Alpha tag (talkgroup name)
                          if (call.groupName.isNotEmpty)
                            Text(
                              call.groupName,
                              style: TextStyle(
                                fontSize: _fontSize(context, 28),
                                fontWeight: FontWeight.bold,
                                color: Colors.white,
                                height: 1.2,
                              ),
                              textAlign: TextAlign.center,
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                            ),
                          
                          // Talkgroup ID
                          if (call.talkgroup > 0) ...[
                            SizedBox(height: _spacing(context, 6)),
                            Text(
                              'TG ${call.talkgroup}',
                              style: TextStyle(
                                fontSize: _fontSize(context, 16),
                                color: Colors.grey[400],
                                fontWeight: FontWeight.w500,
                              ),
                            ),
                          ],
                          
                          // Source ID
                          if (call.sourceId > 0) ...[
                            SizedBox(height: _spacing(context, 24)),
                            Text(
                              'ID ${call.sourceId}',
                              style: TextStyle(
                                fontSize: _fontSize(context, 22),
                                color: Colors.white70,
                                fontWeight: FontWeight.w400,
                              ),
                              textAlign: TextAlign.center,
                            ),
                          ],
                        ],
                      ),
                    ),
                  ),
            ),
            
            // Bottom bar with system info
            if (call.systemName.isNotEmpty || call.slot > 0)
              Container(
                padding: EdgeInsets.symmetric(
                  horizontal: _spacing(context, 16),
                  vertical: _spacing(context, 8),
                ),
                color: Colors.black.withValues(alpha: 0.3),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    if (call.slot > 0) ...[
                      Text(
                        'SLOT ${call.slot}',
                        style: TextStyle(
                          fontSize: _fontSize(context, 11),
                          color: Colors.grey[400],
                          fontWeight: FontWeight.w500,
                        ),
                      ),
                      if (call.systemName.isNotEmpty)
                        Padding(
                          padding: EdgeInsets.symmetric(horizontal: _spacing(context, 8)),
                          child: Text('•', style: TextStyle(color: Colors.grey[600])),
                        ),
                    ],
                    if (call.systemName.isNotEmpty)
                      Flexible(
                        child: Text(
                          call.systemName,
                          style: TextStyle(
                            fontSize: _fontSize(context, 11),
                            color: Colors.grey[400],
                            fontWeight: FontWeight.w500,
                          ),
                          overflow: TextOverflow.ellipsis,
                        ),
                      ),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }

  Widget _buildRecentCallsPanel(BuildContext context) {
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
                Icon(Icons.history, size: _iconSize(context, 16), color: Colors.white70),
                SizedBox(width: _spacing(context, 6)),
                Expanded(
                  child: Text(
                    'Recent',
                    style: TextStyle(
                      fontWeight: FontWeight.bold,
                      fontSize: _fontSize(context, 13),
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
                    style: TextStyle(
                      fontSize: _fontSize(context, 9),
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
                      style: TextStyle(color: Colors.grey[600], fontSize: _fontSize(context, 12)),
                    ),
                  )
                : ListView.builder(
                    padding: const EdgeInsets.symmetric(vertical: 4),
                    itemCount: recentCalls.length,
                    itemBuilder: (context, index) {
                      final call = recentCalls[index];
                      return _buildRecentCallTile(call, context);
                    },
                  ),
          ),
        ],
      ),
    );
  }

  Widget _buildRecentCallTile(CallEvent call, BuildContext context) {
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
                  size: _iconSize(context, 12),
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
                      fontSize: _fontSize(context, 12),
                      color: call.isMuted ? Colors.grey[500] : Colors.white,
                    ),
                    overflow: TextOverflow.ellipsis,
                  ),
                  if (call.sourceDisplay.isNotEmpty)
                    Text(
                      call.sourceDisplay,
                      style: TextStyle(
                        fontSize: _fontSize(context, 10),
                        color: call.isMuted ? Colors.grey[600] : Colors.grey[400],
                      ),
                      overflow: TextOverflow.ellipsis,
                    ),
                ],
              ),
            ),
            SizedBox(width: _spacing(context, 4)),
            Text(
              call.timeDisplay,
              style: TextStyle(
                fontSize: _fontSize(context, 9),
                color: Colors.grey[500],
              ),
            ),
          ],
        ),
      ),
    );
  }

  Widget _buildStatusChip(String label, Color color, BuildContext context) {
    return Container(
      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 3),
      decoration: BoxDecoration(
        color: color,
        borderRadius: BorderRadius.circular(4),
      ),
      child: Text(
        label,
        style: TextStyle(
          fontSize: _fontSize(context, 11),
          fontWeight: FontWeight.bold,
          color: Colors.white,
        ),
      ),
    );
  }

  Widget _buildInfoRow(String label, String value, IconData icon, Color color, BuildContext context) {
    return Row(
      children: [
        Container(
          padding: EdgeInsets.all(_spacing(context, 6)),
          decoration: BoxDecoration(
            color: color.withValues(alpha: 0.2),
            borderRadius: BorderRadius.circular(6),
          ),
          child: Icon(
            icon,
            size: _iconSize(context, 20),
            color: color,
          ),
        ),
        SizedBox(width: _spacing(context, 10)),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                label,
                style: TextStyle(
                  fontSize: _fontSize(context, 11),
                  color: Colors.grey[500],
                  letterSpacing: 0.5,
                ),
              ),
              SizedBox(height: _spacing(context, 1)),
              Text(
                value,
                style: TextStyle(
                  fontSize: _fontSize(context, 16),
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

  Widget _buildInfoRowWithExtraIcon(String label, String value, IconData icon, Color color, BuildContext context, {IconData? extraIcon, Color? extraIconColor}) {
    return Row(
      children: [
        Container(
          padding: EdgeInsets.all(_spacing(context, 6)),
          decoration: BoxDecoration(
            color: color.withValues(alpha: 0.2),
            borderRadius: BorderRadius.circular(6),
          ),
          child: Icon(
            icon,
            size: _iconSize(context, 20),
            color: color,
          ),
        ),
        SizedBox(width: _spacing(context, 10)),
        Expanded(
          child: Column(
            crossAxisAlignment: CrossAxisAlignment.start,
            children: [
              Text(
                label,
                style: TextStyle(
                  fontSize: _fontSize(context, 11),
                  color: Colors.grey[500],
                  letterSpacing: 0.5,
                ),
              ),
              SizedBox(height: _spacing(context, 1)),
              Row(
                children: [
                  Flexible(
                    child: Text(
                      value,
                      style: TextStyle(
                        fontSize: _fontSize(context, 16),
                        color: Colors.white,
                        fontWeight: FontWeight.w500,
                        fontFamily: 'monospace',
                      ),
                    ),
                  ),
                  if (extraIcon != null) ...[
                    SizedBox(width: _spacing(context, 6)),
                    Icon(
                      extraIcon,
                      size: _iconSize(context, 16),
                      color: extraIconColor ?? Colors.white,
                    ),
                  ],
                ],
              ),
            ],
          ),
        ),
      ],
    );
  }

  Widget _buildInfoChip(String label, String value, BuildContext context) {
    return Column(
      crossAxisAlignment: CrossAxisAlignment.start,
      mainAxisSize: MainAxisSize.min,
      children: [
        Text(
          label,
          style: TextStyle(
            fontSize: _fontSize(context, 9),
            color: Colors.grey[500],
            letterSpacing: 1,
          ),
        ),
        Text(
          value,
          style: TextStyle(
            fontSize: _fontSize(context, 12),
            color: Colors.white70,
            fontFamily: 'monospace',
          ),
        ),
      ],
    );
  }
  
  void _showSiteLockDialog(BuildContext context) {
    if (scanningService.currentSystemId == null || scanningService.currentSiteId == null) return;
    
    final isLocked = scanningService.isCurrentSiteLocked;
    final gpsHoppingEnabled = scanningService.gpsHoppingEnabled;
    
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        title: Text(isLocked ? 'Unlock Site?' : 'Lock Site?'),
        content: SingleChildScrollView(
          child: Text(
            isLocked
              ? 'GPS site hopping will be able to switch to "${scanningService.currentSiteName}".'
              : gpsHoppingEnabled
                ? 'GPS site hopping will skip "${scanningService.currentSiteName}".\n\nThe scanner will immediately switch to the next nearest unlocked site.'
                : 'GPS site hopping will skip "${scanningService.currentSiteName}".\n\nYou can still manually tune to this site, but GPS won\'t auto-switch to it.',
          ),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          FilledButton(
            onPressed: () {
              scanningService.toggleSiteLock(
                scanningService.currentSystemId!,
                scanningService.currentSiteId!,
              );
              Navigator.pop(context);
            },
            child: Text(isLocked ? 'Unlock' : 'Lock'),
          ),
        ],
      ),
    );
  }
}
