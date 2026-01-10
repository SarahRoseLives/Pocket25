// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Public API for mbelib-neo vocoder primitives and helpers.
 *
 * This header exposes the stable `mbe_` API used to decode IMBE/AMBE frames
 * and synthesize 8 kHz PCM audio. It is installed for consumers and meant to
 * remain ABI stable across minor releases.
 */

#ifndef MBELIB_NEO_PUBLIC_MBEBELIB_H
#define MBELIB_NEO_PUBLIC_MBEBELIB_H

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

// Expose project version macro. In normal builds this header is
// generated into the build include dir; provide a fallback for linting.
#if defined(__has_include)
#if __has_include("mbelib-neo/version.h")
#include "mbelib-neo/version.h"
#endif
#endif
#ifndef MBELIB_VERSION
#define MBELIB_VERSION "0.0.0-dev"
#endif

#if !defined(MBE_API)
#if defined(_WIN32) || defined(__CYGWIN__)
/*
 * On Windows, exporting from the DLL uses dllexport when building the
 * shared library and dllimport when consuming it. For static libraries,
 * dllimport is incorrect and can cause unresolved externals; in that case
 * consumers should see an empty decoration. We propagate MBE_STATIC from
 * the CMake target for static consumption.
 */
#if defined(MBE_STATIC)
#define MBE_API
#elif defined(MBE_BUILDING)
#define MBE_API __declspec(dllexport)
#else
#define MBE_API __declspec(dllimport)
#endif
#else
#define MBE_API __attribute__((visibility("default")))
#endif
#endif

#if !defined(MBE_DEPRECATED)
#if defined(_MSC_VER)
#define MBE_DEPRECATED(msg) __declspec(deprecated(msg))
#elif defined(__GNUC__) || defined(__clang__)
#define MBE_DEPRECATED(msg) __attribute__((deprecated(msg)))
#else
#define MBE_DEPRECATED(msg)
#endif
#endif

#if !defined(MBE_DEPRECATED_FOR)
#define MBE_DEPRECATED_FOR(newsym) MBE_DEPRECATED("Use " #newsym)
#endif

struct mbe_parameters {
    /** Fundamental radian frequency (w0). */
    float w0;
    /** Number of harmonic bands (L). */
    int L;
    /** Number of voiced bands (K). */
    int K;
    /** Voiced/unvoiced flags per band (1..56). */
    int Vl[57];
    /** Magnitude per band (1..56). */
    float Ml[57];
    /** Base-2 log magnitude per band (1..56). */
    float log2Ml[57];
    /** Absolute phase per band (1..56). */
    float PHIl[57];
    /** Smoothed phase per band (1..56). */
    float PSIl[57];
    /** Spectral amplitude enhancement scale. */
    float gamma;
    /** Legacy/unused field; retained for ABI compatibility. */
    int un;
    /** Repeat frame flag (legacy - use repeatCount instead). */
    int repeat;
    /** Sine wave increment for tone synthesis. */
    int swn;

    /* === Adaptive smoothing state (Algorithms #111-116) === */
    /** Local energy tracking with IIR smoothing (Algorithm #111). */
    float localEnergy;
    /** Amplitude threshold for scaling (Algorithm #115). */
    int amplitudeThreshold;
    /** Bit error rate for current frame (0.0 to 1.0). */
    float errorRate;
    /** Total bit errors detected/corrected in this frame. */
    int errorCountTotal;
    /** Coset 4 error count (IMBE-specific). */
    int errorCount4;

    /* === Frame repeat/muting state === */
    /** Consecutive repeat count (0 to MAX_FRAME_REPEATS). */
    int repeatCount;
    /** Muting threshold for this codec (IMBE: 0.0875, AMBE: 0.096). */
    float mutingThreshold;

    /* === FFT-based unvoiced synthesis state === */
    /** Previous frame inverse FFT output for WOLA (256 samples). */
    float previousUw[256];
    /** LCG noise generator state (seed). */
    float noiseSeed;
    /** Noise buffer overlap for continuity (96 samples). */
    float noiseOverlap[96];
};

