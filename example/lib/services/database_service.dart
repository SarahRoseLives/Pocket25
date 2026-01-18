import 'package:sqflite/sqflite.dart';
import 'package:path/path.dart';

/// Filter status for talkgroups
/// 0 = normal (no filter)
/// 1 = whitelisted (hear this TG when in whitelist mode)
/// 2 = blacklisted (mute this TG when in blacklist mode)
class TalkgroupFilterStatus {
  static const int normal = 0;
  static const int whitelisted = 1;
  static const int blacklisted = 2;
}

class DatabaseService {
  static Database? _database;

  Future<Database> get database async {
    if (_database != null) return _database!;
    _database = await _initDatabase();
    return _database!;
  }

  Future<Database> _initDatabase() async {
    final dbPath = await getDatabasesPath();
    final path = join(dbPath, 'pocket25.db');

    return await openDatabase(
      path,
      version: 3,
      onCreate: (db, version) async {
        await db.execute('''
          CREATE TABLE systems (
            system_id INTEGER PRIMARY KEY,
            system_name TEXT NOT NULL
          )
        ''');

        await db.execute('''
          CREATE TABLE sites (
            site_id INTEGER PRIMARY KEY,
            system_id INTEGER NOT NULL,
            site_number INTEGER,
            site_name TEXT NOT NULL,
            latitude REAL,
            longitude REAL,
            nac TEXT,
            FOREIGN KEY (system_id) REFERENCES systems (system_id) ON DELETE CASCADE
          )
        ''');

        await db.execute('''
          CREATE TABLE control_channels (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            site_id INTEGER NOT NULL,
            frequency REAL NOT NULL,
            priority INTEGER DEFAULT 0,
            FOREIGN KEY (site_id) REFERENCES sites (site_id) ON DELETE CASCADE
          )
        ''');

        await db.execute('''
          CREATE TABLE talkgroups (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            system_id INTEGER NOT NULL,
            tg_decimal INTEGER NOT NULL,
            tg_name TEXT NOT NULL,
            tg_category TEXT,
            tg_tag TEXT,
            filter_status INTEGER DEFAULT 0,
            FOREIGN KEY (system_id) REFERENCES systems (system_id) ON DELETE CASCADE
          )
        ''');

        await db.execute('''
          CREATE INDEX idx_sites_system ON sites(system_id)
        ''');

        await db.execute('''
          CREATE INDEX idx_control_channels_site ON control_channels(site_id)
        ''');

        await db.execute('''
          CREATE INDEX idx_talkgroups_system ON talkgroups(system_id)
        ''');
        
        await db.execute('''
          CREATE INDEX idx_talkgroups_filter ON talkgroups(filter_status)
        ''');
      },
      onUpgrade: (db, oldVersion, newVersion) async {
        if (oldVersion < 2) {
          // Add filter_status column to talkgroups table
          await db.execute('''
            ALTER TABLE talkgroups ADD COLUMN filter_status INTEGER DEFAULT 0
          ''');
          await db.execute('''
            CREATE INDEX IF NOT EXISTS idx_talkgroups_filter ON talkgroups(filter_status)
          ''');
        }
        if (oldVersion < 3) {
          // Add category and tag columns to talkgroups table
          await db.execute('''
            ALTER TABLE talkgroups ADD COLUMN tg_category TEXT
          ''');
          await db.execute('''
            ALTER TABLE talkgroups ADD COLUMN tg_tag TEXT
          ''');
          await db.execute('''
            CREATE INDEX IF NOT EXISTS idx_talkgroups_category ON talkgroups(tg_category)
          ''');
        }
      },
    );
  }

  Future<void> insertSystem(int systemId, String systemName) async {
    final db = await database;
    await db.insert(
      'systems',
      {'system_id': systemId, 'system_name': systemName},
      conflictAlgorithm: ConflictAlgorithm.replace,
    );
  }

