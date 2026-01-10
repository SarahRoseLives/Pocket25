# DSD-neo CLI Guide

Friendly, practical overview of the `dsd-neo` command line. This covers what you’ll use day-to-day, grouped by task. For a quick reference, run `dsd-neo -h`.

## Cheatsheet

- Help: `dsd-neo -h` | UI/logs: `-N`, `-Z` | List devices: `-O`
- Inputs: `-i pulse | file.wav | rtl[:...] | rtltcp[:...] | tcp[:host:7355] | udp[:bind:7355] | m17udp[:bind:17000]`
- Outputs: `-o pulse | null | udp[:host:23456] | m17udp[:host:17000]`
- Record/Logs: `-6 file.wav`, `-w file.wav`, `-P`, `-7 ./calls`, `-d ./mbe`, `-J events.log`, `-L lrrp.log`, `-Q dsp.bin`, `-c symbols.bin`, `-r *.mbe`
- Levels/Audio: `-g 0|1..50`, `-n 0..100`, `-8`, `-V 0|1|2|3`, `-z 0|1|2`, `-y`, `-v 0xF`, `-nm`
- Modes: `-fa | -fs | -f1 | -f2 | -fd | -fx | -fy | -fz | -fU | -fi | -fn | -fp | -fh | -fH | -fe | -fE | -fm`
- Inversions/filtering: `-xx`, `-xr`, `-xd`, `-xz`, `-l`, `-u 3`, `-q`
- Trunking/scan: `-T`, `-Y`, `-C chan.csv`, `-G group.csv`, `-W`, `-E`, `-p`, `-e`, `-I 1234`, `-U 4532`, `-B 12000`, `-t 1`, `--enc-lockout|--enc-follow`
- RTL‑SDR strings: `-i rtl:dev:freq:gain:ppm:bw:sql:vol[:bias=on|off]` or `-i rtltcp:host:port:freq:gain:ppm:bw:sql:vol[:bias=on|off]`
- M17 encode: `-fZ -M M17:CAN:SRC:DST[:RATE[:VOX]]`, `-fP`, `-fB`
- Keys: `-b`, `-H '<hex...>'`, `-R`, `-1`, `-2`, `-! '<hex...>'`, `-@ '<hex...>'`, `-5 '<hex...>'`, `-9`, `-A`, `-S bits:hex`, `-k keys.csv`, `-K keys_hex.csv`, `-4`, `-0`, `-3`
- Tools: `--calc-lcn file`, `--calc-cc-freq 451.2375`, `--calc-cc-lcn 50`, `--calc-step 12500`, `--calc-start-lcn 1`, `--auto-ppm`, `--auto-ppm-snr 6`, `--rtltcp-autotune`

## Quick Start

- Show help: `dsd-neo -h`
- PulseAudio in, play out, UI on: `dsd-neo -i pulse -o pulse -N`
- UDP audio in to PulseAudio out: `dsd-neo -i udp:0.0.0.0:7355 -o pulse -N`
- Follow DMR trunking (TCP PCM input + rigctl): `dsd-neo -fs -i tcp -U 4532 -T -C dmr_t3_chan.csv -G group.csv -N`
- Follow DMR trunking (RTL‑SDR): `dsd-neo -fs -i rtl:0:450M:26:-2:8 -T -C connect_plus_chan.csv -G group.csv -N`
- Play saved MBE files: `dsd-neo -r *.mbe`

Tip: If no config exists, running with no arguments starts the interactive setup (respects `DSD_NEO_NO_BOOTSTRAP`). When a
config file is present and enabled, a no-arg run reuses it; use `--interactive-setup` to force the wizard.

## Configuration Files

- Config loading is opt-in: use `--config` to enable, optionally with a path (e.g. `--config /path/to/config.ini`).
- Convenience: `dsd-neo /path/to/config.ini` (single positional `*.ini`) is treated as `--config /path/to/config.ini`.
- Default path (when `--config` is passed without a path): `${XDG_CONFIG_HOME:-$HOME/.config}/dsd-neo/config.ini`.
- Alternatively, set `DSD_NEO_CONFIG=<path>` environment variable to enable config loading (this is the only way for a no-arg run to load a config).
- Precedence detail: `--config /path/to/config.ini` > `--config` default path (ignores `DSD_NEO_CONFIG`) > `DSD_NEO_CONFIG`.
- `--interactive-setup` runs the wizard even when a config exists.
- `--print-config` prints the effective config as INI after all env/CLI overrides.
- When config is enabled, the final settings are autosaved on exit. See `docs/config-system.md` for details.

