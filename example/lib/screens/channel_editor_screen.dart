import 'package:flutter/material.dart';
import '../models/conventional_channel.dart';
import '../models/conventional_bank.dart';
import '../services/database_service.dart';

class ChannelEditorScreen extends StatefulWidget {
  final ConventionalChannel? channel;
  final List<ConventionalBank> availableBanks;

  const ChannelEditorScreen({
    super.key,
    this.channel,
    required this.availableBanks,
  });

  @override
  State<ChannelEditorScreen> createState() => _ChannelEditorScreenState();
}

class _ChannelEditorScreenState extends State<ChannelEditorScreen> {
  final DatabaseService _db = DatabaseService();
  final _formKey = GlobalKey<FormState>();
  
  late TextEditingController _nameController;
  late TextEditingController _frequencyController;
  late TextEditingController _nacController;
  late TextEditingController _colorCodeController;
  late TextEditingController _toneController;
  late TextEditingController _notesController;
  
  String _selectedModulation = 'P25';
  String _selectedBandwidth = '12.5kHz';
  bool _isFavorite = false;
  Set<int> _selectedBankIds = {};
  bool _isSaving = false;

  final List<String> _modulations = [
    'P25',
    'DMR',
    'NXDN',
    'DSTAR',
    'YSF',
  ];

  final List<String> _bandwidths = [
    '6.25kHz',
    '12.5kHz',
    '25kHz',
  ];

  @override
  void initState() {
    super.initState();
    
    final channel = widget.channel;
    _nameController = TextEditingController(text: channel?.channelName ?? '');
    _frequencyController = TextEditingController(text: channel?.frequency.toString() ?? '');
    _nacController = TextEditingController(text: channel?.nac ?? '');
    _colorCodeController = TextEditingController(text: channel?.colorCode?.toString() ?? '');
    _toneController = TextEditingController(text: channel?.toneSquelch ?? '');
    _notesController = TextEditingController(text: channel?.notes ?? '');
    
    _selectedModulation = channel?.modulation ?? 'P25';
    _selectedBandwidth = channel?.bandwidth ?? '12.5kHz';
    _isFavorite = channel?.favorite ?? false;
    _selectedBankIds = Set.from(channel?.bankIds ?? []);
  }

  @override
  void dispose() {
    _nameController.dispose();
    _frequencyController.dispose();
    _nacController.dispose();
    _colorCodeController.dispose();
    _toneController.dispose();
    _notesController.dispose();
    super.dispose();
  }

