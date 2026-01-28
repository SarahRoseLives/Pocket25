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

class _ConventionalChannelsScreenState extends State<ConventionalChannelsScreen> with SingleTickerProviderStateMixin {
  final DatabaseService _db = DatabaseService();
  final TextEditingController _searchController = TextEditingController();
  
  late TabController _tabController;
  List<ConventionalChannel> _allChannels = [];
  List<ConventionalChannel> _filteredChannels = [];
  List<ConventionalBank> _banks = [];
  bool _isLoading = true;
  int _selectedBankId = -1; // -1 = All, 0 = Favorites, >0 = specific bank

  @override
  void initState() {
    super.initState();
    _tabController = TabController(length: 3, vsync: this);
    _tabController.addListener(_onTabChanged);
    _loadData();
  }

  @override
  void dispose() {
    _tabController.dispose();
    _searchController.dispose();
    super.dispose();
  }

  void _onTabChanged() {
    if (_tabController.indexIsChanging) {
      setState(() {
        _searchController.clear();
        switch (_tabController.index) {
          case 0: // All
            _selectedBankId = -1;
            break;
          case 1: // Favorites
            _selectedBankId = 0;
            break;
          case 2: // Banks - keep current selection
            break;
        }
      });
      _filterChannels();
    }
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
    setState(() {
      List<ConventionalChannel> filtered = List.from(_allChannels);
      
      // Filter by tab
      if (_selectedBankId == 0) {
        // Favorites
        filtered = filtered.where((ch) => ch.favorite).toList();
      } else if (_selectedBankId > 0) {
        // Specific bank
        filtered = filtered.where((ch) => ch.bankIds.contains(_selectedBankId)).toList();
      }
      
      // Filter by search
      final query = _searchController.text.toLowerCase();
      if (query.isNotEmpty) {
        filtered = filtered.where((ch) {
          return ch.channelName.toLowerCase().contains(query) ||
                 ch.frequency.toString().contains(query) ||
                 (ch.notes?.toLowerCase().contains(query) ?? false);
        }).toList();
      }
      
      _filteredChannels = filtered;
    });
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
            gain: widget.settings.gain,
            ppm: widget.settings.ppm,
            biasTee: widget.settings.biasTee,
          );
        }
      });
      
      // Clear any current system selection
      widget.scanningService.clearCurrentSystem();

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
      builder: (context) => SafeArea(
        child: Column(
          mainAxisSize: MainAxisSize.min,
          children: [
            ListTile(
              leading: Icon(channel.favorite ? Icons.star : Icons.star_border),
              title: Text(channel.favorite ? 'Remove from Favorites' : 'Add to Favorites'),
              onTap: () async {
                Navigator.pop(context);
                await _db.toggleChannelFavorite(channel.id!);
                _loadData();
              },
            ),
            ListTile(
              leading: const Icon(Icons.edit),
              title: const Text('Edit'),
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
        title: const Text('Delete Channel'),
        content: Text('Delete "${channel.channelName}"?'),
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

  Widget _buildChannelTile(ConventionalChannel channel) {
    final bankNames = _banks
        .where((bank) => channel.bankIds.contains(bank.id))
        .map((bank) => bank.bankName)
        .join(', ');

    return Card(
      margin: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
      child: ListTile(
        leading: channel.favorite
            ? const Icon(Icons.star, color: Colors.amber)
            : const Icon(Icons.radio),
        title: Text(
          channel.channelName,
          style: const TextStyle(fontWeight: FontWeight.bold),
        ),
        subtitle: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Text('${channel.frequencyDisplay} | ${channel.modulationBadge}'),
            if (bankNames.isNotEmpty)
              Text(
                bankNames,
                style: TextStyle(fontSize: 11, color: Colors.grey[400]),
              ),
          ],
        ),
        trailing: Chip(
          label: Text(channel.modulationBadge, style: const TextStyle(fontSize: 11)),
          backgroundColor: _getModulationColor(channel.modulation),
          padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 0),
        ),
        onTap: () => _tuneToChannel(channel),
        onLongPress: () => _showChannelContextMenu(channel),
      ),
    );
  }

  Color _getModulationColor(String modulation) {
    switch (modulation.toUpperCase()) {
      case 'P25':
        return Colors.cyan.withOpacity(0.3);
      case 'DMR':
        return Colors.blue.withOpacity(0.3);
      case 'NXDN':
        return Colors.purple.withOpacity(0.3);
      case 'DSTAR':
        return Colors.orange.withOpacity(0.3);
      case 'YSF':
        return Colors.teal.withOpacity(0.3);
      default:
        return Colors.grey.withOpacity(0.3);
    }
  }

  Widget _buildBankSelector() {
    if (_banks.isEmpty) return const SizedBox.shrink();
    
    return Container(
      height: 50,
      padding: const EdgeInsets.symmetric(horizontal: 8),
      child: ListView.builder(
        scrollDirection: Axis.horizontal,
        itemCount: _banks.length,
        itemBuilder: (context, index) {
          final bank = _banks[index];
          final isSelected = _selectedBankId == bank.id;
          return Padding(
            padding: const EdgeInsets.symmetric(horizontal: 4),
            child: FilterChip(
              label: Text('${bank.bankName} (${bank.channelCount})'),
              selected: isSelected,
              onSelected: (selected) {
                setState(() {
                  _selectedBankId = selected ? bank.id! : -1;
                });
                _filterChannels();
              },
            ),
          );
        },
      ),
    );
  }

  Widget _buildEmptyState() {
    return Center(
      child: Column(
        mainAxisAlignment: MainAxisAlignment.center,
        children: [
          Icon(Icons.radio, size: 64, color: Colors.grey[600]),
          const SizedBox(height: 16),
          Text(
            'No Channels Yet',
            style: TextStyle(fontSize: 18, color: Colors.grey[400]),
          ),
          const SizedBox(height: 8),
          Text(
            'Tap + to add your first channel',
            style: TextStyle(fontSize: 14, color: Colors.grey[500]),
          ),
        ],
      ),
    );
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Conventional Channels'),
        bottom: TabBar(
          controller: _tabController,
          tabs: const [
            Tab(text: 'All'),
            Tab(text: 'Favorites'),
            Tab(text: 'Banks'),
          ],
        ),
        actions: [
          IconButton(
            icon: const Icon(Icons.refresh),
            onPressed: _loadData,
            tooltip: 'Refresh',
          ),
        ],
      ),
      body: Column(
        children: [
          // Search bar
          Padding(
            padding: const EdgeInsets.all(8.0),
            child: TextField(
              controller: _searchController,
              decoration: InputDecoration(
                hintText: 'Search channels...',
                prefixIcon: const Icon(Icons.search),
                suffixIcon: _searchController.text.isNotEmpty
                    ? IconButton(
                        icon: const Icon(Icons.clear),
                        onPressed: () {
                          _searchController.clear();
                          _filterChannels();
                        },
                      )
                    : null,
                border: OutlineInputBorder(
                  borderRadius: BorderRadius.circular(8),
                ),
              ),
              onChanged: (value) => _filterChannels(),
            ),
          ),
          
          // Bank selector (only on Banks tab)
          if (_tabController.index == 2) _buildBankSelector(),
          
          // Channel list
          Expanded(
            child: _isLoading
                ? const Center(child: CircularProgressIndicator())
                : _filteredChannels.isEmpty
                    ? _buildEmptyState()
                    : ListView.builder(
                        itemCount: _filteredChannels.length,
                        itemBuilder: (context, index) {
                          return _buildChannelTile(_filteredChannels[index]);
                        },
                      ),
          ),
        ],
      ),
      floatingActionButton: FloatingActionButton(
        onPressed: () => _editChannel(null),
        child: const Icon(Icons.add),
        tooltip: 'Add Channel',
      ),
    );
  }
}
