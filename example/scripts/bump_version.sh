#!/bin/bash
# Script to auto-increment build number in pubspec.yaml

PUBSPEC="pubspec.yaml"

# Get current version line
VERSION_LINE=$(grep "^version:" "$PUBSPEC")

# Extract version and build number
CURRENT_VERSION=$(echo "$VERSION_LINE" | sed 's/version: //' | sed 's/+.*//')
CURRENT_BUILD=$(echo "$VERSION_LINE" | sed 's/.*+//')

# Increment build number
NEW_BUILD=$((CURRENT_BUILD + 1))

# Create new version string
NEW_VERSION="${CURRENT_VERSION}+${NEW_BUILD}"

echo "Current version: ${CURRENT_VERSION}+${CURRENT_BUILD}"
echo "New version: ${NEW_VERSION}"

# Update pubspec.yaml
sed -i "s/^version:.*$/version: ${NEW_VERSION}/" "$PUBSPEC"

echo "✓ Version bumped to ${NEW_VERSION}"
