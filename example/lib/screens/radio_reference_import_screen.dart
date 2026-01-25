import 'package:flutter/material.dart';
import 'package:geolocator/geolocator.dart';
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
  final _scrollController = ScrollController();
  final _trunkedSystemsKey = GlobalKey();
  
  String? _countyId;
  String? _countyName;
  List<Map<String, dynamic>>? _trunkedSystems;
  
  // Country/State/County navigation
  List<Map<String, dynamic>>? _countries;
  List<Map<String, dynamic>>? _states;
  List<Map<String, dynamic>>? _counties;
  Map<String, dynamic>? _selectedCountry;
  Map<String, dynamic>? _selectedState;
  String _lookupMethod = 'gps'; // 'gps' or 'browse'
  bool _isLoadingLocation = false;
  
  @override
  void initState() {
    super.initState();
    // Auto-load GPS location when logged in
    WidgetsBinding.instance.addPostFrameCallback((_) {
      if (_service.isLoggedIn) {
        _useGPSLocation();
      }
    });
  }

  @override
  void dispose() {
    _usernameController.dispose();
    _passwordController.dispose();
    _zipcodeController.dispose();
    _scrollController.dispose();
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

  Future<void> _useGPSLocation() async {
    setState(() {
      _isLoadingLocation = true;
      _lookupMethod = 'gps';
    });

    try {
      // Check location permission
      LocationPermission permission = await Geolocator.checkPermission();
      if (permission == LocationPermission.denied) {
        permission = await Geolocator.requestPermission();
        if (permission == LocationPermission.denied) {
          throw Exception('Location permissions are denied');
        }
      }
      
      if (permission == LocationPermission.deniedForever) {
        throw Exception('Location permissions are permanently denied');
      }

      // Get current position
      final position = await Geolocator.getCurrentPosition(
        desiredAccuracy: LocationAccuracy.medium,
        timeLimit: const Duration(seconds: 10),
      );
      
      final result = await _service.getCountyByCoordinates(
        position.latitude, 
        position.longitude,
      );
      
      if (result != null && result['ctid'] != null) {
        setState(() {
          _countyId = result['ctid'].toString();
          _countyName = null;
          _isLoadingLocation = false;
        });
        await _loadCountyInfo();
      } else if (mounted) {
        setState(() => _isLoadingLocation = false);
        _showError('Could not find location information');
      }
    } catch (e) {
      if (mounted) {
        setState(() => _isLoadingLocation = false);
        _showError('GPS location error: $e');
      }
    }
  }
  
  Future<void> _loadAllCountries() async {
    final countries = await _service.getCountryList();
    if (countries != null) {
      setState(() {
        _countries = countries;
        _states = null;
        _counties = null;
        _selectedCountry = null;
        _selectedState = null;
        _lookupMethod = 'browse';
        _countyId = null;
        _countyName = null;
        _trunkedSystems = null;
      });
    } else if (mounted) {
      _showError('Could not load countries');
    }
  }
  
  Future<void> _selectCountry(Map<String, dynamic> country) async {
    final countryId = int.parse(country['coid'].toString());
    final result = await _service.getCountryInfo(countryId);
    
    if (result != null && result['stateList'] != null) {
      var stateList = result['stateList'];
      var items = stateList is Map ? stateList['item'] : stateList;
      
      List<Map<String, dynamic>> states;
      if (items is List) {
        states = items.map((s) => Map<String, dynamic>.from(s)).toList();
      } else if (items is Map) {
        states = [Map<String, dynamic>.from(items)];
      } else {
        states = [];
      }
      
      setState(() {
        _selectedCountry = country;
        _states = states;
        _counties = null;
        _selectedState = null;
        _countyId = null;
        _countyName = null;
        _trunkedSystems = null;
      });
    } else if (mounted) {
      _showError('Could not load states/provinces for this country');
    }
  }
  
  Future<void> _selectState(Map<String, dynamic> state) async {
    final stateId = int.parse(state['stid'].toString());
    final result = await _service.getStateInfo(stateId);
    
    if (result != null && result['countyList'] != null) {
      var countyList = result['countyList'];
      var items = countyList is Map ? countyList['item'] : countyList;
      
      List<Map<String, dynamic>> counties;
      if (items is List) {
        counties = items.map((c) => Map<String, dynamic>.from(c)).toList();
      } else if (items is Map) {
        counties = [Map<String, dynamic>.from(items)];
      } else {
        counties = [];
      }
      
      setState(() {
        _selectedState = state;
        _counties = counties;
        _countyId = null;
        _countyName = null;
        _trunkedSystems = null;
      });
    } else if (mounted) {
      _showError('Could not load counties/regions for this state');
    }
  }
  
  Future<void> _selectCounty(Map<String, dynamic> county) async {
    setState(() {
      _countyId = county['ctid'].toString();
      _countyName = county['countyName']?.toString() ?? 'Unknown';
    });
    await _loadCountyInfo();
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
        
        // Scroll to bottom to show trunked systems
        if (systems.isNotEmpty && mounted) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            if (mounted && _scrollController.hasClients) {
              // Scroll to the maximum scroll extent (bottom)
              _scrollController.animateTo(
                _scrollController.position.maxScrollExtent,
                duration: const Duration(milliseconds: 600),
                curve: Curves.easeInOut,
              );
            }
          });
        }
      }
    }
  }

  Future<void> _importSystem(Map<String, dynamic> system) async {
    final systemId = int.parse(system['sid'].toString());
    final systemName = system['sName']?.toString() ?? 'System $systemId';
    
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
    
    await _service.createSystemTsvFiles(systemId, systemName);
    
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
      controller: _scrollController,
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
                          _countries = null;
                          _states = null;
                          _counties = null;
                          _selectedCountry = null;
                          _selectedState = null;
                          _lookupMethod = 'gps';
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
                  'Lookup Method',
                  style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                ),
                const SizedBox(height: 12),
                Row(
                  children: [
                    Expanded(
                      child: ElevatedButton.icon(
                        onPressed: _isLoadingLocation ? null : _useGPSLocation,
                        icon: _isLoadingLocation 
                          ? const SizedBox(
                              width: 20,
                              height: 20,
                              child: CircularProgressIndicator(strokeWidth: 2),
                            )
                          : const Icon(Icons.my_location),
                        label: const Text('Use GPS Location'),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: _lookupMethod == 'gps' ? Colors.blue : Colors.grey[700],
                          padding: const EdgeInsets.symmetric(vertical: 12),
                        ),
                      ),
                    ),
                    const SizedBox(width: 12),
                    Expanded(
                      child: ElevatedButton.icon(
                        onPressed: _loadAllCountries,
                        icon: const Icon(Icons.public),
                        label: const Text('Browse Location'),
                        style: ElevatedButton.styleFrom(
                          backgroundColor: _lookupMethod == 'browse' ? Colors.blue : Colors.grey[700],
                          padding: const EdgeInsets.symmetric(vertical: 12),
                        ),
                      ),
                    ),
                  ],
                ),
              ],
            ),
          ),
        ),
        const SizedBox(height: 12),
        if (_lookupMethod == 'browse') ...[
          if (_countries != null && _selectedCountry == null)
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    const Text(
                      'Select Country',
                      style: TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                    ),
                    const SizedBox(height: 12),
                    ..._countries!.map((country) => Padding(
                      padding: const EdgeInsets.only(bottom: 8),
                      child: ListTile(
                        tileColor: Colors.grey[800],
                        leading: const Icon(Icons.flag, color: Colors.orange),
                        title: Text(country['countryName'] ?? 'Unknown'),
                        subtitle: Text('Code: ${country['countryCode'] ?? 'N/A'}'),
                        trailing: const Icon(Icons.chevron_right),
                        onTap: () => _selectCountry(country),
                      ),
                    )),
                  ],
                ),
              ),
            ),
          if (_states != null && _selectedState == null)
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        IconButton(
                          icon: const Icon(Icons.arrow_back),
                          onPressed: () {
                            setState(() {
                              _selectedCountry = null;
                              _states = null;
                            });
                          },
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            'Select State/Province in ${_selectedCountry?['countryName']}',
                            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    ..._states!.map((state) => Padding(
                      padding: const EdgeInsets.only(bottom: 8),
                      child: ListTile(
                        tileColor: Colors.grey[800],
                        leading: const Icon(Icons.location_city, color: Colors.green),
                        title: Text(state['stateName'] ?? 'Unknown'),
                        subtitle: Text('Code: ${state['stateCode'] ?? 'N/A'}'),
                        trailing: const Icon(Icons.chevron_right),
                        onTap: () => _selectState(state),
                      ),
                    )),
                  ],
                ),
              ),
            ),
          if (_counties != null)
            Card(
              child: Padding(
                padding: const EdgeInsets.all(16),
                child: Column(
                  crossAxisAlignment: CrossAxisAlignment.stretch,
                  children: [
                    Row(
                      children: [
                        IconButton(
                          icon: const Icon(Icons.arrow_back),
                          onPressed: () {
                            setState(() {
                              _selectedState = null;
                              _counties = null;
                            });
                          },
                        ),
                        const SizedBox(width: 8),
                        Expanded(
                          child: Text(
                            'Select County/Region in ${_selectedState?['stateName']}',
                            style: const TextStyle(fontSize: 16, fontWeight: FontWeight.bold),
                          ),
                        ),
                      ],
                    ),
                    const SizedBox(height: 12),
                    ..._counties!.map((county) => Padding(
                      padding: const EdgeInsets.only(bottom: 8),
                      child: ListTile(
                        tileColor: Colors.grey[800],
                        leading: const Icon(Icons.place, color: Colors.cyan),
                        title: Text(county['countyName'] ?? county['countyHeader'] ?? 'Unknown'),
                        trailing: const Icon(Icons.chevron_right),
                        onTap: () => _selectCounty(county),
                      ),
                    )),
                  ],
                ),
              ),
            ),
        ],
        if (_countyName != null && _lookupMethod == 'browse')
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Row(
                children: [
                  const Icon(Icons.check_circle, color: Colors.green),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      'Selected: $_countyName',
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                  ),
                ],
              ),
            ),
          ),
        if (_countyName != null && _lookupMethod == 'gps')
          Card(
            child: Padding(
              padding: const EdgeInsets.all(16),
              child: Row(
                children: [
                  const Icon(Icons.location_on, color: Colors.green),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      'GPS Location: $_countyName',
                      style: const TextStyle(fontWeight: FontWeight.bold),
                    ),
                  ),
                  IconButton(
                    icon: const Icon(Icons.refresh),
                    onPressed: _useGPSLocation,
                    tooltip: 'Refresh GPS location',
                  ),
                ],
              ),
            ),
          ),
        if (_trunkedSystems != null && _trunkedSystems!.isNotEmpty) ...[
          const SizedBox(height: 12),
          Card(
            key: _trunkedSystemsKey,
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