## Inputs (`-i`)

- PulseAudio: `-i pulse` (default). List sources/sinks: `-O`.
- PulseAudio by name/index: `-i pulse:<index|name>` (use `-O` to discover values)
- WAV file: `-i file.wav` (48 kHz mono). For other rates (e.g., DSDPlus 96 kHz): add `-s 96000`.
- OP25/FME capture BIN: `-i file.bin`.
- RTL‑SDR (USB): `-i rtl` or advanced string:
  - `rtl:dev:freq:gain:ppm:bw:sql:vol[:bias[=on|off]]`
  - Examples: `rtl:0:851.375M:22:-2:24:0:2`, `rtl:1:450M:0:0:12:0:2`
- RTL‑TCP: `-i rtltcp[:host:port[:freq:gain:ppm:bw:sql:vol[:bias[=on|off]]]]`
- TCP raw PCM16LE input (mono): `-i tcp[:host:port]` (default port 7355; sample rate uses `-s`, default 48000)
- UDP PCM16 input: `-i udp[:bind_addr:port]` (defaults 127.0.0.1:7355)
- M17 UDP/IP input: `-i m17udp[:bind_addr:port]` (defaults 127.0.0.1:17000)

- Set sample rate: `-s <rate>` (WAV/TCP/UDP; 48k or 96k typical)

Other input options

- `--input-volume <1..16>` scale non‑RTL input samples (file/UDP/TCP) by an integer factor.
- `--input-level-warn-db <dB>` warn if input power falls below dBFS (default −40).

Tip: If paths or names contain spaces, wrap them in single quotes.

## Outputs (`-o`)

- PulseAudio: `-o pulse` or a specific sink like `-o pulse:alsa_output.pci-0000_0d_00.3.analog-stereo`
- Null (no audio): `-o null`
- UDP audio out (PCM16): `-o udp[:host:port]` (default 127.0.0.1:23456)
- M17 UDP/IP out: `-o m17udp[:host:port]` (default 127.0.0.1:17000)

## Display & UI

- `-N` Use the ncurses terminal UI
- `-Z` Log MBE/PDU payloads to the console (verbose)
- `-O` List PulseAudio input sources and output sinks
- `-j` P25: enable LCW explicit retune (format 0x44)
- `-^` P25: prefer CC candidates during control channel hunt

### P25 Follower (Advanced)

- `--p25-vc-grace <s>` seconds after VC tune before eligible to return to CC
- `--p25-min-follow-dwell <s>` minimum follow dwell after first voice
- `--p25-grant-voice-timeout <s>` max seconds from grant to voice before returning
- `--p25-retune-backoff <s>` block immediate re‑tune to same VC for N seconds after return
- `--p25-mac-hold <s>` keep MAC activity eligible for audio for this many seconds after the last MAC
- `--p25-ring-hold <s>` ring gate window (seconds) used when deciding whether a slot still has recent audio activity
- `--p25-cc-grace <s>` CC hunt grace window; delay hunting for a new control channel by this many seconds after loss
- `--p25-force-release-extra <s>` safety‑net extra seconds beyond hangtime
- `--p25-force-release-margin <s>` safety‑net hard margin seconds beyond extra
- `--p25-p1-err-hold-pct <pct>` P25p1 IMBE error percentage threshold to extend hang
- `--p25-p1-err-hold-sec <s>` additional seconds to hold when threshold exceeded

## Recording & Files

- `-6 <file>` Save raw audio WAV (48k/mono). Large files (≈360 MB/hour)
- `-w <file>` Save decoded audio to a single WAV (mutually exclusive with `-P`)
- `-P` Per‑call WAV saving (auto‑named files in a folder; mutually exclusive with `-w`)
- `-7 <dir>` Set folder for per‑call WAVs (use before `-P`)
- `-r <files>` Play saved MBE files
- `-c <file>` Save symbol captures to a .bin file
- `-d <dir>` Save raw MBE vocoder frames in this folder
- `-J <file>` Append event log output
- `-L <file>` Append LRRP (location) data
- `-Q <file>` Write structured DSP or M17 stream data to `./DSP/<file>`
- `-q` Reverse mute: mute clear audio, unmute encrypted audio

