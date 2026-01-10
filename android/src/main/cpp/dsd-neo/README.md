# DSD-neo

A modular and performance‑enhanced version of the well-known Digital Speech Decoder (DSD) with a modern CMake build, split into focused libraries (`runtime`, `platform`, `dsp`, `io`, `engine`, `fec`, `crypto`, `protocol`, `core`, `ui`) and a thin CLI.

Project homepage: https://github.com/arancormonk/dsd-neo

[![linux-ci](https://github.com/arancormonk/dsd-neo/actions/workflows/linux-ci.yaml/badge.svg)](https://github.com/arancormonk/dsd-neo/actions/workflows/linux-ci.yaml)
[![windows-ci](https://github.com/arancormonk/dsd-neo/actions/workflows/windows-ci.yaml/badge.svg)](https://github.com/arancormonk/dsd-neo/actions/workflows/windows-ci.yaml)
[![macos-ci](https://github.com/arancormonk/dsd-neo/actions/workflows/macos-ci.yaml/badge.svg)](https://github.com/arancormonk/dsd-neo/actions/workflows/macos-ci.yaml)

![DSD-neo](images/dsd-neo_const_view.png)

## Downloads

- Stable releases (**TBD**):
  - Linux AppImage (x86_64): dsd-neo-linux-x86_64-portable-<version>.AppImage
  - Linux AppImage (aarch64): dsd-neo-linux-aarch64-portable-<version>.AppImage
  - macOS DMG (arm64): dsd-neo-macos-arm64-portable-<version>.dmg
  - Windows native ZIP (MSVC x86_64, **recommended**): dsd-neo-msvc-x86_64-native-<version>.zip
  - Windows native ZIP (MinGW x86_64, alternative): dsd-neo-mingw-x86_64-native-<version>.zip
- Nightly builds:
  - Linux AppImage (x86_64): [dsd-neo-linux-x86_64-portable-nightly.AppImage](https://github.com/arancormonk/dsd-neo/releases/download/nightly/dsd-neo-linux-x86_64-portable-nightly.AppImage)
  - Linux AppImage (aarch64): [dsd-neo-linux-aarch64-portable-nightly.AppImage](https://github.com/arancormonk/dsd-neo/releases/download/nightly/dsd-neo-linux-aarch64-portable-nightly.AppImage)
  - macOS DMG (arm64): [dsd-neo-macos-arm64-portable-nightly.dmg](https://github.com/arancormonk/dsd-neo/releases/download/nightly/dsd-neo-macos-arm64-portable-nightly.dmg)
  - Windows native ZIP (MSVC x86_64, **recommended**): [dsd-neo-msvc-x86_64-native-nightly.zip](https://github.com/arancormonk/dsd-neo/releases/download/nightly/dsd-neo-msvc-x86_64-native-nightly.zip)
  - Windows native ZIP (MinGW x86_64, alternative): [dsd-neo-mingw-x86_64-native-nightly.zip](https://github.com/arancormonk/dsd-neo/releases/download/nightly/dsd-neo-mingw-x86_64-native-nightly.zip)
- Arch Linux (AUR): [dsd-neo-git](https://aur.archlinux.org/packages/dsd-neo-git)

On Windows, the native MSVC ZIP is the preferred download for best integration with the Windows console and audio stack. The MinGW ZIP is a fully native alternative.

## Project Status

This project is an active work in progress as we decouple from the upstream fork and continue modularization. Expect breaking changes to build presets, options, CLI flags, and internal library boundaries while this stabilization work proceeds. The main branch may be volatile; for deployments, prefer building a known commit. Issues and PRs are welcome—please include logs and reproduction details when reporting regressions.

## Overview

- A performance‑enhanced fork of [lwvmobile/dsd-fme](https://github.com/lwvmobile/dsd-fme), which is a fork of [szechyjs/dsd](https://github.com/szechyjs/dsd)
- Modularized fork with clear boundaries: `runtime`, `platform`, `dsp`, `io`, `engine`, `fec`, `crypto`, `protocol`, `core`, plus `ui` and a CLI app.
- Protocol coverage: DMR, dPMR, D‑STAR, NXDN, P25 Phase 1/2, X2‑TDMA, EDACS, ProVoice, M17, YSF.
- Requires [arancormonk/mbelib-neo](https://github.com/arancormonk/mbelib-neo) for IMBE/AMBE vocoder primitives.
- Public headers live under `include/dsd-neo/...` and are included as `#include <dsd-neo/<module>/<header>>`.

## How DSD‑neo Is Different

- More input and streaming options

  - Direct RTL‑SDR USB, plus RTL‑TCP (`-i rtltcp[:host:port]`) and generic IQ TCP (`-i tcp[:host:port]`, SDR++/GRC 7355).
  - UDP audio in/out: receive PCM16 over UDP as an input, and send decoded audio to UDP sinks for easy piping to other apps or hosts.
  - M17 UDP/IP in/out: dedicated M17 frame input/output over UDP (`-i m17udp[:bind:17000]`, `-o m17udp[:host:17000]`).

- Built‑in trunking workflow

  - Follow P25 and DMR trunked voice automatically using channel maps and group lists (`-C ...csv`, `-G group.csv`, `-T`, `-N`).
  - On‑the‑fly control via UDP retune or rigctl; pairs well with TCP/RTL‑TCP inputs.

- RTL‑SDR quality‑of‑life features

  - Bias‑tee control (when supported by your librtlsdr), manual or auto gain, power squelch, adjustable tuner bandwidth, and per‑run PPM correction.
  - Optional spectrum‑based auto‑PPM drift correction with SNR/power gating and short training/lock, for long unattended runs.
  - rtl_tcp niceties: configurable prebuffering to reduce dropouts and settings tuned for stable network use.

- RTL‑SDR optimizations and diagnostics

  - Real‑time visual aids in the terminal for faster setup and troubleshooting:
    - Constellation view with adjustable gating and normalization.
    - Eye diagram (Unicode/ASCII, optional color) with adaptive scales and level guides.
    - Spectrum analyzer with adjustable FFT size.
    - FSK 4‑level histogram and live per‑modulation SNR readouts.
  - Heavily optimized RTL path for smoother audio and fewer dropouts:
    - One‑pass byte→I/Q widening with optional 90° rotation and DC‑spur fs/4 capture shift (configurable).
    - Cascaded decimation and an optional rational resampler to keep processing efficient and responsive.
    - Optional auto‑PPM correction from the timing error detector for long unattended runs.
  - Device control from the UI: toggle bias‑tee, switch AGC/manual gain, adjust bandwidth and squelch, and retune quickly.

- M17 encode tooling

  - Generate M17 signals for test/airgap workflows: stream voice (`-fZ`), packet (`-fP`), and BERT (`-fB`) encoders.

- Expanded DSP controls for power users

  - Changes apply instantly from the UI and persist across retunes.
  - See [docs/cli.md](docs/cli.md) for environment variable reference.

- Portable, ready‑to‑run builds
  - Linux AppImage, macOS DMG, and Windows portable ZIP releases.

### How this compares at a glance

- Versus DSD‑FME: similar protocol coverage and UI heritage, but DSD‑neo adds network‑friendly I/O (UDP audio in), refined RTL‑TCP handling (prebuffer, tuned defaults), optional auto‑PPM, and packaged cross‑platform binaries.
- Versus the original DSD: more protocols (notably P25 Phase 2, M17, YSF, EDACS), built‑in trunking, network inputs, device control, and an interactive UI.

## Build From Source

Requirements

- C compiler with C11 and C++ compiler with C++14 support.
- CMake ≥ 3.20.
- Dependencies:
  - Required: libsndfile; a curses backend (ncursesw/PDCurses); and an audio backend (PulseAudio by default, PortAudio on Windows).
  - Optional: librtlsdr (RTL‑SDR support), Codec2 (additional vocoder paths), help2man (man page generation).
  - Vocoder: mbelib-neo (`mbe-neo` CMake package) is required.

OS package hints

- Ubuntu/Debian (apt):
  - `sudo apt-get update && sudo apt-get install -y build-essential cmake ninja-build libsndfile1-dev libpulse-dev libncurses-dev librtlsdr-dev`
- macOS (Homebrew):
  - `brew install cmake ninja libsndfile ncurses pulseaudio librtlsdr codec2`
- Windows:
  - Preferred binary: the native MSVC ZIP. The MinGW ZIP is an alternative native build.
  - Source builds use CMake presets with vcpkg; set `VCPKG_ROOT` and use `win-msvc-*` or `win-mingw-*` presets in `CMakePresets.json`.

Build recipes (copy/paste)

### Linux/macOS — release build (preset `dev-release`, recommended)

```bash
# From the repository root.
#
# OS deps (examples):
# - Ubuntu/Debian: sudo apt-get update && sudo apt-get install -y build-essential cmake ninja-build libsndfile1-dev libpulse-dev libncurses-dev librtlsdr-dev
# - macOS:         brew install cmake ninja libsndfile ncurses pulseaudio librtlsdr codec2
#
# Install is optional; you can run directly from the build tree.

cmake --preset dev-release
cmake --build --preset dev-release -j

# Run (no install required)
build/dev-release/apps/dsd-cli/dsd-neo -h

# Install (optional; pick one)
cmake --install build/dev-release --prefix "$HOME/.local"
# sudo cmake --install build/dev-release
```

### Linux/macOS — debug build + tests (preset `dev-debug`)

```bash
# OS deps (examples):
# - Ubuntu/Debian: sudo apt-get update && sudo apt-get install -y build-essential cmake ninja-build libsndfile1-dev libpulse-dev libncurses-dev librtlsdr-dev
# - macOS:         brew install cmake ninja libsndfile ncurses pulseaudio librtlsdr codec2

cmake --preset dev-debug
cmake --build --preset dev-debug -j
ctest --preset dev-debug -V

# Run (no install required)
build/dev-debug/apps/dsd-cli/dsd-neo -h
```

### Manual configure/build (no presets)

```bash
# Use a build directory that isn't a preset (so you don't expect build/dev-release to exist).
cmake -S . -B build/manual -DCMAKE_BUILD_TYPE=Release
cmake --build build/manual -j

# Run (no install required)
build/manual/apps/dsd-cli/dsd-neo -h

# Install (optional; pick one)
cmake --install build/manual --prefix "$HOME/.local"
# sudo cmake --install build/manual
```

### Coverage (optional)

```bash
tools/coverage.sh  # generates build/coverage-debug/coverage_html
```

Notes

- Presets live in `CMakePresets.json`.
- Presets create out‑of‑source builds under `build/<preset>/`. Run from the repo root.
- The CLI binary outputs to `build/<preset>/apps/dsd-cli/dsd-neo`.
- `cmake --install <build_dir>` only works if you configured that build directory. If you're inside the build directory, use `cmake --install .`.
- If `cmake --install build/dev-release` fails and `build/dev-release/` doesn't exist, you likely did a manual build (install from your actual build dir).

## Install / Uninstall

```bash
# Preset builds (recommended)
# Single-config generators (Unix Makefiles/Ninja):
cmake --install build/dev-release --prefix "$HOME/.local"

# Multi-config generators (Visual Studio/Xcode):
cmake --install build/dev-release --config Release --prefix "$HOME/.local"

# Manual build directory (example above uses `build/manual/`):
cmake --install build/manual --prefix "$HOME/.local"
# cmake --install build --prefix "$HOME/.local"  # if you configured into `build/`

# Uninstall from the same build directory
cmake --build build/dev-release --target uninstall  # preset build
cmake --build build/manual --target uninstall       # manual build directory
# cmake --build build --target uninstall            # if you configured into `build/`
```

## Build Options

These are CMake cache options (set at configure time via `-D...`).

- Build hygiene and optimization:
  - `-DDSD_ENABLE_WARNINGS=ON` — Enable common warnings (default ON).
  - `-DDSD_WARNINGS_AS_ERRORS=ON` — Treat warnings as errors.
  - `-DDSD_ENABLE_FAST_MATH=ON` — Enable fast‑math (`-ffast-math`/`/fp:fast`) across targets.
  - `-DDSD_ENABLE_LTO=ON` — Enable IPO/LTO in Release builds (when supported).
  - `-DDSD_ENABLE_NATIVE=ON` — Enable `-march=native -mtune=native` (non‑portable binaries).
  - `-DDSD_ENABLE_ASAN=ON` — AddressSanitizer in Debug builds.
  - `-DDSD_ENABLE_UBSAN=ON` — UndefinedBehaviorSanitizer in Debug builds.
- Audio backend selection:
  - `-DDSD_USE_PORTAUDIO=ON` — Use PortAudio instead of PulseAudio (default on Windows).
- UI and behavior toggles:
  - `-DCOLORS=OFF` — Disable ncurses color output.
  - `-DCOLORSLOGS=OFF` — Disable colored terminal/log output.
- Protocol and feature knobs:
  - `-DPVC=ON` — Enable ProVoice Conventional Frame Sync.
  - `-DLZ=ON` — Enable LimaZulu‑requested NXDN tweaks.
  - `-DSID=ON` — Enable experimental P25p1 Soft ID decoding.
- Optional features (auto‑detected):
  - RTL‑SDR support is enabled when `librtlsdr` is found.
  - Codec2 support is enabled when `codec2` is found.

## Runtime Tuning

Most users can run with defaults. For advanced tuning, see [docs/cli.md](docs/cli.md).

Common options:

- Auto‑PPM drift correction (RTL‑SDR): `--auto-ppm`
- RTL‑TCP adaptive buffering: `--rtltcp-autotune`
- Rig control (SDR++): `-U 4532` (default port), `-B <Hz>` (bandwidth)

## Using The CLI

- See the friendly CLI guide: [docs/cli.md](docs/cli.md)
  - Or run `dsd-neo -h` for quick usage in your terminal.
  - Digital/analog output gain: `-g <float>` (digital; `0` = auto, `1` ≈ 2%, `50` = 100%) and `-n <float>` (analog 0–100%).
  - DMR mono helpers:
    - Modern form: `-fs -nm` (DMR BS/MS simplex + mono audio).
    - Legacy alias: `-fr` (kept as a shorthand for the same DMR‑mono profile).

Quick examples

- UDP in → Pulse out with UI: `dsd-neo -i udp -o pulse -N`
- DMR trunking from TCP IQ (with rigctl): `dsd-neo -fs -i tcp -U 4532 -T -C dmr_t3_chan.csv -G group.csv -N`

## Configuration

- INI‑style user config is implemented for stable defaults (input/output/mode/trunking); see `docs/config-system.md`.
- Config loading is opt-in: use `--config` to enable (optionally with a path), set `DSD_NEO_CONFIG=<path>`, or pass a single positional `*.ini` path (treated as `--config <path>`).
- Default path (when `--config` is passed without a path): `${XDG_CONFIG_HOME:-$HOME/.config}/dsd-neo/config.ini`.
- `--interactive-setup` forces the bootstrap wizard even when a config exists; `--print-config` dumps the effective config as INI.
- When config is enabled, the final settings are autosaved on exit.

## Tests

- Run all tests: `ctest --preset dev-debug -V` (or `ctest --test-dir build/dev-debug -V`).
- Scope: unit tests cover runtime config parsing/validation, DSP primitives (filters/resampler/demod helpers), and FEC/crypto helpers.

## Documentation

- Module overview and targets are documented in `docs/code_map.md`.

## Project Layout

- Apps: `apps/dsd-cli` — CLI entrypoint, target `dsd-neo`.
- Core: `src/core`, headers `<dsd-neo/core/...>` — glue (audio, vocoder, frame dispatch, GPS, file import).
- Engine: `src/engine`, headers `<dsd-neo/engine/...>` — top-level decode/encode runner and lifecycle.
- Platform: `src/platform`, headers `<dsd-neo/platform/...>` — cross-platform primitives (audio backend, sockets, threading, timing, curses).
- Runtime: `src/runtime`, headers `<dsd-neo/runtime/...>` — config, logging, aligned memory, rings, worker pool, RT scheduling, git version.
- DSP: `src/dsp`, headers `<dsd-neo/dsp/...>` — demod pipeline, resampler, filters, FLL/TED, SIMD helpers.
- IO: `src/io`, headers `<dsd-neo/io/...>` — radio (RTL‑SDR), audio (PulseAudio/PortAudio + UDP PCM input/output), control (UDP/rigctl/serial).
- FEC: `src/fec`, headers `<dsd-neo/fec/...>` — BCH, Golay, Hamming, RS, BPTC, CRC/FCS.
- Crypto: `src/crypto`, headers `<dsd-neo/crypto/...>` — RC2/RC4/DES/AES and helpers.
- Protocols: `src/protocol/<name>`, headers `<dsd-neo/protocol/<name>/...>` — DMR, dPMR, D‑STAR, NXDN, P25, X2‑TDMA, EDACS, ProVoice, M17, YSF.
- Third‑party: `src/third_party/ezpwd` (INTERFACE target `dsd-neo_ezpwd`), `src/third_party/pffft` (FFT helper).

## Tooling

- Format: `tools/format.sh` (requires `clang-format`; see `.clang-format`).
- Static analysis: `tools/clang_tidy.sh` (use `--strict` for extra checks) or `clang-tidy -p build/dev-debug <files>`.
- Git hooks: `tools/install-git-hooks.sh` enables auto‑format on commit and clang‑tidy/cppcheck on push.

## Contributing

- Languages: C (C11) and C++ (C++14). Indent width 4 spaces; no tabs; brace all control statements; line length ≤ 120.
- Use project‑prefixed includes only: `#include <dsd-neo/...>`.
- Prefer small, testable helpers and add focused tests under `tests/<area>`.
- Before sending changes: build presets you touched, run `tools/format.sh`, address feasible clang‑tidy and cppcheck warnings.

## License

- Project license: GPL‑3.0‑or‑later (see `LICENSE`).
- Portions remain under ISC per the original DSD author (see `COPYRIGHT`).
- Third-party notices live in `THIRD_PARTY.md` (installed license texts: `share/doc/dsd-neo/licenses/`).
- Source files carry SPDX identifiers reflecting their license.
