import 'package:flutter/material.dart';
import 'package:dsd_flutter/dsd_flutter.dart';
import '../models/conventional_channel.dart';
import '../models/conventional_bank.dart';
import '../services/database_service.dart';
import '../services/scanning_service.dart';
import '../services/settings_service.dart';
import '../services/native_rtlsdr_service.dart';
import 'channel_editor_screen.dart';

class ConventionalChannelsScreen extends StatefulWidget {
  final ScanningService scanningService;
  final SettingsService settings;
  final DsdFlutter dsdPlugin;
  final bool isRunning;
  final VoidCallback onStart;
  final VoidCallback onStop;

  const ConventionalChannelsScreen({
    super.key,
    required this.scanningService,
    required this.settings,
    required this.dsdPlugin,
    required this.isRunning,
    required this.onStart,
    required this.onStop,
  });

  @override
  State<ConventionalChannelsScreen> createState() => _ConventionalChannelsScreenState();
}

class _ConventionalChannelsScreenState extends State<ConventionalChannelsScreen> {
  final DatabaseService _db = DatabaseService();
  
  List<ConventionalChannel> _allChannels = [];
  List<List<ConventionalChannel>> _channelPages = [];
  List<ConventionalBank> _banks = [];
  bool _isLoading = true;
  int _selectedBankId = -1; // -1 = All, 0 = Favorites, >0 = specific bank
  int _currentPage = 0;
  int _itemsPerPage = 9; // Will be calculated dynamically
  late PageController _pageController;

  @override
  void initState() {
    super.initState();
    _pageController = PageController();
    _loadData();
  }

  @override
  void dispose() {
    _pageController.dispose();
    super.dispose();
  }

