class SiteDetails {
  final int wacn;
  final int siteId;
  final int rfssId;
  final int systemId;
  final int nac;
  final DateTime timestamp;

  SiteDetails({
    required this.wacn,
    required this.siteId,
    required this.rfssId,
    required this.systemId,
    required this.nac,
    DateTime? timestamp,
  }) : timestamp = timestamp ?? DateTime.now();

  factory SiteDetails.fromMap(Map<String, dynamic> map) {
    return SiteDetails(
      wacn: map['wacn'] as int,
      siteId: map['siteId'] as int,
      rfssId: map['rfssId'] as int,
      systemId: map['systemId'] as int,
      nac: map['nac'] as int,
    );
  }

  String get wacnHex => '0x${wacn.toRadixString(16).toUpperCase().padLeft(5, '0')}';
  String get siteIdHex => '0x${siteId.toRadixString(16).toUpperCase().padLeft(3, '0')}';
  String get rfssIdHex => '0x${rfssId.toRadixString(16).toUpperCase().padLeft(2, '0')}';
  String get systemIdHex => '0x${systemId.toRadixString(16).toUpperCase()}';
  String get nacHex => '0x${nac.toRadixString(16).toUpperCase().padLeft(3, '0')}';
  
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
