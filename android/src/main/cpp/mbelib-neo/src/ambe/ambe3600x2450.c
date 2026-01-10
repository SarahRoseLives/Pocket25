// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 *
 * Copyright (C) 2010 mbelib Author
 * GPG Key ID: 0xEA5EFE2C (9E7A 5527 9CDC EBF7 BF1B  D772 4F98 E863 EA5E FE2C)
 *
 * Portions were originally under the ISC license; this mbelib-neo
 * distribution is provided under GPL-2.0-or-later. See LICENSE for details.
 */

/**
 * @file
 * @brief AMBE 3600x2450 parameter decode, ECC, and synthesis hooks.
 */

#include <math.h>
#include <stdio.h>

#include "ambe3600x2450_const.h"
#include "ambe_common.h"
#include "mbe_compiler.h"
#include "mbelib-neo/mbelib.h"

/**
 * @brief Thread-local cache for AMBE DCT cosine coefficients.
 *
 * Pre-computes the cosine terms used in the Ri inverse DCT and per-block
 * inverse DCT loops to eliminate repeated cosf() calls per frame.
 *
 * Ri DCT: cosf(M_PI * (m-1) * (i-0.5) / 8) for m=1..8, i=1..8
 * Per-block IDCT: cosf(M_PI * (k-1) * (j-0.5) / ji) for ji=1..17, j=1..ji, k=1..ji
 */
struct ambe2450_dct_cache {
    int inited;
    float ri_cos[9][9];         /* [m][i] for m=1..8, i=1..8 (index 0 unused) */
    float idct_cos[18][18][18]; /* [ji][j][k] for ji=1..17, j=1..ji, k=1..ji */
};

static MBE_THREAD_LOCAL struct ambe2450_dct_cache ambe2450_cache = {0};

/**
 * @brief Initialize or return the thread-local AMBE 2450 DCT cache.
 *
 * Fills the cosine tables on first use. Because the cache is thread-local,
 * no locking is required.
 *
 * @return Pointer to the initialized cache.
 */
static struct ambe2450_dct_cache*
ambe2450_get_dct_cache(void) {
    if (ambe2450_cache.inited) {
        return &ambe2450_cache;
    }

    /* Fill Ri DCT cosine table: cosf(M_PI * (m-1) * (i-0.5) / 8) */
    for (int m = 1; m <= 8; m++) {
        for (int i = 1; i <= 8; i++) {
            ambe2450_cache.ri_cos[m][i] = cosf((M_PI * (float)(m - 1) * ((float)i - 0.5f)) / 8.0f);
        }
    }

    /* Fill per-block IDCT cosine table: cosf(M_PI * (k-1) * (j-0.5) / ji) */
    for (int ji = 1; ji <= 17; ji++) {
        for (int j = 1; j <= ji; j++) {
            for (int k = 1; k <= ji; k++) {
                ambe2450_cache.idct_cos[ji][j][k] = cosf((M_PI * (float)(k - 1) * ((float)j - 0.5f)) / (float)ji);
            }
        }
    }

    ambe2450_cache.inited = 1;
    return &ambe2450_cache;
}

/**
 * @brief Print AMBE 2450 parameter bits to stderr (debug aid).
 * @param ambe_d AMBE parameter bits (49).
 */
void
mbe_dumpAmbe2450Data(char* ambe_d) {

    int i;
    char* ambe;

    ambe = ambe_d;
    for (i = 0; i < 49; i++) {
        fprintf(stderr, "%i", *ambe);
        ambe++;
    }
    fprintf(stderr, " ");
}

/**
 * @brief Print raw AMBE 3600x2450 frame bitplanes to stderr.
 * @param ambe_fr Frame as 4x24 bitplanes.
 */
