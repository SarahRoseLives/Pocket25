# Release Asset License Checklist

Required license/notice files in every shipped asset:

- `LICENSE`
- `COPYRIGHT`
- `THIRD_PARTY.md`
- `licenses/ezpwd-LGPL-2.1-or-later.txt` (from `src/third_party/ezpwd/lesser.txt`)
- `licenses/pffft-FFTPACK.txt` (from `src/third_party/pffft/COPYING`)

## TODOs per uploaded asset

### Linux (AppImage)

- [ ] Download `dsd-neo-linux-x86_64-portable-*.AppImage`.
  - [ ] Extract with `./dsd-neo-linux-x86_64-portable-*.AppImage --appimage-extract`.
  - [ ] Confirm `squashfs-root/usr/share/doc/dsd-neo/` contains all required files.
- [ ] Download `dsd-neo-linux-aarch64-portable-*.AppImage`.
  - [ ] Extract with `--appimage-extract` on aarch64 or in a container.
  - [ ] Confirm `usr/share/doc/dsd-neo/` contains all required files.

### macOS (DMG)

- [ ] Download `dsd-neo-macos-arm64-portable-*.dmg`.
  - [ ] Mount the DMG.
  - [ ] Confirm `dsd-neo-macos/share/doc/dsd-neo/` contains all required files.

### Windows (ZIP)

- [ ] Download `dsd-neo-msvc-x86_64-native-*.zip`.
  - [ ] Unzip and confirm `dsd-neo-msvc/share/doc/dsd-neo/` contains all required files.
- [ ] Download `dsd-neo-mingw-x86_64-native-*.zip`.
  - [ ] Unzip and confirm `dsd-neo-mingw/share/doc/dsd-neo/` contains all required files.

## CI-side sanity

- [ ] For each workflow run, verify the staging steps did not emit “missing required license file” errors.
- [ ] Spot-check nightly and tag builds across all OSes after any packaging changes.