typedef struct mbe_parameters mbe_parms;

/**
 * @brief Correct a (23,12) Golay encoded block in-place and extract data.
 * @param block Pointer to packed 23-bit block (upper bits ignored). On return, contains 12-bit data.
 */
MBE_API void mbe_checkGolayBlock(long int* block);
/**
 * @brief Decode a (23,12) Golay codeword.
 * @param in  Input bits, LSB at index 0, length 23.
 * @param out Output bits, corrected, LSB at index 0, length 23.
 * @return Number of corrected bit errors in the protected portion.
 */
MBE_API int mbe_golay2312(char* in, char* out);
/**
 * @brief Decode a (15,11) Hamming codeword (IMBE/AMBE common use).
 * @param in  Input bits, LSB at index 0, length 15.
 * @param out Output bits, corrected, LSB at index 0, length 15.
 * @return Number of corrected bit errors (0 or 1).
 */
MBE_API int mbe_hamming1511(char* in, char* out);
/**
 * @brief Decode a (15,11) Hamming codeword with IMBE 7100x4400 mapping.
 * @param in  Input bits, LSB at index 0, length 15.
 * @param out Output bits, corrected, LSB at index 0, length 15.
 * @return Number of corrected bit errors (0 or 1).
 */
MBE_API int mbe_7100x4400hamming1511(char* in, char* out);

/* Prototypes from ambe3600x2400.c */
/**
 * @brief Apply ECC to AMBE 3600x2400 C0 and update in-place.
 * @param ambe_fr AMBE frame as 4x24 bitplanes.
 * @return Number of corrected errors in C0.
 */
MBE_API int mbe_eccAmbe3600x2400C0(char ambe_fr[4][24]);
/**
 * @brief Apply ECC to AMBE 3600x2400 data and pack parameters.
 * @param ambe_fr AMBE frame as 4x24 bitplanes.
 * @param ambe_d  Output parameter bits (49).
 * @return Number of corrected errors in protected fields.
 */
MBE_API int mbe_eccAmbe3600x2400Data(char ambe_fr[4][24], char* ambe_d);
/**
 * @brief Decode AMBE 2400 parameters from demodulated bits.
 * @param ambe_d  Demodulated AMBE parameter bits (49).
 * @param cur_mp  Output: current frame parameters.
 * @param prev_mp Input: previous frame parameters (for prediction).
 * @return Tone index or 0 for voice; implementation-specific non-zero for tone frames.
 */
MBE_API int mbe_decodeAmbe2400Parms(char* ambe_d, mbe_parms* cur_mp, mbe_parms* prev_mp);
/**
 * @brief Demodulate interleaved AMBE 3600x2400 data in-place.
 * @param ambe_fr AMBE frame as 4x24 bitplanes, updated in-place.
 */
MBE_API void mbe_demodulateAmbe3600x2400Data(char ambe_fr[4][24]);
/**
 * @brief Process AMBE 2400 parameters into 8 kHz float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param errs     Output: corrected error count in protected fields.
 * @param errs2    Output: raw parity mismatch count.
 * @param err_str  Output: human-readable error summary (optional).
 * @param ambe_d   Demodulated parameter bits (49).
 * @param cur_mp   In/out: current frame parameters (may be enhanced).
 * @param prev_mp  In/out: previous frame parameters.
 * @param prev_mp_enhanced In/out: enhanced previous parameters for continuity.
 * @param uvquality Unvoiced synthesis quality (1..64).
 */
MBE_API void mbe_processAmbe2400Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                                      mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                                      int uvquality);
/**
 * @brief Process AMBE 2400 parameters into 8 kHz 16-bit PCM.
 * @see mbe_processAmbe2400Dataf for details.
 */
