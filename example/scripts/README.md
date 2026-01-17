# Build Scripts

## Version Management

### bump_version.sh
Automatically increments the build number in `pubspec.yaml`.

Usage:
```bash
./scripts/bump_version.sh
```

### build_release.sh
All-in-one script that:
1. Auto-increments the version
2. Builds the release APK
3. Shows build info
4. Optionally installs on connected device

Usage:
```bash
./scripts/build_release.sh
```

## Version Format

The version follows this format: `MAJOR.MINOR.PATCH+BUILD_NUMBER`

- **MAJOR**: Increment for major changes/breaking changes
- **MINOR**: Increment for new features
- **PATCH**: Increment for bug fixes
- **BUILD_NUMBER**: Auto-incremented on each release build

Example: `1.0.0+1`

### Manual Version Updates

To update the major/minor/patch version, edit `pubspec.yaml` directly:

```yaml
version: 1.2.0+15
```

The build number will continue to auto-increment from there.
