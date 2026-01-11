import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:shared_preferences/shared_preferences.dart';
import 'package:xml/xml.dart';
import 'database_service.dart';

class RadioReferenceService extends ChangeNotifier {
  String? username;
  String? password;
  bool isLoggedIn = false;
  bool isLoading = false;
  String? errorMessage;
  final DatabaseService _db = DatabaseService();

  RadioReferenceService({this.username, this.password}) {
    _loadCredentials();
  }
  
  Future<void> _loadCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    final savedUsername = prefs.getString('rr_username');
    final savedPassword = prefs.getString('rr_password');
    
    if (savedUsername != null && savedPassword != null) {
      username = savedUsername;
      password = savedPassword;
      isLoggedIn = true;
      notifyListeners();
      
      if (kDebugMode) {
        print('Loaded saved Radio Reference credentials');
      }
    }
  }
  
  Future<void> _saveCredentials() async {
    if (username != null && password != null) {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString('rr_username', username!);
      await prefs.setString('rr_password', password!);
      
      if (kDebugMode) {
        print('Saved Radio Reference credentials');
      }
    }
  }
  
  Future<void> _clearCredentials() async {
    final prefs = await SharedPreferences.getInstance();
    await prefs.remove('rr_username');
    await prefs.remove('rr_password');
    
    if (kDebugMode) {
      print('Cleared Radio Reference credentials');
    }
  }

  Future<bool> validateCredentials(String user, String pass) async {
    isLoading = true;
    errorMessage = null;
    notifyListeners();
    try {
      final authInfo = _buildAuthInfo(user, pass);
      final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
      final result = await _soapRequest(
        wsdlUrl,
        'getZipcodeInfo',
        {'zipcode': 90210, 'authInfo': authInfo}
      );
      
      if (result == null) {
        errorMessage = "No response from API";
        isLoggedIn = false;
        isLoading = false;
        notifyListeners();
        return false;
      }
      
      if (result.containsKey('fault')) {
        errorMessage = result['fault'].toString();
        isLoggedIn = false;
        isLoading = false;
        notifyListeners();
        return false;
      }
      
      if (result.containsKey('ctid')) {
        username = user;
        password = pass;
        isLoggedIn = true;
        isLoading = false;
        errorMessage = null;
        await _saveCredentials();
        notifyListeners();
        return true;
      } else {
        errorMessage = "API login failed: ${result.toString()}";
        isLoggedIn = false;
        isLoading = false;
        notifyListeners();
        return false;
      }
    } catch (e) {
      errorMessage = "Login error: $e";
      isLoggedIn = false;
      isLoading = false;
      notifyListeners();
      return false;
    }
  }

  void logout() {
    username = null;
    password = null;
    isLoggedIn = false;
    errorMessage = null;
    _clearCredentials();
    notifyListeners();
  }

  Map<String, dynamic> _buildAuthInfo(String user, String pass) {
    return {
      "appKey": utf8.decode(base64Decode('Mjg4MDExNjM=')),
      "username": user,
      "password": pass,
      "version": "latest",
      "style": "rpc"
    };
  }

  String _buildSoapEnvelope(String method, Map<String, dynamic> params) {
    final authInfo = params['authInfo'];
    final otherParams = params..remove('authInfo');
    final paramXml = otherParams.entries.map((e) => '<${e.key}>${e.value}</${e.key}>').join('');
    final authXml = '''<authInfo>
<appKey>${authInfo['appKey']}</appKey>
<username>${authInfo['username']}</username>
<password>${authInfo['password']}</password>
<version>${authInfo['version']}</version>
<style>${authInfo['style']}</style>
</authInfo>''';
    return '''<?xml version="1.0" encoding="UTF-8"?>
<SOAP-ENV:Envelope xmlns:SOAP-ENV="http://schemas.xmlsoap.org/soap/envelope/" xmlns:ns1="http://api.radioreference.com/soap2/" xmlns:xsd="http://www.w3.org/2001/XMLSchema" xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xmlns:SOAP-ENC="http://schemas.xmlsoap.org/soap/encoding/" SOAP-ENV:encodingStyle="http://schemas.xmlsoap.org/soap/encoding/">
<SOAP-ENV:Body>
<ns1:$method>
$paramXml
$authXml
</ns1:$method>
</SOAP-ENV:Body>
</SOAP-ENV:Envelope>''';
  }

  Future<Map<String, dynamic>?> _soapRequest(
    String wsdlUrl,
    String method,
    Map<String, dynamic> params
  ) async {
    final endpoint = wsdlUrl.replaceFirst('?wsdl&v=latest&s=rpc', '');
    final envelope = _buildSoapEnvelope(method, Map.of(params));
    final headers = {
      'Content-Type': 'text/xml; charset=utf-8',
      'SOAPAction': 'http://api.radioreference.com/soap2/$method'
    };
    
    try {
      final response = await http.post(
        Uri.parse(endpoint),
        headers: headers,
        body: envelope,
      );
      
      if (response.statusCode == 200 || response.statusCode == 500) {
        final document = XmlDocument.parse(response.body);
        
        // Check for SOAP Fault
        final faultNode = document.findAllElements('faultstring').firstOrNull;
        if (faultNode != null) {
          return {'fault': faultNode.innerText};
        }
        
        // Look for the response node (e.g., getZipcodeInfoResponse)
        // Use descendantElements to search recursively and ignore namespaces
        final responseNodes = document.descendantElements
            .where((e) => e.name.local == '${method}Response')
            .toList();
        
        if (responseNodes.isNotEmpty) {
          final responseNode = responseNodes.first;
          // Look for return element inside response
          final returnNodes = responseNode.descendantElements
              .where((e) => e.name.local == 'return')
              .toList();
          
          if (returnNodes.isNotEmpty) {
            final result = _xmlToMap(returnNodes.first);
            if (kDebugMode) {
              print('Parsed result: $result');
            }
            return result;
          }
          // Fallback to parsing the whole response
          return _xmlToMap(responseNode);
        }
        
        // Legacy: Try to find result node (for JSON responses)
        final resultNode = document.findAllElements('${method}Result').firstOrNull;
        if (resultNode != null) {
          try {
            final jsonMap = jsonDecode(resultNode.innerText);
            return jsonMap is Map<String, dynamic>
                ? jsonMap
                : Map<String, dynamic>.from(jsonMap);
          } catch (e) {
            return _xmlToMap(resultNode);
          }
        }
      }
      
      if (kDebugMode) {
        print('SOAP Response Status: ${response.statusCode}');
        print('SOAP Response Body: ${response.body}');
      }
      
      return null;
    } catch (e) {
      if (kDebugMode) {
        print('SOAP Request Error: $e');
      }
      rethrow;
    }
  }

  Map<String, dynamic> _xmlToMap(XmlElement node) {
    final map = <String, dynamic>{};
    final elementGroups = <String, List<XmlElement>>{};
    
    // Group elements by name to detect arrays
    for (final child in node.children.whereType<XmlElement>()) {
      final name = child.name.local;
      elementGroups.putIfAbsent(name, () => []);
      elementGroups[name]!.add(child);
    }
    
    // Process each group
    for (final entry in elementGroups.entries) {
      final name = entry.key;
      final elements = entry.value;
      
      if (elements.length == 1) {
        // Single element
        final child = elements.first;
        final childElements = child.children.whereType<XmlElement>().toList();
        if (childElements.isEmpty) {
          // Leaf node - get text value
          map[name] = child.innerText.trim();
        } else {
          // Has child elements - recurse
          map[name] = _xmlToMap(child);
        }
      } else {
        // Multiple elements with same name - create array
        final items = elements.map((child) {
          final childElements = child.children.whereType<XmlElement>().toList();
          if (childElements.isEmpty) {
            return child.innerText.trim();
          } else {
            return _xmlToMap(child);
          }
        }).toList();
        map[name] = items;
      }
    }
    return map;
  }

  Future<Map<String, dynamic>?> getZipcodeInfo(String zip) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    return await _soapRequest(wsdlUrl, 'getZipcodeInfo', {
      'zipcode': int.parse(zip),
      'authInfo': authInfo,
    });
  }

  Future<Map<String, dynamic>?> getCountyInfo(String countyId) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    return await _soapRequest(wsdlUrl, 'getCountyInfo', {
      'ctid': countyId,
      'authInfo': authInfo,
    });
  }

  Future<List<Map<String, dynamic>>?> getTrsSites(int systemId) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    
    if (kDebugMode) {
      print('=== getTrsSites called for system $systemId ===');
    }
    
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    final result = await _soapRequest(wsdlUrl, 'getTrsSites', {
      'sid': systemId,
      'authInfo': authInfo,
    });
    
    if (kDebugMode) {
      print('getTrsSites raw result keys: ${result?.keys}');
      if (result != null) {
        for (final key in result.keys) {
          print('  $key: ${result[key].runtimeType}');
        }
      }
    }
    
    if (result != null) {
      // The result should directly contain 'item' which can be:
      // - A single Map (one site)
      // - A List of Maps (multiple sites)
      var siteData = result['item'];
      
      if (kDebugMode) {
        print('Extracted item, type: ${siteData.runtimeType}');
      }
      
      if (siteData is List) {
        final sites = (siteData as List).map((e) => Map<String, dynamic>.from(e as Map)).toList();
        if (kDebugMode) {
          print('=== Returning ${sites.length} sites from list ===');
        }
        return sites;
      } else if (siteData is Map) {
        if (kDebugMode) {
          print('=== Returning 1 site from map ===');
        }
        return [Map<String, dynamic>.from(siteData)];
      }
      
      if (kDebugMode) {
        print('Could not extract sites - unexpected structure');
      }
    }
    
    return null;
  }

  Future<List<List<dynamic>>?> getTrsTalkgroups(int systemId) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    
    if (kDebugMode) {
      print('=== getTrsTalkgroups called for system $systemId ===');
    }
    
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    final result = await _soapRequest(wsdlUrl, 'getTrsTalkgroups', {
      'sid': systemId,
      'start': 0,
      'limit': 0,
      'filter': 0,
      'authInfo': authInfo,
    });
    
    if (kDebugMode) {
      print('getTrsTalkgroups raw result keys: ${result?.keys}');
    }
    
    if (result != null) {
      // The result should directly contain 'item' which can be:
      // - A single Map (one talkgroup)
      // - A List of Maps (multiple talkgroups)
      var talkgroupData = result['item'];
      
      if (kDebugMode) {
        print('talkgroup data type: ${talkgroupData.runtimeType}');
        if (talkgroupData == null) {
          print('WARNING: result[item] is null - checking for alternative keys');
          print('Available keys in result: ${result.keys}');
        }
      }
      
      // No need for nested 'item' structure handling since we're already at the right level
      
      final talkgroups = <List<dynamic>>[];
      
      if (talkgroupData is List) {
        for (final tg in talkgroupData) {
          if (tg['enc'] == '0' || tg['enc'] == 0) {
            talkgroups.add([tg['tgDec'], tg['tgAlpha']]);
          }
        }
        if (kDebugMode) {
          print('Processed ${talkgroups.length} unencrypted talkgroups from list of ${talkgroupData.length}');
        }
      } else if (talkgroupData is Map) {
        final tg = talkgroupData;
        if (tg['enc'] == '0' || tg['enc'] == 0) {
          talkgroups.add([tg['tgDec'], tg['tgAlpha']]);
        }
        if (kDebugMode) {
          print('Processed 1 talkgroup from single map');
        }
      }
      
      return talkgroups.isNotEmpty ? talkgroups : null;
    }
    return null;
  }

  Future<void> createSystemTsvFiles(int systemId, String systemName) async {
    isLoading = true;
    errorMessage = null;
    notifyListeners();
    try {
      if (kDebugMode) {
        print('Creating system database entries for system ID: $systemId');
      }
      
      // Insert/update system first
      await _db.insertSystem(systemId, systemName);
      
      // Try to fetch sites (may fail if system already imported)
      final sitesInfo = await getTrsSites(systemId);
      if (sitesInfo != null && sitesInfo.isNotEmpty) {
        if (kDebugMode) {
          print('Found ${sitesInfo.length} sites');
        }
        
        // Insert sites and control channels
        for (final site in sitesInfo) {
          await _insertSiteToDb(systemId, site);
        }
      } else {
        if (kDebugMode) {
          print('No sites returned (may already be imported or API error)');
        }
      }
      
      // Always try to fetch talkgroups, even if sites failed
      if (kDebugMode) {
        print('Fetching talkgroups for system $systemId...');
      }
      
      final talkgroupsInfo = await getTrsTalkgroups(systemId);
      if (kDebugMode) {
        print('Found ${talkgroupsInfo?.length ?? 0} talkgroups');
        if (talkgroupsInfo == null) {
          print('WARNING: getTrsTalkgroups returned null - no talkgroup data available');
        } else if (talkgroupsInfo.isEmpty) {
          print('WARNING: getTrsTalkgroups returned empty list - no unencrypted talkgroups found');
        }
      }

      // Insert talkgroups
      if (talkgroupsInfo != null && talkgroupsInfo.isNotEmpty) {
        await _clearAndInsertTalkgroups(systemId, talkgroupsInfo);
      } else {
        if (kDebugMode) {
          print('No talkgroups to import');
        }
      }

      isLoading = false;
      errorMessage = null;
      notifyListeners();
      
      if (kDebugMode) {
        print('System imported successfully to database');
      }
    } catch (e) {
      errorMessage = "Error creating system: $e";
      isLoading = false;
      notifyListeners();
      
      if (kDebugMode) {
        print('Error in createSystemTsvFiles: $e');
      }
    }
  }

  Future<void> _insertSiteToDb(int systemId, Map<String, dynamic> site) async {
    final siteId = int.parse(site['siteId'].toString());
    final siteDescr = site['siteDescr'] ?? 'Site $siteId';
    final siteNumber = site['siteNumber'] != null ? int.tryParse(site['siteNumber'].toString()) : null;
    final lat = site['lat'] != null ? double.tryParse(site['lat'].toString()) : null;
    final lon = site['lon'] != null ? double.tryParse(site['lon'].toString()) : null;
    final nac = site['nac']?.toString();
    
    var siteFreqs = site['siteFreqs'];
    
    // Handle nested 'item' structure
    if (siteFreqs is Map && siteFreqs.containsKey('item')) {
      siteFreqs = siteFreqs['item'];
    }
    
    final List<dynamic> freqs;
    if (siteFreqs is List) {
      freqs = siteFreqs;
    } else if (siteFreqs != null) {
      freqs = [siteFreqs];
    } else {
      freqs = [];
    }
    
    // Insert site
    await _db.insertSite({
      'site_id': siteId,
      'system_id': systemId,
      'site_number': siteNumber,
      'site_name': siteDescr,
      'latitude': lat,
      'longitude': lon,
      'nac': nac,
    });
    
    // Clear existing control channels
    await _db.clearControlChannels(siteId);
    
    // Insert control channels
    final controlChannels = freqs
        .where((f) => f is Map && f['use'] != null && f['use'].toString().isNotEmpty)
        .toList();
    
    for (int i = 0; i < controlChannels.length; i++) {
      final freq = controlChannels[i];
      final frequency = double.parse(freq['freq'].toString());
      final priority = freq['use'] == 'a' ? 1 : 0; // Primary vs alternate
      
      await _db.insertControlChannel(siteId, frequency, priority);
    }
    
    if (kDebugMode) {
      print('Inserted site $siteId ($siteDescr) with ${controlChannels.length} control channels');
    }
  }

  Future<void> _clearAndInsertTalkgroups(int systemId, List<List<dynamic>> talkgroups) async {
    await _db.clearTalkgroups(systemId);
    
    if (kDebugMode) {
      print('Inserting ${talkgroups.length} talkgroups for system $systemId');
    }
    
    for (final tg in talkgroups) {
      final tgDecimal = int.parse(tg[0].toString());
      final tgName = tg[1].toString();
      
      if (kDebugMode && talkgroups.indexOf(tg) < 5) {
        print('  TG $tgDecimal: $tgName');
      }
      
      await _db.insertTalkgroup(
        systemId,
        tgDecimal,
        tgName,
      );
    }
    
    if (kDebugMode) {
      print('Inserted ${talkgroups.length} talkgroups');
    }
  }
}
