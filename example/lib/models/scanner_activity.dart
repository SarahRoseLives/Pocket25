enum CallEventType {
  callStart,
  callUpdate,
  callEnd,
}

class CallEvent {
  final CallEventType eventType;
  final int talkgroup;
  final int sourceId;
  final int nac;
  final String callType;
  final bool isEncrypted;
  final bool isEmergency;
  final String algName;
  final int slot;
  final double frequency;
  final String systemName;
  final String groupName;
  final String sourceName;
  final DateTime timestamp;

  CallEvent({
    required this.eventType,
    required this.talkgroup,
    required this.sourceId,
    required this.nac,
    required this.callType,
    required this.isEncrypted,
    required this.isEmergency,
    required this.algName,
    required this.slot,
    required this.frequency,
    required this.systemName,
    required this.groupName,
    required this.sourceName,
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();

  factory CallEvent.fromMap(Map<String, dynamic> map) {
    return CallEvent(
      eventType: CallEventType.values[map['eventType'] as int],
      talkgroup: map['talkgroup'] as int,
      sourceId: map['sourceId'] as int,
      nac: map['nac'] as int,
      callType: map['callType'] as String,
      isEncrypted: map['isEncrypted'] as bool,
      isEmergency: map['isEmergency'] as bool,
      algName: map['algName'] as String,
      slot: map['slot'] as int,
      frequency: map['frequency'] as double,
      systemName: map['systemName'] as String,
      groupName: map['groupName'] as String,
      sourceName: map['sourceName'] as String,
    );
  }

  String get talkgroupDisplay {
    if (groupName.isNotEmpty) {
      return groupName;
    }
    return talkgroup.toString();
  }
  
  String get sourceDisplay {
    if (sourceName.isNotEmpty) {
      return sourceName;
    }
    if (sourceId == 0) {
      return '';
    }
    return sourceId.toString();
  }

  String get nacDisplay => nac > 0 ? '0x${nac.toRadixString(16).toUpperCase()}' : '';
  
  String get durationDisplay {
    final now = DateTime.now();
    final diff = now.difference(timestamp);
    
    if (diff.inSeconds < 60) {
      return '${diff.inSeconds}s';
    } else if (diff.inMinutes < 60) {
      final secs = diff.inSeconds % 60;
      return '${diff.inMinutes}m ${secs}s';
    } else {
      return '${diff.inHours}h ${diff.inMinutes % 60}m';
    }
  }
  
  String get timeDisplay {
    final now = DateTime.now();
    final diff = now.difference(timestamp);
    
    if (diff.inSeconds < 60) {
      return '${diff.inSeconds}s ago';
    } else if (diff.inMinutes < 60) {
      return '${diff.inMinutes}m ago';
    } else {
      return '${diff.inHours}h ago';
    }
  }
}

