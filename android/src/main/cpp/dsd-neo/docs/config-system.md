# DSD-neo Configuration System

This document describes the configuration file system in `dsd-neo`, covering
file format, options, profiles, validation, and CLI integration.

The goal is to let users avoid re-entering common options on every run while
preserving strict backward compatibility with existing CLI / environment
workflows.

---

## Goals

- **Persist user preferences** such as input source, output backend, decode
  mode, trunking basics, and UI behavior.
- **Non-breaking behavior** for existing users:
  - If no config file exists, behavior is identical to current releases.
  - CLI arguments and environment variables always take precedence over
    config file values.
- **Simple INI-style format** that's easy to read and edit.
- **Future-proof**: Config files are versioned; unknown keys/sections are
  ignored on load (use `--validate-config` to report warnings).
- **Validation** with helpful diagnostics including line numbers.
- **User experience enhancements**:
  - Template generation (`--dump-config-template`).
  - Path expansion (`~`, `$VAR`, `${VAR}`).
  - Profile support for switching between configurations.
  - Include directive for modular configs.

---

## Config File Format

We use a minimal INI-style format:

- Sections: `[section-name]`
- Key-value pairs: `key = value`
- Comments: lines starting with `#` or `;`
- Values:
  - Strings (unquoted or double-quoted).
  - Integers (parsed with `strtol`).
  - Booleans (`true` / `false`, `yes` / `no`, `on` / `off`, `1` / `0`).
  - Paths (support `~` and `$VAR` expansion).
  - Frequencies (support K/M/G suffix, e.g., `851.375M`).

Design principles:

- **Whitespace is ignored** around keys, the `=` sign, and values.
- **Case-insensitive booleans** (e.g., `True`, `YES`, `0`).
- **Unknown sections/keys are ignored** during normal startup; `--validate-config`
  reports them as warnings.
- A top-level `version` key defines the config schema version.

Example:

```ini
version = 1

[input]
source = "rtl"              # pulse / rtl / rtltcp / file / tcp / udp
rtl_device = 0
rtl_freq = "851.375M"       # supports K/M/G suffix or raw Hz

[output]
backend = "pulse"           # pulse / null / (future output types)
ncurses_ui = true           # map to -N / use_ncurses_terminal

[trunking]
enabled = true
chan_csv = "~/dsd-neo/dmr_t3_chan.csv"   # path expansion supported
group_csv = "$HOME/dsd-neo/group.csv"
allow_list = true

[mode]
decode = "auto"             # auto / p25p1 / p25p2 / dmr / nxdn48 / ...
```

---

## Path Expansion

Configuration values of type `PATH` support shell-like expansion:

- `~` expands to the user's home directory (`$HOME` on Unix,
  `%USERPROFILE%` on Windows).
- `$VAR` expands to environment variable `VAR`.
- `${VAR}` expands to environment variable `VAR` (braced form).
- Missing variables expand to empty string (no error).

Path expansion is applied to:
- `[input] file_path`
- `[trunking] chan_csv`
- `[trunking] group_csv`
- `[logging] event_log`
- `[recording] per_call_wav_dir`
- `[recording] static_wav`
- `[recording] raw_wav`
- Include directive paths

---

## Profile Support

Profiles allow defining multiple named configurations in a single file.
Use `--profile NAME` to activate a specific profile.

### Syntax

Profiles are defined using `[profile.NAME]` sections with dotted key syntax:

```ini
version = 1

[input]
source = "pulse"

[mode]
decode = "auto"

[trunking]
enabled = false

[profile.p25_trunk]
# Dotted syntax: section.key = value
mode.decode = "p25p1"
trunking.enabled = true
trunking.chan_csv = "~/p25/channels.csv"

[profile.local_dmr]
input.source = "rtl"
input.rtl_device = 0
input.rtl_freq = "446.5M"
input.rtl_gain = 30
mode.decode = "dmr"

[profile.scanner]
mode.decode = "auto"
output.ncurses_ui = true
```

### Usage

```bash
# Load base config
dsd-neo --config config.ini

# Load base config with profile overlay
dsd-neo --config config.ini --profile p25_trunk

# List available profiles
dsd-neo --config config.ini --list-profiles
```

### Behavior

- Base config is loaded first.
- Profile keys override base config values.
- If `--profile NAME` is specified but profile doesn't exist, an error is
  returned.
- Profile sections do not affect base loading (they are skipped unless
  `--profile` is specified).

---

## Include Directive

Config files can include other files using the `include` directive:

```ini
# Main config file
include = "/etc/dsd-neo/system.ini"
include = "~/.config/dsd-neo/local.ini"

version = 1

[input]
source = "rtl"  # overrides anything from includes
```

