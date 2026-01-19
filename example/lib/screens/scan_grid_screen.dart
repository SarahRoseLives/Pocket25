import 'package:flutter/material.dart';
import '../services/database_service.dart';
import '../services/scanning_service.dart';

class ScanGridScreen extends StatefulWidget {
  final ScanningService scanningService;
  final Function(Set<int>) onMutedTalkgroupsChanged;
  final Set<int> initialMutedTalkgroups;

  const ScanGridScreen({
    super.key,
    required this.scanningService,
    required this.onMutedTalkgroupsChanged,
    required this.initialMutedTalkgroups,
  });

  @override
  State<ScanGridScreen> createState() => _ScanGridScreenState();
}

class _ScanGridScreenState extends State<ScanGridScreen> {
  final DatabaseService _db = DatabaseService();
  List<Map<String, dynamic>> _allTalkgroups = [];
  List<List<Map<String, dynamic>>> _talkgroupPages = [];
  Set<int> _mutedTalkgroups = {};
  int _currentPage = 0;
  bool _isLoading = true;
  String? _systemName;
  List<String> _categories = [];
  String? _selectedCategory;
  int _itemsPerPage = 9; // Will be calculated dynamically
  late PageController _pageController;

  @override
  void initState() {
    super.initState();
    _pageController = PageController();
    _mutedTalkgroups = Set<int>.from(widget.initialMutedTalkgroups);
    _loadTalkgroups();
  }

  @override
  void dispose() {
    _pageController.dispose();
    super.dispose();
  }

  Future<void> _loadTalkgroups() async {
    final systemId = widget.scanningService.currentSystemId;
    
    if (systemId == null) {
      setState(() {
        _isLoading = false;
      });
      return;
    }

    try {
      // Get system name
      final systems = await _db.getSystems();
      final system = systems.firstWhere(
        (s) => s['system_id'] == systemId,
        orElse: () => {'system_name': 'Unknown System'},
      );
      _systemName = system['system_name'] as String;

      // Get categories for this system
      final categories = await _db.getTalkgroupCategories(systemId);
      
      // Get talkgroups (filtered by category if selected)
      List<Map<String, dynamic>> talkgroups;
      if (_selectedCategory != null) {
        talkgroups = await _db.getTalkgroupsByCategory(systemId, _selectedCategory!);
      } else {
        talkgroups = await _db.getTalkgroups(systemId);
      }
      
      // Split into pages based on items per page
      final pages = <List<Map<String, dynamic>>>[];
      for (int i = 0; i < talkgroups.length; i += _itemsPerPage) {
        final end = (i + _itemsPerPage < talkgroups.length) ? i + _itemsPerPage : talkgroups.length;
        pages.add(talkgroups.sublist(i, end));
      }

      setState(() {
        _allTalkgroups = talkgroups;
        _talkgroupPages = pages;
        _categories = categories;
        _isLoading = false;
        _currentPage = 0; // Reset to first page when category changes
      });
    } catch (e) {
      debugPrint('Error loading talkgroups: $e');
      setState(() {
        _isLoading = false;
      });
    }
  }

  void _recalculatePages() {
    // Recalculate pages based on new items per page
    final pages = <List<Map<String, dynamic>>>[];
    for (int i = 0; i < _allTalkgroups.length; i += _itemsPerPage) {
      final end = (i + _itemsPerPage < _allTalkgroups.length) ? i + _itemsPerPage : _allTalkgroups.length;
      pages.add(_allTalkgroups.sublist(i, end));
    }
    
    // Make sure current page is still valid
    int newCurrentPage = _currentPage;
    if (newCurrentPage >= pages.length && pages.isNotEmpty) {
      newCurrentPage = pages.length - 1;
    }
    
    setState(() {
      _talkgroupPages = pages;
      _currentPage = newCurrentPage;
    });
    
    // Jump to the current page in case page count changed
    if (pages.isNotEmpty && _pageController.hasClients) {
      _pageController.jumpToPage(newCurrentPage);
    }
  }

  void _onCategoryChanged(String? category) {
    setState(() {
      _selectedCategory = category;
      _isLoading = true;
    });
    _loadTalkgroups();
  }

  void _toggleTalkgroup(int tgDecimal) {
    setState(() {
      if (_mutedTalkgroups.contains(tgDecimal)) {
        _mutedTalkgroups.remove(tgDecimal);
      } else {
        _mutedTalkgroups.add(tgDecimal);
      }
    });
    widget.onMutedTalkgroupsChanged(_mutedTalkgroups);
  }