## Levels & Audio

- `-g <num>` Digital output gain. `0` = auto; `1` ≈ 2%; `50` = 100%
- `-n <num>` Analog output gain (0–100%)
- `-nm` Enable legacy DMR mono audio path (does not change which frames are decoded)
- `-z <0|1|2>` TDMA slot preference (0 = slot 1, 1 = slot 2, 2 = auto)
- `-8` Monitor the source audio (helpful when mixing analog/digital)
- `-V <0|1|2|3>` TDMA voice synthesis (0 = off; 1 = slot 1; 2 = slot 2; 3 = both; default 3)
- `-y` Use experimental float audio output
- `-a` Enable call alert beep (UI)

## Modes & Decoders (`-f`)

- Auto: `-fa`
- Passive analog monitor: `-fA`
- Trunking helper: `-ft` (P25p1 CC + P25p1/p2/DMR voice)
- DMR simplex (BS/MS): `-fs` (stereo output), `-fr` (legacy mono alias)
- P25 Phase 1 only: `-f1`
- P25 Phase 2 only (6000 sps): `-f2`
- D‑STAR: `-fd`
- X2‑TDMA: `-fx`
- YSF: `-fy`
- M17: `-fz` (radio); `-fU` (M17 UDP/IP frame)
- NXDN48: `-fi` (6.25 kHz)
- NXDN96: `-fn` (12.5 kHz)
- ProVoice: `-fp`
- EDACS/ProVoice: `-fh` (standard), `-fH` (with ESK 0xA0)
  - Custom AFS bit splits: `-fh344`, `-fH434`
- EDACS EA/ProVoice: `-fe` (standard), `-fE` (with ESK 0xA0)
- dPMR: `-fm`

Notes

- All frame types are auto-detectable with `-fa` (multi-rate SPS hunting).
- P25p2 on a single frequency may require `-X` (below) if MAC_SIGNAL is missing.

## Mode Tweaks & Advanced

- Inversions: `-xx` X2 non‑inverted, `-xr` DMR inverted, `-xd` dPMR inverted, `-xz` M17 inverted
- Disable DMR/dPMR/NXDN/M17 input filtering: `-l`
- Analog filter bitmap (advanced): `-v <hex>` (bitmask for HPF/LPF/PBF)
- Unvoiced speech quality: `-u <1–64>` (default 3)
- Modulation optimizations: `-ma` (auto), `-mc` (C4FM), `-mg` (GFSK), `-mq` (QPSK), `-m2` (P25p2 QPSK 6000 sps)
- Relax CRC checks: `-F` (P25p2 MAC_SIGNAL, DMR RAS/CRC, NXDN SACCH/FACCH/CAC/F2U, M17 LSF/PKT)
- P25p2 manual WACN/SYSID/CC: `-X <hex>` (e.g., `-X BEE00ABC123`)
- DMR Tier III Location Area n‑bits: `-D <0–10>`
- Env (C4FM timing layers):
  - `DSD_NEO_TED=1` enables Gardner TED in the RTL demod pipeline (timing at complex baseband).
  - `DSD_NEO_C4FM_CLK=el|mm` enables a lightweight Early–Late or M&M clock assist in the symbol sampler for P25p1 C4FM.
  - When using RTL input, an internal TED‑bias auto‑centering helper may also gently nudge `symbolCenter` based on the smoothed TED residual.
  - Together these can all influence C4FM timing; if you are debugging “drifting” symbol centers, consider freezing windows (`DSD_NEO_WINDOW_FREEZE=1`) and disabling `DSD_NEO_C4FM_CLK` while testing.

## Trunking & Scanning