  Future<void> _saveChannel() async {
    if (!_formKey.currentState!.validate()) return;
    
    setState(() => _isSaving = true);
    
    try {
      final channelData = {
        'channel_name': _nameController.text.trim(),
        'frequency': double.parse(_frequencyController.text),
        'modulation': _selectedModulation,
        'bandwidth': _selectedBandwidth,
        'nac': _nacController.text.trim().isEmpty ? null : _nacController.text.trim(),
        'color_code': _colorCodeController.text.trim().isEmpty 
            ? null 
            : int.tryParse(_colorCodeController.text.trim()),
        'tone_squelch': _toneController.text.trim().isEmpty ? null : _toneController.text.trim(),
        'notes': _notesController.text.trim().isEmpty ? null : _notesController.text.trim(),
        'favorite': _isFavorite ? 1 : 0,
        'sort_order': 0,
      };
      
      int channelId;
      if (widget.channel?.id != null) {
        // Update existing
        await _db.updateConventionalChannel(widget.channel!.id!, channelData);
        channelId = widget.channel!.id!;
      } else {
        // Insert new
        channelData['created_at'] = DateTime.now().millisecondsSinceEpoch;
        channelId = await _db.insertConventionalChannel(channelData);
      }
      
      // Update bank mappings
      await _db.setChannelBanks(channelId, _selectedBankIds.toList());
      
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text(widget.channel == null 
                ? 'Channel created' 
                : 'Channel updated'),
          ),
        );
        Navigator.pop(context, true);
      }
    } catch (e) {
      setState(() => _isSaving = false);
      if (mounted) {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(
            content: Text('Error saving channel: $e'),
            backgroundColor: Colors.red,
          ),
        );
      }
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: Text(widget.channel == null ? 'Add Channel' : 'Edit Channel'),
        actions: [
          if (_isSaving)
            const Padding(
              padding: EdgeInsets.all(16.0),
              child: SizedBox(
                width: 24,
                height: 24,
                child: CircularProgressIndicator(strokeWidth: 2),
              ),
            )
          else
            IconButton(
              icon: const Icon(Icons.save),
              onPressed: _saveChannel,
              tooltip: 'Save',
            ),
        ],
      ),
      body: Form(
        key: _formKey,
        child: ListView(
          padding: const EdgeInsets.all(16),
          children: [
            // Channel Name
            TextFormField(
              controller: _nameController,
              decoration: const InputDecoration(
                labelText: 'Channel Name *',
                hintText: 'Police Dispatch',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.label),
              ),
              validator: (value) {
                if (value == null || value.trim().isEmpty) {
                  return 'Please enter a channel name';
                }
                return null;
              },
            ),
            const SizedBox(height: 16),
            
            // Frequency
            TextFormField(
              controller: _frequencyController,
              decoration: const InputDecoration(
                labelText: 'Frequency (MHz) *',
                hintText: '154.9200',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.radio),
              ),
              keyboardType: const TextInputType.numberWithOptions(decimal: true),
              validator: (value) {
                if (value == null || value.trim().isEmpty) {
                  return 'Please enter a frequency';
                }
                final freq = double.tryParse(value);
                if (freq == null || freq <= 0) {
                  return 'Please enter a valid frequency';
                }
                return null;
              },
            ),
            const SizedBox(height: 16),
            
            // Modulation
            DropdownButtonFormField<String>(
              value: _selectedModulation,
              decoration: const InputDecoration(
                labelText: 'Modulation',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.settings_input_antenna),
              ),
              items: _modulations.map((mod) {
                return DropdownMenuItem(value: mod, child: Text(mod));
              }).toList(),
              onChanged: (value) {
                setState(() => _selectedModulation = value!);
              },
            ),
            const SizedBox(height: 16),
            
            // Bandwidth
            DropdownButtonFormField<String>(
              value: _selectedBandwidth,
              decoration: const InputDecoration(
                labelText: 'Bandwidth',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.tune),
              ),
              items: _bandwidths.map((bw) {
                return DropdownMenuItem(value: bw, child: Text(bw));
              }).toList(),
              onChanged: (value) {
                setState(() => _selectedBandwidth = value!);
              },
            ),
            const SizedBox(height: 24),
            
            // Modulation-specific fields
            if (_selectedModulation == 'P25') ...[
              TextFormField(
                controller: _nacController,
                decoration: const InputDecoration(
                  labelText: 'NAC (optional)',
                  hintText: '293',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.code),
                ),
              ),
              const SizedBox(height: 16),
            ],
            
            if (_selectedModulation == 'DMR') ...[
              TextFormField(
                controller: _colorCodeController,
                decoration: const InputDecoration(
                  labelText: 'Color Code (optional)',
                  hintText: '1',
                  border: OutlineInputBorder(),
                  prefixIcon: Icon(Icons.palette),
                ),
                keyboardType: TextInputType.number,
              ),
              const SizedBox(height: 16),
            ],
            
            // Notes
            TextFormField(
              controller: _notesController,
              decoration: const InputDecoration(
                labelText: 'Notes (optional)',
                hintText: 'Additional information...',
                border: OutlineInputBorder(),
                prefixIcon: Icon(Icons.notes),
              ),
              maxLines: 3,
            ),
            const SizedBox(height: 24),
            
            // Favorite toggle
            SwitchListTile(
              value: _isFavorite,
              onChanged: (value) {
                setState(() => _isFavorite = value);
              },
              title: const Text('Add to Favorites'),
              subtitle: const Text('Quick access from Favorites tab'),
              secondary: const Icon(Icons.star),
            ),
            const SizedBox(height: 16),
            
            // Bank assignment
            if (widget.availableBanks.isNotEmpty) ...[
              const Divider(),
              const ListTile(
                leading: Icon(Icons.folder),
                title: Text('Assign to Banks', style: TextStyle(fontWeight: FontWeight.bold)),
              ),
              ...widget.availableBanks.map((bank) {
                final isSelected = _selectedBankIds.contains(bank.id);
                return CheckboxListTile(
                  value: isSelected,
                  onChanged: (value) {
                    setState(() {
                      if (value == true) {
                        _selectedBankIds.add(bank.id!);
                      } else {
                        _selectedBankIds.remove(bank.id);
                      }
                    });
                  },
                  title: Text(bank.bankName),
                  subtitle: bank.description != null 
                      ? Text(bank.description!) 
                      : null,
                );
              }).toList(),
            ],
            
            const SizedBox(height: 32),
            
            // Save button
            ElevatedButton.icon(
              onPressed: _isSaving ? null : _saveChannel,
              icon: const Icon(Icons.save),
              label: Text(widget.channel == null ? 'Create Channel' : 'Update Channel'),
              style: ElevatedButton.styleFrom(
                padding: const EdgeInsets.all(16),
                backgroundColor: Colors.cyan,
                foregroundColor: Colors.black,
              ),
            ),
          ],
        ),
      ),
    );
  }
}