  void _muteAll() {
    setState(() {
      _mutedTalkgroups = _allTalkgroups.map((tg) => tg['tg_decimal'] as int).toSet();
    });
    widget.onMutedTalkgroupsChanged(_mutedTalkgroups);
  }

  void _unmuteAll() {
    setState(() {
      _mutedTalkgroups.clear();
    });
    widget.onMutedTalkgroupsChanged(_mutedTalkgroups);
  }

  Widget _buildTalkgroupGrid() {
    if (_talkgroupPages.isEmpty) {
      return Center(
        child: Text(
          'No talkgroups found.\nImport a system from Radio Reference.',
          textAlign: TextAlign.center,
          style: TextStyle(
            color: Colors.grey[400],
            fontSize: 16,
          ),
        ),
      );
    }
    
    return LayoutBuilder(
      builder: (context, constraints) {
        // Calculate optimal grid size based on available space
        const double minButtonHeight = 80.0;
        const double minButtonWidth = 100.0;
        const double gridSpacing = 8.0;
        
        // Calculate how many columns fit
        int columns = (constraints.maxWidth / (minButtonWidth + gridSpacing)).floor();
        columns = columns.clamp(2, 4); // Min 2, max 4 columns
        
        // Calculate how many rows fit
        int rows = (constraints.maxHeight / (minButtonHeight + gridSpacing)).floor();
        rows = rows.clamp(2, 5); // Min 2, max 5 rows
        
        final calculatedItemsPerPage = columns * rows;
        
        // If items per page changed, recalculate pages
        if (calculatedItemsPerPage != _itemsPerPage) {
          WidgetsBinding.instance.addPostFrameCallback((_) {
            setState(() {
              _itemsPerPage = calculatedItemsPerPage;
            });
            _recalculatePages();
          });
        }
        
        final buttonWidth = (constraints.maxWidth - (gridSpacing * (columns - 1))) / columns;
        final buttonHeight = (constraints.maxHeight - (gridSpacing * (rows - 1))) / rows;

        return PageView.builder(
          controller: _pageController,
          itemCount: _talkgroupPages.length,
          onPageChanged: (page) {
            setState(() {
              _currentPage = page;
            });
          },
          itemBuilder: (context, pageIndex) {
            final talkgroups = _talkgroupPages[pageIndex];
            
            return GridView.builder(
              physics: const NeverScrollableScrollPhysics(),
              itemCount: _itemsPerPage,
              gridDelegate: SliverGridDelegateWithFixedCrossAxisCount(
                crossAxisCount: columns,
                crossAxisSpacing: gridSpacing,
                mainAxisSpacing: gridSpacing,
                childAspectRatio: buttonWidth / buttonHeight,
              ),
              itemBuilder: (context, i) {
                // Show empty slot if no talkgroup at this position
                if (i >= talkgroups.length) {
                  return Card(
                    color: const Color(0xFF2A2A2A),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                    elevation: 0,
                    child: const Center(
                      child: Text(
                        '---',
                        style: TextStyle(
                          color: Colors.grey,
                          fontSize: 16,
                        ),
                      ),
                    ),
                  );
                }

                final tg = talkgroups[i];
                final tgDecimal = tg['tg_decimal'] as int;
                final tgName = tg['tg_name'] as String;
                final isEnabled = !_mutedTalkgroups.contains(tgDecimal);

                return GestureDetector(
                  onTap: () => _toggleTalkgroup(tgDecimal),
                  child: Card(
                    color: isEnabled
                        ? Colors.green[600]
                        : const Color(0xFF424242),
                    shape: RoundedRectangleBorder(
                      borderRadius: BorderRadius.circular(10),
                    ),
                    elevation: isEnabled ? 3 : 1,
                    child: Padding(
                      padding: const EdgeInsets.all(8.0),
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Flexible(
                            child: Text(
                              tgName,
                              textAlign: TextAlign.center,
                              maxLines: 2,
                              overflow: TextOverflow.ellipsis,
                              style: const TextStyle(
                                fontWeight: FontWeight.bold,
                                fontSize: 14,
                                color: Colors.white,
                              ),
                            ),
                          ),
                          const SizedBox(height: 4),
                          Text(
                            '$tgDecimal',
                            style: const TextStyle(
                              color: Colors.white70,
                              fontSize: 12,
                            ),
                          ),
                        ],
                      ),
                    ),
                  ),
                );
              },
            );
          },
        );
      },
    );
  }

  @override
  Widget build(BuildContext context) {
    if (_isLoading) {
      return const Scaffold(
        backgroundColor: Color(0xFF232323),
        body: Center(
          child: CircularProgressIndicator(),
        ),
      );
    }

    final systemId = widget.scanningService.currentSystemId;
    
    if (systemId == null) {
      return Scaffold(
        backgroundColor: const Color(0xFF232323),
        body: Center(
          child: Column(
            mainAxisAlignment: MainAxisAlignment.center,
            children: [
              Icon(
                Icons.grid_off,
                size: 64,
                color: Colors.grey[600],
              ),
              const SizedBox(height: 16),
              Text(
                'No System Selected',
                style: TextStyle(
                  color: Colors.grey[400],
                  fontSize: 18,
                  fontWeight: FontWeight.bold,
                ),
              ),
              const SizedBox(height: 8),
              Text(
                'Start scanning a system to use the scan grid',
                style: TextStyle(
                  color: Colors.grey[500],
                  fontSize: 14,
                ),
              ),
            ],
          ),
        ),
      );
    }

    return Scaffold(
      backgroundColor: const Color(0xFF232323),
      body: SafeArea(
        child: Column(
          children: [
            // System name header
            Container(
              padding: const EdgeInsets.all(12),
              color: const Color(0xFF2A2A2A),
              child: Row(
                children: [
                  Icon(Icons.grid_on, color: Colors.cyan, size: 20),
                  const SizedBox(width: 8),
                  Expanded(
                    child: Text(
                      _systemName ?? 'Scan Grid',
                      style: const TextStyle(
                        color: Colors.white,
                        fontSize: 16,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ),
                  Text(
                    '${_mutedTalkgroups.length} muted',
                    style: TextStyle(
                      color: Colors.grey[400],
                      fontSize: 12,
                    ),
                  ),
                  const SizedBox(width: 12),
                  // Mute all button
                  IconButton(
                    icon: const Icon(Icons.volume_off, size: 20),
                    color: Colors.red[400],
                    tooltip: 'Mute All',
                    onPressed: _muteAll,
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(),
                  ),
                  const SizedBox(width: 8),
                  // Unmute all button
                  IconButton(
                    icon: const Icon(Icons.volume_up, size: 20),
                    color: Colors.green[400],
                    tooltip: 'Unmute All',
                    onPressed: _unmuteAll,
                    padding: EdgeInsets.zero,
                    constraints: const BoxConstraints(),
                  ),
                ],
              ),
            ),
            // Category filter sliding bar
            if (_categories.isNotEmpty)
              Container(
                color: const Color(0xFF313131),
                height: 48,
                child: ListView.builder(
                  scrollDirection: Axis.horizontal,
                  itemCount: _categories.length + 1, // +1 for "All"
                  itemBuilder: (context, i) {
                    final isAllCategories = i == 0;
                    final category = isAllCategories ? null : _categories[i - 1];
                    final label = isAllCategories ? 'All' : category!;
                    final isSelected = _selectedCategory == category;

                    return InkWell(
                      onTap: () => _onCategoryChanged(category),
                      child: Container(
                        padding: const EdgeInsets.symmetric(horizontal: 16, vertical: 8),
                        decoration: BoxDecoration(
                          border: Border(
                            bottom: BorderSide(
                              color: isSelected
                                  ? Colors.cyan
                                  : Colors.transparent,
                              width: 3,
                            ),
                          ),
                          color: isSelected
                              ? const Color(0xFF444444)
                              : const Color(0xFF313131),
                        ),
                        child: Center(
                          child: Text(
                            label,
                            style: TextStyle(
                              color: isSelected
                                  ? Colors.cyan
                                  : Colors.white70,
                              fontSize: 14,
                              fontWeight: FontWeight.w600,
                            ),
                          ),
                        ),
                      ),
                    );
                  },
                ),
              ),
            // Grid (swipeable pages)
            Expanded(
              child: Padding(
                padding: const EdgeInsets.all(8.0),
                child: _buildTalkgroupGrid(),
              ),
            ),
            // Page indicator
            if (_talkgroupPages.length > 1)
              Container(
                padding: const EdgeInsets.symmetric(vertical: 8),
                child: Row(
                  mainAxisAlignment: MainAxisAlignment.center,
                  children: [
                    Text(
                      'Page ${_currentPage + 1} of ${_talkgroupPages.length}',
                      style: TextStyle(
                        color: Colors.grey[400],
                        fontSize: 12,
                      ),
                    ),
                  ],
                ),
              ),
          ],
        ),
      ),
    );
  }
}