MBE_API void mbe_processAmbe2400Data(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                                     mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
/**
 * @brief Process a complete AMBE 3600x2400 frame into 8 kHz float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param errs,errs2,err_str Error reporting as per Dataf variant.
 * @param ambe_fr  Input frame as 4x24 bitplanes.
 * @param ambe_d   Scratch/output parameter bits (49).
 * @param cur_mp,prev_mp,prev_mp_enhanced Parameter state as per Dataf variant.
 * @param uvquality Unvoiced synthesis quality (1..64).
 */
MBE_API void mbe_processAmbe3600x2400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                                            char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                            mbe_parms* prev_mp_enhanced, int uvquality);
/**
 * @brief Process a complete AMBE 3600x2400 frame into 8 kHz 16-bit PCM.
 * @see mbe_processAmbe3600x2400Framef for details.
 */
MBE_API void mbe_processAmbe3600x2400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                                           char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                           mbe_parms* prev_mp_enhanced, int uvquality);

/* Prototypes from ambe3600x2450.c */
/** @brief ECC correction for AMBE 3600x2450 C0. */
MBE_API int mbe_eccAmbe3600x2450C0(char ambe_fr[4][24]);
/** @brief ECC and parameter packing for AMBE 3600x2450. */
MBE_API int mbe_eccAmbe3600x2450Data(char ambe_fr[4][24], char* ambe_d);
/** @brief Decode AMBE 2450 parameters. */
MBE_API int mbe_decodeAmbe2450Parms(char* ambe_d, mbe_parms* cur_mp, mbe_parms* prev_mp);
/** @brief Demodulate AMBE 3600x2450 interleaved data. */
MBE_API void mbe_demodulateAmbe3600x2450Data(char ambe_fr[4][24]);
/** @brief Process AMBE 2450 parameters into float PCM. */
MBE_API void mbe_processAmbe2450Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                                      mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                                      int uvquality);
/** @brief Process AMBE 2450 parameters into 16-bit PCM. */
MBE_API void mbe_processAmbe2450Data(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49],
                                     mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
/** @brief Process AMBE 3600x2450 frame into float PCM. */
MBE_API void mbe_processAmbe3600x2450Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                                            char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                            mbe_parms* prev_mp_enhanced, int uvquality);
/** @brief Process AMBE 3600x2450 frame into 16-bit PCM. */
MBE_API void mbe_processAmbe3600x2450Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                                           char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                           mbe_parms* prev_mp_enhanced, int uvquality);

/* Prototypes from imbe7200x4400.c */
/** @brief Print IMBE 4400 parameter bits to stderr (debug). */
MBE_API void mbe_dumpImbe4400Data(char* imbe_d);
/** @brief Print IMBE 7200x4400 parameter bits to stderr (debug). */
MBE_API void mbe_dumpImbe7200x4400Data(char* imbe_d);
/** @brief Print a raw IMBE 7200x4400 frame to stderr (debug). */
MBE_API void mbe_dumpImbe7200x4400Frame(char imbe_fr[8][23]);
/** @brief ECC correction for IMBE 7200x4400 C0. */
MBE_API int mbe_eccImbe7200x4400C0(char imbe_fr[8][23]);
/** @brief ECC and parameter packing for IMBE 7200x4400. */
MBE_API int mbe_eccImbe7200x4400Data(char imbe_fr[8][23], char* imbe_d);
/** @brief Decode IMBE 4400 parameters. */
MBE_API int mbe_decodeImbe4400Parms(char* imbe_d, mbe_parms* cur_mp, mbe_parms* prev_mp);
/** @brief Demodulate IMBE 7200x4400 interleaved data. */
MBE_API void mbe_demodulateImbe7200x4400Data(char imbe[8][23]);
/** @brief Process IMBE 4400 parameters into float PCM. */
MBE_API void mbe_processImbe4400Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char imbe_d[88],
                                      mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                                      int uvquality);
/** @brief Process IMBE 4400 parameters into 16-bit PCM. */
MBE_API void mbe_processImbe4400Data(short* aout_buf, int* errs, int* errs2, char* err_str, char imbe_d[88],
                                     mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality);
