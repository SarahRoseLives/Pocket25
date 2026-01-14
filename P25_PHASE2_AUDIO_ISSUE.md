# P25 Phase 2 Audio Quality Issue

## Problem
P25 Phase 2 audio is choppy with periodic pauses, while Phase 1 audio works perfectly.

## Root Cause
This is an **upstream issue in DSD-Neo** (and dsd-fme) architecture:

1. **Bursty Audio Output**: Phase 2 uses `playSynthesizedVoiceSS18()` which accumulates 18 voice frames (~360ms of audio) before outputting them all at once via 18 sequential `write_s16_audio()` calls.

2. **Timing Mismatch**: The Android OpenSL ES callback consumes audio every ~32ms (256 frames @ 8kHz), but SS18 dumps 2880 frames in a burst, then nothing for hundreds of milliseconds until the next superframe.

3. **Signal Quality Impact**: Reed-Solomon errors on either slot can disrupt frame accumulation timing, causing additional gaps.

## What We Tried

### Android Audio Buffering
- ✗ Increased ring buffer to 2 seconds  
- ✗ Changed buffer size from 1024→256 frames
- ✗ Increased buffer count from 2→4
- ✗ Made writes non-blocking with overflow handling
- ✗ Added 1-second pre-buffering of silence

### Phase 2 Audio Architecture  
- ✗ Switched from SS18 (18-frame) to SS4 (4-frame) output - didn't work properly
- ✗ Disabled slot 2 to eliminate R-S errors - slots are still decoded
- ✗ Changed from stereo to mono - didn't help

## Why Phase 1 Works
Phase 1 uses `playSynthesizedVoiceSS()` which outputs **1 frame at a time** (160 samples = 20ms), resulting in smooth continuous audio without bursty behavior.

## Technical Details

### Phase 2 Timing
- Superframe = 18 voice frames
- Each frame = 160 samples @ 8kHz = 20ms
- Total per superframe = 2880 frames = 360ms
- SS18 called once per superframe, writes all 18 frames at once

### Phase 1 Timing  
- LDU has 9 voice frames
- SS mixer called 9 times per LDU
- Each call writes 160 samples (20ms)
- Continuous smooth output

## Possible Solutions (Not Implemented)

1. **Modify DSD-Neo SS18**: Change to output incrementally as frames are decoded instead of batching 18 frames
2. **Use Software Resampler**: Add a jitter buffer/resampler between DSD and Android audio
3. **Switch to Floating Point**: Try `playSynthesizedVoiceFS4()` (requires floating_point=1)
4. **Signal Quality**: Improve antenna/SDR positioning to reduce R-S errors

## Conclusion
This is a fundamental limitation of DSD-Neo's Phase 2 audio architecture. The issue also exists in dsd-fme. A proper fix requires modifying the core DSD decoder to output Phase 2 audio incrementally rather than in 360ms bursts.

## Files Modified (Reverted)
- `android/src/main/cpp/dsd-neo/src/platform/audio_android.c` - Buffer tuning attempts
- `android/src/main/cpp/dsd-neo/src/protocol/p25/phase2/p25p2_frame.c` - SS4 attempt
- `android/src/main/cpp/dsd_flutter_jni.cpp` - Slot disable attempt, stereo config

Current state: All experimental changes reverted. Using upstream DSD-Neo defaults.
Phase 2 audio quality remains suboptimal due to upstream issue.
