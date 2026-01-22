import 'dart:convert';
import 'package:flutter/foundation.dart';
import 'package:http/http.dart' as http;
import 'package:package_info_plus/package_info_plus.dart';
import 'package:shared_preferences/shared_preferences.dart';

class UpdateInfo {
  final String version;
  final String releaseDate;
  final List<String> changelog;
  
  UpdateInfo({
    required this.version,
    required this.releaseDate,
    required this.changelog,
  });
  
  factory UpdateInfo.fromJson(Map<String, dynamic> json) {
    final changelogJson = json['changelog'] as List<dynamic>?;
    return UpdateInfo(
      version: json['version'] as String,
      releaseDate: json['releaseDate'] as String? ?? '',
      changelog: changelogJson?.map((e) => e.toString()).toList() ?? [],
    );
  }
}

class UpdateService {
  static const String _updateCheckUrl = 'https://pocket25.com/update.json';
  static const String _lastCheckKey = 'last_update_check';
  static const String _dismissedVersionKey = 'dismissed_update_version';
  static const Duration _checkInterval = Duration(hours: 24);
  
  /// Check if there's a newer version available
  /// Returns UpdateInfo if update is available, null otherwise
  Future<UpdateInfo?> checkForUpdates({bool force = false}) async {
    try {
      // Check if we should skip based on last check time
      if (!force && !await _shouldCheck()) {
        return null;
      }
      
      // Fetch update info from server
      final response = await http.get(
        Uri.parse(_updateCheckUrl),
      ).timeout(const Duration(seconds: 10));
      
      if (response.statusCode != 200) {
        if (kDebugMode) {
          print('Update check failed: HTTP ${response.statusCode}');
        }
        return null;
      }
      
      final json = jsonDecode(response.body) as Map<String, dynamic>;
      final updateInfo = UpdateInfo.fromJson(json);
      
      // Get current app version (includes build number)
      final packageInfo = await PackageInfo.fromPlatform();
      final currentVersion = '${packageInfo.version}+${packageInfo.buildNumber}';
      
      // Check if dismissed
      if (!force && await _isVersionDismissed(updateInfo.version)) {
        if (kDebugMode) {
          print('Update ${updateInfo.version} was previously dismissed');
        }
        return null;
      }
      
      // Compare versions
      if (_isNewerVersion(currentVersion, updateInfo.version)) {
        if (kDebugMode) {
          print('Update available: $currentVersion -> ${updateInfo.version}');
        }
        
        // Save last check time
        await _saveLastCheckTime();
        
        return updateInfo;
      }
      
      // No update available
      if (kDebugMode) {
        print('App is up to date: $currentVersion');
      }
      
      // Save last check time
      await _saveLastCheckTime();
      
      return null;
    } catch (e) {
      if (kDebugMode) {
        print('Update check error: $e');
      }
      return null;
    }
  }
  
  /// Check if enough time has passed since last check
  Future<bool> _shouldCheck() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final lastCheck = prefs.getInt(_lastCheckKey);
      
      if (lastCheck == null) {
        return true;
      }
      
      final lastCheckTime = DateTime.fromMillisecondsSinceEpoch(lastCheck);
      final now = DateTime.now();
      
      return now.difference(lastCheckTime) >= _checkInterval;
    } catch (e) {
      return true;
    }
  }
  
  /// Save the last check time
  Future<void> _saveLastCheckTime() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setInt(_lastCheckKey, DateTime.now().millisecondsSinceEpoch);
    } catch (e) {
      if (kDebugMode) {
        print('Failed to save last check time: $e');
      }
    }
  }
  
  /// Check if a version was dismissed by the user
  Future<bool> _isVersionDismissed(String version) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      final dismissedVersion = prefs.getString(_dismissedVersionKey);
      return dismissedVersion == version;
    } catch (e) {
      return false;
    }
  }
  
  /// Mark a version as dismissed
  Future<void> dismissVersion(String version) async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.setString(_dismissedVersionKey, version);
      if (kDebugMode) {
        print('Dismissed update version: $version');
      }
    } catch (e) {
      if (kDebugMode) {
        print('Failed to dismiss version: $e');
      }
    }
  }
  
  /// Clear dismissed version (for testing or when user manually checks)
  Future<void> clearDismissed() async {
    try {
      final prefs = await SharedPreferences.getInstance();
      await prefs.remove(_dismissedVersionKey);
    } catch (e) {
      if (kDebugMode) {
        print('Failed to clear dismissed version: $e');
      }
    }
  }
  
  /// Compare version strings (format: major.minor.patch+build)
  /// Returns true if newVersion is greater than currentVersion
  bool _isNewerVersion(String currentVersion, String newVersion) {
    // Split version and build number
    final currentSplit = currentVersion.split('+');
    final newSplit = newVersion.split('+');
    
    final currentVersionPart = currentSplit[0];
    final newVersionPart = newSplit[0];
    
    final currentBuild = currentSplit.length > 1 ? int.tryParse(currentSplit[1]) ?? 0 : 0;
    final newBuild = newSplit.length > 1 ? int.tryParse(newSplit[1]) ?? 0 : 0;
    
    // Parse version parts (major.minor.patch)
    final currentParts = currentVersionPart.split('.').map((e) => int.tryParse(e) ?? 0).toList();
    final newParts = newVersionPart.split('.').map((e) => int.tryParse(e) ?? 0).toList();
    
    // Ensure we have at least 3 parts (major.minor.patch)
    while (currentParts.length < 3) {
      currentParts.add(0);
    }
    while (newParts.length < 3) {
      newParts.add(0);
    }
    
    // Compare major.minor.patch
    for (int i = 0; i < 3; i++) {
      if (newParts[i] > currentParts[i]) {
        return true;
      } else if (newParts[i] < currentParts[i]) {
        return false;
      }
    }
    
    // Version parts are equal, compare build numbers
    return newBuild > currentBuild;
  }
}
