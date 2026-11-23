# Pocket25 — LSM / V4L patch series changelog

Unofficial patch series on top of Pocket25 `master` @ `3510c70` (1.0.0+14, 2026-07-01).
Developed and field-tested on a Google Pixel 8 + RTL-SDR Blog V4L against the
Michigan MPSCS Washtenaw County Simulcast site (P25 Phase 1, LSM/CQPSK,
control channels 855.2875 / 855.0875 / 853.25 MHz).

Test builds use app ID `com.pocket25.lsmtest` ("Pocket25 LSM test") so they
install beside the official app. The patch file does not include that rename.

Status legend: **Tested** = verified on the hardware above.
**Built** = compiles and was reviewed, but not yet exercised on hardware.

---

## 1.0.0-lsm13 — exit button, no-signal watchdog, display fixes

New behavior that changes scanning (the auto-restart) defaults to off.

### Added
- **Exit button.** Power icon at the top right of the Scanner screen, with a
  confirmation. Stops any scan (writes the log SESSION END), releases the
  dongle and closes the app (`finishAndRemoveTask`, so it also leaves
  Recents). (`scanner_screen.dart`, `main.dart`, `MainActivity.kt`
  `exitApp`) — **Built**
- **No-signal warning.** During a system scan, if no control channel has been
  decoded for 3 minutes (counted across every control channel in the list),
  the Scanner screen shows an orange box and the log gets a timestamped
  `[watch] No control channel heard for 3 min on any of N control channels`
  line. Both clear when a channel is heard again
  (`[watch] Control channel heard again after about N min of silence`).
  Prompted by a 55-minute silent stretch where the dongle stayed connected
  (no USB errors) but nothing decoded. (`scanning_service.dart`) — **Built**
- **Restart the radio if nothing is heard for 3 min** (SDR Settings →
  Trunked Call Following, default off). On the first warning of an outage the
  scan is fully stopped and restarted, which closes and reopens the USB
  connection (the software equivalent of a replug). Only once per outage; if
  still nothing 3 min later the log says so and scanning continues.
  (`scanning_service.dart`, `settings_service.dart`) — **Built**
- **Affiliations tab shows each radio's last talkgroup** (and its name if it
  is in your list), from the Group Attachments data. Radios that only
  registered show "—". (`network_screen.dart`) — **Built**

### Fixed
- **Recent list missing radio IDs** (original-app behavior). The first event
  of a call often has the talkgroup but not yet the radio ID; the late ID was
  dropped because a row for that talkgroup already existed. The row is now
  filled in, and a different radio ID on the same talkgroup within 5 s gets
  its own row. (`main.dart`) — **Built**
- **Log tab holds 5000 lines** (was 2000), so long sessions keep their start.
  (`main.dart`) — **Built**

---

## 1.0.0-lsm12 — reliability and quality-of-life

All new options default to off; nothing else changes unless you turn them on.

### Added
- **"Radio not responding" message.** When the phone loses USB contact with
  the dongle (loose OTG adapter, overheating, USB power cut), librtlsdr prints
  a burst of `rtlsdr_*_reg failed` / `i2c ... failed` errors. Five or more in
  5 s during a system scan now stop the scan and show on the Scanner screen:
  "Radio not responding. Check the USB-C OTG adapter and cable (reseat them),
  and that the dongle isn't overheating, then tap RESUME." Log line:
  `[cfg] Radio not responding (repeated USB errors): scan stopped`.
  (`main.dart`, `scanning_service.dart`) — **Built**
- **Auto-start when the dongle is plugged in** (SDR Settings → Trunked Call
  Following, default off). Android already opens the app when an RTL-SDR is
  attached; with this on, the app resumes the last site ~2 s later. The last
  site is now remembered across app restarts. Addresses Pocket25 issue #21.
  (`MainActivity.kt` `consumeUsbAttach`/`usbAttached`, `main.dart`,
  `scanning_service.dart`) — **Built**