void
mbe_dumpAmbe3600x2450Frame(char ambe_fr[4][24]) {

    int j;

    // c0
    fprintf(stderr, "ambe_fr c0: ");
    for (j = 23; j >= 0; j--) {
        fprintf(stderr, "%i", ambe_fr[0][j]);
    }
    fprintf(stderr, " ");
    // c1
    fprintf(stderr, "ambe_fr c1: ");
    for (j = 22; j >= 0; j--) {
        fprintf(stderr, "%i", ambe_fr[1][j]);
    }
    fprintf(stderr, " ");
    // c2
    fprintf(stderr, "ambe_fr c2: ");
    for (j = 10; j >= 0; j--) {
        fprintf(stderr, "%i", ambe_fr[2][j]);
    }
    fprintf(stderr, " ");
    // c3
    fprintf(stderr, "ambe_fr c3: ");
    for (j = 13; j >= 0; j--) {
        fprintf(stderr, "%i", ambe_fr[3][j]);
    }
    fprintf(stderr, " ");
}

/**
 * @brief Apply ECC to AMBE 3600x2450 C0 and update in-place.
 * @param ambe_fr Frame as 4x24 bitplanes.
 * @return Number of corrected errors in C0.
 */
int
mbe_eccAmbe3600x2450C0(char ambe_fr[4][24]) {
    return mbe_eccAmbe3600C0_common(ambe_fr);
}

/**
 * @brief Apply ECC to AMBE 3600x2450 data and pack parameter bits.
 * @param ambe_fr Frame as 4x24 bitplanes.
 * @param ambe_d  Output parameter bits (49).
 * @return Number of corrected errors in protected fields.
 */
int
mbe_eccAmbe3600x2450Data(char ambe_fr[4][24], char* ambe_d) {
    return mbe_eccAmbe3600Data_common(ambe_fr, ambe_d);
}

/**
 * @brief Decode AMBE 2450 parameters from demodulated bitstream.
 * @param ambe_d  Demodulated AMBE parameter bits (49).
 * @param cur_mp  Output: current frame parameters.
 * @param prev_mp Input: previous frame parameters (for prediction).
 * @return Tone index or 0 for voice; implementation-specific non-zero for tone frames.
 */
