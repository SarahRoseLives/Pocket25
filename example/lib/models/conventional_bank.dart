class ConventionalBank {
  final int? id;
  final String bankName;
  final String? description;
  final bool enabled;
  final int sortOrder;
  final int channelCount;

  ConventionalBank({
    this.id,
    required this.bankName,
    this.description,
    this.enabled = true,
    this.sortOrder = 0,
    this.channelCount = 0,
  });

  /// Create from database map
  factory ConventionalBank.fromMap(Map<String, dynamic> map, {int? channelCount}) {
    return ConventionalBank(
      id: map['id'] as int?,
      bankName: map['bank_name'] as String,
      description: map['description'] as String?,
      enabled: (map['enabled'] as int? ?? 1) == 1,
      sortOrder: map['sort_order'] as int? ?? 0,
      channelCount: channelCount ?? (map['channel_count'] as int? ?? 0),
    );
  }

  /// Convert to database map
  Map<String, dynamic> toMap() {
    return {
      if (id != null) 'id': id,
      'bank_name': bankName,
      'description': description,
      'enabled': enabled ? 1 : 0,
      'sort_order': sortOrder,
    };
  }

  /// Create a copy with updated fields
  ConventionalBank copyWith({
    int? id,
    String? bankName,
    String? description,
    bool? enabled,
    int? sortOrder,
    int? channelCount,
  }) {
    return ConventionalBank(
      id: id ?? this.id,
      bankName: bankName ?? this.bankName,
      description: description ?? this.description,
      enabled: enabled ?? this.enabled,
      sortOrder: sortOrder ?? this.sortOrder,
      channelCount: channelCount ?? this.channelCount,
    );
  }

  /// Get display name with channel count
  String get displayName => '$bankName ($channelCount)';

  /// Get status text
  String get statusText => enabled ? 'Enabled' : 'Disabled';

  @override
  String toString() {
    return 'ConventionalBank(id: $id, name: $bankName, channels: $channelCount, enabled: $enabled)';
  }

  @override
  bool operator ==(Object other) {
    if (identical(this, other)) return true;
    return other is ConventionalBank && other.id == id;
  }

  @override
  int get hashCode => id.hashCode;
}