- **Don't re-join a call that just ended** (same card, default off). For 1.5 s
  after a call ends, repeat announcements of the same talkgroup on the same
  channel are ignored (log `grant-cooldown-recent-call`). Saves the ~1 s
  "0-second" revisit; a very quick reply on the same channel may be joined up
  to 1.5 s late. (`p25_trunk_sm.c`, `dsd_flutter_jni.cpp`
  `dsd_flutter_revisit_cooldown_ms`, plugin `setRevisitCooldown`) — **Built**

### Changed
- **BER on the Scanner screen is now the last ~10 s** ("BER 10s: x%"), coloured
  green < 2 %, orange < 10 %, red above; it shows the session average until
  there is enough recent data. Idea from ClayF/Pocket25. — **Built**
- Log header/footer adds the cooldown and auto-start settings.

### Already in the original app (no change)
- Emergency calls are shown with a red background and a warning icon.

---

## 1.0.0-lsm11 — receive options from ClayF/Pocket25

Ported from the `cqpsk-lsm-mode` branch of github.com/ClayF/Pocket25
(commit cbdddf8, 2026-10-19, "Receive improvements"). Thanks to ClayF.

### Fixed
- **rtl_tcp gain was 10x too low.** The decoder expects tenths of a dB; the
  rtl_tcp path passed whole dB (gain 48 became 4.8 dB). Fixed in the system
  scan, Quick Scan and Conventional screens. Native USB was already correct.
  — **Built**

### Added (SDR Settings → Receive Options (advanced))
- **P25-only sync during system scans** (default ON): while following a P25
  system the decoder ignores DMR/D-STAR/YSF/NXDN/etc. sync, avoiding false
  syncs. Armed before each system-scan start and cleared on stop; the
  previous protocol flags are restored for other screens. — **Built**
- **Play both TDMA slots** (default off = original behavior, which mutes
  slot 2 to avoid choppy audio): P25 Phase 2 / DMR. — **Built**
- **Automatic PPM correction, Automatic gain, Carrier tracking (FLL),
  Symbol timing recovery (TED)** (all default off). — **Built**
- Log header/footer: `[cfg] Receive: ...` shows these settings. — **Built**

Not ported: ClayF's app-wide modulation setting (we use per-site modulation)
and the 10-second BER window.

---

## 1.0.0-lsm10 — priority talkgroups

