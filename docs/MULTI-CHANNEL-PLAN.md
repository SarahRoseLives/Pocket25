# Planning: SDRTrunk-style multi-channel decoding in Pocket25

Status: **proposal / not started.** This explains how Pocket25 differs from
SDRTrunk, what it would take to close the gap, and what each step buys you.

## The short version

SDRTrunk's "mono / stereo" audio is just its playback choice. What really sets
it apart is underneath: it **decodes several voice channels at the same time**
and then picks what to play. Pocket25 has **one decoder that retunes the radio
for each call**. All of this is software; nothing about the phone or the
dongle prevents it. It's an architecture change, not a patch.

## How each works today

| | SDRTrunk | Pocket25 (DSD-Neo) |
|---|---|---|
| Radio | Holds one center frequency, captures a ~2.4 MHz slice | Retunes the dongle for every grant |
| Channels | Channelizer splits the slice into many 12.5 kHz channels | One channel at a time |
| Decoders | One per active voice channel, in parallel | One per app |
| Audio | Mixer: mono = highest priority; stereo = two calls (L/R) | Whatever the one decoder is on |
| Missed calls | Only calls outside the captured slice | Any call that overlaps the one being followed |

## What it would take

1. **Fixed-center capture.** Stop retuning per grant; keep the dongle on a
   center that covers the control channel and as many voice channels as fit.
2. **Channelizer.** A polyphase filter bank (or per-channel DDC) that turns the
   2.4 MS/s stream into many narrowband streams. Standard DSP; the Pixel 8 has
   ample CPU. *Moderate.*
3. **Multiple decoder instances.** The hard part. DSD-Neo, and Pocket25's JNI
   bridge (`g_opts` / `g_state` singletons, global audio/filter state), assume
   one decoder per process. Options:
   - Refactor DSD-Neo to keep all state per instance. *Large; touches most of DSD-Neo.*
   - Run N decoder processes or services and pipe audio and events back. *Large;
     Android process and IPC overhead, but leaves DSD-Neo mostly intact.*
   - Use only DSD-Neo's IMBE voice decoding plus a new lightweight P25
     channel/frame layer per channel. *Large; it's a new engine.*
4. **Call manager.** Assigns grants to free channel decoders, tracks call
   start and end, and applies talkgroup priority and lockouts. *Moderate.*
5. **Audio mixer.** Mono (priority with preemption) and stereo (two calls,
   left/right), with a hold time. *Moderate.*
6. **UI.** Multiple active calls on the Scanner screen, priorities per
   talkgroup, and a mono/stereo setting. *Moderate.*

Rough size: **weeks**, plus field testing on real systems.

## Hardware limit to plan around

One RTL-SDR covers about **2.4 MHz**. The Washtenaw site's voice channels seen
in logs span about **851.86–856.19 MHz (~4.3 MHz)**, so a single dongle can't
see every call even with perfect software. SDRTrunk on a Mac with one dongle
has the same limit. Full coverage of this site needs **two dongles** (or a
wider receiver such as an Airspy), and the engine would need to manage
more than one tuner. Android USB hubs and power make two dongles on a phone
possible but awkward.

## Cheaper steps, in order of value

| Step | Effort | What it gets you |
|---|---|---|
| Skip encrypted calls + follow my talkgroups only | **Done in lsm5** | Removes most dead air and wrong-call time |
| Priority preemption (jump to a higher-priority TG's grant mid-call) | Hours | Closest single-receiver equivalent of SDRTrunk mono |
| Update bundled DSD-Neo to current release | Days (JNI bridge touches internals) | Newer simulcast/LSM decoding fixes |
| Home SDRTrunk + stream to phone (Broadcastify / rdio-scanner) | No code | Full SDRTrunk behavior on the phone; radio stays at home |
| Multi-channel engine (this plan) | Weeks | True SDRTrunk-style decoding in your pocket |

## Tradeoffs

- **Battery and heat.** A continuous wideband channelizer plus several decoders
  uses much more CPU than one decoder. Expect noticeably higher drain.
- **Complexity versus maintainability.** A multi-decoder engine diverges from
  upstream DSD-Neo, making future DSD-Neo updates harder to merge.
- **Coverage still depends on the dongle.** Without a second tuner, wide systems
  still miss calls.
- **Upstream alignment.** Before investing, check whether DSD-Neo's own
  Android app or roadmap is heading toward multi-channel support. Building on
  that would be cheaper than building alongside it.
