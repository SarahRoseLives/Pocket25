import 'package:flutter/material.dart';
import '../models/site_details.dart';
import '../services/scanning_service.dart';

class SiteDetailsScreen extends StatelessWidget {
  final SiteDetails? siteDetails;
  final ScanningService? scanningService;
  
  const SiteDetailsScreen({
    super.key,
    this.siteDetails,
    this.scanningService,
  });

  @override
  Widget build(BuildContext context) {
    return Scaffold(
      appBar: AppBar(
        title: const Text('Site Details'),
        backgroundColor: Theme.of(context).colorScheme.inversePrimary,
      ),
      body: siteDetails == null
          ? const Center(
              child: Column(
                mainAxisAlignment: MainAxisAlignment.center,
                children: [
                  Icon(Icons.cell_tower, size: 64, color: Colors.grey),
                  SizedBox(height: 16),
                  Text(
                    'Waiting for site information...',
                    style: TextStyle(fontSize: 16, color: Colors.grey),
                  ),
                  SizedBox(height: 8),
                  Text(
                    'Site details will appear once the scanner\nreceives system identification data',
                    textAlign: TextAlign.center,
                    style: TextStyle(fontSize: 12, color: Colors.grey),
                  ),
                ],
              ),
            )
          : SingleChildScrollView(
              padding: const EdgeInsets.all(16),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  _buildInfoCard(
                    'System Information',
                    Icons.hub,
                    Colors.blue,
                    [
                      _buildInfoRow('WACN', siteDetails!.wacnHex,
                          '${siteDetails!.wacn}'),
                      _buildInfoRow('System ID', siteDetails!.systemIdHex,
                          '${siteDetails!.systemId}'),
                      _buildInfoRow('NAC', siteDetails!.nacHex,
                          '${siteDetails!.nac}'),
                    ],
                  ),
                  const SizedBox(height: 16),
                  _buildInfoCard(
                    'Site Information',
                    Icons.cell_tower,
                    Colors.green,
                    [
                      _buildInfoRow('Site ID', siteDetails!.siteIdHex,
                          '${siteDetails!.siteId}'),
                      _buildInfoRow('RFSS ID', siteDetails!.rfssIdHex,
                          '${siteDetails!.rfssId}'),
                      if (scanningService?.downlinkFreq != null)
                        _buildInfoRow('Downlink', 
                          '${scanningService!.downlinkFreq!.toStringAsFixed(6)} MHz', ''),
                      if (scanningService?.uplinkFreq != null)
                        _buildInfoRow('Uplink', 
                          '${scanningService!.uplinkFreq!.toStringAsFixed(6)} MHz', ''),
                      if (scanningService != null && scanningService!.neighborFreqs.isNotEmpty)
                        _buildNeighborSitesSection(scanningService!.neighborFreqs),
                    ],
                  ),
                  const SizedBox(height: 16),
                  _buildInfoCard(
                    'Status',
                    Icons.info_outline,
                    Colors.orange,
                    [
                      _buildInfoRow('Last Updated', siteDetails!.timeDisplay, ''),
                      _buildInfoRow('Status', 'Active', ''),
                    ],
                  ),
                  const SizedBox(height: 24),
                  _buildLegend(),
                ],
              ),
            ),
    );
  }

  Widget _buildInfoCard(
      String title, IconData icon, Color color, List<Widget> children) {
    return Card(
      elevation: 2,
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            Row(
              children: [
                Icon(icon, color: color, size: 24),
                const SizedBox(width: 8),
                Text(
                  title,
                  style: const TextStyle(
                    fontSize: 18,
                    fontWeight: FontWeight.bold,
                  ),
                ),
              ],
            ),
            const Divider(height: 24),
            ...children,
          ],
        ),
      ),
    );
  }

  Widget _buildInfoRow(String label, String hexValue, String decValue) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          Text(
            label,
            style: const TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w500,
            ),
          ),
          Column(
            crossAxisAlignment: CrossAxisAlignment.end,
            children: [
              Text(
                hexValue,
                style: const TextStyle(
                  fontSize: 16,
                  fontFamily: 'monospace',
                  fontWeight: FontWeight.bold,
                ),
              ),
              if (decValue.isNotEmpty)
                Text(
                  decValue,
                  style: const TextStyle(
                    fontSize: 12,
                    color: Colors.grey,
                    fontFamily: 'monospace',
                  ),
                ),
            ],
          ),
        ],
      ),
    );
  }

  Widget _buildNeighborSitesSection(List<int> neighborFreqs) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 8),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          Text(
            'Neighbor Sites (${neighborFreqs.length})',
            style: const TextStyle(
              fontSize: 16,
              fontWeight: FontWeight.w500,
            ),
          ),
          const SizedBox(height: 8),
          ...neighborFreqs.asMap().entries.map((entry) {
            final index = entry.key;
            final freqHz = entry.value;
            final freqMhz = freqHz / 1000000.0;
            
            return Padding(
              padding: const EdgeInsets.only(bottom: 8),
              child: Container(
                padding: const EdgeInsets.all(12),
                decoration: BoxDecoration(
                  color: Colors.grey[800],
                  borderRadius: BorderRadius.circular(8),
                  border: Border.all(color: Colors.grey[700]!),
                ),
                child: Row(
                  children: [
                    Container(
                      padding: const EdgeInsets.symmetric(horizontal: 8, vertical: 4),
                      decoration: BoxDecoration(
                        color: Colors.grey[600],
                        borderRadius: BorderRadius.circular(4),
                      ),
                      child: Text(
                        'Site ${index + 1}',
                        style: const TextStyle(
                          fontSize: 13,
                          fontWeight: FontWeight.bold,
                          color: Colors.white,
                        ),
                      ),
                    ),
                    const SizedBox(width: 12),
                    const Icon(Icons.cell_tower, color: Colors.white70, size: 16),
                    const SizedBox(width: 8),
                    Text(
                      '${freqMhz.toStringAsFixed(6)} MHz',
                      style: const TextStyle(
                        fontSize: 14,
                        fontFamily: 'monospace',
                        color: Colors.white,
                        fontWeight: FontWeight.bold,
                      ),
                    ),
                  ],
                ),
              ),
            );
          }),
        ],
      ),
    );
  }

  Widget _buildLegend() {
    return Card(
      elevation: 1,
      color: Colors.grey[800],
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Column(
          crossAxisAlignment: CrossAxisAlignment.start,
          children: [
            const Text(
              'Field Descriptions',
              style: TextStyle(
                fontSize: 16,
                fontWeight: FontWeight.bold,
                color: Colors.white,
              ),
            ),
            const SizedBox(height: 12),
            _buildLegendItem('WACN', 'Wide Area Communications Network identifier'),
            _buildLegendItem('System ID', 'System identifier within the WACN'),
            _buildLegendItem('NAC', 'Network Access Code for this system'),
            _buildLegendItem('Site ID', 'Unique identifier for this site'),
            _buildLegendItem('RFSS ID', 'Radio Frequency Subsystem identifier'),
          ],
        ),
      ),
    );
  }

  Widget _buildLegendItem(String term, String description) {
    return Padding(
      padding: const EdgeInsets.symmetric(vertical: 4),
      child: Row(
        crossAxisAlignment: CrossAxisAlignment.start,
        children: [
          SizedBox(
            width: 80,
            child: Text(
              term,
              style: const TextStyle(
                fontWeight: FontWeight.w600,
                fontSize: 13,
                color: Colors.white,
              ),
            ),
          ),
          Expanded(
            child: Text(
              description,
              style: const TextStyle(
                fontSize: 13,
                color: Colors.white70,
              ),
            ),
          ),
        ],
      ),
    );
  }
}
