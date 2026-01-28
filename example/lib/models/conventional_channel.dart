class ConventionalChannel {
  final int? id;
  final String channelName;
  final double frequency;
  final String modulation;
  final String bandwidth;
  final String? nac;
  final int? colorCode;
  final String? toneSquelch;
  final String? notes;
  final bool favorite;
  final int sortOrder;
  final DateTime? createdAt;
  final DateTime? lastUsedAt;
  final List<int> bankIds;

  ConventionalChannel({
    this.id,
    required this.channelName,
    required this.frequency,
    this.modulation = 'P25',
    this.bandwidth = '12.5kHz',
    this.nac,
    this.colorCode,
    this.toneSquelch,
    this.notes,
    this.favorite = false,
    this.sortOrder = 0,
    this.createdAt,
    this.lastUsedAt,
    this.bankIds = const [],
  });

  /// Create from database map
  factory ConventionalChannel.fromMap(Map<String, dynamic> map, {List<int>? bankIds}) {
    return ConventionalChannel(
      id: map['id'] as int?,
      channelName: map['channel_name'] as String,
      frequency: map['frequency'] as double,
      modulation: map['modulation'] as String? ?? 'P25',
      bandwidth: map['bandwidth'] as String? ?? '12.5kHz',
      nac: map['nac'] as String?,
      colorCode: map['color_code'] as int?,
      toneSquelch: map['tone_squelch'] as String?,
      notes: map['notes'] as String?,
      favorite: (map['favorite'] as int? ?? 0) == 1,
      sortOrder: map['sort_order'] as int? ?? 0,
      createdAt: map['created_at'] != null
          ? DateTime.fromMillisecondsSinceEpoch(map['created_at'] as int)
          : null,
      lastUsedAt: map['last_used_at'] != null
          ? DateTime.fromMillisecondsSinceEpoch(map['last_used_at'] as int)
          : null,
      bankIds: bankIds ?? [],
    );
  }

  /// Convert to database map
  Map<String, dynamic> toMap() {
    return {
      if (id != null) 'id': id,
      'channel_name': channelName,
      'frequency': frequency,
      'modulation': modulation,
      'bandwidth': bandwidth,
      'nac': nac,
      'color_code': colorCode,
      'tone_squelch': toneSquelch,
      'notes': notes,
      'favorite': favorite ? 1 : 0,
      'sort_order': sortOrder,
      'created_at': createdAt?.millisecondsSinceEpoch,
      'last_used_at': lastUsedAt?.millisecondsSinceEpoch,
    };
  }

  /// Create a copy with updated fields
  ConventionalChannel copyWith({
    int? id,
    String? channelName,
    double? frequency,
    String? modulation,
    String? bandwidth,
    String? nac,
    int? colorCode,
    String? toneSquelch,
    String? notes,
    bool? favorite,
    int? sortOrder,
    DateTime? createdAt,
    DateTime? lastUsedAt,
    List<int>? bankIds,
  }) {
    return ConventionalChannel(
      id: id ?? this.id,
      channelName: channelName ?? this.channelName,
      frequency: frequency ?? this.frequency,
      modulation: modulation ?? this.modulation,
      bandwidth: bandwidth ?? this.bandwidth,
      nac: nac ?? this.nac,
      colorCode: colorCode ?? this.colorCode,
      toneSquelch: toneSquelch ?? this.toneSquelch,
      notes: notes ?? this.notes,
      favorite: favorite ?? this.favorite,
      sortOrder: sortOrder ?? this.sortOrder,
      createdAt: createdAt ?? this.createdAt,
      lastUsedAt: lastUsedAt ?? this.lastUsedAt,
      bankIds: bankIds ?? this.bankIds,
    );
  }

  /// Get formatted frequency display
  String get frequencyDisplay => '${frequency.toStringAsFixed(4)} MHz';

  /// Get modulation badge color
  String get modulationBadge {
    switch (modulation.toUpperCase()) {
      case 'P25':
        return 'P25';
      case 'DMR':
        return 'DMR';
      case 'NXDN':
        return 'NXDN';
      case 'DSTAR':
        return 'D-STAR';
      case 'YSF':
        return 'YSF';
      default:
        return modulation;
    }
  }

  @override
  String toString() {
    return 'ConventionalChannel(id: $id, name: $channelName, freq: $frequency MHz, mod: $modulation)';
  }

  @override
  bool operator ==(Object other) {
    if (identical(this, other)) return true;
    return other is ConventionalChannel && other.id == id;
  }

  @override
  int get hashCode => id.hashCode;
}
