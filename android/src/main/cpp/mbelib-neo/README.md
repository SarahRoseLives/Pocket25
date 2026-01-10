# mbelib-neo

Performance‑enhanced IMBE/AMBE vocoder primitives with a modern CMake build, installable headers, and pkg-config/CMake package integration.

Project homepage: https://github.com/arancormonk/mbelib-neo

[![CI](https://github.com/arancormonk/mbelib-neo/actions/workflows/ci.yml/badge.svg)](https://github.com/arancormonk/mbelib-neo/actions/workflows/ci.yml)

## Patent Notice

```
This source code is provided for educational purposes only. It
is a written description of how certain voice encoding/decoding
algorithms could be implemented. Executable objects compiled or
derived from this package may be covered by one or more patents.
Readers are strongly advised to check for any patent restrictions
or licensing requirements before compiling or using this source code.
```

This notice is advisory and does not modify the license. See `LICENSE` for terms.

## License

- mbelib-neo is distributed under GPL-2.0-or-later (`LICENSE`). All compiled binaries and source distributions fall under that license.
- Portions originate from the original ISC-licensed mbelib; the retained ISC notice is in `COPYRIGHT` for attribution and clarity. Those contributions are redistributed under GPL-2.0-or-later as permitted by the ISC terms.

## Overview

- A performance‑enhanced fork of [lwvmobile/mbelib](https://github.com/lwvmobile/mbelib), which is a fork of [szechyjs/mbelib](https://github.com/szechyjs/mbelib)
- Supports IMBE 7200x4400 (P25 Phase 1), IMBE 7100x4400 (ProVoice), AMBE (D‑STAR), and AMBE+2 (DMR, NXDN, P25 Phase 2, dPMR, etc.).
- Stable public API in `#include <mbelib-neo/mbelib.h>` with version macro `MBELIB_VERSION`.
- Ships as both shared and static libraries: `libmbe-neo.{so|dylib|dll}` and `libmbe-neo.a`.
- Installable CMake package (`mbe_neo::mbe_shared` / `mbe_neo::mbe_static`) and pkg-config file (`libmbe-neo`).

## Build From Source

Requirements

- C compiler with C99 support.
- CMake ≥ 3.20.
- On non‑Windows platforms, links against `-lm` automatically.

Using CMake presets (recommended)

```
# From the repository root:

# Debug build with tests/examples
cmake --preset dev-debug
cmake --build --preset dev-debug -j
ctest --preset dev-debug -V

# Release build (SIMD + fast-math + LTO)
cmake --preset dev-release
cmake --build --preset dev-release -j

# Optional sanitizers (Linux/Clang/GCC)
cmake --preset asan-ubsan-debug
cmake --build --preset asan-ubsan-debug -j
ctest --preset asan-ubsan-debug -V

# Disable tone synthesis (AMBE tones)
cmake --preset notones-debug
```

Notes

- Presets create out-of-source builds under `build/<preset>/`. Run the above from the repo root.
- Alternatively, you can run from anywhere using `cmake -S <repo> -B <builddir>`.

Manual configure/build

```
# From the repository root:
mkdir build && cd build
cmake -DMBELIB_BUILD_TESTS=ON -DMBELIB_BUILD_EXAMPLES=ON ..
cmake --build . -j
ctest -V

# Alternative (from anywhere):
cmake -S . -B build -DMBELIB_BUILD_TESTS=ON -DMBELIB_BUILD_EXAMPLES=ON
cmake --build build -j
ctest --test-dir build -V
```

## Install / Uninstall

```
# Single-config generators (Unix Makefiles/Ninja):
cmake --install build/dev-release

# Multi-config generators (Visual Studio/Xcode):
cmake --install build/dev-release --config Release

# Uninstall from the same build directory
cmake --build build/dev-release --target uninstall
```

## Configuration Options

- `-DNOTONES=ON` — Disable AMBE/AMBE+2 tone synthesis (adds `-DDISABLE_AMBE_TONES`).
- `-DMBELIB_ENABLE_WARNINGS=ON` — Enable common warnings (default ON).
- `-DMBELIB_WARNINGS_AS_ERRORS=ON` — Treat warnings as errors.
- `-DMBELIB_ENABLE_ASAN=ON` — Enable AddressSanitizer in Debug builds.
- `-DMBELIB_ENABLE_UBSAN=ON` — Enable UndefinedBehaviorSanitizer in Debug builds.
- `-DMBE_ENABLE_DEBUG_LOGS=ON` — Verbose debug logging in codec sources.
- `-DMBELIB_BUILD_DOCS=ON` — Add `docs` target (requires Doxygen).
- `-DMBELIB_ENABLE_FAST_MATH=ON` — Enable fast-math (`-ffast-math`/`/fp:fast`) on library targets.
- `-DMBELIB_ENABLE_LTO=ON` — Enable IPO/LTO in Release builds when supported.
- `-DMBELIB_ENABLE_SIMD=ON` — Enable SIMD-accelerated routines (SSE2 on x86/x86_64, NEON on ARM64) for hot paths like float→int16 conversion. Falls back to portable scalar code when unavailable.
- Note: the `dev-release` preset enables SIMD, fast-math, and LTO by default when supported.
- `-DMBELIB_BUILD_BENCHMARKS=ON` — Build optional local micro‑benchmarks (not run in CI) under the `bench_` targets.

## Using The Library

Headers and linking

- Public header: `#include <mbelib-neo/mbelib.h>`
- pkg-config: `pkg-config --cflags --libs libmbe-neo`
- CMake package:
  ```cmake
  find_package(mbe-neo CONFIG REQUIRED)
  target_link_libraries(your_target PRIVATE mbe_neo::mbe_shared) # or mbe_neo::mbe_static
  ```

Minimal example

```c
#include <stdio.h>
#include <mbelib-neo/mbelib.h>

int main(void) {
  char ver[32] = {0};
  mbe_printVersion(ver);
  printf("mbelib version: %s\n", ver);
  // Or: puts(mbe_versionString());
  return 0;
}
```

You can also build and run the bundled example:

```
cmake --build build/dev-debug --target example_print_version
./build/dev-debug/example_print_version
```

## Windows (MSVC) Quickstart

- Open the "x64 Native Tools Command Prompt for VS".
- From the repository root:

```
cmake --preset dev-release
cmake --build --preset dev-release --config Release -j
ctest --preset dev-release -C Release
cmake --install build/dev-release --config Release
```

Consuming with CMake on Windows (link static to avoid DLL path issues):

```cmake
cmake_minimum_required(VERSION 3.20)
project(consumer C)
find_package(mbe-neo CONFIG REQUIRED)
add_executable(consumer consumer.c)
target_link_libraries(consumer PRIVATE mbe_neo::mbe_static) # or mbe_neo::mbe_shared
```

## pkg-config on Windows (MSYS2/MinGW)

- Install MSYS2 and open a MinGW64 shell (e.g., UCRT64 or MINGW64).
- Install toolchain and pkg-config:

```
pacman -S --needed mingw-w64-x86_64-toolchain mingw-w64-x86_64-cmake mingw-w64-x86_64-pkg-config
```

- Configure, build, and install using the MinGW generator (example to the default /mingw64 prefix):

```
cmake -G "MinGW Makefiles" -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
cmake --install build --prefix /mingw64
```

- Compile a consumer using pkg-config:

```
cat > consumer.c << 'EOF'
#include <stdio.h>
#include <mbelib-neo/mbelib.h>
int main(void) { char ver[32]={0}; mbe_printVersion(ver); printf("%s\n", ver); }
EOF
cc consumer.c $(pkg-config --cflags --libs libmbe-neo) -o consumer.exe
```

Notes

- If you install to a custom prefix, set `PKG_CONFIG_PATH="<prefix>/lib/pkgconfig:$PKG_CONFIG_PATH"`.
- For shared builds, ensure `<prefix>/bin` is on `PATH` (so the DLL is found) or copy the DLL next to your `.exe`.
- To prefer static linking via pkg-config, you can try `pkg-config --static --libs libmbe-neo` (requires static libs available) or use the CMake package and link `mbe_neo::mbe_static`.

## Audio Quality Improvements

mbelib-neo implements JMBE-compatible audio synthesis algorithms for improved audio quality:

- **FFT-based unvoiced synthesis** (Algorithms #117-126): Uses 256-point FFT with band-level scaling instead of the legacy oscillator bank approach. Provides cleaner, more natural unvoiced sounds with proper spectral shaping.

- **Weighted Overlap-Add (WOLA)** (Algorithm #126): Smooth frame transitions for unvoiced synthesis using a 211-element trapezoidal synthesis window. Eliminates audible discontinuities between frames.

- **Adaptive smoothing** (Algorithms #111-116): Error-rate-based parameter smoothing that gracefully handles corrupted frames. Includes local energy tracking, adaptive voicing thresholds, and amplitude scaling.

- **Voiced phase/amplitude interpolation** (Algorithms #134-138): Smooth interpolation of pitch and amplitude for low-frequency harmonics during stable pitch periods. Reduces "buzzy" artifacts in voiced speech.

- **Frame repeat/muting with comfort noise**: Graceful degradation under high error conditions. Generates low-level white noise during frame muting to maintain audio continuity.

- **LCG noise generator with buffer overlap**: JMBE-compatible Linear Congruential Generator for deterministic noise, with 96-sample overlap for smooth continuity between frames.

These improvements bring mbelib-neo's audio quality closer to the reference JMBE (Java Multi-Band Excitation) implementation.

## API Notes

- Public API is prefixed `mbe_` and declared in `mbelib.h`.
- Float and 16‑bit PCM variants are provided (e.g., `mbe_processAmbe3600x2400Framef` and `mbe_processAmbe3600x2400Frame`).
- Version macro `MBELIB_VERSION` is defined in the generated header `mbelib-neo/version.h` and returned by `mbe_printVersion`.
- `mbe_versionString()` returns a const pointer to the version string.
- Noise source for unvoiced synthesis uses a fast thread‑local PRNG. Call
  `mbe_setThreadRngSeed(uint32_t)` in each thread for deterministic output when desired.
- ABI: shared library `SOVERSION` follows the project major version; minor updates aim to remain ABI compatible.

### Determinism & RNG

- The unvoiced synthesis path uses a per‑thread xorshift32 PRNG. For reproducible output,
  set a fixed seed in each thread with `mbe_setThreadRngSeed(seed)`. Different threads must
  seed independently to avoid correlated sequences.
- Compile‑time option `-DMBELIB_STRICT_ORDER=ON` preserves the legacy sample‑major RNG draw and
  accumulation order at a small performance cost. With the default (OFF), a faster oscillator‑major
  path is used; it remains deterministic for a given seed and platform but is not bit‑identical to
  the legacy ordering.
- Enabling `MBELIB_ENABLE_SIMD=ON` selects vectorized math on supported CPUs. This can change
  floating‑point rounding at the bit level. Tests enforce exactness for int16 on x86 in Debug, and
  sanity bounds elsewhere.

## Tests and Examples

- Run tests with `ctest -V` from the build directory.
- Included tests: `test_api` (version/headers), `test_ecc` (Golay and Hamming).
- Example: `examples/print_version.c` shows linking and header usage.

## Documentation

Optional Doxygen documentation can be generated:

```
cmake -S . -B build -DMBELIB_BUILD_DOCS=ON
cmake --build build --target docs
# Output in docs/html
```

## Project Layout

- Public headers: `include/mbelib-neo/`
- Sources: `src/core/`, `src/ecc/`, `src/ambe/`, `src/imbe/`
- Internal headers: `src/internal/`
- External libraries: `src/external/kiss_fft/` (bundled KISS FFT, BSD-3-Clause)
- Tests: `tests/` • Examples: `examples/`

## Contributing

- Follow `.clang-format` (LLVM style, 4‑space indent, 120 cols). You can run `tools/format.sh`.
- Prefer keeping internal symbols `static` and declarations in headers where shared.
- Before sending changes: build locally, run `ctest -V`, and ensure examples still link.

## License

- Project license: GPL‑2.0‑or‑later (see `LICENSE` and `LICENSES/GPL-2.0-or-later.txt`).
- Original mbelib sources were ISC; mbelib-neo redistributes them under GPL‑2.0‑or‑later with the original ISC text retained for attribution (see `COPYRIGHT` and `LICENSES/ISC.txt`).
- Source files carry SPDX identifiers reflecting their license.