/** @brief Process IMBE 7200x4400 frame into float PCM. */
MBE_API void mbe_processImbe7200x4400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[8][23],
                                            char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                            mbe_parms* prev_mp_enhanced, int uvquality);
/** @brief Process IMBE 7200x4400 frame into 16-bit PCM. */
MBE_API void mbe_processImbe7200x4400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[8][23],
                                           char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                           mbe_parms* prev_mp_enhanced, int uvquality);

/* Prototypes from imbe7100x4400.c */
/** @brief Print IMBE 7100x4400 parameter bits to stderr (debug). */
MBE_API void mbe_dumpImbe7100x4400Data(char* imbe_d);
/** @brief Print IMBE 7100x4400 frame to stderr (debug). */
MBE_API void mbe_dumpImbe7100x4400Frame(char imbe_fr[7][24]);
/** @brief ECC correction for IMBE 7100x4400 C0. */
MBE_API int mbe_eccImbe7100x4400C0(char imbe_fr[7][24]);
/** @brief ECC and parameter packing for IMBE 7100x4400. */
MBE_API int mbe_eccImbe7100x4400Data(char imbe_fr[7][24], char* imbe_d);
/** @brief Demodulate IMBE 7100x4400 interleaved data. */
MBE_API void mbe_demodulateImbe7100x4400Data(char imbe[7][24]);
/** @brief Convert IMBE 7100x4400 parameter set into 7200x4400 layout. */
MBE_API void mbe_convertImbe7100to7200(char* imbe_d);
/** @brief Process IMBE 7100x4400 frame into float PCM. */
MBE_API void mbe_processImbe7100x4400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[7][24],
                                            char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                            mbe_parms* prev_mp_enhanced, int uvquality);
/** @brief Process IMBE 7100x4400 frame into 16-bit PCM. */
MBE_API void mbe_processImbe7100x4400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char imbe_fr[7][24],
                                           char imbe_d[88], mbe_parms* cur_mp, mbe_parms* prev_mp,
                                           mbe_parms* prev_mp_enhanced, int uvquality);

/* Prototypes from mbelib.c */
/**
 * @brief Write the mbelib-neo version string into the provided buffer.
 * @param str Output buffer receiving a NUL-terminated version string.
 */
MBE_API void mbe_printVersion(char* str);
/**
 * @brief Get a pointer to a static NUL-terminated version string.
 *        The returned pointer remains valid for the lifetime of the program.
 */
MBE_API const char* mbe_versionString(void);

/**
 * @brief Set the thread-local RNG seed used by unvoiced synthesis noise.
 *        Improves determinism and thread-safety vs. global rand().
 * @param seed Any non-zero 32-bit seed value.
 */
MBE_API void mbe_setThreadRngSeed(uint32_t seed);
/**
 * @brief Copy MBE parameter set from one struct to another.
 * @param cur_mp Source parameters.
 * @param prev_mp Destination parameters.
 */
MBE_API void mbe_moveMbeParms(mbe_parms* cur_mp, mbe_parms* prev_mp);
/**
 * @brief Replace current parameters with the last known parameters.
 * @param cur_mp Destination parameters to fill.
 * @param prev_mp Source parameters from previous frame.
 */
MBE_API void mbe_useLastMbeParms(mbe_parms* cur_mp, mbe_parms* prev_mp);
/**
 * @brief Initialize parameter state for decoding and synthesis.
 * @param cur_mp Output: current parameter state.
 * @param prev_mp Output: previous parameter state (zeroed/reset).
 * @param prev_mp_enhanced Output: enhanced previous parameter state.
 */
MBE_API void mbe_initMbeParms(mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced);
/**
 * @brief Apply spectral amplitude enhancement in-place.
 * @param cur_mp In/out parameter set to enhance.
 */