### Rules

- Include directives must appear **before** any section headers.
- Paths support expansion (`~`, `$VAR`, `${VAR}`).
- Maximum include depth: 3 (to prevent infinite recursion).
- Circular includes are detected and silently skipped.
- Included files are processed first; main file values override includes.
- Profile sections in included files are ignored (profiles are only read
  from the main config file).

---

## Config Discovery and Locations

Config loading is **opt-in**. By default, no config file is loaded unless
explicitly requested.

### Enabling Config

- CLI: `--config` (uses default path) or `--config /path/to/config.ini`
- Convenience: `dsd-neo /path/to/config.ini` (single positional `*.ini`) is treated as `--config /path/to/config.ini`.
- Environment: `DSD_NEO_CONFIG=/path/to/config.ini`

When both are present, CLI wins:

1. `--config PATH` (explicit path)
2. Default path (when `--config` is passed without a path; this ignores `DSD_NEO_CONFIG`)
3. `DSD_NEO_CONFIG`

If the file cannot be read or parsed:

- Log a warning (path + reason).
- Proceed without applying any user config values (defaults + env + CLI).
- If config loading was enabled (`--config` or `DSD_NEO_CONFIG`), autosave is still enabled for decode runs and the
  effective settings are written back on exit (this can create the file).

### Default Path

When `--config` is passed without a path argument, the default per-user
config location is used:

- On Unix-like systems:
  - If `$XDG_CONFIG_HOME` is set:
    - `${XDG_CONFIG_HOME}/dsd-neo/config.ini`
  - Else:
    - `${HOME}/.config/dsd-neo/config.ini`
- On Windows:
  - `%APPDATA%\dsd-neo\config.ini`

If the default file does not exist, it will be created when settings are saved
(DSD-neo autosaves on exit whenever config loading is enabled).

---

## Precedence Rules

For the keys covered by this user config system:

1. **Built-in defaults**
2. **User config file** (explicit or default path, when enabled)
3. **CLI arguments** (highest priority)

Environment variables are separate advanced runtime knobs (some CLI flags set
`DSD_NEO_*` env vars). Most are not persisted in the user config file, but a
small subset is exposed as config keys for convenience (for example
`[input] auto_ppm`, `[dsp] iq_balance`, `[dsp] iq_dc_block`).

---

## Configuration Reference

**[input] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `source` | ENUM | Input source type | `pulse` |
| `pulse_source` | STRING | PulseAudio source device | (empty) |
| `rtl_device` | INT (0-255) | RTL-SDR device index | `0` |
| `rtl_freq` | FREQ | RTL-SDR frequency | `851.375M` |
| `rtl_gain` | INT (0-49) | RTL-SDR gain in dB | `0` |
| `rtl_ppm` | INT (-1000-1000) | Frequency correction | `0` |
| `rtl_bw_khz` | INT (4-48) | DSP bandwidth | `48` |
| `rtl_sql` | INT (-100-0) | Squelch level | `0` |
| `rtl_volume` | INT (1-3) | Volume multiplier | `2` |
| `auto_ppm` | BOOL | Enable spectrum-based RTL auto-PPM correction | `false` |
| `rtl_auto_ppm` | BOOL | Enable spectrum-based RTL auto-PPM correction (alias for `auto_ppm`) | `false` |
| `rtltcp_host` | STRING | RTL-TCP hostname | `127.0.0.1` |
| `rtltcp_port` | INT (1-65535) | RTL-TCP port | `1234` |
| `file_path` | PATH | Input file path (WAV/BIN/RAW/SYM) | (empty) |
| `file_sample_rate` | INT (8000-192000) | File sample rate (WAV/RAW) | `48000` |
| `tcp_host` | STRING | TCP PCM input host | `127.0.0.1` |
| `tcp_port` | INT (1-65535) | TCP PCM input port | `7355` |
| `udp_addr` | STRING | UDP bind address | `127.0.0.1` |
| `udp_port` | INT (1-65535) | UDP port | `7355` |

**[output] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `backend` | ENUM | Audio output backend | `pulse` |
| `pulse_sink` | STRING | PulseAudio sink device | (empty) |
| `ncurses_ui` | BOOL | Enable ncurses UI | `false` |

**[mode] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `decode` | ENUM | Decode mode preset | `auto` |
| `demod` | ENUM | Demodulator path | `auto` |

