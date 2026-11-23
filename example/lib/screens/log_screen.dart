import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

class LogScreen extends StatelessWidget {
  final List<String> logLines;
  final ScrollController scrollController;

  const LogScreen({
    super.key,
    required this.logLines,
    required this.scrollController,
  });

  Color _getLineColor(String line) {
    if (line.contains('P25') || line.contains('BER')) {
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

  static const MethodChannel _filesChannel = MethodChannel('pocket25/files');

  /// Save the log as Download/Pocket25/pocket25-log-YYYYMMDD-HHMMSS.txt
  Future<void> _saveToDownloads(BuildContext context) async {
    final t = DateTime.now();
    String two(int v) => v.toString().padLeft(2, '0');
    final name = 'pocket25-log-${t.year}${two(t.month)}${two(t.day)}-'
        '${two(t.hour)}${two(t.minute)}${two(t.second)}.txt';
    String message;
    try {
      final path = await _filesChannel.invokeMethod<String>(
          'saveTextToDownloads', {'fileName': name, 'text': logLines.join('\n')});
      message = 'Saved ${logLines.length} lines to ${path ?? name}';
    } on PlatformException catch (e) {
      message = 'Save failed: ${e.message ?? e.code}';
    } catch (e) {
      message = 'Save failed: $e';
    }
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(content: Text(message), duration: const Duration(seconds: 3)),
      );
    }
  }

  /// Copy the whole log so it can be pasted into an email, note, or bug report.
  Future<void> _copyAll(BuildContext context) async {
    await Clipboard.setData(ClipboardData(text: logLines.join('\n')));
    if (context.mounted) {
      ScaffoldMessenger.of(context).showSnackBar(
        SnackBar(
          content: Text('Copied ${logLines.length} log lines'),
          duration: const Duration(seconds: 2),
        ),
      );
    }
  }

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('DSD Log'),
        backgroundColor: Colors.blueGrey[900],
        actions: [
          IconButton(
            tooltip: 'Save to Downloads',
            icon: const Icon(Icons.save_alt),
            onPressed: logLines.isEmpty ? null : () => _saveToDownloads(context),
          ),
          IconButton(
            tooltip: 'Copy all',
            icon: const Icon(Icons.copy_all),
            onPressed: logLines.isEmpty ? null : () => _copyAll(context),
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