### Added
- **Priority talkgroups (★).** In the Scan Grid, **long-press** a talkgroup
  tile to mark it priority (amber border, ★ before the number); long-press
  again to clear it. Takes effect immediately, mid-scan; no web programmer or
  restart. Saved per system in the app database (schema v6 adds
  `talkgroups.priority`).
  While the scanner is on a call for a non-priority talkgroup, the voice
  channel keeps announcing other active calls (link control "Group Voice
  Channel Update"). If one of those is a ★ talkgroup, the scanner drops the
  current call and switches to it (log line `priority-preempt`). A ★ call is
  never interrupted by another ★ call. The talkgroup filter, "Skip encrypted
  calls" and the start-up retune freeze still apply. With no ★ set, nothing
  changes. (`p25_trunk_sm.c` `p25_sm_priority_preempt`, `p25_lcw.c`,
  `dsd_flutter_jni.cpp`, plugin, `scan_grid_screen.dart`, `main.dart`) — **Built**
- Log: `[cfg] Priority talkgroups: ...` in the session header/footer, and
  `[cfg] Talkgroup filter changed: ...` / `[cfg] Priority talkgroups changed:
  ...` when you change them in the Scan Grid during a session. — **Built**

### Known limits
- The switch costs a retune, so the ★ call is joined mid-transmission.
- Preemption happens from the voice channel only; on the control channel the
  scanner still takes the first wanted call in a message, then switches within
  a fraction of a second if a ★ call is announced on that voice channel.
- Re-importing talkgroups from the web programmer may clear ★ marks (the CSV
  doesn't carry them yet).

---

## 1.0.0-lsm9 — "no radio device detected"

### Added
- **Radio check at scan start.** Starting or resuming a system scan first
  checks that the radio chosen in SDR Settings is connected (Native USB:
  an RTL-SDR on USB; HackRF: a HackRF). If none is found the scan doesn't
  start and the Scanner screen shows **"No radio device detected. Plug in the
  RTL-SDR (with a USB-C OTG adapter) and tap RESUME."** Remote rtl_tcp is not
  checked. If the check itself fails, scanning goes ahead as before. — **Built**
- If the RTL-SDR is unplugged during a scan, the Scanner screen says
  "Radio device disconnected" instead of silently stopping. — **Built**

---

## 1.0.0-lsm8 — scanner no longer abandons long calls

### Fixed
- **Radio restarted on another control channel in the middle of a call.**
  The app decides a control channel is lost if it hears nothing for 15 s.
  The native side only reported signal when the TSBK counters or sync type
  changed, and on a voice channel neither changes. Any call longer than about
  15 s therefore looked like a lost signal, and the app stopped the engine and
  moved to the next control channel. The native poll loop now also reports
  about once a second while in sync. Present in the original app.
  (`dsd_flutter_jni.cpp`) — **Built**
- **Stuck on a dead control channel.** Neighbor-site list changes counted as
  activity even with no signal (entries aging out change the count), so a
  channel with no sync kept looking alive and the app never moved on. Neighbor
  updates now count only if sync was seen in the last 3 s.
  (`scanning_service.dart`) — **Built**

---

## 1.0.0-lsm7 — silent "tuned" state after start fix

### Fixed
- **Call shown as followed but nothing heard (receiver stayed on the control
  channel).** The app freezes retunes every time the native-USB engine starts or
  switches control channel, until it has seen a steady lock. A grant arriving in
  that window made DSD-Neo's trunking state machine switch to TUNED and mark the
  receiver as on the voice channel, but the hardware retune was silently refused,
  so it sat on the control channel until the grant timed out. Present in the
  original app; more visible when the app cycles control channels. The state
  machine now defers grants while retunes are frozen (log line
  `grant-deferred-retune-frozen`); the repeated grant updates pick the call up
  once retunes are allowed. (`p25_trunk_sm.c`) — **Built**
- **Session footer showed `system id: ?` / `control channels: ?`.** The footer
  now reuses the values recorded earlier in the session. — **Built**

---

## 1.0.0-lsm6 — missed calls fix, session log header

### Fixed
- **Wanted calls skipped when two calls are announced together** (DSD-Neo,
  `p25p2_vpdu.c`; also used for Phase 1 TSBKs). Grant-update messages often list
  two active calls. The loop tried the first one and then always stopped,
  even if that one was blocked (talkgroup filter, lockout, hold). So with
  "Follow my talkgroups only" on, a wanted talkgroup listed second
  (e.g. `Ch1 TG 42057 / Ch2 TG 12302`) was never tuned. Now it only stops after
  a candidate actually tuned. Affects all three multi-candidate loops. *Built.*

### Added
- **Log session header and footer.** Each new scan session starts the log with
  `[cfg]` lines: start time, app version, mode/site/system, modulation, control
  channels, GPS hopping, SDR settings (source, gain, PPM, bias-T, sample rate),
  call-following switches and talkgroup filter. The same block is written
  again when the session ends (with duration), so long sessions still show
  their configuration. *Built.*
- Log now holds **2,000 lines** (rolling).

### Changed
- The log is no longer wiped when the scanner moves to another control channel
  in the same session (the engine restarts internally). It's cleared only when
  a new session starts.

## 1.0.0-lsm5 — call following

### Added
- **Settings → SDR Settings → Trunked Call Following** (both **off** by default,
  so behavior is unchanged unless you turn them on):
  - **Skip encrypted calls** — sets DSD-Neo `trunk_tune_enc_calls = 0`.
    Encrypted grants are skipped; a call discovered to be encrypted mid-stream
    (e.g. Motorola regroup supergroups, whose grants carry no encryption flag)
    is released and that group is locked out for the session. *Built.*
  - **Follow my talkgroups only** — during a system scan, the talkgroup filter
    becomes an allow list: the system's talkgroups minus any muted in Scan Grid.
    Talkgroups not in the system's list are neither tuned nor played. If the
    allow list would be empty it falls back to the original mute list. *Built.*
- Both settings are applied each time a scan starts and when the system changes
  (e.g. GPS site hopping).

### Why
A Pocket25 log from Washtenaw showed clean decoding (steady sync, one voice
FEC error in ~1,100 lines) but choppy listening: with a single receiver the
scanner followed the *first* grant — including 4–5 s encrypted regroup calls
(ALG 0xAA) and talkgroups not in the user's list — while the wanted talkgroup
(12302) was active on another channel. The original talkgroup filter only
muted audio after tuning; it did not stop the tune.

---

## 1.0.0-lsm4 — log save

### Added
- **Log → Save** writes `Download/Pocket25/pocket25-log-YYYYMMDD-HHMMSS.txt`
  via Android MediaStore (Android 10+, no storage permission). *Tested.*

## 1.0.0-lsm3 — stop, log, Advanced Scan, PPM

### Added
- **STOP** button on the main Scanner screen (next to LOCKED/SEARCHING);
  becomes **RESUME <site>** when stopped. Resume re-reads settings and the
  site from the database (same path as Settings → Systems). *Tested.*
- **Log → Copy all.** Log is a 1,000-line rolling buffer (was 500), stays
  visible after Stop, and is cleared when the next scan starts. *Tested.*
- **Advanced Scan remembers the last command.** *Built.*

### Fixed
- **Advanced Scan with Native USB**: an `-i rtl:…` input now opens the dongle
  through Android USB permission / file descriptor like normal scans
  (was: "failed to open rtlsdr device via usb"). rtltcp:, HackRF and other
  inputs unchanged. *Built.*
- Misleading `WARNING: Failed to set ppm error` when the PPM was simply
  unchanged (librtlsdr returns -2). Now logs `Tuner error already N ppm
  (unchanged)`. *Built.*
- Auto modulation now always returns to the input type's own defaults, so a
  HackRF used after an LSM site is not left locked to CQPSK. *Built.*

## 1.0.0-lsm2 — stop fix

### Fixed
- Stopping from Quick Scan / Advanced Scan during a trunked scan left the
  scanning service's lock-check timer running, which restarted the scan
  10–15 s later. Stop now ends the system scan properly. *Tested.*

## 1.0.0-lsm1 — LSM simulcast and V4L

### Added
- **Per-site Modulation: Auto / C4FM / LSM (CQPSK)** — web programmer site
  editor dropdown, shown on site cards, stored in the database (schema v5,
  existing sites migrate to Auto), and included in CSV export/import as a
  15th column (14-column files still import). LSM enables DSD-Neo's CQPSK
  front end and locks the demodulator. *Tested — Washtenaw locks with LSM.*
- **RTL-SDR Blog V4L (V4 Lite, R828S tuner)** support ported from the official
  rtl-sdr-blog driver: HF/UHF diplexed input switching, upconverter GPIO5,
  VCO power reference, HF upconvert offset. Detected by USB strings
  `RTLSDRBlog` / `Blog V4L` (exact match — V3/generic R820T and V4 paths
  unchanged). *Tested on V4L; V3 untested.*

### Fixed
- Root cause of no-lock on LSM simulcast: on RTL-SDR (native USB and
  RTL-TCP) Pocket25 never enabled QPSK, so DSD-Neo's CQPSK front end stayed
  off; only the HackRF path enabled it. Affects **all RTL dongles**
  (V3, V4, V4L, generic).
- Stale Advanced Scan arguments (including its `-i rtl:…:-2:…` PPM) were
  re-applied to later normal scans until the app was force-stopped.
- Web programmer CSV export wrote talkgroup rows two columns short, so they
  were misaligned and skipped on re-import.
- Web programmer site editor rejected 4-decimal control-channel frequencies
  (e.g. 855.2875) due to `step="0.001"`.

---

## Known limitations

- **One voice receiver.** The phone follows one call at a time. Two
  talkgroups talking at once → you hear whichever was granted first.
  See `docs/MULTI-CHANNEL-PLAN.md`.
- **Bundled DSD-Neo is from January 2026** (upstream `de27a5ba`, pre-v1.0.0).
  See `EVAL-dsd-neo-update.md` in the project for what updating it involves.
- **Untested here:** HackRF, RTL-TCP remote, RTL-SDR V3, DMR/NXDN systems.