**[trunking] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `enabled` | BOOL | Enable trunking | `false` |
| `chan_csv` | PATH | Channel map CSV | (empty) |
| `group_csv` | PATH | Group list CSV | (empty) |
| `allow_list` | BOOL | Use as allow list | `false` |
| `tune_group_calls` | BOOL | Follow group calls | `true` |
| `tune_private_calls` | BOOL | Follow private calls | `true` |
| `tune_data_calls` | BOOL | Follow data calls | `false` |
| `tune_enc_calls` | BOOL | Follow encrypted calls | `true` |

**[logging] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `event_log` | PATH | Event history log file path | (empty) |

**[recording] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `per_call_wav` | BOOL | Enable per-call decoded voice WAV output | `false` |
| `per_call_wav_dir` | PATH | Per-call WAV output directory | `./WAV` |
| `static_wav` | PATH | Static decoded voice WAV output file | (empty) |
| `raw_wav` | PATH | Raw (48 kHz) audio WAV output file | (empty) |

Note: `per_call_wav` and `static_wav` are mutually exclusive (same as `-P` vs `-w` on the CLI).

**[dsp] section:**
| Key | Type | Description | Default |
|-----|------|-------------|---------|
| `iq_balance` | BOOL | Enable RTL IQ balance (image suppression) | `false` |
| `iq_dc_block` | BOOL | Enable RTL I/Q DC blocker | `false` |

Note: The defaults shown match the generated template (`--dump-config-template`).
Missing keys generally mean “leave the engine default unchanged”; some input
sources require specific keys to actually switch the input at startup (see
Notes on Input Sources).

---

## Validation

The config system validates files and reports issues with line numbers:

- **Error**: Invalid enum value, type mismatch, parse failure
- **Warning**: Unknown key or section, integer out of range
- **Info**: Deprecated key usage (key still works)

```bash
# Validate a config file
dsd-neo --validate-config /path/to/config.ini

# Validate with strict mode (warnings become errors)
dsd-neo --validate-config /path/to/config.ini --strict-config
```

Exit codes:
- `0`: No errors (may have warnings)
- `1`: Errors present
- `2`: Only warnings (strict mode only)

Diagnostics are only printed when you run `--validate-config`. During normal
startup, DSD-neo logs a notice when a config is loaded and warns if loading
fails; unknown keys are ignored without warnings.

---

## Template Generation

Generate a commented config template with all options:

```bash
dsd-neo --dump-config-template > config.ini
```

Output format:

```ini
# DSD-neo configuration template
# Generated by: dsd-neo --dump-config-template
#
# Uncomment and modify values as needed.
# Lines starting with # are comments.
#
# Precedence: CLI arguments > environment variables > config file > defaults

version = 1

[input]
# Input source type
# Allowed: pulse|rtl|rtltcp|file|tcp|udp
# source = "pulse"

# RTL-SDR device index (0-based)
# Range: 0 to 255
# rtl_device = 0

# RTL-SDR frequency (supports K/M/G suffix)
# Frequency (supports K/M/G suffix)
# rtl_freq = "851.375M"
...
```

---

## Notes on Input Sources

- **RTL-SDR (`source = "rtl"`)**: Uses the `rtl_*` keys for frequency,
  gain, PPM correction, bandwidth, squelch, volume, and auto-PPM.
  Omitted values use sensible defaults. To switch the input to RTL at
  startup, set at least `rtl_freq` (and optionally `rtl_device`).

- **RTL-TCP (`source = "rtltcp"`)**: Uses `rtltcp_host`/`rtltcp_port`
  for the network endpoint, plus the same `rtl_*` tuning keys. To switch
  the input to RTL-TCP at startup, set at least `rtltcp_host`.

- **PulseAudio (`source = "pulse"`)**: Use `pulse_source` to specify
  a particular input device. The older `pulse_input` key is accepted
  as an alias.

- **TCP (`source = "tcp"`)**: Set `tcp_host` (port optional) to switch
  the input to TCP PCM audio (raw PCM16LE mono). Sample rate uses the
  global `-s` CLI setting (default 48000).

- **UDP (`source = "udp"`)**: Set `udp_addr` (port optional) to switch
  the input to UDP PCM audio (PCM16LE datagrams). Sample rate uses the
  global `-s` CLI setting (default 48000).

- **File (`source = "file"`)**: Set `file_path` to switch the input to a file
  input (WAV/BIN/RAW/SYM). Use `file_sample_rate` for PCM16 WAV/RAW inputs that
  are not 48 kHz (symbol capture formats ignore it).

### Decode Modes

The `decode` key in `[mode]` configures the frame types and modulation.
Supported values: `auto`, `p25p1`, `p25p2`, `dmr`, `nxdn48`, `nxdn96`,
`x2tdma`, `ysf`, `dstar`, `edacs_pv`, `dpmr`, `m17`, `tdma`, `analog`.