- Enable trunking (NXDN/P25/EDACS/DMR): `-T`
- Conventional scan mode: `-Y` (not trunking; scans for sync on enabled decoders)
- Channel map CSV: `-C <file>` (e.g., `connect_plus_chan.csv`)
- Group list CSV (allow/block + labels): `-G <file>`
- Use group list as allow/whitelist: `-W`
- Tune controls: `-E` disable group calls, `-p` disable private calls, `-e` enable data calls, `--enc-lockout` do not tune encrypted P25 calls, `--enc-follow` allow encrypted (default)
- Hold talkgroup: `-I <dec>`
- rigctl over TCP: `-U <port>` (SDR++ default 4532)
- Set rigctl bandwidth (Hz): `-B <hertz>` (e.g., 7000–48000 by mode)
- Hang time after voice/sync loss (seconds): `-t <secs>`
  - Env (advanced): Optional hangtime extension when P25p1 IMBE error % is high:
    - `DSD_NEO_P25P1_ERR_HOLD_PCT=<percent>` (default 0 = off)
    - `DSD_NEO_P25P1_ERR_HOLD_S=<seconds>` (default 0 = off)
  - Env (DMR): Hangtime and grant timeout overrides:
    - `DSD_NEO_DMR_HANGTIME=<seconds>` — post‑voice hangtime before returning to CC
    - `DSD_NEO_DMR_GRANT_TIMEOUT=<seconds>` — max seconds waiting for voice after grant

## RTL‑SDR details (`-i rtl` / `-i rtltcp`)

- Fields: `dev` (device index), `freq` (Hz/MHz), `gain` (0–49), `ppm`, `bw` (kHz: 4, 6, 8, 12, 16, 24, 48), `sql` (dB or linear), `vol` (0–3; typical 1–3), optional `bias[=on|off]`.
- Examples:
  - `-i rtl:0:851.375M:22:-2:24:0:2`
  - `-i rtltcp:192.168.1.10:1234:851.375M:22:-2:24:0:2`

Advanced (env)

- `DSD_NEO_RTL_DIRECT=0|1|2|I|Q` — Direct sampling selection (0 off; 1 I‑ADC; 2 Q‑ADC).
- `DSD_NEO_RTL_OFFSET_TUNING=0|1` — Disable/enable offset tuning (default is to try enabling).
- `DSD_NEO_RTL_XTAL_HZ` / `DSD_NEO_TUNER_XTAL_HZ` — Override crystal refs in Hz (optional).
- `DSD_NEO_RTL_IF_GAINS="stage:gain[,stage:gain]..."` — Set IF gain(s); gain in dB (e.g., `10`) or 0.1 dB (`125`).
- `DSD_NEO_RTL_TESTMODE=0|1` — Enable librtlsdr test mode (ramp) instead of I/Q (for diagnostics).
- On rtl_tcp reconnects, these settings are automatically reapplied.

## M17 Encoding

- Stream encoder: `-fZ` with `-M M17:CAN:SRC:DST[:INPUT_RATE[:VOX]]`
- BERT encoder: `-fB`
- Packet encoder: `-fP`

M17 `-M` details

- `CAN` 0–15 (default 7; values > 15 clamp to 15)
- `SRC`/`DST` up to 9 UPPER base40 chars (` ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.`)
- `INPUT_RATE` default 48000; use multiples of 8000 up to 48000
- `VOX` enable with `1` (default `0`)

Examples

- `dsd-neo -fZ -M M17:9:DSD-NEO:ARANCORMO -i pulse -6 m17signal.wav -8 -N`
- `dsd-neo -fP -M M17:9:DSD-NEO:ARANCORMO -6 m17pkt.wav -8`

## Keys & Privacy (advanced)

- Basic Privacy key (decimal): `-b <dec>`
- Hytera 10/32/64‑char BP or AES‑128/256 key (hex, groups of 16): `-H '<hex…>'`
- dPMR/NXDN scrambler (decimal): `-R <dec>`
- RC4/DES key (hex): `-1 <hex>`
- TYT Basic Privacy (16‑bit, hex, enforced): `-2 <hex>`
- TYT Advanced Privacy PC4 (hex stream): `-! '<hex…>'`
- Retevis Advanced Privacy RC2 (hex stream): `-@ '<hex…>'`
- TYT Enhanced Privacy AES‑128 (hex stream): `-5 '<hex…>'`
- Kenwood 15‑bit scrambler (decimal): `-9 <dec>`
- Anytone 16‑bit BP (hex): `-A <hex>`
- Generic keystream (length:hexbytes): `-S <bits:hex>` (e.g., `-S 49:123456789ABC80`)
- Import keys CSV (decimal): `-k <file>`
- Import keys CSV (hex): `-K <file>`
- Force key over identifiers: `-4` (DMR BP/NXDN scrambler), `-0` (DMR RC4 when PI/LE missing)
- Disable DMR Late Entry IDs: `-3` (avoid false ENC)

