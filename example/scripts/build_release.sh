#!/bin/bash
# Convenience script to build release APK with auto version bump

cd "$(dirname "$0")/.."

echo "=== Pocket25 Release Builder ==="
echo ""

# Bump version
./scripts/bump_version.sh

echo ""
echo "Building release APK..."
flutter build apk --release

if [ $? -eq 0 ]; then
    echo ""
    echo "✓ Build successful!"
    echo ""
    
    # Show version info
    VERSION=$(grep "^version:" pubspec.yaml | sed 's/version: //')
    echo "Version: $VERSION"
    
    APK_PATH="build/app/outputs/flutter-apk/app-release.apk"
    if [ -f "$APK_PATH" ]; then
        SIZE=$(du -h "$APK_PATH" | cut -f1)
        echo "APK size: $SIZE"
        echo "Location: $APK_PATH"
    fi
    
    echo ""
    read -p "Install on connected device? (y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        echo "Installing..."
        adb install -r "$APK_PATH"
    fi
else
    echo ""
    echo "✗ Build failed"
    exit 1
fi