The optional `demod` key selects a demodulator path (`auto`, `c4fm`, `gfsk`,
`qpsk`). When set, it locks demodulator selection similarly to the `-m*`
CLI modulation options.

If you choose RTL/RTLTCP input and omit specific tuning fields, DSD-neo falls
back to its built-in RTL defaults: center frequency 850 MHz, DSP bandwidth
48 kHz, and volume multiplier 2. The template still shows 851.375M as the
example frequency.

### Trunking

When `[trunking] enabled = true`:

- Trunking is activated for the selected mode.
- CSV paths (`chan_csv`, `group_csv`) are passed to the decoder.
- CSV paths in the config are applied the same as passing `-C`/`-G` and are
  loaded when trunking is enabled.
- If you start DSD-neo with any CLI args and you do not explicitly set trunking
  or scan mode (`-T`/`-Y`), trunking inherited from the config is disabled for
  that run.

---

## Startup Behavior

1. Config is loaded early, before CLI argument parsing.
2. Config values are applied, then CLI arguments override them.
3. One-shot commands (`--dump-config-template`, `--validate-config`,
   `--list-profiles`, `--print-config`) execute and exit immediately.
4. If no CLI args and no config is loaded, the interactive bootstrap wizard runs.
5. When a config is loaded: interactive bootstrap is skipped unless
   `--interactive-setup` is specified.

### File Sample Rate

When using `source = "file"` with a non-48 kHz sample rate:

- Set `file_sample_rate` to match your input file.
- Symbol timing is adjusted automatically.

---

## Interactive Bootstrap

The interactive bootstrap wizard (`--interactive-setup`) guides you
through selecting input, mode, trunking, and UI options.

### Auto-Saving

- When the wizard completes, settings are automatically saved to the
  config path (default or explicit).
- Auto-save only occurs when config is enabled (`--config` or `DSD_NEO_CONFIG`).
- Outside the wizard, the app also auto-saves the effective settings at exit
  whenever a config path is in use. To avoid having your file rewritten,
  run without `--config` and without `DSD_NEO_CONFIG`.

### Config and CLI Interaction

- A no-arg run only loads a config if you enable it via `DSD_NEO_CONFIG`.
- If config is enabled and loads successfully and no other CLI args are provided:
  skip the wizard, use config settings.
- If CLI args are provided: config is loaded first, then CLI overrides it.
- CLI:
  - `--interactive-setup` forces running the wizard even if a config
    exists; the resulting setup is automatically saved to the current
    config path.

---

## CLI Flags Reference

### Config Loading

| Flag | Description |
|------|-------------|
| `--config [PATH]` | Enable config loading; optionally specify path (uses default if omitted) |
| `--print-config` | Print effective config (as INI) and exit |

### Template and Validation

| Flag | Description |
|------|-------------|
| `--dump-config-template` | Print commented template with all options |
| `--validate-config [PATH]` | Validate config and report diagnostics |
| `--strict-config` | Treat warnings as errors (with `--validate-config`) |

### Profiles

| Flag | Description |
|------|-------------|
| `--profile NAME` | Load named profile (overrides base config) |
| `--list-profiles` | List all profile names in config file |

### Bootstrap

| Flag | Description |
|------|-------------|
| `--interactive-setup` | Force interactive wizard even if config exists |

---

## Versioning and Compatibility

- Config files use `version = 1` at the top.
- If `version` is missing, defaults to 1.
- Unknown keys are ignored on load; `--validate-config` reports them as warnings.
- This allows newer binaries to add options without breaking older configs.

---

## ncurses UI Config Menu

When running with the ncurses UI (`ncurses_ui = true` or `-N`), the
**Config** menu provides:

- **Save Config (Current)**: Save current settings to the active config path (loaded via `--config` or **Load Config...**).
- **Save Config (Default)**: Save current settings to the default config path.
- **Save Config As...**: Save to a custom path.
- **Load Config...**: Load a config file and apply it to the running session.

### Live Reload

The following can be changed without restarting:

- PulseAudio input/output device
- RTL-SDR and RTLTCP tuning parameters (frequency, gain, PPM, etc.)
- TCP/UDP connection parameters
- File input path

Switching between different input types (e.g., Pulse → RTL) requires a
restart to take full effect.

---

## Summary

- INI-based config with clear precedence (CLI > env > config > defaults).
- Platform-specific default paths with automatic discovery.
- Validation with line-number diagnostics.
- Template generation for discoverability.
- Path expansion (`~`, `$VAR`, `${VAR}`) for portability.
- Profile support for switching configurations.
- Include directive for modular configs.
- Interactive bootstrap can persist user choices automatically.