## Tools & Extras

- DMR Tier III LCN calculator:
  - `--calc-lcn <file>` (CSV of freqs)
  - `--calc-cc-freq <freq>` and `--calc-cc-lcn <num>` (anchor)
  - `--calc-step <hz>` (override channel step)
  - `--calc-start-lcn <num>` (start when no anchor)
- RTL auto‑PPM drift correction:

  - `--auto-ppm` enable
  - `--auto-ppm-snr <dB>` set SNR gate (default 6)

- RTL‑TCP networking:
  - `--rtltcp-autotune` enable adaptive tuning of buffering/recv size for RTL‑TCP links.

## Environment Variables (Advanced Tuning)

These environment variables provide fine‑grained control for power users.

Auto‑PPM (RTL‑SDR)

- `DSD_NEO_AUTO_PPM=1` — enable spectrum‑based drift correction
- `DSD_NEO_AUTO_PPM_SNR_DB=<dB>` — SNR gate (default 6)
- `DSD_NEO_AUTO_PPM_PWR_DB=<dB>` — absolute peak gate (default −80)
- `DSD_NEO_AUTO_PPM_ZEROLOCK_PPM=<ppm>` — zero‑step lock guard (default 0.6)
- `DSD_NEO_AUTO_PPM_ZEROLOCK_HZ=<Hz>` — frequency lock guard (default 60)
- `DSD_NEO_AUTO_PPM_FREEZE=0/1` — freeze retunes during training (default 1)

Resampler

- `DSD_NEO_RESAMP=48000` — target rate (default); `off` or `0` to disable

FLL/TED controls

- `DSD_NEO_FLL=0/1` — disable/enable residual CFO frequency‑locked loop (default 0)
- `DSD_NEO_FLL_ALPHA=<float>`, `DSD_NEO_FLL_BETA=<float>`, `DSD_NEO_FLL_DEADBAND=<float>`, `DSD_NEO_FLL_SLEW=<float>` — loop parameters (mode defaults when unset: analog-ish ≈ 0.0015/0.00015/0.0086/0.012; digital ≈ 0.008/0.0008/0.002/0.004)
- `DSD_NEO_TED=0/1` — disable/enable timing error detector (default 0)
- `DSD_NEO_TED_GAIN=<float>` — TED gain
- `DSD_NEO_TED_FORCE=1` — force TED

FM/C4FM stabilization

- `DSD_NEO_FM_AGC=1` — enable FM AGC (default off)
- `DSD_NEO_FM_AGC_TARGET`, `DSD_NEO_FM_AGC_MIN`, `DSD_NEO_FM_AGC_ALPHA_UP`, `DSD_NEO_FM_AGC_ALPHA_DOWN`
- `DSD_NEO_FM_LIMITER=1` — constant‑envelope limiter
- `DSD_NEO_IQ_DC_BLOCK=1` — enable DC blocker
- `DSD_NEO_IQ_DC_SHIFT=<k>` — DC shift coefficient

Digital SNR squelch

- `DSD_NEO_SNR_SQL_DB=<dB>` — skip sync when SNR below threshold

Capture/retune behavior

- `DSD_NEO_DISABLE_FS4_SHIFT=1` — disable +fs/4 capture shift
- `DSD_NEO_OUTPUT_CLEAR_ON_RETUNE=1` — clear output on retune
- `DSD_NEO_RETUNE_DRAIN_MS=<ms>` — drain time before retune

RTL‑TCP networking

- `DSD_NEO_TCP_PREBUF_MS=<ms>` — prebuffer duration (default 1000, range 5–1000)
- `DSD_NEO_TCP_RCVBUF=<bytes>` — OS socket receive buffer (default ~4 MiB)
- `DSD_NEO_TCP_BUFSZ=<bytes>` — user‑space read size (default ~16 KiB)
- `DSD_NEO_TCP_RCVTIMEO=<ms>` — socket receive timeout in milliseconds (default 2000)
- `DSD_NEO_TCP_WAITALL=0/1` — require full reads (default off)
- `DSD_NEO_TCP_STATS=1` — print throughput/queue stats
- `DSD_NEO_TCP_AUTOTUNE=1` — enable adaptive buffering/recv size for TCP links
- `DSD_NEO_TCP_MAX_TIMEOUTS=<n>` — max consecutive timeouts before giving up

