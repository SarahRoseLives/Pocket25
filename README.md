# Pocket25 - Mobile P25 Digital Radio Decoder

**Pocket25** is an Android application designed primarily for decoding **APCO Project 25 (P25)** trunked and conventional radio systems. Built on the powerful [DSD-Neo](https://github.com/arancormonk/dsd-neo) decoder engine, it brings professional-grade digital radio monitoring to your mobile device.

Download an APK here: https://pocket25.com

## 🎯 Primary Focus: P25 Systems

This application was built with P25 in mind and provides full UI support for:

- **P25 Phase 1** (C4FM modulation) - ✅ Audio working perfectly
- **P25 Phase 2** (TDMA/QPSK modulation) - ⚠️ Audio is choppy (upstream DSD-Neo limitation, see `P25_PHASE2_AUDIO_ISSUE.md`)
- **Trunked System Following** - Automatically follows voice traffic across control channels
- **Conventional Monitoring** - Manual frequency configuration
- **RadioReference Import** - Easy system configuration from RadioReference.com
- **Talkgroup Filtering** - Whitelist/Blacklist support for selective monitoring
- **Real-time Call Display** - Talkgroup, Radio ID, NAC, encryption status, emergency flags
- **Site Details** - WACN, System ID, Site ID, RFSS ID tracking
- **Native USB RTL-SDR Support** - Direct USB dongle support (no root required)
- **Remote RTL_TCP** - Connect to network-based RTL-SDR servers

## 🔧 Based on DSD-Neo Engine

Under the hood, Pocket25 uses the full DSD-Neo decoder, which means it **technically supports** many more digital voice protocols:

### Protocols Supported by DSD-Neo:
- ✅ **P25 Phase 1** (Full UI support, audio working perfectly)
- ⚠️ **P25 Phase 2** (Full UI support, audio choppy - see `P25_PHASE2_AUDIO_ISSUE.md`)
- ❓ **DMR** (Tier I/II/III) - Untested, UI support limited
- ❓ **NXDN** (NXDN48/96) - Untested, UI support limited
- ❓ **D-STAR** - Untested, UI support limited
- ❓ **YSF (Yaesu System Fusion)** - Untested, UI support limited
- ❓ **dPMR** - Untested, UI support limited
- ❓ **X2-TDMA** - Untested, UI support limited
- ❓ **ProVoice (EDACS)** - Untested, UI support limited
- ❓ **M17** - Untested, UI support limited

**Important Note:** While DSD-Neo will decode these protocols and you'll hear audio, the UI currently displays call information in a P25-centric format. Non-P25 systems may show incomplete or incorrect metadata in the interface.

## 📡 RTL-SDR Support

Pocket25 supports two methods for RTL-SDR:

### 1. Native USB (Recommended)
- Direct connection via USB OTG
- No root required
- Lower latency
- Better performance

### 2. Remote RTL_TCP
- Connect to RTL-SDR over network
- Useful for remote monitoring
- Works with existing rtl_tcp servers

## 🎬 Need Help: Sample Recordings Wanted!

### I need your help to improve multi-protocol support!

To properly implement UI support for DMR, NXDN, D-STAR, YSF, dPMR, and other protocols, **I need sample recordings** of these systems in action.

#### How You Can Help:

Use **[rtl_tcp_echo](https://github.com/SarahRoseLives/rtl_tcp_echo)** to capture IQ samples:

```bash
# rtl_tcp_echo is a middleman application that:
# 1. Sits between DSD-Neo and RTL_TCP
# 2. Captures raw IQ samples to a .bin file
# 3. Allows perfect playback for development/testing

# Do the following:
# 1. rtl_tcp -a 0.0.0.0
# 2. rtl_tcp_echo -listen 0.0.0.0:1235 -record iq_recording.bin
# 3. Run DSD-Neo as RTL_TCP port 1235
```

**What I'm looking for:**
- ✅ **DMR** systems (Tier I, II, or III with trunking)
- ✅ **NXDN** systems (NXDN48 or NXDN96)
- ✅ **D-STAR** repeaters/conventional
- ✅ **YSF (C4FM)** repeaters/conventional
- ✅ **dPMR** systems
- ✅ **ProVoice/EDACS** systems
- ✅ **M17** conventional

**What makes a good sample:**
- Contains actual voice traffic (not just idle/control)
- At least 60-90 seconds of activity
- Clear signal (minimal static/interference)
- Include system details known (frequency, system ID, etc.)

**Where to send samples:**
- Open an issue on GitHub with a link to your recording
- Include: Protocol type, frequency, location (general area), any known system details

With your samples, I can build proper UI support for all DSD-Neo protocols!

## 🚀 Features

### Current Features (P25):
- ✅ Real-time P25 Phase 1 decoding (audio working perfectly)
- ⚠️ P25 Phase 2 decoding (audio is choppy - upstream DSD-Neo limitation)
- ✅ Trunked system following with automatic VC tracking
- ✅ RadioReference.com system import
- ✅ Talkgroup whitelist/blacklist filtering
- ✅ Manual frequency configuration
- ✅ Call history and activity log
- ✅ Site detail monitoring (WACN/SysID/Site/RFSS)
- ✅ Emergency call detection
- ✅ Encryption status indication
- ✅ Dual timeslot support (P25 Phase 2)
- ✅ Native USB RTL-SDR support
- ✅ Remote RTL_TCP support

### Planned Features:
- 🔄 Full DMR UI support (talkgroups, color codes, talker alias)
- 🔄 NXDN UI support (call types, RAN, radio IDs)
- 🔄 D-STAR UI support (callsigns, routing info)
- 🔄 YSF UI support
- 🔄 Conventional scanner mode with frequency stepping
- 🔄 Encryption key loading
- 🔄 Per-call recording
- 🔄 GPS/Location decoding (LRRP)

## 📋 Requirements

- **Android 8.0+** (API 26+)
- **RTL-SDR compatible dongle** (RTL2832U chipset)
  - R820T/R820T2 tuner recommended
  - USB OTG cable/adapter for direct connection
- **Or:** Access to an RTL_TCP server on your network

### Supported RTL-SDR Dongles:
- NooElec NESDR series
- RTL-SDR Blog V3/V4
- Generic RTL2832U dongles
- Any rtl_tcp compatible source

## 🔧 Installation

1. Download the latest APK from https://sarahsforge.dev/products/Pocket25
2. Enable "Install from Unknown Sources" on your Android device
3. Install the APK
4. Grant USB permissions when prompted (for native USB mode)

## 📖 Usage

### Quick Start (P25 Trunked System):

1. **Import from RadioReference:**
   - Tap "Import from RadioReference"
   - Search for your system
   - Select and import

2. **Connect RTL-SDR:**
   - Native USB: Connect dongle, grant permission
   - Remote: Configure host/port in Manual Configuration

3. **Start Scanning:**
   - Tap "Start" to begin monitoring
   - Application will automatically follow voice traffic

### Manual Configuration (Conventional):

1. Navigate to "Manual Configuration"
2. Enter frequency in MHz (e.g., 771.18125)
3. Configure gain and PPM correction
4. Tap "Apply & Connect"
5. Tap "Start"

### Talkgroup Filtering:

- Long-press any talkgroup in the call history
- Choose "Mute" to blacklist (ignore)
- Use Settings to manage whitelist mode

## 🛠️ Building from Source

### Prerequisites:
- Flutter 3.10+
- Android NDK r26+
- CMake 3.22+

### Build Steps:

```bash
# Clone repository
git clone https://github.com/SarahRoseLives/Pocket25.git
cd Pocket25

# Get dependencies
flutter pub get
cd example
flutter pub get

# Build APK
flutter build apk --release
```

## 🤝 Contributing

Contributions are welcome! Areas where help is needed:

1. **Sample Recordings** - See "Need Help" section above
2. **Protocol UI Implementation** - DMR, NXDN, D-STAR display logic
3. **Feature Development** - Conventional scanner, squelch, recording
4. **Testing** - Bug reports and feature requests
5. **Documentation** - Usage guides, protocol information

## 📄 License

This project includes:
- **Pocket25 App Code:** GPL-3.0
- **DSD-Neo:** GPL-3.0
- **mbelib-neo:** GPL-3.0
- **librtlsdr-android:** GPL-2.0

## 🙏 Credits

- **DSD-Neo** by [arancormonk](https://github.com/arancormonk/dsd-fme)
- **mbelib** - AMBE/IMBE vocoder implementation
- **librtlsdr** - RTL-SDR driver library
- **RadioReference.com** - System database
- **Copilot** - Compiling DSD into an Android library and Flutter integration

## 📞 Contact

- **GitHub Issues:** [Report bugs or request features](../../issues)
- **Sample Submissions:** Open an issue with recording details

## ⚠️ Disclaimer

This software is intended for authorized monitoring only. Users are responsible for ensuring compliance with all applicable laws and regulations regarding radio monitoring in their jurisdiction.
