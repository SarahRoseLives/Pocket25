import 'dart:async';
import 'dart:convert';
import 'dart:developer' as developer;
import 'dart:io';
import 'package:shelf/shelf.dart';
import 'package:shelf/shelf_io.dart' as shelf_io;
import 'database_service.dart';

class WebProgrammerService {
  HttpServer? _server;
  bool _isRunning = false;
  static const int _port = 8080;
  final DatabaseService _dbService = DatabaseService();

  bool get isRunning => _isRunning;
  int get port => _port;

  Future<void> startServer() async {
    if (_isRunning) {
      return;
    }

    try {
      final handler = const Pipeline()
          .addMiddleware(logRequests())
          .addHandler(_handleRequest);

      _server = await shelf_io.serve(
        handler,
        InternetAddress.anyIPv4,
        _port,
      );

      _isRunning = true;
      developer.log('Web Programmer server started on port $_port');
    } catch (e) {
      developer.log('Failed to start Web Programmer server: $e');
      rethrow;
    }
  }

  Future<void> stopServer() async {
    if (!_isRunning || _server == null) {
      return;
    }

    await _server?.close(force: true);
    _server = null;
    _isRunning = false;
    developer.log('Web Programmer server stopped');
  }

  Future<Response> _handleRequest(Request request) async {
    final path = request.url.path;
    
    // Serve HTML pages
    if (request.method == 'GET' && path == '') {
      return Response.ok(
        _getIndexPage(),
        headers: {'Content-Type': 'text/html'},
      );
    }

    if (request.method == 'GET' && path == 'manage') {
      return Response.ok(
        _getManagePage(),
        headers: {'Content-Type': 'text/html'},
      );
    }

    if (request.method == 'GET' && path == 'create') {
      return Response.ok(
        _getCreatePage(),
        headers: {'Content-Type': 'text/html'},
      );
    }

    // Systems API
    if (request.method == 'GET' && path == 'api/systems') {
      return await _getSystemsHandler();
    }

    if (request.method == 'GET' && path.startsWith('api/systems/') && 
        !path.contains('/sites') && !path.contains('/talkgroups')) {
      final systemId = int.tryParse(path.split('/').last);
      if (systemId != null) {
        return await _getSystemHandler(systemId);
      }
    }

    if (request.method == 'POST' && path == 'api/systems') {
      return await _addSystemHandler(request);
    }

    if (request.method == 'PUT' && path.startsWith('api/systems/') && 
        !path.contains('/sites') && !path.contains('/talkgroups')) {
      final systemId = int.tryParse(path.split('/').last);
      if (systemId != null) {
        return await _updateSystemHandler(request, systemId);
      }
    }

    if (request.method == 'DELETE' && path.startsWith('api/systems/') && 
        !path.contains('/sites') && !path.contains('/talkgroups')) {
      final systemId = int.tryParse(path.split('/').last);
      if (systemId != null) {
        return await _deleteSystemHandler(systemId);
      }
    }

    // Sites API
    if (request.method == 'GET' && path.startsWith('api/systems/') && 
        path.endsWith('/sites')) {
      final systemId = int.tryParse(path.split('/')[2]);
      if (systemId != null) {
        return await _getSitesHandler(systemId);
      }
    }

    if (request.method == 'POST' && path.startsWith('api/systems/') && 
        path.endsWith('/sites')) {
      final systemId = int.tryParse(path.split('/')[2]);
      if (systemId != null) {
        return await _addSiteHandler(request, systemId);
      }
    }

    if (request.method == 'PUT' && path.contains('/sites/')) {
      final parts = path.split('/');
      final siteId = int.tryParse(parts.last);
      if (siteId != null) {
        return await _updateSiteHandler(request, siteId);
      }
    }

    if (request.method == 'DELETE' && path.contains('/sites/')) {
      final parts = path.split('/');
      final siteId = int.tryParse(parts.last);
      if (siteId != null) {
        return await _deleteSiteHandler(siteId);
      }
    }

    // Talkgroups API
    if (request.method == 'GET' && path.startsWith('api/systems/') && 
        path.endsWith('/talkgroups')) {
      final systemId = int.tryParse(path.split('/')[2]);
      if (systemId != null) {
        return await _getTalkgroupsHandler(systemId);
      }
    }

    if (request.method == 'POST' && path.startsWith('api/systems/') && 
        path.endsWith('/talkgroups')) {
      final systemId = int.tryParse(path.split('/')[2]);
      if (systemId != null) {
        return await _addTalkgroupHandler(request, systemId);
      }
    }

    if (request.method == 'PUT' && path.contains('/talkgroups/')) {
      final parts = path.split('/');
      final systemId = int.tryParse(parts[2]);
      final tgId = int.tryParse(parts.last);
      if (systemId != null && tgId != null) {
        return await _updateTalkgroupHandler(request, systemId, tgId);
      }
    }

    if (request.method == 'DELETE' && path.contains('/talkgroups/')) {
      final parts = path.split('/');
      final tgId = int.tryParse(parts.last);
      if (tgId != null) {
        return await _deleteTalkgroupHandler(tgId);
      }
    }

    return Response.notFound('Not Found');
  }