RTL‑SDR driver options

- `DSD_NEO_RTL_DIRECT=0|1|2|I|Q` — direct sampling (0 off, 1 I‑ADC, 2 Q‑ADC)
- `DSD_NEO_RTL_OFFSET_TUNING=0|1` — offset tuning (default: try enable)
- `DSD_NEO_RTL_XTAL_HZ=<Hz>`, `DSD_NEO_TUNER_XTAL_HZ=<Hz>` — crystal overrides
- `DSD_NEO_RTL_IF_GAINS="stage:gain[,...]"` — IF stage gains (dB or 0.1 dB)
- `DSD_NEO_RTL_TESTMODE=0|1` — test mode (ramp source)
- `DSD_NEO_RTL_AGC=0|1` — RTL2832U AGC enable/disable (default on)
- `DSD_NEO_TUNER_BW_HZ=<Hz>` — override automatic tuner bandwidth

Tuner autogain (experimental)

- `DSD_NEO_TUNER_AUTOGAIN=1` — enable automatic tuner gain adjustment
- `DSD_NEO_TUNER_AUTOGAIN_PROBE_MS=<ms>` — probe interval
- `DSD_NEO_TUNER_AUTOGAIN_SEED_DB=<dB>` — initial gain seed
- `DSD_NEO_TUNER_AUTOGAIN_SPEC_SNR_DB=<dB>` — spectrum SNR threshold
- `DSD_NEO_TUNER_AUTOGAIN_INBAND_RATIO=<ratio>` — in‑band power ratio
- `DSD_NEO_TUNER_AUTOGAIN_UP_STEP_DB=<dB>` — gain up step size
- `DSD_NEO_TUNER_AUTOGAIN_UP_PERSIST=<n>` — persistence before increasing gain

Audio/DSP helpers

- `DSD_NEO_DEEMPH=off|50|75|nfm` — deemphasis curve
- `DSD_NEO_AUDIO_LPF=<Hz>|off` — audio low‑pass filter cutoff (or disable)
- `DSD_NEO_C4FM_CLK=el|mm` — C4FM clock assist mode (Early–Late or M&M)
- `DSD_NEO_C4FM_CLK_SYNC=1` — enable C4FM clock sync
- `DSD_NEO_COSTAS_BW=<float>`, `DSD_NEO_COSTAS_DAMPING=<float>` — Costas loop tuning
- `DSD_NEO_CHANNEL_LPF=0|1` — channel LPF enable/disable (mode‑dependent default)
- `DSD_NEO_WINDOW_FREEZE=1` — freeze symbol‑center window timing for debugging
- `DSD_NEO_COMBINE_ROT=0|1` — enable combined rotation (default 1)
- `DSD_NEO_UPSAMPLE_FP=0|1` — enable upsampler fixed‑point path (default 1)
- `DSD_NEO_CQPSK=1` — enable CQPSK demodulation
- `DSD_NEO_CQPSK_SYNC_INV=1`, `DSD_NEO_CQPSK_SYNC_NEG=1` — CQPSK sync polarity tweaks

Misc

- `DSD_NEO_MT=1` — enable light worker pool (2 threads)
- `DSD_NEO_PDU_JSON=1` — emit P25 PDU JSON to stderr
- `DSD_NEO_RT_SCHED=1` — enable real‑time thread scheduling (requires privileges)
- `DSD_NEO_RT_PRIO_USB|DSD_NEO_RT_PRIO_DONGLE|DSD_NEO_RT_PRIO_DEMOD=<1..99>` — per-thread RT priority (only used when `DSD_NEO_RT_SCHED=1`)
- `DSD_NEO_CPU_USB|DSD_NEO_CPU_DONGLE|DSD_NEO_CPU_DEMOD=<cpu>` — per-thread CPU affinity (only used when `DSD_NEO_RT_SCHED=1`)
- `DSD_NEO_FTZ_DAZ=1` — enable SSE flush‑to‑zero / denormals‑are‑zero
- `DSD_NEO_INPUT_VOLUME=<1..16>` — scale non‑RTL input samples (env alternative to `--input-volume`)
- `DSD_NEO_INPUT_WARN_DB=<dB>` — warn if input power falls below dBFS (default −40)
- `DSD_NEO_RIGCTL_RCVTIMEO=<ms>` — rigctl socket receive timeout
- `DSD_NEO_TCPIN_BACKOFF_MS=<ms>` — TCP input read backoff

