import 'dart:convert';
import 'dart:math';
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

  Future<Map<String, dynamic>?> getCountyByCoordinates(double lat, double lon) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    
    if (kDebugMode) {
      print('=== Looking up location for coordinates: $lat, $lon ===');
    }
    
    try {
      // Get all countries
      final countries = await getCountryList();
      if (countries == null) return null;
      
      // Determine country based on coordinates
      // USA: roughly 24-50°N, 125-66°W
      // Canada: roughly 41-83°N, 141-52°W
      final country = countries.firstWhere(
        (c) {
          final name = c['countryName'].toString().toLowerCase();
          if (lat > 50) {
            return name.contains('canada');
          } else if (lat > 24 && lon > -125 && lon < -66) {
            return name.contains('united states');
          }
          return name.contains('united states'); // default to US
        },
        orElse: () => countries.first,
      );
      
      if (kDebugMode) {
        print('Selected country: ${country['countryName']}');
      }
      
      final countryId = int.parse(country['coid'].toString());
      final countryInfo = await getCountryInfo(countryId);
      
      if (countryInfo == null || countryInfo['stateList'] == null) return null;
      
      var stateList = countryInfo['stateList'];
      var items = stateList is Map ? stateList['item'] : stateList;
      
      List<Map<String, dynamic>> states;
      if (items is List) {
        states = items.map((s) => Map<String, dynamic>.from(s)).toList();
      } else if (items is Map) {
        states = [Map<String, dynamic>.from(items)];
      } else {
        return null;
      }
      
      if (kDebugMode) {
        print('Found ${states.length} states/provinces');
      }
      
      // Optimize: use state code mapping to narrow down search
      final targetState = _guessStateFromCoordinates(lat, lon);
      
      // Try the guessed state first
      if (targetState != null) {
        final state = states.where((s) => s['stateCode']?.toString().toUpperCase() == targetState).firstOrNull;
        if (state != null) {
          if (kDebugMode) {
            print('Trying guessed state: ${state['stateName']}');
          }
          
          final result = await _findClosestCountyInState(int.parse(state['stid'].toString()), lat, lon);
          if (result != null) return result;
        }
      }
      
      // If guess failed, check all states (but limit to reasonable distance)
      Map<String, dynamic>? closestCounty;
      double closestDistance = double.infinity;
      
      for (final state in states) {
        final stateId = int.parse(state['stid'].toString());
        
        if (kDebugMode) {
          print('Checking state: ${state['stateName']}');
        }
        
        final result = await _findClosestCountyInState(stateId, lat, lon);
        if (result != null) {
          final countyLat = result['lat'] != null ? double.tryParse(result['lat'].toString()) : null;
          final countyLon = result['lon'] != null ? double.tryParse(result['lon'].toString()) : null;
          
          if (countyLat != null && countyLon != null) {
            final distance = _calculateDistance(lat, lon, countyLat, countyLon);
            
            if (distance < closestDistance) {
              closestDistance = distance;
              closestCounty = result;
              
              // If we found something within 100km, that's probably good enough
              if (distance < 100) break;
            }
          }
        }
      }
      
      if (kDebugMode && closestCounty != null) {
        print('Closest county: ${closestCounty['countyName']} at ${closestDistance.toStringAsFixed(2)} km');
      }
      
      return closestCounty;
    } catch (e) {
      if (kDebugMode) {
        print('Error in getCountyByCoordinates: $e');
      }
      errorMessage = "Could not find location: $e";
      notifyListeners();
      return null;
    }
  }
  
  Future<Map<String, dynamic>?> _findClosestCountyInState(int stateId, double lat, double lon) async {
    final stateInfo = await getStateInfo(stateId);
    
    if (stateInfo == null || stateInfo['countyList'] == null) return null;
    
    var countyList = stateInfo['countyList'];
    var countyItems = countyList is Map ? countyList['item'] : countyList;
    
    List<Map<String, dynamic>> counties;
    if (countyItems is List) {
      counties = countyItems.map((c) => Map<String, dynamic>.from(c)).toList();
    } else if (countyItems is Map) {
      counties = [Map<String, dynamic>.from(countyItems)];
    } else {
      return null;
    }
    
    Map<String, dynamic>? closestCounty;
    double closestDistance = double.infinity;
    
    // Sample some counties to find closest (checking all would be too slow)
    final sampleSize = counties.length > 10 ? 10 : counties.length;
    for (int i = 0; i < sampleSize; i++) {
      final county = counties[i];
      final ctid = county['ctid']?.toString();
      if (ctid == null) continue;
      
      final countyInfo = await getCountyInfo(ctid);
      if (countyInfo == null) continue;
      
      final countyLat = countyInfo['lat'] != null ? double.tryParse(countyInfo['lat'].toString()) : null;
      final countyLon = countyInfo['lon'] != null ? double.tryParse(countyInfo['lon'].toString()) : null;
      
      if (countyLat != null && countyLon != null) {
        final distance = _calculateDistance(lat, lon, countyLat, countyLon);
        
        if (distance < closestDistance) {
          closestDistance = distance;
          closestCounty = countyInfo;
        }
      }
    }
    
    return closestCounty;
  }
  
  String? _guessStateFromCoordinates(double lat, double lon) {
    // Simple state code mapping based on rough coordinate ranges
    // This is a heuristic to speed up search
    
    // California
    if (lat >= 32.5 && lat <= 42 && lon >= -124.5 && lon <= -114) return 'CA';
    // Texas
    if (lat >= 25.8 && lat <= 36.5 && lon >= -106.6 && lon <= -93.5) return 'TX';
    // Florida
    if (lat >= 24.5 && lat <= 31 && lon >= -87.6 && lon <= -80) return 'FL';
    // New York
    if (lat >= 40.5 && lat <= 45 && lon >= -79.8 && lon <= -71.8) return 'NY';
    // Pennsylvania
    if (lat >= 39.7 && lat <= 42 && lon >= -80.5 && lon <= -74.7) return 'PA';
    // Illinois
    if (lat >= 37 && lat <= 42.5 && lon >= -91.5 && lon <= -87.5) return 'IL';
    // Ohio
    if (lat >= 38.4 && lat <= 42 && lon >= -84.8 && lon <= -80.5) return 'OH';
    // Michigan
    if (lat >= 41.7 && lat <= 48.3 && lon >= -90.4 && lon <= -82.4) return 'MI';
    // Georgia
    if (lat >= 30.4 && lat <= 35 && lon >= -85.6 && lon <= -80.8) return 'GA';
    // North Carolina
    if (lat >= 33.8 && lat <= 36.6 && lon >= -84.3 && lon <= -75.4) return 'NC';
    // Virginia
    if (lat >= 36.5 && lat <= 39.5 && lon >= -83.7 && lon <= -75.2) return 'VA';
    // Washington
    if (lat >= 45.5 && lat <= 49 && lon >= -124.8 && lon <= -116.9) return 'WA';
    // Ontario
    if (lat >= 41.7 && lat <= 56.9 && lon >= -95.2 && lon <= -74.3) return 'ON';
    // Quebec
    if (lat >= 45 && lat <= 62.6 && lon >= -79.8 && lon <= -57) return 'QC';
    // British Columbia
    if (lat >= 48.3 && lat <= 60 && lon >= -139 && lon <= -114.1) return 'BC';
    
    return null; // Unknown, will check all states
  }
  
  double _calculateDistance(double lat1, double lon1, double lat2, double lon2) {
    // Haversine formula for distance between two coordinates
    const double earthRadius = 6371; // km
    
    final dLat = _toRadians(lat2 - lat1);
    final dLon = _toRadians(lon2 - lon1);
    
    final a = sin(dLat / 2) * sin(dLat / 2) +
        cos(_toRadians(lat1)) * cos(_toRadians(lat2)) *
        sin(dLon / 2) * sin(dLon / 2);
    
    final c = 2 * atan2(sqrt(a), sqrt(1 - a));
    
    return earthRadius * c;
  }
  
  double _toRadians(double degrees) {
    return degrees * pi / 180;
  }

  Future<List<Map<String, dynamic>>?> getCountryList() async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    final result = await _soapRequest(wsdlUrl, 'getCountryList', {
      'authInfo': authInfo,
    });
    
    if (result != null && result.containsKey('item')) {
      var items = result['item'];
      if (items is List) {
        return items.map((e) => Map<String, dynamic>.from(e as Map)).toList();
      } else if (items is Map) {
        return [Map<String, dynamic>.from(items)];
      }
    }
    return null;
  }

  Future<Map<String, dynamic>?> getCountryInfo(int countryId) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    return await _soapRequest(wsdlUrl, 'getCountryInfo', {
      'coid': countryId,
      'authInfo': authInfo,
    });
  }

  Future<Map<String, dynamic>?> getStateInfo(int stateId) async {
    if (!(isLoggedIn && username != null && password != null)) {
      errorMessage = "Please login first.";
      notifyListeners();
      return null;
    }
    final wsdlUrl = "http://api.radioreference.com/soap2/?wsdl&v=latest&s=rpc";
    final authInfo = _buildAuthInfo(username!, password!);
    return await _soapRequest(wsdlUrl, 'getStateInfo', {
      'stid': stateId,
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
        final sites = siteData.map((e) => Map<String, dynamic>.from(e as Map)).toList();
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