  Future<Response> _getSystemsHandler() async {
    try {
      final systems = await _dbService.getSystems();
      final systemsWithSites = await Future.wait(
        systems.map((system) async {
          final systemId = system['system_id'] as int;
          final sites = await _dbService.getSitesBySystem(systemId);
          
          final sitesWithChannels = await Future.wait(
            sites.map((site) async {
              final siteId = site['site_id'] as int;
              final channels = await _dbService.getControlChannels(siteId);
              return {
                ...site,
                'control_channels': channels,
              };
            }).toList(),
          );
          
          final talkgroups = await _dbService.getTalkgroups(systemId);
          
          return {
            ...system,
            'sites': sitesWithChannels,
            'talkgroups': talkgroups,
          };
        }).toList(),
      );
      
      return Response.ok(
        jsonEncode(systemsWithSites),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _getSystemHandler(int systemId) async {
    try {
      final systems = await _dbService.getSystems();
      final system = systems.firstWhere(
        (s) => s['system_id'] == systemId,
        orElse: () => <String, dynamic>{},
      );
      
      if (system.isEmpty) {
        return Response.notFound('System not found');
      }
      
      final sites = await _dbService.getSitesBySystem(systemId);
      final sitesWithChannels = await Future.wait(
        sites.map((site) async {
          final siteId = site['site_id'] as int;
          final channels = await _dbService.getControlChannels(siteId);
          return {
            ...site,
            'control_channels': channels,
          };
        }).toList(),
      );
      
      final talkgroups = await _dbService.getTalkgroups(systemId);
      
      final result = {
        ...system,
        'sites': sitesWithChannels,
        'talkgroups': talkgroups,
      };
      
      return Response.ok(
        jsonEncode(result),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _addSystemHandler(Request request) async {
    try {
      final body = await request.readAsString();
      final data = jsonDecode(body) as Map<String, dynamic>;
      
      final systemId = DateTime.now().millisecondsSinceEpoch;
      final systemName = data['system_name'] as String;
      
      await _dbService.insertSystem(systemId, systemName);
      
      // Add sites if provided
      if (data['sites'] != null) {
        final sites = data['sites'] as List;
        for (var i = 0; i < sites.length; i++) {
          final site = sites[i] as Map<String, dynamic>;
          final siteId = systemId + i + 1;
          
          await _dbService.insertSite({
            'site_id': siteId,
            'system_id': systemId,
            'site_number': site['site_number'] ?? (i + 1),
            'site_name': site['site_name'] as String,
            'nac': site['nac'],
            'latitude': site['latitude'],
            'longitude': site['longitude'],
          });
          
          // Add control channels for this site
          if (site['control_channels'] != null) {
            final channels = site['control_channels'] as List;
            for (var j = 0; j < channels.length; j++) {
              final channel = channels[j];
              await _dbService.insertControlChannel(
                siteId,
                (channel['frequency'] as num).toDouble(),
                channel['priority'] ?? j,
              );
            }
          }
        }
      }
      
      // Add talkgroups if provided
      if (data['talkgroups'] != null) {
        final talkgroups = data['talkgroups'] as List;
        for (var tg in talkgroups) {
          await _dbService.insertTalkgroup(
            systemId,
            tg['tg_decimal'] as int,
            tg['tg_name'] as String,
          );
        }
      }
      
      return Response.ok(
        jsonEncode({'success': true, 'system_id': systemId}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _updateSystemHandler(Request request, int systemId) async {
    try {
      final body = await request.readAsString();
      final data = jsonDecode(body) as Map<String, dynamic>;
      
      final systemName = data['system_name'] as String;
      await _dbService.insertSystem(systemId, systemName);
      
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _deleteSystemHandler(int systemId) async {
    try {
      await _dbService.deleteSystem(systemId);
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  // Sites handlers
  Future<Response> _getSitesHandler(int systemId) async {
    try {
      final sites = await _dbService.getSitesBySystem(systemId);
      final sitesWithChannels = await Future.wait(
        sites.map((site) async {
          final siteId = site['site_id'] as int;
          final channels = await _dbService.getControlChannels(siteId);
          return {
            ...site,
            'control_channels': channels,
          };
        }).toList(),
      );
      
      return Response.ok(
        jsonEncode(sitesWithChannels),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _addSiteHandler(Request request, int systemId) async {
    try {
      final body = await request.readAsString();
      final data = jsonDecode(body) as Map<String, dynamic>;
      
      final siteId = DateTime.now().millisecondsSinceEpoch;
      
      await _dbService.insertSite({
        'site_id': siteId,
        'system_id': systemId,
        'site_number': data['site_number'],
        'site_name': data['site_name'] as String,
        'nac': data['nac'],
        'latitude': data['latitude'],
        'longitude': data['longitude'],
      });
      
      // Add control channels
      if (data['control_channels'] != null) {
        final channels = data['control_channels'] as List;
        for (var i = 0; i < channels.length; i++) {
          final channel = channels[i];
          await _dbService.insertControlChannel(
            siteId,
            (channel['frequency'] as num).toDouble(),
            channel['priority'] ?? i,
          );
        }
      }
      
      return Response.ok(
        jsonEncode({'success': true, 'site_id': siteId}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _updateSiteHandler(Request request, int siteId) async {
    try {
      final body = await request.readAsString();
      final data = jsonDecode(body) as Map<String, dynamic>;
      
      final systemId = await _dbService.getSystemIdForSite(siteId);
      if (systemId == null) {
        return Response.notFound('Site not found');
      }
      
      await _dbService.insertSite({
        'site_id': siteId,
        'system_id': systemId,
        'site_number': data['site_number'],
        'site_name': data['site_name'] as String,
        'nac': data['nac'],
        'latitude': data['latitude'],
        'longitude': data['longitude'],
      });
      
      // Update control channels
      if (data['control_channels'] != null) {
        await _dbService.clearControlChannels(siteId);
        final channels = data['control_channels'] as List;
        for (var i = 0; i < channels.length; i++) {
          final channel = channels[i];
          await _dbService.insertControlChannel(
            siteId,
            (channel['frequency'] as num).toDouble(),
            channel['priority'] ?? i,
          );
        }
      }
      
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _deleteSiteHandler(int siteId) async {
    try {
      final db = await _dbService.database;
      await db.delete('sites', where: 'site_id = ?', whereArgs: [siteId]);
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  // Talkgroups handlers
  Future<Response> _getTalkgroupsHandler(int systemId) async {
    try {
      final talkgroups = await _dbService.getTalkgroups(systemId);
      return Response.ok(
        jsonEncode(talkgroups),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _addTalkgroupHandler(Request request, int systemId) async {
    try {
      final body = await request.readAsString();
      final data = jsonDecode(body) as Map<String, dynamic>;
      
      await _dbService.insertTalkgroup(
        systemId,
        data['tg_decimal'] as int,
        data['tg_name'] as String,
      );
      
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _updateTalkgroupHandler(Request request, int systemId, int tgId) async {
    try {
      final body = await request.readAsString();
      final data = jsonDecode(body) as Map<String, dynamic>;
      
      final db = await _dbService.database;
      await db.update(
        'talkgroups',
        {
          'tg_name': data['tg_name'] as String,
        },
        where: 'id = ? AND system_id = ?',
        whereArgs: [tgId, systemId],
      );
      
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  Future<Response> _deleteTalkgroupHandler(int tgId) async {
    try {
      final db = await _dbService.database;
      await db.delete('talkgroups', where: 'id = ?', whereArgs: [tgId]);
      return Response.ok(
        jsonEncode({'success': true}),
        headers: {'Content-Type': 'application/json'},
      );
    } catch (e) {
      return Response.internalServerError(
        body: jsonEncode({'error': e.toString()}),
        headers: {'Content-Type': 'application/json'},
      );
    }
  }

  String _getIndexPage() {
    return r'''
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Pocket25 Web Programmer</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Oxygen, Ubuntu, Cantarell, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #e0e0e0;
            min-height: 100vh;
            padding: 20px;
        }
        
        .container {
            max-width: 1200px;
            margin: 0 auto;
        }
        
        header {
            text-align: center;
            padding: 40px 20px;
            background: rgba(255, 255, 255, 0.05);
            border-radius: 15px;
            margin-bottom: 30px;
            backdrop-filter: blur(10px);
        }
        
        h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            background: linear-gradient(45deg, #667eea 0%, #764ba2 100%);
            -webkit-background-clip: text;
            -webkit-text-fill-color: transparent;
            background-clip: text;
        }
        
        .subtitle {
            color: #a0a0a0;
            font-size: 1.1em;
        }
        
        .card {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 15px;
            padding: 30px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        h2 {
            margin-bottom: 20px;
            color: #667eea;
        }
        
        .form-group {
            margin-bottom: 20px;
        }
        
        label {
            display: block;
            margin-bottom: 8px;
            color: #b0b0b0;
            font-weight: 500;
        }
        
        input, select, textarea {
            width: 100%;
            padding: 12px 15px;
            background: rgba(0, 0, 0, 0.3);
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 8px;
            color: #e0e0e0;
            font-size: 1em;
            transition: all 0.3s ease;
        }
        
        input:focus, select:focus, textarea:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        
        button {
            background: linear-gradient(45deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 12px 30px;
            border: none;
            border-radius: 8px;
            font-size: 1em;
            font-weight: 600;
            cursor: pointer;
            transition: all 0.3s ease;
            margin-right: 10px;
        }
        
        button:hover {
            transform: translateY(-2px);
            box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
        }
        
        button:active {
            transform: translateY(0);
        }
        
        button.secondary {
            background: rgba(255, 255, 255, 0.1);
        }
        
        button.danger {
            background: linear-gradient(45deg, #f44336 0%, #e91e63 100%);
        }
        
        button.small {
            padding: 8px 16px;
            font-size: 0.9em;
        }
        
        .status {
            padding: 15px;
            border-radius: 8px;
            margin-top: 15px;
            display: none;
        }
        
        .status.success {
            background: rgba(76, 175, 80, 0.2);
            border: 1px solid rgba(76, 175, 80, 0.5);
            color: #81c784;
        }
        
        .status.error {
            background: rgba(244, 67, 54, 0.2);
            border: 1px solid rgba(244, 67, 54, 0.5);
            color: #e57373;
        }
        
        .info-box {
            background: rgba(33, 150, 243, 0.2);
            border: 1px solid rgba(33, 150, 243, 0.5);
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 20px;
            color: #64b5f6;
        }
        
        .system-item {
            padding: 20px;
            background: rgba(0, 0, 0, 0.3);
            border-radius: 8px;
            margin-bottom: 15px;
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        .system-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }
        
        .system-name {
            font-size: 1.3em;
            font-weight: bold;
            color: #667eea;
        }
        
        .system-actions {
            display: flex;
            gap: 10px;
        }
        
        .system-details {
            color: #b0b0b0;
            font-size: 0.9em;
            line-height: 1.6;
        }
        
        .site-info {
            margin-top: 10px;
            padding-top: 10px;
            border-top: 1px solid rgba(255, 255, 255, 0.1);
        }
        
        .frequency-tag {
            display: inline-block;
            background: rgba(102, 126, 234, 0.2);
            border: 1px solid rgba(102, 126, 234, 0.5);
            padding: 4px 12px;
            border-radius: 6px;
            font-size: 0.85em;
            margin-right: 8px;
            margin-top: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <header>
            <h1>📡 Pocket25 Web Programmer</h1>
            <p class="subtitle">Manage your radio systems remotely</p>
        </header>
        
        <div class="info-box">
            <strong>ℹ️ Note:</strong> Use this interface to manage complete radio system configurations including multiple sites and talkgroup lists.
        </div>
        
        <div class="card">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 20px;">
                <h2 style="margin: 0;">Configured Systems</h2>
                <button onclick="window.location.href='/create'">+ Create New System</button>
            </div>
            <div id="systemsList">
                <p style="color: #808080; font-style: italic;">Loading systems...</p>
            </div>
        </div>
    </div>
    
    <script>
        async function deleteSystem(systemId, systemName) {
            if (!confirm(`Delete system "${systemName}" and all its sites/talkgroups? This cannot be undone.`)) return;
            
            try {
                const response = await fetch(`/api/systems/${systemId}`, { method: 'DELETE' });
                if (response.ok) {
                    await loadSystems();
                } else {
                    alert('Failed to delete system');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
        
        async function loadSystems() {
            try {
                const response = await fetch('/api/systems');
                const systems = await response.json();
                
                const listDiv = document.getElementById('systemsList');
                if (systems.length === 0) {
                    listDiv.innerHTML = '<p style="color: #808080; font-style: italic;">No systems configured yet.</p>';
                } else {
                    listDiv.innerHTML = systems.map(sys => {
                        const sites = sys.sites || [];
                        const talkgroups = sys.talkgroups || [];
                        
                        return `
                        <div class="system-item">
                            <div class="system-header">
                                <div class="system-name">${sys.system_name}</div>
                                <div class="system-actions">
                                    <button class="small" onclick='window.location.href="/manage?system=${sys.system_id}"'>⚙️ Manage</button>
                                    <button class="small danger" onclick="deleteSystem(${sys.system_id}, '${sys.system_name.replace(/'/g, "\\'")}')">🗑 Delete</button>
                                </div>
                            </div>
                            <div class="system-details">
                                <span style="margin-right: 20px;">📍 <strong>${sites.length}</strong> site${sites.length !== 1 ? 's' : ''}</span>
                                <span>📻 <strong>${talkgroups.length}</strong> talkgroup${talkgroups.length !== 1 ? 's' : ''}</span>
                            </div>
                        </div>
                        `;
                    }).join('');
                }
            } catch (error) {
                console.error('Failed to load systems:', error);
                document.getElementById('systemsList').innerHTML = 
                    '<p style="color: #e57373;">Error loading systems. Please refresh the page.</p>';
            }
        }
        
        // Load systems on page load
        loadSystems();
    </script>
</body>
</html>
''';
  }

  String _getCreatePage() {
    return r'''
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Create System - Pocket25</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #e0e0e0;
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1200px; margin: 0 auto; }
        .nav { margin-bottom: 20px; }
        .nav a {
            color: #667eea;
            text-decoration: none;
            font-size: 1.1em;
        }
        .nav a:hover { text-decoration: underline; }
        .card {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 15px;
            padding: 30px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        h1 { color: #667eea; margin-bottom: 10px; }
        h2 { color: #764ba2; margin-top: 25px; margin-bottom: 15px; }
        .form-group { margin-bottom: 15px; }
        label {
            display: block;
            margin-bottom: 6px;
            color: #b0b0b0;
            font-weight: 500;
        }
        input, textarea {
            width: 100%;
            padding: 10px;
            background: rgba(0, 0, 0, 0.3);
            border: 1px solid rgba(255, 255, 255, 0.2);
            border-radius: 6px;
            color: #e0e0e0;
            font-size: 0.95em;
        }
        input:focus, textarea:focus {
            outline: none;
            border-color: #667eea;
            box-shadow: 0 0 0 3px rgba(102, 126, 234, 0.1);
        }
        button {
            background: linear-gradient(45deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 10px 24px;
            border: none;
            border-radius: 6px;
            font-size: 0.95em;
            font-weight: 600;
            cursor: pointer;
            margin-right: 8px;
            margin-top: 8px;
        }
        button:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4); }
        button.secondary { background: rgba(255, 255, 255, 0.1); }
        button.danger { background: linear-gradient(45deg, #f44336 0%, #e91e63 100%); }
        button.small { padding: 6px 14px; font-size: 0.85em; }
        .item-list {
            background: rgba(0, 0, 0, 0.2);
            border-radius: 8px;
            padding: 15px;
            margin-top: 10px;
        }
        .item {
            background: rgba(255, 255, 255, 0.03);
            padding: 10px;
            border-radius: 6px;
            margin-bottom: 8px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .status {
            padding: 12px;
            border-radius: 6px;
            margin-top: 15px;
            display: none;
        }
        .status.success {
            background: rgba(76, 175, 80, 0.2);
            border: 1px solid rgba(76, 175, 80, 0.5);
            color: #81c784;
        }
        .status.error {
            background: rgba(244, 67, 54, 0.2);
            border: 1px solid rgba(244, 67, 54, 0.5);
            color: #e57373;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="nav">
            <a href="/">← Back to Systems</a>
        </div>
        
        <div class="card">
            <h1>Create New System</h1>
            
            <div class="form-group">
                <label for="systemName">System Name *</label>
                <input type="text" id="systemName" required placeholder="e.g., City Trunked Radio">
            </div>
            
            <h2>Sites</h2>
            <div id="sitesList" class="item-list">
                <p style="color: #888;">No sites added yet</p>
            </div>
            <button onclick="addSite()">+ Add Site</button>
            
            <h2>Talkgroups</h2>
            <div id="talkgroupsList" class="item-list">
                <p style="color: #888;">No talkgroups added yet</p>
            </div>
            <button onclick="addTalkgroup()">+ Add Talkgroup</button>
            
            <div style="margin-top: 30px; padding-top: 20px; border-top: 1px solid rgba(255,255,255,0.1);">
                <button onclick="saveSystem()">💾 Save System</button>
                <button class="secondary" onclick="window.location.href='/'">Cancel</button>
            </div>
            
            <div id="status" class="status"></div>
        </div>
    </div>
    
    <script>
        let sites = [];
        let talkgroups = [];
        
        function addSite() {
            const siteName = prompt('Enter site name:');
            if (!siteName) return;
            
            const frequency = prompt('Enter control channel frequency (MHz):');
            if (!frequency) return;
            
            const nac = prompt('Enter NAC (optional):');
            
            sites.push({
                site_name: siteName,
                nac: nac || null,
                control_channels: [{ frequency: parseFloat(frequency), priority: 0 }]
            });
            
            renderSites();
        }
        
        function removeSite(index) {
            sites.splice(index, 1);
            renderSites();
        }
        
        function renderSites() {
            const list = document.getElementById('sitesList');
            if (sites.length === 0) {
                list.innerHTML = '<p style="color: #888;">No sites added yet</p>';
                return;
            }
            
            list.innerHTML = sites.map((site, i) => `
                <div class="item">
                    <div>
                        <strong>${site.site_name}</strong><br>
                        <small>${site.control_channels[0].frequency} MHz ${site.nac ? '| NAC: ' + site.nac : ''}</small>
                    </div>
                    <button class="small danger" onclick="removeSite(${i})">Remove</button>
                </div>
            `).join('');
        }
        
        function addTalkgroup() {
            const decimal = prompt('Enter talkgroup decimal ID:');
            if (!decimal) return;
            
            const name = prompt('Enter talkgroup name:');
            if (!name) return;
            
            talkgroups.push({
                tg_decimal: parseInt(decimal),
                tg_name: name
            });
            
            renderTalkgroups();
        }
        
        function removeTalkgroup(index) {
            talkgroups.splice(index, 1);
            renderTalkgroups();
        }
        
        function renderTalkgroups() {
            const list = document.getElementById('talkgroupsList');
            if (talkgroups.length === 0) {
                list.innerHTML = '<p style="color: #888;">No talkgroups added yet</p>';
                return;
            }
            
            list.innerHTML = talkgroups.map((tg, i) => `
                <div class="item">
                    <div><strong>${tg.tg_decimal}</strong> - ${tg.tg_name}</div>
                    <button class="small danger" onclick="removeTalkgroup(${i})">Remove</button>
                </div>
            `).join('');
        }
        
        async function saveSystem() {
            const systemName = document.getElementById('systemName').value.trim();
            if (!systemName) {
                alert('Please enter a system name');
                return;
            }
            
            if (sites.length === 0) {
                if (!confirm('No sites added. Continue anyway?')) return;
            }
            
            const statusDiv = document.getElementById('status');
            statusDiv.style.display = 'none';
            
            try {
                const response = await fetch('/api/systems', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        system_name: systemName,
                        sites: sites,
                        talkgroups: talkgroups
                    })
                });
                
                if (response.ok) {
                    statusDiv.className = 'status success';
                    statusDiv.textContent = '✓ System created successfully!';
                    statusDiv.style.display = 'block';
                    
                    setTimeout(() => {
                        window.location.href = '/';
                    }, 1500);
                } else {
                    throw new Error('Failed to create system');
                }
            } catch (error) {
                statusDiv.className = 'status error';
                statusDiv.textContent = '✗ Error: ' + error.message;
                statusDiv.style.display = 'block';
            }
        }
    </script>
</body>
</html>
''';
  }

  String _getManagePage() {
    return r'''
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Manage System - Pocket25</title>
    <style>
        * { margin: 0; padding: 0; box-sizing: border-box; }
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: linear-gradient(135deg, #1a1a2e 0%, #16213e 100%);
            color: #e0e0e0;
            min-height: 100vh;
            padding: 20px;
        }
        .container { max-width: 1400px; margin: 0 auto; }
        .nav { margin-bottom: 20px; }
        .nav a {
            color: #667eea;
            text-decoration: none;
            font-size: 1.1em;
        }
        .nav a:hover { text-decoration: underline; }
        .card {
            background: rgba(255, 255, 255, 0.05);
            border-radius: 15px;
            padding: 30px;
            margin-bottom: 20px;
            backdrop-filter: blur(10px);
            border: 1px solid rgba(255, 255, 255, 0.1);
        }
        h1 { color: #667eea; margin-bottom: 20px; }
        h2 { color: #764ba2; margin-bottom: 15px; }
        button {
            background: linear-gradient(45deg, #667eea 0%, #764ba2 100%);
            color: white;
            padding: 10px 24px;
            border: none;
            border-radius: 6px;
            font-size: 0.95em;
            font-weight: 600;
            cursor: pointer;
            margin-right: 8px;
            margin-top: 8px;
        }
        button:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4); }
        button.secondary { background: rgba(255, 255, 255, 0.1); }
        button.danger { background: linear-gradient(45deg, #f44336 0%, #e91e63 100%); }
        button.small { padding: 6px 14px; font-size: 0.85em; }
        .item {
            background: rgba(0, 0, 0, 0.2);
            padding: 15px;
            border-radius: 8px;
            margin-bottom: 12px;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .frequency-tag {
            display: inline-block;
            background: rgba(102, 126, 234, 0.2);
            border: 1px solid rgba(102, 126, 234, 0.5);
            padding: 3px 10px;
            border-radius: 4px;
            font-size: 0.8em;
            margin-right: 6px;
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="nav">
            <a href="/">← Back to Systems</a>
        </div>
        
        <div class="card">
            <h1 id="systemName">Loading...</h1>
            
            <h2>Sites</h2>
            <div id="sitesList">Loading...</div>
            <button onclick="addSite()">+ Add Site</button>
            
            <h2>Talkgroups</h2>
            <div id="talkgroupsList">Loading...</div>
            <button onclick="addTalkgroup()">+ Add Talkgroup</button>
        </div>
    </div>
    
    <script>
        const urlParams = new URLSearchParams(window.location.search);
        const systemId = urlParams.get('system');
        let currentSystem = null;
        
        if (!systemId) {
            alert('No system specified');
            window.location.href = '/';
        }
        
        async function loadSystem() {
            try {
                const response = await fetch(`/api/systems/${systemId}`);
                currentSystem = await response.json();
                
                document.getElementById('systemName').textContent = currentSystem.system_name;
                renderSites();
                renderTalkgroups();
            } catch (error) {
                alert('Error loading system: ' + error.message);
            }
        }
        
        function renderSites() {
            const sites = currentSystem.sites || [];
            const list = document.getElementById('sitesList');
            
            if (sites.length === 0) {
                list.innerHTML = '<p style="color: #888;">No sites configured</p>';
                return;
            }
            
            list.innerHTML = sites.map(site => {
                const channels = site.control_channels || [];
                return `
                    <div class="item">
                        <div>
                            <strong style="font-size: 1.1em;">${site.site_name}</strong><br>
                            <small style="color: #888;">${site.nac ? 'NAC: ' + site.nac + ' | ' : ''}${channels.length} channel(s)</small><br>
                            ${channels.map(ch => `<span class="frequency-tag">${ch.frequency} MHz</span>`).join('')}
                        </div>
                        <div>
                            <button class="small" onclick="editSite(${site.site_id})">Edit</button>
                            <button class="small danger" onclick="deleteSite(${site.site_id}, '${site.site_name}')">Delete</button>
                        </div>
                    </div>
                `;
            }).join('');
        }
        
        function renderTalkgroups() {
            const talkgroups = currentSystem.talkgroups || [];
            const list = document.getElementById('talkgroupsList');
            
            if (talkgroups.length === 0) {
                list.innerHTML = '<p style="color: #888;">No talkgroups configured</p>';
                return;
            }
            
            list.innerHTML = talkgroups.map(tg => `
                <div class="item">
                    <div><strong>${tg.tg_decimal}</strong> - ${tg.tg_name}</div>
                    <div>
                        <button class="small" onclick="editTalkgroup(${tg.id}, ${tg.tg_decimal}, '${tg.tg_name}')">Edit</button>
                        <button class="small danger" onclick="deleteTalkgroup(${tg.id})">Delete</button>
                    </div>
                </div>
            `).join('');
        }
        
        async function addSite() {
            const siteName = prompt('Enter site name:');
            if (!siteName) return;
            
            const frequency = prompt('Enter control channel frequency (MHz):');
            if (!frequency) return;
            
            const nac = prompt('Enter NAC (optional):');
            
            try {
                const response = await fetch(`/api/systems/${systemId}/sites`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        site_name: siteName,
                        nac: nac || null,
                        control_channels: [{ frequency: parseFloat(frequency), priority: 0 }]
                    })
                });
                
                if (response.ok) {
                    await loadSystem();
                } else {
                    alert('Failed to add site');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
        
        async function editSite(siteId) {
            alert('Site editing form coming soon! Site ID: ' + siteId);
        }
        
        async function deleteSite(siteId, siteName) {
            if (!confirm(`Delete site "${siteName}"?`)) return;
            
            try {
                const response = await fetch(`/api/systems/${systemId}/sites/${siteId}`, { method: 'DELETE' });
                if (response.ok) {
                    await loadSystem();
                } else {
                    alert('Failed to delete site');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
        
        async function addTalkgroup() {
            const decimal = prompt('Enter talkgroup decimal ID:');
            if (!decimal) return;
            
            const name = prompt('Enter talkgroup name:');
            if (!name) return;
            
            try {
                const response = await fetch(`/api/systems/${systemId}/talkgroups`, {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({
                        tg_decimal: parseInt(decimal),
                        tg_name: name
                    })
                });
                
                if (response.ok) {
                    await loadSystem();
                } else {
                    alert('Failed to add talkgroup');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
        
        async function editTalkgroup(tgId, decimal, name) {
            const newName = prompt('Enter new talkgroup name:', name);
            if (!newName || newName === name) return;
            
            try {
                const response = await fetch(`/api/systems/${systemId}/talkgroups/${tgId}`, {
                    method: 'PUT',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify({ tg_name: newName })
                });
                
                if (response.ok) {
                    await loadSystem();
                } else {
                    alert('Failed to update talkgroup');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
        
        async function deleteTalkgroup(tgId) {
            if (!confirm('Delete this talkgroup?')) return;
            
            try {
                const response = await fetch(`/api/systems/${systemId}/talkgroups/${tgId}`, { method: 'DELETE' });
                if (response.ok) {
                    await loadSystem();
                } else {
                    alert('Failed to delete talkgroup');
                }
            } catch (error) {
                alert('Error: ' + error.message);
            }
        }
        
        loadSystem();
    </script>
</body>
</html>
''';
  }
}