int
mbe_decodeAmbe2450Parms(char* ambe_d, mbe_parms* cur_mp, mbe_parms* prev_mp) {

    int ji, i, j, k, l, L = 0, L9, m, am, ak;
    int intkl[57];
    int b0, b1, b2, b3, b4, b5, b6, b7, b8;
    float f0, Cik[5][18], flokl[57], deltal[57];
    float Sum42, Sum43, Tl[57] = {0}, Gm[9], Ri[9], sum, c1, c2;
    int silence;
    int Ji[5], jl;
    float deltaGamma, BigGamma;
    float unvc, rconst;

    silence = 0;

#ifdef AMBE_DEBUG
    fprintf(stderr, "\n");
#endif

    int u0, u1, u2, u3;
    u0 = u1 = u2 = u3 = 0;

    for (i = 0; i < 12; i++) {
        u0 = u0 << 1;
        u0 = u0 | (int)ambe_d[i];
    }

    for (i = 12; i < 24; i++) {
        u1 = u1 << 1;
        u1 = u1 | (int)ambe_d[i];
    }

    for (i = 24; i < 35; i++) {
        u2 = u2 << 1;
        u2 = u2 | (int)ambe_d[i];
    }

    for (i = 35; i < 49; i++) {
        u3 = u3 << 1;
        u3 = u3 | (int)ambe_d[i];
    }

    //bitchin'
    int bitchk1, bitchk2;
    bitchk1 = (u0 >> 6) & 0x3f;
    bitchk2 = (u3 & 0xf);

#ifdef AMBE_DEBUG
    fprintf(stderr, "BIT1 = %d BIT2 = %d ", bitchk1, bitchk2);
#endif

    // copy repeat from prev_mp
    cur_mp->repeat = prev_mp->repeat;

    // decode fundamental frequency w0 from b0
    b0 = 0;
    b0 |= ambe_d[0] << 6;
    b0 |= ambe_d[1] << 5;
    b0 |= ambe_d[2] << 4;
    b0 |= ambe_d[3] << 3;
    b0 |= ambe_d[37] << 2;
    b0 |= ambe_d[38] << 1;
    b0 |= ambe_d[39];

    if (bitchk1 == 63 && bitchk2 == 0) {
#ifdef AMBE_DEBUG
        fprintf(stderr, "Tone Frame 2\n");
#endif
        return (7);
    }

    if ((b0 >= 120)
        && (b0
            <= 123)) // if w0 bits are 1111000, 1111001, 1111010 or 1111011, frame is erasure --  this is not entirely correct, tones are identified as erasures here
    {
#ifdef AMBE_DEBUG
        fprintf(stderr, "Erasure Frame b0 = %d\n", b0);
#endif
        return (2);
    }
    if ((b0 == 124) || (b0 == 125)) // if w0 bits are 1111100 or 1111101, frame is silence
    {
#ifdef AMBE_DEBUG
        fprintf(stderr, "Silence Frame\n");
#endif
        silence = 1;
        cur_mp->w0 = ((float)2 * M_PI) / (float)32;
        f0 = (float)1 / (float)32;
        L = 14;
        cur_mp->L = 14;
        for (l = 1; l <= L; l++) {
            cur_mp->Vl[l] = 0;
        }
    }
    //the below check doesn't seem to be entirely representative of all tones (could be entirely wrong)
    if ((b0 == 126) || (b0 == 127)) // if w0 bits are 1111110 or 1111111, frame is tone
    {
#ifdef AMBE_DEBUG
        fprintf(stderr, "Tone Frame 1\n");
#endif
        return (3);
    }

    if (silence == 0) {
        // w0 from specification document
        f0 = AmbeW0table[b0];
        cur_mp->w0 = f0 * (float)2 * M_PI;
        // w0 from patent filings
        //f0 = powf (2, ((float) b0 + (float) 195.626) / -(float) 45.368);
        //cur_mp->w0 = f0 * (float) 2 *M_PI;
    }

    unvc = (float)0.2046 / sqrtf(cur_mp->w0);
    //unvc = (float) 1;
    //unvc = (float) 0.2046 / sqrtf (f0);

    // decode L
    if (silence == 0) {
        // L from specification document
        // lookup L in tabl3
        L = AmbeLtable[b0];
        // L formula from patent filings
        //L=(int)((float)0.4627 / f0);
        cur_mp->L = L;
    }
    L9 = L - 9;
    (void)L9;

    // decode V/UV parameters
    // load b1 from ambe_d
    b1 = 0;
    b1 |= ambe_d[4] << 4;
    b1 |= ambe_d[5] << 3;
    b1 |= ambe_d[6] << 2;
    b1 |= ambe_d[7] << 1;
    b1 |= ambe_d[35];

    for (l = 1; l <= L; l++) {
        // jl from specification document
        jl = (int)((float)l * (float)16.0 * f0);
        // jl from patent filings?
        //jl = (int)(((float)l * (float)16.0 * f0) + 0.25);

        if (silence == 0) {
            cur_mp->Vl[l] = AmbeVuv[b1][jl];
        }
#ifdef AMBE_DEBUG
        fprintf(stderr, "jl[%i]:%i Vl[%i]:%i\n", l, jl, l, cur_mp->Vl[l]);
#endif
    }
#ifdef AMBE_DEBUG
    fprintf(stderr, "\nb0:%i w0:%f L:%i b1:%i\n", b0, cur_mp->w0, L, b1);
#endif

    // decode gain vector
    // load b2 from ambe_d
    b2 = 0;
    b2 |= ambe_d[8] << 4;
    b2 |= ambe_d[9] << 3;
    b2 |= ambe_d[10] << 2;
    b2 |= ambe_d[11] << 1;
    b2 |= ambe_d[36];

    deltaGamma = AmbeDg[b2];
    cur_mp->gamma = deltaGamma + ((float)0.5 * prev_mp->gamma);
#ifdef AMBE_DEBUG
    fprintf(stderr, "b2: %i, deltaGamma: %f gamma: %f gamma-1: %f\n", b2, deltaGamma, cur_mp->gamma, prev_mp->gamma);
#endif

    // decode PRBA vectors
    Gm[1] = 0;

    // load b3 from ambe_d
    b3 = 0;
    b3 |= ambe_d[12] << 8;
    b3 |= ambe_d[13] << 7;
    b3 |= ambe_d[14] << 6;
    b3 |= ambe_d[15] << 5;
    b3 |= ambe_d[16] << 4;
    b3 |= ambe_d[17] << 3;
    b3 |= ambe_d[18] << 2;
    b3 |= ambe_d[19] << 1;
    b3 |= ambe_d[40];
    Gm[2] = AmbePRBA24[b3][0];
    Gm[3] = AmbePRBA24[b3][1];
    Gm[4] = AmbePRBA24[b3][2];

    // load b4 from ambe_d
    b4 = 0;
    b4 |= ambe_d[20] << 6;
    b4 |= ambe_d[21] << 5;
    b4 |= ambe_d[22] << 4;
    b4 |= ambe_d[23] << 3;
    b4 |= ambe_d[41] << 2;
    b4 |= ambe_d[42] << 1;
    b4 |= ambe_d[43];
    Gm[5] = AmbePRBA58[b4][0];
    Gm[6] = AmbePRBA58[b4][1];
    Gm[7] = AmbePRBA58[b4][2];
    Gm[8] = AmbePRBA58[b4][3];

#ifdef AMBE_DEBUG
    fprintf(stderr, "b3: %i Gm[2]: %f Gm[3]: %f Gm[4]: %f b4: %i Gm[5]: %f Gm[6]: %f Gm[7]: %f Gm[8]: %f\n", b3, Gm[2],
            Gm[3], Gm[4], b4, Gm[5], Gm[6], Gm[7], Gm[8]);
#endif

    // compute Ri (using cached cosine coefficients)
    struct ambe2450_dct_cache* cache = ambe2450_get_dct_cache();
    for (i = 1; i <= 8; i++) {
        sum = 0;
        for (m = 1; m <= 8; m++) {
            if (m == 1) {
                am = 1;
            } else {
                am = 2;
            }
            sum = sum + ((float)am * Gm[m] * cache->ri_cos[m][i]);
        }
        Ri[i] = sum;
#ifdef AMBE_DEBUG
        fprintf(stderr, "R%i: %f ", i, Ri[i]);
#endif
    }
#ifdef AMBE_DEBUG
    fprintf(stderr, "\n");
#endif

    // generate first to elements of each Ci,k block from PRBA vector
    rconst = ((float)1 / ((float)2 * M_SQRT2));
    Cik[1][1] = (float)0.5 * (Ri[1] + Ri[2]);
    Cik[1][2] = rconst * (Ri[1] - Ri[2]);
    Cik[2][1] = (float)0.5 * (Ri[3] + Ri[4]);
    Cik[2][2] = rconst * (Ri[3] - Ri[4]);
    Cik[3][1] = (float)0.5 * (Ri[5] + Ri[6]);
    Cik[3][2] = rconst * (Ri[5] - Ri[6]);
    Cik[4][1] = (float)0.5 * (Ri[7] + Ri[8]);
    Cik[4][2] = rconst * (Ri[7] - Ri[8]);

    // decode HOC

    // load b5 from ambe_d
    b5 = 0;
    b5 |= ambe_d[24] << 4;
    b5 |= ambe_d[25] << 3;
    b5 |= ambe_d[26] << 2;
    b5 |= ambe_d[27] << 1;
    b5 |= ambe_d[44];

    // load b6 from ambe_d
    b6 = 0;
    b6 |= ambe_d[28] << 3;
    b6 |= ambe_d[29] << 2;
    b6 |= ambe_d[30] << 1;
    b6 |= ambe_d[45];

    // load b7 from ambe_d
    b7 = 0;
    b7 |= ambe_d[31] << 3;
    b7 |= ambe_d[32] << 2;
    b7 |= ambe_d[33] << 1;
    b7 |= ambe_d[46];

    // load b8 from ambe_d
    b8 = 0;
    b8 |= ambe_d[34] << 2;
    b8 |= ambe_d[47] << 1;
    b8 |= ambe_d[48];

    // lookup Ji
    Ji[1] = AmbeLmprbl[L][0];
    Ji[2] = AmbeLmprbl[L][1];
    Ji[3] = AmbeLmprbl[L][2];
    Ji[4] = AmbeLmprbl[L][3];
#ifdef AMBE_DEBUG
    fprintf(stderr, "Ji[1]: %i Ji[2]: %i Ji[3]: %i Ji[4]: %i\n", Ji[1], Ji[2], Ji[3], Ji[4]);
    fprintf(stderr, "b5: %i b6: %i b7: %i b8: %i\n", b5, b6, b7, b8);
#endif

    // Load Ci,k with the values from the HOC tables
    // there appear to be a couple typos in eq. 37 so we will just do what makes sense
    // (3 <= k <= Ji and k<=6)
    for (k = 3; k <= Ji[1]; k++) {
        if (k > 6) {
            Cik[1][k] = 0;
        } else {
            Cik[1][k] = AmbeHOCb5[b5][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C1,%i: %f ", k, Cik[1][k]);
#endif
        }
    }
    for (k = 3; k <= Ji[2]; k++) {
        if (k > 6) {
            Cik[2][k] = 0;
        } else {
            Cik[2][k] = AmbeHOCb6[b6][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C2,%i: %f ", k, Cik[2][k]);
#endif
        }
    }
    for (k = 3; k <= Ji[3]; k++) {
        if (k > 6) {
            Cik[3][k] = 0;
        } else {
            Cik[3][k] = AmbeHOCb7[b7][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C3,%i: %f ", k, Cik[3][k]);
#endif
        }
    }
    for (k = 3; k <= Ji[4]; k++) {
        if (k > 6) {
            Cik[4][k] = 0;
        } else {
            Cik[4][k] = AmbeHOCb8[b8][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C4,%i: %f ", k, Cik[4][k]);
#endif
        }
    }
#ifdef AMBE_DEBUG
    fprintf(stderr, "\n");
#endif

    // inverse DCT each Ci,k to give ci,j (Tl) - using cached cosines
    l = 1;
    for (i = 1; i <= 4; i++) {
        ji = Ji[i];
        for (j = 1; j <= ji; j++) {
            sum = 0;
            for (k = 1; k <= ji; k++) {
                if (k == 1) {
                    ak = 1;
                } else {
                    ak = 2;
                }
#ifdef AMBE_DEBUG
                fprintf(stderr, "j: %i Cik[%i][%i]: %f ", j, i, k, Cik[i][k]);
#endif
                sum = sum + ((float)ak * Cik[i][k] * cache->idct_cos[ji][j][k]);
            }
            Tl[l] = sum;
#ifdef AMBE_DEBUG
            fprintf(stderr, "Tl[%i]: %f\n", l, Tl[l]);
#endif
            l++;
        }
    }

    // determine log2Ml by applying ci,j to previous log2Ml

    // fix for when L > L(-1)
    if (cur_mp->L > prev_mp->L) {
        for (l = (prev_mp->L) + 1; l <= cur_mp->L; l++) {
            prev_mp->Ml[l] = prev_mp->Ml[prev_mp->L];
            prev_mp->log2Ml[l] = prev_mp->log2Ml[prev_mp->L];
        }
    }
    prev_mp->log2Ml[0] = prev_mp->log2Ml[1];
    prev_mp->Ml[0] = prev_mp->Ml[1];

    // Part 1
    Sum43 = 0;
    for (l = 1; l <= cur_mp->L; l++) {

        // eq. 40
        flokl[l] = ((float)prev_mp->L / (float)cur_mp->L) * (float)l;
        intkl[l] = (int)(flokl[l]);
#ifdef AMBE_DEBUG
        fprintf(stderr, "flok%i: %f, intk%i: %i ", l, flokl[l], l, intkl[l]);
#endif
        // eq. 41
        deltal[l] = flokl[l] - (float)intkl[l];
#ifdef AMBE_DEBUG
        fprintf(stderr, "delta%i: %f ", l, deltal[l]);
#endif
        // eq 43
        Sum43 = Sum43
                + ((((float)1 - deltal[l]) * prev_mp->log2Ml[intkl[l]]) + (deltal[l] * prev_mp->log2Ml[intkl[l] + 1]));
    }
    Sum43 = (((float)0.65 / (float)cur_mp->L) * Sum43);
#ifdef AMBE_DEBUG
    fprintf(stderr, "\n");
    fprintf(stderr, "Sum43: %f\n", Sum43);
#endif

    // Part 2
    Sum42 = 0;
    for (l = 1; l <= cur_mp->L; l++) {
        Sum42 += Tl[l];
    }
    Sum42 = Sum42 / (float)cur_mp->L;
    BigGamma = cur_mp->gamma - (0.5f * log2f((float)cur_mp->L)) - Sum42;
    //BigGamma=cur_mp->gamma - ((float)0.5 * log((float)cur_mp->L)) - Sum42;

    // Part 3
    for (l = 1; l <= cur_mp->L; l++) {
        c1 = ((float)0.65 * ((float)1 - deltal[l]) * prev_mp->log2Ml[intkl[l]]);
        c2 = ((float)0.65 * deltal[l] * prev_mp->log2Ml[intkl[l] + 1]);
        cur_mp->log2Ml[l] = Tl[l] + c1 + c2 - Sum43 + BigGamma;
        // inverse log to generate spectral amplitudes
        if (cur_mp->Vl[l] == 1) {
            cur_mp->Ml[l] = exp2f(cur_mp->log2Ml[l]);
        } else {
            cur_mp->Ml[l] = unvc * exp2f(cur_mp->log2Ml[l]);
        }
#ifdef AMBE_DEBUG
        fprintf(stderr, "flokl[%i]: %f, intkl[%i]: %i ", l, flokl[l], l, intkl[l]);
        fprintf(stderr, "deltal[%i]: %f ", l, deltal[l]);
        fprintf(stderr, "prev_mp->log2Ml[%i]: %f\n", l, prev_mp->log2Ml[intkl[l]]);
        fprintf(stderr, "BigGamma: %f c1: %f c2: %f Sum43: %f Tl[%i]: %f log2Ml[%i]: %f Ml[%i]: %f\n", BigGamma, c1, c2,
                Sum43, l, Tl[l], l, cur_mp->log2Ml[l], l, cur_mp->Ml[l]);
#endif
    }

    return (0);
}

/**
 * @brief Demodulate interleaved AMBE 3600x2450 data in-place.
 * @param ambe_fr Frame as 4x24 bitplanes (modified).
 */
void
mbe_demodulateAmbe3600x2450Data(char ambe_fr[4][24]) {
    mbe_demodulateAmbe3600Data_common(ambe_fr);
}

/**
 * @brief Process AMBE 2450 parameters into 160 float samples at 8 kHz.
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
void
mbe_processAmbe2450Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49], mbe_parms* cur_mp,
                         mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality) {

    int i, bad;

    /* Set AMBE-specific muting threshold (9.6% vs IMBE's 8.75%).
     * This matches JMBE AMBEModelParameters.isFrameMuted(). */
    cur_mp->mutingThreshold = MBE_MUTING_THRESHOLD_AMBE;

    /* Set error metrics for adaptive smoothing (JMBE Algorithms #55-56, #111-116).
     * IIR-filtered error rate: errorRate = 0.95 * prev + 0.001064 * totalErrors
     * This matches JMBE AMBEModelParameters constructor.
     * Note: AMBE uses different coefficient (0.001064) than IMBE (0.000365). */
    cur_mp->errorCountTotal = *errs + *errs2;
    cur_mp->errorCount4 = 0; /* AMBE has no Hamming cosets */
    cur_mp->errorRate = (0.95f * prev_mp->errorRate) + (0.001064f * (float)cur_mp->errorCountTotal);

    for (i = 0; i < *errs2; i++) {
        *err_str = '=';
        err_str++;
    }
    //it should be noted that in this context, 'bad' isn't referring to bad decode, but is a return
    //value for which type of frame we should synthesize (voice, repeat, silence, erasure, or tone, etc)
    bad = mbe_decodeAmbe2450Parms(ambe_d, cur_mp, prev_mp);
    if (bad == 2) {
        // Erasure frame
        *err_str = 'E';
        err_str++;
        cur_mp->repeat = 0;
        cur_mp->repeatCount = 0;
    } else if (bad == 3 || bad == 7) {
        // Tone Frame
        *err_str = 'T';
        err_str++;
        cur_mp->repeat = 0;
        cur_mp->repeatCount = 0;
    } else if (*errs2 > 3) {
        mbe_useLastMbeParms(cur_mp, prev_mp);
        cur_mp->repeat++;
        cur_mp->repeatCount++;
        *err_str = 'R';
        err_str++;
    } else {
        cur_mp->repeat = 0;
        cur_mp->repeatCount = 0;
    }

    if (bad == 0) {
        if (cur_mp->repeat <= 3) {
            mbe_moveMbeParms(cur_mp, prev_mp);
            mbe_spectralAmpEnhance(cur_mp);
            mbe_synthesizeSpeechf(aout_buf, cur_mp, prev_mp_enhanced, uvquality);
            mbe_moveMbeParms(cur_mp, prev_mp_enhanced);
        } else {
            *err_str = 'M';
            err_str++;
            mbe_synthesizeSilencef(aout_buf);
            mbe_initMbeParms(cur_mp, prev_mp, prev_mp_enhanced);
        }
    }

    else if (bad == 7 && *errs < 2 && *errs2 < 3) //only run if no more than x errs accumulated
    {
        //synthesize tone
        mbe_synthesizeTonef(aout_buf, ambe_d, cur_mp);
        mbe_moveMbeParms(cur_mp, prev_mp);
    } else {
        mbe_synthesizeSilencef(aout_buf);
        mbe_initMbeParms(cur_mp, prev_mp, prev_mp_enhanced);
    }
    *err_str = 0;
}

/**
 * @brief Process AMBE 2450 parameters into 160 16-bit samples at 8 kHz.
 * @see mbe_processAmbe2450Dataf for parameter details.
 */
void
mbe_processAmbe2450Data(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49], mbe_parms* cur_mp,
                        mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality) {
    float float_buf[160];

    mbe_processAmbe2450Dataf(float_buf, errs, errs2, err_str, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
    mbe_floattoshort(float_buf, aout_buf);
}

/**
 * @brief Process a complete AMBE 3600x2450 frame into float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param errs,errs2,err_str Error reporting as per Dataf variant.
 * @param ambe_fr  Input frame as 4x24 bitplanes.
 * @param ambe_d   Scratch/output parameter bits (49).
 * @param cur_mp,prev_mp,prev_mp_enhanced Parameter state as per Dataf variant.
 * @param uvquality Unvoiced synthesis quality (1..64).
 */
void
mbe_processAmbe3600x2450Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                               char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                               int uvquality) {

    *errs = 0;
    *errs2 = 0;
    *errs = mbe_eccAmbe3600x2450C0(ambe_fr);
    mbe_demodulateAmbe3600x2450Data(ambe_fr);
    *errs2 = *errs;
    *errs2 += mbe_eccAmbe3600x2450Data(ambe_fr, ambe_d);

    mbe_processAmbe2450Dataf(aout_buf, errs, errs2, err_str, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
}

/**
 * @brief Process a complete AMBE 3600x2450 frame into 16-bit PCM.
 * @see mbe_processAmbe3600x2450Framef for details.
 */
void
mbe_processAmbe3600x2450Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                              char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                              int uvquality) {
    float float_buf[160];

    mbe_processAmbe3600x2450Framef(float_buf, errs, errs2, err_str, ambe_fr, ambe_d, cur_mp, prev_mp, prev_mp_enhanced,
                                   uvquality);
    mbe_floattoshort(float_buf, aout_buf);
}