  Future<void> insertSite(Map<String, dynamic> site) async {
    final db = await database;
    await db.insert('sites', site, conflictAlgorithm: ConflictAlgorithm.replace);
  }

  Future<void> insertControlChannel(int siteId, double frequency, int priority) async {
    final db = await database;
    await db.insert('control_channels', {
      'site_id': siteId,
      'frequency': frequency,
      'priority': priority,
    });
  }

  Future<void> insertTalkgroup(int systemId, int tgDecimal, String tgName, {String? category, String? tag}) async {
    final db = await database;
    await db.insert('talkgroups', {
      'system_id': systemId,
      'tg_decimal': tgDecimal,
      'tg_name': tgName,
      'tg_category': category,
      'tg_tag': tag,
    }, conflictAlgorithm: ConflictAlgorithm.replace);
  }

  Future<List<Map<String, dynamic>>> getSystems() async {
    final db = await database;
    return await db.query('systems', orderBy: 'system_name');
  }

  Future<List<Map<String, dynamic>>> getSitesBySystem(int systemId) async {
    final db = await database;
    return await db.query(
      'sites',
      where: 'system_id = ?',
      whereArgs: [systemId],
      orderBy: 'site_name',
    );
  }

  Future<int?> getSystemIdForSite(int siteId) async {
    final db = await database;
    final results = await db.query(
      'sites',
      columns: ['system_id'],
      where: 'site_id = ?',
      whereArgs: [siteId],
      limit: 1,
    );
    
    if (results.isNotEmpty) {
      return results.first['system_id'] as int?;
    }
    return null;
  }

  Future<List<Map<String, dynamic>>> getControlChannels(int siteId) async {
    final db = await database;
    return await db.query(
      'control_channels',
      where: 'site_id = ?',
      whereArgs: [siteId],
      orderBy: 'priority DESC, frequency',
    );
  }

  Future<List<Map<String, dynamic>>> getTalkgroups(int systemId) async {
    final db = await database;
    return await db.query(
      'talkgroups',
      where: 'system_id = ?',
      whereArgs: [systemId],
      orderBy: 'tg_decimal',
    );
  }

  Future<String?> getTalkgroupName(int systemId, int tgDecimal) async {
    final db = await database;
    final results = await db.query(
      'talkgroups',
      columns: ['tg_name'],
      where: 'system_id = ? AND tg_decimal = ?',
      whereArgs: [systemId, tgDecimal],
      limit: 1,
    );
    
    if (results.isNotEmpty) {
      return results.first['tg_name'] as String?;
    }
    return null;
  }

  Future<void> deleteSystem(int systemId) async {
    final db = await database;
    // Cascading delete will handle sites, control channels, and talkgroups
    await db.delete('systems', where: 'system_id = ?', whereArgs: [systemId]);
  }

  Future<void> clearControlChannels(int siteId) async {
    final db = await database;
    await db.delete('control_channels', where: 'site_id = ?', whereArgs: [siteId]);
  }

  Future<void> clearTalkgroups(int systemId) async {
    final db = await database;
    await db.delete('talkgroups', where: 'system_id = ?', whereArgs: [systemId]);
  }
  
  // ============================================================================
  // Talkgroup Filter Methods
  // ============================================================================
  
  /// Set the filter status for a talkgroup
  Future<void> setTalkgroupFilterStatus(int systemId, int tgDecimal, int filterStatus) async {
    final db = await database;
    await db.update(
      'talkgroups',
      {'filter_status': filterStatus},
      where: 'system_id = ? AND tg_decimal = ?',
      whereArgs: [systemId, tgDecimal],
    );
  }
  
  /// Get all whitelisted talkgroups for a system
  Future<List<int>> getWhitelistedTalkgroups(int systemId) async {
    final db = await database;
    final results = await db.query(
      'talkgroups',
      columns: ['tg_decimal'],
      where: 'system_id = ? AND filter_status = ?',
      whereArgs: [systemId, TalkgroupFilterStatus.whitelisted],
    );
    return results.map((row) => row['tg_decimal'] as int).toList();
  }
  