  Future<void> _loadData() async {
    setState(() => _isLoading = true);
    try {
      final channelMaps = await _db.getAllConventionalChannels();
      final bankMaps = await _db.getAllConventionalBanks();
      
      // Load channels with their bank IDs
      final channels = <ConventionalChannel>[];
      for (final map in channelMaps) {
        final bankIds = await _db.getBankIdsForChannel(map['id'] as int);
        channels.add(ConventionalChannel.fromMap(map, bankIds: bankIds));
      }
      
      // Load banks with channel counts
      final banks = <ConventionalBank>[];
      for (final map in bankMaps) {
        final count = await _db.getChannelCountForBank(map['id'] as int);
        banks.add(ConventionalBank.fromMap(map, channelCount: count));
      }
      
      setState(() {
        _allChannels = channels;
        _banks = banks;
        _isLoading = false;
      });
      _filterChannels();
    } catch (e) {
      setState(() => _isLoading = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Error loading channels: $e')),
        );
      }
    }
  }

  void _filterChannels() {
    List<ConventionalChannel> filtered = List.from(_allChannels);
    
    // Filter by bank selection
    if (_selectedBankId == 0) {
      // Favorites
      filtered = filtered.where((ch) => ch.favorite).toList();
    } else if (_selectedBankId > 0) {
      // Specific bank
      filtered = filtered.where((ch) => ch.bankIds.contains(_selectedBankId)).toList();
    }
    
    // Split into pages based on items per page
    final pages = <List<ConventionalChannel>>[];
    for (int i = 0; i < filtered.length; i += _itemsPerPage) {
      final end = (i + _itemsPerPage < filtered.length) ? i + _itemsPerPage : filtered.length;
      pages.add(filtered.sublist(i, end));
    }

    setState(() {
      _channelPages = pages;
      _currentPage = 0; // Reset to first page when filter changes
    });
  }

  void _recalculatePages() {
    // Recalculate pages based on new items per page
    List<ConventionalChannel> filtered = List.from(_allChannels);
    
    if (_selectedBankId == 0) {
      filtered = filtered.where((ch) => ch.favorite).toList();
    } else if (_selectedBankId > 0) {
      filtered = filtered.where((ch) => ch.bankIds.contains(_selectedBankId)).toList();
    }
    
    final pages = <List<ConventionalChannel>>[];
    for (int i = 0; i < filtered.length; i += _itemsPerPage) {
      final end = (i + _itemsPerPage < filtered.length) ? i + _itemsPerPage : filtered.length;
      pages.add(filtered.sublist(i, end));
    }
    
    // Make sure current page is still valid
    int newCurrentPage = _currentPage;
    if (newCurrentPage >= pages.length && pages.isNotEmpty) {
      newCurrentPage = pages.length - 1;
    }
    
    setState(() {
      _channelPages = pages;
      _currentPage = newCurrentPage;
    });
    
    // Jump to the current page in case page count changed
    if (pages.isNotEmpty && _pageController.hasClients) {
      _pageController.jumpToPage(newCurrentPage);
    }
  }

  void _onBankFilterChanged(int? bankId) {
    setState(() {
      _selectedBankId = bankId ?? -1;
    });
    _filterChannels();
  }

  Future<void> _tuneToChannel(ConventionalChannel channel) async {
    // Show loading indicator
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Row(
            children: [
              const SizedBox(
                width: 20,
                height: 20,
                child: CircularProgressIndicator(strokeWidth: 2, color: Colors.white),
              ),
              const SizedBox(width: 16),
              Text('Tuning to ${channel.channelName}...'),
            ],
          ),
          duration: const Duration(seconds: 10),
        ),
      );
    }

    try {
      // Stop existing scanner if running
      if (widget.isRunning) {
        widget.onStop();
        await Future.delayed(const Duration(milliseconds: 500));
      }
      
      // Update frequency in settings
      widget.settings.updateFrequency(channel.frequency);
      
      // Disable trunk following for conventional mode
      // Not an Advanced Scan: clear stale custom args and use automatic modulation
      await widget.dsdPlugin.setCustomArgs('');
      await widget.dsdPlugin.setModulation(0);
      await widget.dsdPlugin.setTrunkFollowing(false);
      
      // Freeze retunes to prevent buffered P25 data from causing retunes to old frequencies
      await widget.dsdPlugin.setRetuneFrozen(true);
      
      // Reset P25 state to clear old frequency tables
      await widget.dsdPlugin.resetP25State();
      
      // Initialize SDR based on source type
      await Future.microtask(() async {
        if (widget.settings.rtlSource == RtlSource.nativeUsb) {
          // Native USB RTL-SDR
          final devices = await NativeRtlSdrService.listDevices();
          if (devices.isEmpty) {
            throw Exception('No RTL-SDR USB devices found. Please connect a device.');
          }
          
          final result = await NativeRtlSdrService.openDevice(devices.first.deviceName);
          if (result == null) {
            throw Exception('Failed to open RTL-SDR USB device. Please grant USB permission.');
          }
          
          widget.settings.setNativeUsbDevice(
            result['fd'] as int,
            result['devicePath'] as String,
          );
          
          final success = await widget.dsdPlugin.connectNativeUsb(
            fd: result['fd'] as int,
            devicePath: result['devicePath'] as String,
            freqHz: widget.settings.frequencyHz,
            sampleRate: widget.settings.sampleRate,
            gain: widget.settings.gain * 10,
            ppm: widget.settings.ppm,
          );
          
          if (!success) {
            throw Exception('Failed to configure native RTL-SDR');
          }
        } else if (widget.settings.rtlSource == RtlSource.hackrf) {
          // HackRF mode
          final dsdSuccess = await widget.dsdPlugin.startHackRfMode(
            widget.settings.frequencyHz,
            widget.settings.sampleRate,
          ).timeout(
            const Duration(seconds: 10),
            onTimeout: () => throw Exception('HackRF init timeout'),
          );
          
          if (!dsdSuccess) {
            throw Exception('Failed to initialize HackRF');
          }
          
          await widget.dsdPlugin.hackrfSetFrequency(widget.settings.frequencyHz);
          await widget.dsdPlugin.hackrfSetSampleRate(widget.settings.sampleRate);
          await widget.dsdPlugin.hackrfSetLnaGain(widget.settings.hackrfLnaGain);
          await widget.dsdPlugin.hackrfSetVgaGain(widget.settings.hackrfVgaGain);
          
          final rxSuccess = await widget.dsdPlugin.hackrfStartRx();
          if (!rxSuccess) {
            throw Exception('Failed to start HackRF RX');
          }
        } else {
          // RTL-TCP remote mode
          await widget.dsdPlugin.connect(
            widget.settings.remoteHost,
            widget.settings.remotePort,
            widget.settings.frequencyHz,
            gain: widget.settings.gain * 10, // decoder expects tenths of dB (fix from ClayF/Pocket25)
            ppm: widget.settings.ppm,
            biasTee: widget.settings.biasTee,
          );
        }
      });
      
      // Clear any current system selection
      widget.scanningService.clearCurrentSystem();
      
      // Set the channel name and frequency for display
      widget.scanningService.setChannelName(channel.channelName);
      widget.scanningService.setCurrentFrequency(channel.frequency);

      // Start DSD
      widget.onStart();
      
      // Wait for connection to be established
      await Future.delayed(const Duration(milliseconds: 500));
      
      // Explicitly retune to ensure the frequency is set
      await widget.dsdPlugin.retune(widget.settings.frequencyHz);
      
      // Mark that we need to unfreeze retunes once we get lock
      widget.scanningService.setPendingRetuneUnfreeze();
      
      // Update last used timestamp
      await _db.updateChannelLastUsed(channel.id!);
      
      if (mounted) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Now monitoring ${channel.channelName} (${channel.frequencyDisplay})'),
            duration: const Duration(seconds: 2),
          ),
        );
      }
    } catch (e) {
      if (mounted) {
        ScaffoldMessenger.of(context).hideCurrentSnackBar();
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Error tuning: $e'),
            backgroundColor: Colors.red,
            duration: const Duration(seconds: 4),
          ),
        );
      }
    }
  }

  void _showChannelContextMenu(ConventionalChannel channel) {
    showModalBottomSheet(
      context: context,
      backgroundColor: const Color(0xFF2A2A2A),
      builder: (context) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: Icon(channel.favorite ? Icons.star : Icons.star_border, color: Colors.amber),
              title: Text(
                channel.favorite ? 'Remove from Favorites' : 'Add to Favorites',
                style: const TextStyle(color: Colors.white),
              ),
              onTap: () async {
                Navigator.pop(context);
                await _db.toggleChannelFavorite(channel.id!);
                _loadData();
              },
            ),
            ListTile(
              leading: const Icon(Icons.edit, color: Colors.cyan),
              title: const Text('Edit', style: TextStyle(color: Colors.white)),
              onTap: () {
                Navigator.pop(context);
                _editChannel(channel);
              },
            ),
            ListTile(
              leading: const Icon(Icons.delete, color: Colors.red),
              title: const Text('Delete', style: TextStyle(color: Colors.red)),
              onTap: () {
                Navigator.pop(context);
                _confirmDeleteChannel(channel);
              },
            ),
          ],
        ),
      ),
    );
  }

  void _editChannel(ConventionalChannel? channel) async {
    final result = await Navigator.push(
      context,
      MaterialPageRoute(
        builder: (context) => ChannelEditorScreen(
          channel: channel,
          availableBanks: _banks,
        ),
      ),
    );
    if (result == true) {
      _loadData();
    }
  }

  void _confirmDeleteChannel(ConventionalChannel channel) {
    showDialog(
      context: context,
      builder: (context) => AlertDialog(
        backgroundColor: const Color(0xFF2A2A2A),
        title: const Text('Delete Channel', style: TextStyle(color: Colors.white)),
        content: Text('Delete "${channel.channelName}"?', style: const TextStyle(color: Colors.white70)),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('Cancel'),
          ),
          TextButton(
            onPressed: () async {
              Navigator.pop(context);
              await _db.deleteConventionalChannel(channel.id!);
              _loadData();
              if (mounted) {
                ScaffoldMessenger.of(context).showSnackBar(
                  const SnackBar(content: Text('Channel deleted')),
                );
              }
            },
            child: const Text('Delete', style: TextStyle(color: Colors.red)),
          ),
        ],
      ),
    );
  }

  Widget _buildChannelGrid() {
    if (_channelPages.isEmpty) {
      return Center(
        child: Text(
          'No channels found.\nTap + to add a channel.',
          textAlign: TextAlign.center,
          style: TextStyle(
            color: Colors.grey[400],
            fontSize: 16,
          ),
        ),
      );
    }
    
    return LayoutBuilder(
      builder: (context, constraints) {
        // Calculate optimal grid size based on available space
        const double minButtonHeight = 80.0;
        const double minButtonWidth = 100.0;
        const double gridSpacing = 8.0;
        
        // Calculate how many columns fit
        int columns = (constraints.maxWidth / (minButtonWidth + gridSpacing)).floor();
        columns = columns.clamp(2, 4); // Min 2, max 4 columns
        
        // Calculate how many rows fit
        int rows = (constraints.maxHeight / (minButtonHeight + gridSpacing)).floor();
        rows = rows.clamp(2, 5); // Min 2, max 5 rows
        
        final calculatedItemsPerPage = columns * rows;
        
        // If items per page changed, recalculate pages
        if (calculatedItemsPerPage != _itemsPerPage) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            setState(() {
              _itemsPerPage = calculatedItemsPerPage;
            });
            _recalculatePages();
          });
        }
        
        final buttonWidth = (constraints.maxWidth - (gridSpacing * (columns - 1))) / columns;
        final buttonHeight = (constraints.maxHeight - (gridSpacing * (rows - 1))) / rows;

        return PageView.builder(
          controller: _pageController,
          itemCount: _channelPages.length,
          onPageChanged: (page) {
            setState(() {
              _currentPage = page;
            });
          },
          itemBuilder: (context, pageIndex) {
            final channels = _channelPages[pageIndex];
            
            return GridView.builder(
              physics: const NeverScrollableScrollPhysics(),
              itemCount: _itemsPerPage,
              gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: columns,
                crossAxisSpacing: gridSpacing,
                mainAxisSpacing: gridSpacing,
                childAspectRatio: buttonWidth / buttonHeight,
              ),
              itemBuilder: (context, i) {
                // Show empty slot if no channel at this position
                if (i >= channels.length) {
                  return Card(
                    color: const Color(0xFF2A2A2A),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                    elevation: 0,
                    child: const Center(
                      child: Text(
                        '---',
                        style: TextStyle(
                          color: Colors.grey,
                          fontSize: 16,
                        ),
                      ),
                    ),
                  );
                }

                final channel = channels[i];

                return GestureDetector(
                  onTap: () => _tuneToChannel(channel),
                  onLongPress: () => _showChannelContextMenu(channel),
                  child: Card(
                    color: Colors.green[600],
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                    elevation: 3,
                    child: Stack(
                      children: [
                        // Main content
                        Padding(
                          padding: const EdgeInsets.all(8.0),
                          child: Column(
                            mainAxisAlignment: MainAxisAlignment.center,
                            children: [
                              Flexible(
                                child: Text(
                                  channel.channelName,
                                  textAlign: TextAlign.center,
                                  maxLines: 2,
                                  overflow: TextOverflow.ellipsis,
                                  style: const TextStyle(
                                    fontWeight: FontWeight.bold,
                                    fontSize: 14,
                                    color: Colors.white,
                                  ),
                                ),
                              ),
                              const SizedBox(height: 4),
                              Text(
                                channel.frequencyDisplay,
                                style: const TextStyle(
                                  color: Colors.white70,
                                  fontSize: 12,
                                ),
                              ),
                            ],
                          ),
                        ),
                        // Favorite star overlay (top-right corner)
                        if (channel.favorite)
                          const Positioned(
                            top: 4,
                            right: 4,
                            child: Icon(
                              Icons.star,
                              color: Colors.amber,
                              size: 16,
                            ),
                          ),
                      ],
                    ),
                  ),
                );
              },
            );
          },
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    if (_isLoading) {
      return const Scaffold(
        backgroundColor: Color(0xFF232323),
        body: Center(
          child: CircularProgressIndicator(),
        ),
      );
    }

    return Scaffold(
      backgroundColor: const Color(0xFF232323),
      body: SafeArea(
        child: Column(
          children: [
            // Header
            Container(
              padding: const EdgeInsets.all(12),
              color: const Color(0xFF2A2A2A),
              child: Row(
                children: [
                  const Icon(Icons.radio, color: Colors.cyan, size: 20),
                  const SizedBox(width: 8),
                  const Expanded(
                    child: Text(
                      'Conventional Channels',
                      style: TextStyle(
                        color: Colors.white,
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ),
                  Text(
                    '${_allChannels.length} channels',
                    style: TextStyle(
                      color: Colors.grey[400],
                      fontSize: 12,
                    ),
                  ),
                  const SizedBox(width: 12),
                  // Refresh button
                  IconButton(
                    icon: const Icon(Icons.refresh, size: 20),
                    color: Colors.cyan[400],
                    tooltip: 'Refresh',
                    onPressed: _loadData,
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(),
                  ),
                  const SizedBox(width: 8),
                  // Add button
                  IconButton(
                    icon: const Icon(Icons.add, size: 20),
                    color: Colors.green[400],
                    tooltip: 'Add Channel',
                    onPressed: () => _editChannel(null),
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(),
                  ),
                ],
              ),
            ),
            // Bank filter dropdown
            Container(
              color: const Color(0xFF313131),
              padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
              child: Row(
                children: [
                  const Text(
                    'Filter:',
                    style: TextStyle(
                      color: Colors.white70,
                      fontSize: 14,
                    ),
                  ),
                  const SizedBox(width: 12),
                  Expanded(
                    child: DropdownButton<int>(
                      value: _selectedBankId,
                      isExpanded: true,
                      dropdownColor: const Color(0xFF2A2A2A),
                      style: const TextStyle(color: Colors.white, fontSize: 14),
                      underline: Container(
                        height: 1,
                        color: Colors.cyan,
                      ),
                      items: [
                        const DropdownMenuItem<int>(
                          value: -1,
                          child: Text('All Channels'),
                        ),
                        const DropdownMenuItem<int>(
                          value: 0,
                          child: Row(
                            children: [
                              Icon(Icons.star, color: Colors.amber, size: 16),
                              SizedBox(width: 8),
                              Text('Favorites'),
                            ],
                          ),
                        ),
                        ..._banks.map((bank) => DropdownMenuItem<int>(
                          value: bank.id,
                          child: Text('${bank.bankName} (${bank.channelCount})'),
                        )),
                      ],
                      onChanged: _onBankFilterChanged,
                    ),
                  ),
                ],
              ),
            ),
            // Grid (swipeable pages)
            Expanded(
              child: Padding(
                padding: const EdgeInsets.all(8.0),
                child: _buildChannelGrid(),
              ),
            ),
            // Page indicator
            if (_channelPages.length > 1)
              Container(
                padding: const EdgeInsets.symmetric(vertical: 8),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      'Page ${_currentPage + 1} of ${_channelPages.length}',
                      style: TextStyle(
                        color: Colors.grey[400],
                        fontSize: 12,
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
}
