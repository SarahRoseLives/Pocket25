import 'package:flutter/material.dart';
import '../services/radio_reference_service.dart';

class RadioReferenceImportScreen extends StatefulWidget {
  const RadioReferenceImportScreen({super.key});

  @override
  State<RadioReferenceImportScreen> createState() => _RadioReferenceImportScreenState();
}

class _RadioReferenceImportScreenState extends State<RadioReferenceImportScreen> {
  final _service = RadioReferenceService();
  final _usernameController = TextEditingController();
  final _passwordController = TextEditingController();
  final _zipcodeController = TextEditingController();
  
  String? _countyId;
  String? _countyName;
  List<Map<String, dynamic>>? _trunkedSystems;
  
  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    _zipcodeController.dispose();
    _service.dispose();
    super.dispose();
  }

  Future<void> _login() async {
    if (_usernameController.text.isEmpty || _passwordController.text.isEmpty) {
      _showError('Please enter username and password');
      return;
    }
    
    final success = await _service.validateCredentials(
      _usernameController.text,
      _passwordController.text,
    );
    
    if (success && mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        const SnackBar(content: Text('Login successful!')),
      );
    } else if (!success && mounted) {
      _showError(_service.errorMessage ?? 'Login failed');
    }
  }

  Future<void> _lookupZipcode() async {
    if (_zipcodeController.text.isEmpty) {
      _showError('Please enter a zipcode');
      return;
    }
    
    final result = await _service.getZipcodeInfo(_zipcodeController.text);
    if (result != null && result['ctid'] != null) {
      setState(() {
        _countyId = result['ctid'].toString();
        _countyName = null; // Will be loaded from county info
      });
      await _loadCountyInfo();
    } else if (mounted) {
      _showError('Could not find zipcode information');
    }
  }

  Future<void> _loadCountyInfo() async {
    if (_countyId == null) return;
    
    final result = await _service.getCountyInfo(_countyId!);
    if (result != null) {
      setState(() {
        _countyName = result['countyName']?.toString() ?? 'Unknown County';
      });
      
      if (result['trsList'] != null) {
        final trsList = result['trsList'];
        List<Map<String, dynamic>> systems;
        
        // Handle nested 'item' structure
        final items = trsList is Map ? trsList['item'] : trsList;
        
        if (items is List) {
          systems = items.map((s) => Map<String, dynamic>.from(s)).toList();
        } else if (items is Map) {
          systems = [Map<String, dynamic>.from(items)];
        } else {
          systems = [];
        }
        
        setState(() {
          _trunkedSystems = systems;
        });
      }
    }
  }

  Future<void> _importSystem(Map<String, dynamic> system) async {
    final systemId = int.parse(system['sid'].toString());
    
    showDialog(
      context: context,
      barrierDismissible: false,
      builder: (context) => const AlertDialog(
        content: Row(
          children: [
            CircularProgressIndicator(),
            SizedBox(width: 16),
            Text('Importing system...'),
          ],
        ),
      ),
    );
    
    await _service.createSystemTsvFiles(systemId);
    
    if (mounted) {
      Navigator.pop(context);
      if (_service.errorMessage != null) {
        _showError(_service.errorMessage!);
      } else {
        ScaffoldMessenger.of(context).showSnackBar(
          SnackBar(content: Text('Imported ${system['sName']}')),
        );
      }
    }
  }

  void _showError(String message) {
    if (mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text(message),
          backgroundColor: Colors.red,
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Radio Reference Import'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      backgroundColor: Colors.grey[900],
      body: SafeArea(
        child: AnimatedBuilder(
          animation: _service,
          builder: (context, _) {
            if (!_service.isLoggedIn) {
              return _buildLoginForm();
            } else {
              return _buildImportForm();
            }
          },
        ),
      ),
    );
  }

  Widget _buildLoginForm() {
    return Center(
      child: SingleChildScrollView(
        padding: const EdgeInsets.all(24),
        child: Card(
          child: Padding(
            padding: const EdgeInsets.all(24),
            child: Column(
              mainAxisSize: MainAxisSize.min,
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const Icon(Icons.cloud_download, size: 64, color: Colors.purple),
                const SizedBox(height: 16),
                const Text(
                  'Radio Reference Login',
                  style: TextStyle(fontSize: 20, fontWeight: FontWeight.bold),
                  textAlign: TextAlign.center,
                ),
                const SizedBox(height: 24),
                TextField(
                  controller: _usernameController,
                  decoration: const InputDecoration(
                    labelText: 'Username',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.person),
                  ),
                ),
                const SizedBox(height: 16),
                TextField(
                  controller: _passwordController,
                  decoration: const InputDecoration(
                    labelText: 'Password',
                    border: OutlineInputBorder(),
                    prefixIcon: Icon(Icons.lock),
                  ),
                  obscureText: true,
                ),
                const SizedBox(height: 24),
                ElevatedButton(
                  onPressed: _service.isLoading ? null : _login,
                  style: ElevatedButton.styleFrom(
                    padding: const EdgeInsets.symmetric(vertical: 16),
                  ),
                  child: _service.isLoading
                      ? const SizedBox(
                          height: 20,
                          width: 20,
                          child: CircularProgressIndicator(strokeWidth: 2),
                        )
                      : const Text('Login'),
                ),
              ],
            ),
          ),
        ),
      ),
    );
  }

  Widget _buildImportForm() {
    return ListView(
      padding: const EdgeInsets.all(12),
      children: [
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                Row(
                  children: [
                    const Icon(Icons.check_circle, color: Colors.green),
                    const SizedBox(width: 8),
                    Text('Logged in as ${_service.username}'),
                    const Spacer(),
                    TextButton(
                      onPressed: () {
                        setState(() {
                          _service.logout();
                          _countyId = null;
                          _countyName = null;
                          _trunkedSystems = null;
                        });
                      },
                      child: const Text('Logout'),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 12),
        Card(
          child: Padding(
            padding: const EdgeInsets.all(16),
            child: Column(
              crossAxisAlignment: CrossAxisAlignment.stretch,
              children: [
                const Text(
                  'Enter Zipcode',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: TextField(
                        controller: _zipcodeController,
                        decoration: const InputDecoration(
                          labelText: 'Zipcode',
                          border: OutlineInputBorder(),
                        ),
                        keyboardType: TextInputType.number,
                      ),
                    ),
                    const SizedBox(width: 12),
                    ElevatedButton(
                      onPressed: _service.isLoading ? null : _lookupZipcode,
                      child: const Text('Lookup'),
                    ),
                  ],
                ),
                if (_countyName != null) ...[
                  const SizedBox(height: 12),
                  Text(
                    'County: $_countyName',
                    style: const TextStyle(color: Colors.green),
                  ),
                ],
              ],
            ),
          ),
        ),
        if (_trunkedSystems != null && _trunkedSystems!.isNotEmpty) ...[
          const SizedBox(height: 12),
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.stretch,
                children: [
                  const Text(
                    'Available Trunked Systems',
                    style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                  ),
                  const SizedBox(height: 12),
                  ..._trunkedSystems!.map((system) => Padding(
                    padding: const EdgeInsets.only(bottom: 8),
                    child: ListTile(
                      tileColor: Colors.grey[800],
                      leading: const Icon(Icons.cell_tower, color: Colors.cyan),
                      title: Text(system['sName'] ?? 'Unknown System'),
                      subtitle: Text('ID: ${system['sid']}'),
                      trailing: ElevatedButton(
                        onPressed: () => _importSystem(system),
                        child: const Text('Import'),
                      ),
                    ),
                  )),
                ],
              ),
            ),
          ),
        ],
      ],
    );
  }
}
