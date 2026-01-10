import 'package:flutter/material.dart';

class LogScreen extends StatelessWidget {
  final List<String> logLines;
  final ScrollController scrollController;

  const LogScreen({
    super.key,
    required this.logLines,
    required this.scrollController,
  });

  Color _getLineColor(String line) {
    if (line.contains('P25') || line.contains('TSBK')) {
      return Colors.cyan;
    } else if (line.contains('TG:') || line.contains('talkgroup')) {
      return Colors.yellow;
    } else if (line.contains('Error') || line.contains('error')) {
      return Colors.red;
    } else if (line.contains('SPS hunt')) {
      return Colors.grey;
    }
    return Colors.green;
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('DSD Log'),
        backgroundColor: Colors.blueGrey[900],
        actions: [
          IconButton(
            icon: const Icon(Icons.vertical_align_bottom),
            onPressed: () {
              if (scrollController.hasClients) {
                scrollController.animateTo(
                  scrollController.position.maxScrollExtent,
                  duration: const Duration(milliseconds: 300),
                  curve: Curves.easeOut,
                );
              }
            },
            tooltip: 'Jump to bottom',
          ),
        ],
      ),
      body: Container(
        color: Colors.black,
        padding: const EdgeInsets.all(8),
        child: logLines.isEmpty
            ? Center(
                child: Text(
                  'No log output yet. Start the scanner to see activity.',
                  style: TextStyle(
                    color: Colors.grey[600],
                    fontSize: 14,
                  ),
                ),
              )
            : ListView.builder(
                controller: scrollController,
                itemCount: logLines.length,
                itemBuilder: (context, index) {
                  final line = logLines[index];
                  return SelectableText(
                    line,
                    style: TextStyle(
                      fontFamily: 'monospace',
                      fontSize: 11,
                      color: _getLineColor(line),
                      height: 1.3,
                    ),
                  );
                },
              ),
      ),
    );
  }
}