MBE_API void mbe_spectralAmpEnhance(mbe_parms* cur_mp);
/**
 * @brief Synthesize tone frame (AMBE tone indices) into float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param ambe_d   AMBE parameter bits (49).
 * @param cur_mp   Current parameter set (tone synthesis state).
 */
MBE_API void mbe_synthesizeTonef(float* aout_buf, char* ambe_d, mbe_parms* cur_mp);
/**
 * @brief Synthesize tone for D-STAR style indices into float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param ambe_d   AMBE parameter bits (49).
 * @param cur_mp   Current parameter set.
 * @param ID1      Tone index selector.
 */
MBE_API void mbe_synthesizeTonefdstar(float* aout_buf, char* ambe_d, mbe_parms* cur_mp, int ID1);
/** @brief Fill float PCM buffer with 160 samples of silence. */
MBE_API void mbe_synthesizeSilencef(float* aout_buf);
/** @brief Fill 16-bit PCM buffer with 160 samples of silence. */
MBE_API void mbe_synthesizeSilence(short* aout_buf);
/**
 * @brief Synthesize one speech frame into float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param cur_mp   Current parameter set.
 * @param prev_mp  Previous parameter set.
 * @param uvquality Unvoiced synthesis quality (1..64).
 */
MBE_API void mbe_synthesizeSpeechf(float* aout_buf, mbe_parms* cur_mp, mbe_parms* prev_mp, int uvquality);
/** @brief Synthesize one speech frame into 16-bit PCM. */
MBE_API void mbe_synthesizeSpeech(short* aout_buf, mbe_parms* cur_mp, mbe_parms* prev_mp, int uvquality);
/**
 * @brief Convert 160 float samples to clipped/scaled 16-bit PCM.
 * @param float_buf Input 160 float samples.
 * @param aout_buf  Output 160 16-bit samples.
 */
MBE_API void mbe_floattoshort(float* float_buf, short* aout_buf);

/* === Frame repeat and muting functions === */

/** Maximum consecutive frame repeats before muting. */
#define MBE_MAX_FRAME_REPEATS     4

/** IMBE muting threshold (8.75% error rate). */
#define MBE_MUTING_THRESHOLD_IMBE 0.0875f

/** AMBE muting threshold (9.6% error rate). */
#define MBE_MUTING_THRESHOLD_AMBE 0.096f

/**
 * @brief Check if frame should be muted due to excessive errors.
 * @param mp Parameter set to check.
 * @return Non-zero if frame should be muted.
 */
MBE_API int mbe_requiresMuting(const mbe_parms* mp);

/**
 * @brief Check if max repeat threshold has been exceeded.
 * @param mp Parameter set to check.
 * @return Non-zero if repeatCount >= MBE_MAX_FRAME_REPEATS.
 */
MBE_API int mbe_isMaxFrameRepeat(const mbe_parms* mp);

/**
 * @brief Generate comfort noise for muted frames.
 * @param aout_buf Output buffer of 160 float samples.
 */
MBE_API void mbe_synthesizeComfortNoisef(float* aout_buf);

/**
 * @brief Generate comfort noise for muted frames (16-bit).
 * @param aout_buf Output buffer of 160 16-bit samples.
 */
MBE_API void mbe_synthesizeComfortNoise(short* aout_buf);

/* === Adaptive smoothing functions === */

/**
 * @brief Apply adaptive smoothing to parameters based on error rates.
 *        Implements JMBE Algorithms #111-116.
 * @param cur_mp Current frame parameters (modified in-place).
 * @param prev_mp Previous frame parameters (for local energy).
 */
MBE_API void mbe_applyAdaptiveSmoothing(mbe_parms* cur_mp, const mbe_parms* prev_mp);

/**
 * @brief Check if adaptive smoothing is required based on error rates.
 * @param mp Parameter set to check.
 * @return Non-zero if smoothing should be applied.
 */
MBE_API int mbe_requiresAdaptiveSmoothing(const mbe_parms* mp);

#ifdef __cplusplus
}
#endif

#endif // MBELIB_NEO_PUBLIC_MBEBELIB_H