P25 trunking timing

- `DSD_NEO_P25_HANGTIME=<seconds>` — post‑voice hangtime before returning to CC
- `DSD_NEO_P25_GRANT_TIMEOUT=<seconds>` — max seconds waiting for voice after grant
- `DSD_NEO_P25_VC_GRACE=<seconds>` — grace after VC tune before eligible to return (also via `--p25-vc-grace`)
- `DSD_NEO_P25_MIN_FOLLOW_DWELL=<seconds>` — minimum follow dwell after first voice
- `DSD_NEO_P25_GRANT_VOICE_TO=<seconds>` — grant‑to‑voice timeout
- `DSD_NEO_P25_RETUNE_BACKOFF=<seconds>` — block immediate re‑tune to same VC
- `DSD_NEO_P25_MAC_HOLD=<seconds>` — keep MAC activity eligible for audio (also via `--p25-mac-hold`)
- `DSD_NEO_P25_RING_HOLD=<seconds>` — ring gate window for recent audio activity (also via `--p25-ring-hold`)
- `DSD_NEO_P25_VOICE_HOLD=<seconds>` — voice activity hold window
- `DSD_NEO_P25_CC_GRACE=<seconds>` — CC hunt grace window (also via `--p25-cc-grace`)
- `DSD_NEO_P25_FORCE_RELEASE_EXTRA=<seconds>` — safety‑net extra beyond hangtime
- `DSD_NEO_P25_FORCE_RELEASE_MARGIN=<seconds>` — safety‑net hard margin
- `DSD_NEO_P25_WD_MS=<ms>` — P25 state machine watchdog interval (100–2000)
- `DSD_NEO_P25P1_ERR_HOLD_PCT=<percent>` — extend hangtime when P25p1 IMBE error % exceeds threshold (default 0 = off)
- `DSD_NEO_P25P1_ERR_HOLD_S=<seconds>` — additional hold seconds when threshold exceeded (default 0 = off)
- `DSD_NEO_P25P1_SOFT_ERASURE_THRESH=<0..255>` — P25p1 soft-decision erasure threshold (default 64; falls back to `DSD_NEO_P25P2_SOFT_ERASURE_THRESH`)
- `DSD_NEO_P25P2_SOFT_ERASURE_THRESH=<0..255>` — P25p2 soft-decision erasure threshold (default 64)
- `DSD_NEO_CC_CACHE=0|1` — enable/disable control channel frequency caching
- `DSD_NEO_CACHE_DIR=<path>` — override cache directory for CC frequency cache

DMR Tier III (env helpers for `--calc-lcn`)

- `DSD_NEO_DMR_T3_CALC_CSV=<file>` — CSV file of frequencies
- `DSD_NEO_DMR_T3_STEP_HZ=<Hz>` — channel step (e.g., 12500)
- `DSD_NEO_DMR_T3_CC_FREQ=<Hz>` — control channel anchor frequency
- `DSD_NEO_DMR_T3_CC_LCN=<n>` — control channel anchor LCN
- `DSD_NEO_DMR_T3_START_LCN=<n>` — start LCN when no anchor
- `DSD_NEO_DMR_T3_HEUR=1` — enable heuristic LCN fill

Debug (verbose/developer)

- `DSD_NEO_DEBUG_SYNC=1` — verbose sync detection output
- `DSD_NEO_DEBUG_CQPSK=1` — verbose CQPSK/TED/FLL state output
- `DSD_NEO_SYNC_WARMSTART=0` — disable sync warm‑start

## Handy Examples

- UDP in → Pulse out with UI: `dsd-neo -i udp -o pulse -N`
- RTL‑TCP in with ncurses UI: `dsd-neo -i rtltcp:127.0.0.1:1234 -N`
- Save per‑call WAVs to a folder: `dsd-neo -7 ./calls -P -N`
- Strictly P25 Phase 1 from TCP audio: `dsd-neo -f1 -i tcp -N`

Tip: Many options can be mixed; start simple, add only what you need.
