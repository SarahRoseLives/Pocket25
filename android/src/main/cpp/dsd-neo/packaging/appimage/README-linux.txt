Portable Linux packaging via AppImage

This repository builds a portable AppImage that bundles required
runtime libraries (excluding core system libs like glibc).

Key bits:
- AppDir layout with prefix /usr
- .desktop file at packaging/appimage/dsd-neo.desktop
- Icon name: dsd-neo (PNG is picked up if present)
- Built on Ubuntu 20.04 (glibc 2.31) for broad compatibility

End users:
- Download dsd-neo-linux-<arch>-portable-<version>.AppImage
- chmod +x and run: ./dsd-neo-*.AppImage -h
