DSD-NEO Portable for macOS
==========================

Contents
- bin/dsd-neo              : The decoder executable
- lib/*.dylib              : Bundled runtime library dependencies (when available)
- dsd-neo.sh           : Launcher that sets DYLD paths and runs dsd-neo

Usage
1) Open the DMG (double‑click the .dmg file).
2) Drag the “dsd-neo-macos” folder to a location on your disk (e.g., ~/Applications/dsd-neo).
3) Eject the DMG.
4) From Terminal, run inside the copied folder:
   ./dsd-neo.sh -h

Notes
- The launcher sets DYLD_FALLBACK_LIBRARY_PATH to use bundled libs.
- PulseAudio is required for audio backends used by dsd-neo. If you do not
  see audio devices, install and start PulseAudio via Homebrew:
    brew install pulseaudio
    brew services start pulseaudio
- RTL-SDR support requires appropriate permissions and drivers; consult the
  RTL-SDR project documentation for macOS specifics.

- Gatekeeper/Quarantine: macOS may warn about running binaries from the Internet.
  You can right‑click → Open the first time, or remove quarantine attributes:
    xattr -dr com.apple.quarantine dsd-neo-macos

Examples
- Show help:
  ./dsd-neo.sh -h

- Use a WAV file as input:
  ./dsd-neo.sh -i sample.wav

Licensing
This distribution includes third-party components under their respective
licenses. dsd-neo is GPL-3.0-or-later. For source and license details, visit:
- dsd-neo:    https://github.com/arancormonk/dsd-neo
- mbelib-neo: https://github.com/arancormonk/mbelib-neo
