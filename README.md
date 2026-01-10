# Pocket25

A P25 Phase 1 scanner app built with Flutter and DSD-Neo.

## Features

- **Scanner Screen**: View active talkgroups and their activity in real-time
- **Log Screen**: Monitor raw DSD-Neo decoder output with color-coded messages
- **Settings Screen**: Configure RTL-TCP connection and scanner parameters
- **Real-time Parsing**: Automatically extracts talkgroup information from DSD logs

## Requirements

- RTL-SDR device (or compatible SDR)
- RTL-TCP server running on network
- P25 Phase 1 signal source

## Getting Started

### Installation

1. Clone the repository
2. Navigate to the example directory: `cd example`
3. Install dependencies: `flutter pub get`
4. Run the app: `flutter run`

### Configuration

1. Open the **Settings** tab
2. Enter your RTL-TCP server details:
   - **Host**: IP address of RTL-TCP server (e.g., 192.168.1.240)
   - **Port**: RTL-TCP port (default: 1234)
   - **Frequency**: P25 frequency in MHz (e.g., 771.18125)
3. Tap **Apply Configuration**
4. Toggle audio output if desired
5. Tap **Start Scanner** to begin

### Usage

- **Scanner Tab**: Shows active talkgroups with source IDs and timing
- **Log Tab**: Raw DSD output for debugging and detailed analysis
- **Settings Tab**: Configure and control the scanner

## Architecture

- **Flutter Plugin**: `dsd_flutter` provides native DSD-Neo integration
- **Platform Support**: Android and Linux (native code in Java/C++)
- **Streaming Output**: Real-time log parsing and talkgroup detection

## Development

This is a Flutter plugin project with:
- `/lib`: Plugin Dart API
- `/example`: Pocket25 scanner app
- `/android`: Android native implementation
- `/linux`: Linux native implementation

## License

See LICENSE file for details.

