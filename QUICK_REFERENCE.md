# Pocket25 Quick Reference

## Project Structure

### Main Application
```
example/lib/
├── main.dart                      # App entry & navigation
│   ├── Pocket25App               # MaterialApp setup
│   ├── MainScreen                # Bottom nav container
│   └── _MainScreenState          # State management
│
├── models/
│   └── scanner_activity.dart     # Talkgroup data model
│       └── ScannerActivity       # TG, source, timestamp, status
│
├── services/
│   ├── log_parser.dart           # Parse DSD logs
│   │   └── LogParser             # Extract TG/source from text
│   └── settings_service.dart     # Manage settings
│       └── SettingsService       # Host, port, freq, audio
│
└── screens/
    ├── scanner_screen.dart       # Tab 0: Live TG activity
    ├── log_screen.dart           # Tab 1: Raw DSD output
    └── settings_screen.dart      # Tab 2: Config & controls
```

### Plugin (DSD-Neo Integration)
```
lib/
├── dsd_flutter.dart                    # Main API
├── dsd_flutter_platform_interface.dart # Platform abstraction
└── dsd_flutter_method_channel.dart     # Method channel impl
```

## Key Components

### Main App Flow
1. `main()` → `Pocket25App` → `MainScreen`
2. Bottom nav switches between 3 screens
3. All screens share same DSD plugin instance
4. Log stream parsed in real-time

### Scanner Screen
**Purpose**: Live talkgroup monitoring
**Features**:
- Active TG list with timing
- Source ID display
- Auto-timeout (30s)
- Status indicator
**State**: Receives `activities` list from parent

### Log Screen
**Purpose**: Raw DSD output debugging
**Features**:
- Color-coded text
- Selectable lines
- Auto-scroll
- Jump to bottom
**State**: Receives `logLines` list from parent

### Settings Screen
**Purpose**: Configuration & control
**Features**:
- RTL-TCP settings
- Audio toggle
- Start/Stop buttons
- Configuration apply
**State**: Receives callbacks and settings service

## Data Flow

```
DSD Plugin (Native)
    ↓ Stream<String>
Main Screen State
    ↓ Parse + Store
    ├→ logLines (List<String>)
    └→ activities (List<ScannerActivity>)
        ↓ Pass to Screens
    ┌─────┴─────┬─────┐
Scanner     Log    Settings
```

## Color Coding (Logs)

| Pattern | Color | Meaning |
|---------|-------|---------|
| P25, TSBK | Cyan | P25 protocol messages |
| TG:, talkgroup | Yellow | Talkgroup activity |
| Error, error | Red | Error messages |
| SPS hunt | Grey | Signal hunting |
| Default | Green | Normal output |

## Activity Timeout Logic

- Talkgroup detected → marked active
- Last activity stored in `_lastTalkgroupActivity` map
- Timer checks every 5 seconds
- If no activity for 30s → marked inactive
- Inactive TGs fade from scanner screen

## Settings Service

Manages user preferences:
```dart
- host: String (RTL-TCP IP)
- port: int (RTL-TCP port)
- frequency: double (MHz)
- audioEnabled: bool
```

Notifies listeners on changes (ChangeNotifier pattern)

## Log Parser Regex

```dart
Talkgroup: r'TG:?\s*(\d+)'
Source:    r'(SRC|Source):?\s*(\d+)'
```

## Commands

### Development
```bash
cd example
flutter pub get          # Install dependencies
flutter analyze          # Check code
flutter test             # Run tests
flutter run              # Run on device
```

### Building
```bash
flutter build apk        # Android APK
flutter build appbundle  # Android App Bundle
flutter build linux      # Linux desktop
```

## Configuration Example

**RTL-TCP Server Setup** (on Raspberry Pi or Linux):
```bash
rtl_tcp -a 0.0.0.0 -p 1234 -f 771181250
```

**App Settings**:
- Host: `192.168.1.240` (Pi's IP)
- Port: `1234`
- Frequency: `771.18125` (MHz)

## Navigation Map

```
┌─────────────────────────────────┐
│     Pocket25 Main Screen        │
│  ┌───────────────────────────┐  │
│  │                           │  │
│  │   Current Screen Content  │  │
│  │                           │  │
│  └───────────────────────────┘  │
│  ┌───────────────────────────┐  │
│  │ 📻 Scanner │ 📝 Log │⚙️ Set │  │
│  └───────────────────────────┘  │
└─────────────────────────────────┘
```

## Theme Colors

```dart
Primary: Colors.cyan
Secondary: Colors.blue
Surface: Colors.blueGrey[900]
AppBar: Colors.blueGrey[900]
```

## Future Enhancement Ideas

- [ ] Save/load talkgroup names
- [ ] Multiple frequency profiles
- [ ] Audio recording
- [ ] System/NAC filtering
- [ ] Signal strength display
- [ ] Export logs to file
- [ ] Widget/notification support
- [ ] Dark/light theme toggle