  /// Get all blacklisted talkgroups for a system
  Future<List<int>> getBlacklistedTalkgroups(int systemId) async {
    final db = await database;
    final results = await db.query(
      'talkgroups',
      columns: ['tg_decimal'],
      where: 'system_id = ? AND filter_status = ?',
      whereArgs: [systemId, TalkgroupFilterStatus.blacklisted],
    );
    return results.map((row) => row['tg_decimal'] as int).toList();
  }
  
  /// Get talkgroups with their filter status for a system
  Future<List<Map<String, dynamic>>> getTalkgroupsWithFilterStatus(int systemId) async {
    final db = await database;
    return await db.query(
      'talkgroups',
      where: 'system_id = ?',
      whereArgs: [systemId],
      orderBy: 'tg_decimal',
    );
  }
  
  /// Clear all filter statuses for a system (reset to normal)
  Future<void> clearTalkgroupFilters(int systemId) async {
    final db = await database;
    await db.update(
      'talkgroups',
      {'filter_status': TalkgroupFilterStatus.normal},
      where: 'system_id = ?',
      whereArgs: [systemId],
    );
  }
  
  /// Toggle whitelist status for a talkgroup
  Future<int> toggleWhitelist(int systemId, int tgDecimal) async {
    final db = await database;
    final results = await db.query(
      'talkgroups',
      columns: ['filter_status'],
      where: 'system_id = ? AND tg_decimal = ?',
      whereArgs: [systemId, tgDecimal],
      limit: 1,
    );
    
    if (results.isEmpty) return TalkgroupFilterStatus.normal;
    
    final currentStatus = results.first['filter_status'] as int? ?? TalkgroupFilterStatus.normal;
    final newStatus = currentStatus == TalkgroupFilterStatus.whitelisted 
        ? TalkgroupFilterStatus.normal 
        : TalkgroupFilterStatus.whitelisted;
    
    await setTalkgroupFilterStatus(systemId, tgDecimal, newStatus);
    return newStatus;
  }
  
  /// Toggle blacklist status for a talkgroup
  Future<int> toggleBlacklist(int systemId, int tgDecimal) async {
    final db = await database;
    final results = await db.query(
      'talkgroups',
      columns: ['filter_status'],
      where: 'system_id = ? AND tg_decimal = ?',
      whereArgs: [systemId, tgDecimal],
      limit: 1,
    );
    
    if (results.isEmpty) return TalkgroupFilterStatus.normal;
    
    final currentStatus = results.first['filter_status'] as int? ?? TalkgroupFilterStatus.normal;
    final newStatus = currentStatus == TalkgroupFilterStatus.blacklisted 
        ? TalkgroupFilterStatus.normal 
        : TalkgroupFilterStatus.blacklisted;
    
    await setTalkgroupFilterStatus(systemId, tgDecimal, newStatus);
    return newStatus;
  }
  
  /// Get distinct categories for a system
  Future<List<String>> getTalkgroupCategories(int systemId) async {
    final db = await database;
    final results = await db.rawQuery('''
      SELECT DISTINCT tg_category 
      FROM talkgroups 
      WHERE system_id = ? AND tg_category IS NOT NULL AND tg_category != ''
      ORDER BY tg_category
    ''', [systemId]);
    
    return results
        .map((row) => row['tg_category'] as String)
        .where((cat) => cat.isNotEmpty)
        .toList();
  }
  
  /// Get talkgroups filtered by category
  Future<List<Map<String, dynamic>>> getTalkgroupsByCategory(int systemId, String category) async {
    final db = await database;
    return await db.query(
      'talkgroups',
      where: 'system_id = ? AND tg_category = ?',
      whereArgs: [systemId, category],
      orderBy: 'tg_decimal',
    );
  }
}
