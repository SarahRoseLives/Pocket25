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
 * @brief AMBE 3600x2400 parameter decode, ECC, and synthesis hooks.
 */

#include <math.h>
#include <stdio.h>

#include "ambe3600x2400_const.h"
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
struct ambe_dct_cache {
    int inited;
    float ri_cos[9][9];         /* [m][i] for m=1..8, i=1..8 (index 0 unused) */
    float idct_cos[18][18][18]; /* [ji][j][k] for ji=1..17, j=1..ji, k=1..ji */
};

static MBE_THREAD_LOCAL struct ambe_dct_cache ambe_cache = {0};

/**
 * @brief Initialize or return the thread-local AMBE DCT cache.
 *
 * Fills the cosine tables on first use. Because the cache is thread-local,
 * no locking is required.
 *
 * @return Pointer to the initialized cache.
 */
static struct ambe_dct_cache*
ambe_get_dct_cache(void) {
    if (ambe_cache.inited) {
        return &ambe_cache;
    }

    /* Fill Ri DCT cosine table: cosf(M_PI * (m-1) * (i-0.5) / 8) */
    for (int m = 1; m <= 8; m++) {
        for (int i = 1; i <= 8; i++) {
            ambe_cache.ri_cos[m][i] = cosf((M_PI * (float)(m - 1) * ((float)i - 0.5f)) / 8.0f);
        }
    }

    /* Fill per-block IDCT cosine table: cosf(M_PI * (k-1) * (j-0.5) / ji) */
    for (int ji = 1; ji <= 17; ji++) {
        for (int j = 1; j <= ji; j++) {
            for (int k = 1; k <= ji; k++) {
                ambe_cache.idct_cos[ji][j][k] = cosf((M_PI * (float)(k - 1) * ((float)j - 0.5f)) / (float)ji);
            }
        }
    }

    ambe_cache.inited = 1;
    return &ambe_cache;
}

/**
 * @brief Print AMBE 2400 parameter bits to stderr (debug aid).
 * @param ambe_d AMBE parameter bits (49).
 */
void
mbe_dumpAmbe2400Data(char* ambe_d) {

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
 * @brief Print raw AMBE 3600x2400 frame bitplanes to stderr.
 * @param ambe_fr Frame as 4x24 bitplanes.
 */
void
mbe_dumpAmbe3600x2400Frame(char ambe_fr[4][24]) {

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
 * @brief Apply ECC to AMBE 3600x2400 C0 and update in-place.
 * @param ambe_fr Frame as 4x24 bitplanes.
 * @return Number of corrected errors in C0.
 */
int
mbe_eccAmbe3600x2400C0(char ambe_fr[4][24]) {
    return mbe_eccAmbe3600C0_common(ambe_fr);
}

/**
 * @brief Apply ECC to AMBE 3600x2400 data and pack parameter bits.
 * @param ambe_fr Frame as 4x24 bitplanes.
 * @param ambe_d  Output parameter bits (49).
 * @return Number of corrected errors in protected fields.
 */
int
mbe_eccAmbe3600x2400Data(char ambe_fr[4][24], char* ambe_d) {
    return mbe_eccAmbe3600Data_common(ambe_fr, ambe_d);
}

/**
 * @brief Decode AMBE 2400 parameters from demodulated bitstream.
 * @param ambe_d  Demodulated AMBE parameter bits (49).
 * @param cur_mp  Output: current frame parameters.
 * @param prev_mp Input: previous frame parameters (for prediction).
 * @return Tone index or 0 for voice; implementation-specific non-zero for tone frames.
 */
int
mbe_decodeAmbe2400Parms(char* ambe_d, mbe_parms* cur_mp, mbe_parms* prev_mp) {

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

    // copy repeat from prev_mp
    cur_mp->repeat = prev_mp->repeat;

    // check if frame is tone or other; this matches section 7.2 on the P25 Half rate vocoder annex doc
    b0 = 0;
    b0 |= ambe_d[0] << 6;
    b0 |= ambe_d[1] << 5;
    b0 |= ambe_d[2] << 4;
    b0 |= ambe_d[3] << 3;
    b0 |= ambe_d[4] << 2;
    b0 |= ambe_d[5] << 1;
    b0 |= ambe_d[48];

    if ((b0 & 0x7E) == 0x7E) // frame is tone
    {
        // find tone index
        // Cx# 0000000000001111111111112222222222233333333333333
        //
        // IDX 0000000000111111111122222222223333333333444444444
        // idx 0123456789012345678901234567890123456789012345678
        // exm 1111110101001110100000001000000000000000001100000 : t=0111100
        // ex2 1111110110101110100000000000000000000000000000000 : t=1100010
        // ex3 1111110010101110110000001000000000000000000110000 : t=0000110
        // tt1 1111110010011110100000001000000000000000000101000 : t=0000101
        // tt3 1111110010011110000000001000000000000000000101000
        // ton HHHHHHDEF410======......P.................32==...
        // vol             765430                          21
        //DEF indexes the following tables for tone bits 5-7
        int t7tab[8] = {1, 0, 0, 0, 0, 1, 1, 1};
        int t6tab[8] = {0, 0, 0, 1, 1, 1, 1, 0};
        int t5tab[8] = {0, 0, 1, 0, 1, 1, 0, 1};
        //              V V V V V G G G     V = verified, G = guessed (and unused by all normal tone indices)
        b1 = 0;
        b1 |= t7tab[((ambe_d[6] << 2) | (ambe_d[7] << 1) | ambe_d[8])] << 7; //t7 128
        b1 |= t6tab[((ambe_d[6] << 2) | (ambe_d[7] << 1) | ambe_d[8])] << 6; //t6 64
        b1 |= t5tab[((ambe_d[6] << 2) | (ambe_d[7] << 1) | ambe_d[8])] << 5; //t5 32
        b1 |= ambe_d[9] << 4;                                                //t4 16  e verified
        b1 |= ambe_d[42] << 3;                                               //t3 8   d verified
        b1 |= ambe_d[43] << 2;                                               //t2 4   c verified
        b1 |= ambe_d[10] << 1;                                               //t1 2   b verified
        b1 |= ambe_d[11];                                                    //t0 1   a verified

        /* Tone volume bits were only used for debugging; avoid dead store when not used. */
#ifdef AMBE_DEBUG
        b2 = 0;
        b2 |= ambe_d[12] << 7; //v7 128 h verified
        b2 |= ambe_d[13] << 6; //v6 64  g verified
        b2 |= ambe_d[14] << 5; //v5 32  f verified
        b2 |= ambe_d[15] << 4; //v4 16  e guess based on data
        b2 |= ambe_d[16] << 3; //v3 8   d guess based on data
        b2 |= ambe_d[44] << 2; //v2 4   c guess based on data
        b2 |= ambe_d[45] << 1; //v1 2   b guess based on data
        b2 |= ambe_d[17];      //v0 1   a guess based on data
                               // the order of the last 3 bits may really be 17,44,45 not 44,45,17 as above
                               // fprintf(stderr,"Tone volume: %d; ", b2);
#endif

        /* Collapse repeated branches: valid single tone returns; dual-tone falls through; others -> silence. */
        if ((b1 >= 5) && (b1 <= 122)) {
            // fprintf(stderr, "index: %d, Single tone hz: %f\n", b1, (float)b1*31.25);
            return (b1); // use the return value to play a single frequency valid tone
        }

        if ((b1 >= 128) && (b1 <= 163)) {
            // fprintf(stderr, "index: %d, Dual tone\n", b1);
            // note: dual tone index is different on ambe(dstar) and ambe2+
        } else {
            // All other indices are treated as silence
            silence = 1;
        }

        if (silence == 1) {
#ifdef AMBE_DEBUG
            fprintf(stderr, "Silence Frame\n");
#endif
            cur_mp->w0 = ((float)2 * M_PI) / (float)32;
            L = 14;
            cur_mp->L = 14;
            for (l = 1; l <= L; l++) {
                cur_mp->Vl[l] = 0;
            }
        }
#ifdef AMBE_DEBUG
        fprintf(stderr, "Tone Frame\n");
#endif
        return (3);
    }
    //fprintf(stderr,"Voice Frame, Pitch = %f\n", exp2f(((float)b0+195.626f)/-46.368f)*8000); // was 45.368
    //fprintf(stderr,"Voice Frame, rawPitch = %02d, Pitch = %f\n", b0, exp2f(((-1*(float)(17661/((int)1<<12))) - (2.1336e-2f * ((float)b0+0.5f))))*8000);
    //fprintf(stderr,"Voice Frame, Pitch = %f, ", exp2f(-4.311767578125f - (2.1336e-2f * ((float)b0+0.5f)))*8000);

    // decode fundamental frequency w0 from b0 is already done

    if (silence == 0) {
        // w0 from specification document
        //f0 = AmbeW0table[b0];
        //cur_mp->w0 = f0 * (float) 2 *M_PI;
        // w0 from patent filings
        //f0 = powf (2, ((float) b0 + (float) 195.626) / -(float) 46.368); // was 45.368
        // w0 guess
        f0 = exp2f(-4.311767578125f - (2.1336e-2f * ((float)b0 + 0.5f)));
        cur_mp->w0 = f0 * (float)2 * M_PI;
    }

    unvc = (float)0.2046 / sqrtf(cur_mp->w0);
    //unvc = (float) 1;
    //unvc = (float) 0.2046 / sqrtf (f0);

    // decode L
    if (silence == 0) {
        // L from specification document
        // lookup L in tabl3
        L = AmbePlusLtable[b0];
        // L formula from patent filings
        //L=(int)((float)0.4627 / f0);
        cur_mp->L = L;
    }
    L9 = L - 9;
    (void)L9;

    // decode V/UV parameters
    // load b1 from ambe_d
    //TODO: use correct table (i.e. 0x0000 0x0005 0x0050 0x0055 etc)
    b1 = 0;
    b1 |= ambe_d[38] << 3;
    b1 |= ambe_d[39] << 2;
    b1 |= ambe_d[40] << 1;
    b1 |= ambe_d[41];
    //fprintf(stderr,"V/UV = %d, ", b1);
    for (l = 1; l <= L; l++) {
        // jl from specification document
        jl = (int)((float)l * (float)16.0 * f0);
        // jl from patent filings?
        //jl = (int)(((float)l * (float)16.0 * f0) + 0.25);

        if (silence == 0) {
            cur_mp->Vl[l] = AmbePlusVuv[b1][jl];
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
    b2 |= ambe_d[6] << 5;
    b2 |= ambe_d[7] << 4;
    b2 |= ambe_d[8] << 3;
    b2 |= ambe_d[9] << 2;
    b2 |= ambe_d[42] << 1;
    b2 |= ambe_d[43];
    //fprintf(stderr,"Gain = %d,\n", b2);
    deltaGamma = AmbePlusDg[b2];
    cur_mp->gamma = deltaGamma + ((float)0.5 * prev_mp->gamma);
#ifdef AMBE_DEBUG
    fprintf(stderr, "b2: %i, deltaGamma: %f gamma: %f gamma-1: %f\n", b2, deltaGamma, cur_mp->gamma, prev_mp->gamma);
#endif

    // decode PRBA vectors
    Gm[1] = 0;

    // load b3 from ambe_d
    b3 = 0;
    b3 |= ambe_d[10] << 8;
    b3 |= ambe_d[11] << 7;
    b3 |= ambe_d[12] << 6;
    b3 |= ambe_d[13] << 5;
    b3 |= ambe_d[14] << 4;
    b3 |= ambe_d[15] << 3;
    b3 |= ambe_d[16] << 2;
    b3 |= ambe_d[44] << 1;
    b3 |= ambe_d[45];
    Gm[2] = AmbePlusPRBA24[b3][0];
    Gm[3] = AmbePlusPRBA24[b3][1];
    Gm[4] = AmbePlusPRBA24[b3][2];

    // load b4 from ambe_d
    b4 = 0;
    b4 |= ambe_d[17] << 6;
    b4 |= ambe_d[18] << 5;
    b4 |= ambe_d[19] << 4;
    b4 |= ambe_d[20] << 3;
    b4 |= ambe_d[21] << 2;
    b4 |= ambe_d[46] << 1;
    b4 |= ambe_d[47];
    Gm[5] = AmbePlusPRBA58[b4][0];
    Gm[6] = AmbePlusPRBA58[b4][1];
    Gm[7] = AmbePlusPRBA58[b4][2];
    Gm[8] = AmbePlusPRBA58[b4][3];

#ifdef AMBE_DEBUG
    fprintf(stderr, "b3: %i Gm[2]: %f Gm[3]: %f Gm[4]: %f b4: %i Gm[5]: %f Gm[6]: %f Gm[7]: %f Gm[8]: %f\n", b3, Gm[2],
            Gm[3], Gm[4], b4, Gm[5], Gm[6], Gm[7], Gm[8]);
#endif

    // compute Ri (using cached cosine coefficients)
    struct ambe_dct_cache* cache = ambe_get_dct_cache();
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
    b5 |= ambe_d[22] << 3;
    b5 |= ambe_d[23] << 2;
    b5 |= ambe_d[25] << 1;
    b5 |= ambe_d[26];

    // load b6 from ambe_d
    b6 = 0;
    b6 |= ambe_d[27] << 3;
    b6 |= ambe_d[28] << 2;
    b6 |= ambe_d[29] << 1;
    b6 |= ambe_d[30];

    // load b7 from ambe_d
    b7 = 0;
    b7 |= ambe_d[31] << 3;
    b7 |= ambe_d[32] << 2;
    b7 |= ambe_d[33] << 1;
    b7 |= ambe_d[34];

    // load b8 from ambe_d
    b8 = 0;
    b8 |= ambe_d[35] << 3;
    b8 |= ambe_d[36] << 2;
    b8 |= ambe_d[37] << 1;
    //b8 |= 0; // least significant bit of hoc3 unused here, and according to the patent is forced to 0 when not used

    // lookup Ji
    Ji[1] = AmbePlusLmprbl[L][0];
    Ji[2] = AmbePlusLmprbl[L][1];
    Ji[3] = AmbePlusLmprbl[L][2];
    Ji[4] = AmbePlusLmprbl[L][3];
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
            Cik[1][k] = AmbePlusHOCb5[b5][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C1,%i: %f ", k, Cik[1][k]);
#endif
        }
    }
    for (k = 3; k <= Ji[2]; k++) {
        if (k > 6) {
            Cik[2][k] = 0;
        } else {
            Cik[2][k] = AmbePlusHOCb6[b6][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C2,%i: %f ", k, Cik[2][k]);
#endif
        }
    }
    for (k = 3; k <= Ji[3]; k++) {
        if (k > 6) {
            Cik[3][k] = 0;
        } else {
            Cik[3][k] = AmbePlusHOCb7[b7][k - 3];
#ifdef AMBE_DEBUG
            fprintf(stderr, "C3,%i: %f ", k, Cik[3][k]);
#endif
        }
    }
    for (k = 3; k <= Ji[4]; k++) {
        if (k > 6) {
            Cik[4][k] = 0;
        } else {
            Cik[4][k] = AmbePlusHOCb8[b8][k - 3];
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
 * @brief Demodulate interleaved AMBE 3600x2400 data in-place.
 * @param ambe_fr Frame as 4x24 bitplanes (modified).
 */
void
mbe_demodulateAmbe3600x2400Data(char ambe_fr[4][24]) {
    mbe_demodulateAmbe3600Data_common(ambe_fr);
}

/**
 * @brief Process AMBE 2400 parameters into 160 float samples at 8 kHz.
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
mbe_processAmbe2400Dataf(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49], mbe_parms* cur_mp,
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

    bad = mbe_decodeAmbe2400Parms(ambe_d, cur_mp, prev_mp);
    if (bad == 2) {
        // Erasure frame
        *err_str = 'E';
        err_str++;
        cur_mp->repeat = 0;
        cur_mp->repeatCount = 0;
    } else if (bad == 3) {
        // Tone Frame
        *err_str = 'T';
        err_str++;
        cur_mp->repeat = 0;
        cur_mp->repeatCount = 0;
    }
    // If we have a plausible single-frequency tone and errors are within margin
    else if ((bad >= 7) && (bad <= 122) && (*errs < 2) && (*errs2 < 3)) {
        // Synthesize single-frequency tone; 'bad' carries the tone ID value
        mbe_synthesizeTonefdstar(aout_buf, ambe_d, cur_mp, bad);
        mbe_moveMbeParms(cur_mp, prev_mp);
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
    } else {
        mbe_synthesizeSilencef(aout_buf);
        mbe_initMbeParms(cur_mp, prev_mp, prev_mp_enhanced);
    }
    *err_str = 0;
}

/**
 * @brief Process AMBE 2400 parameters into 160 16-bit samples at 8 kHz.
 * @see mbe_processAmbe2400Dataf for parameter details.
 */
void
mbe_processAmbe2400Data(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_d[49], mbe_parms* cur_mp,
                        mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced, int uvquality) {
    float float_buf[160];

    mbe_processAmbe2400Dataf(float_buf, errs, errs2, err_str, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
    mbe_floattoshort(float_buf, aout_buf);
}

/**
 * @brief Process a complete AMBE 3600x2400 frame into float PCM.
 * @param aout_buf Output buffer of 160 float samples.
 * @param errs,errs2,err_str Error reporting as per Dataf variant.
 * @param ambe_fr  Input frame as 4x24 bitplanes.
 * @param ambe_d   Scratch/output parameter bits (49).
 * @param cur_mp,prev_mp,prev_mp_enhanced Parameter state as per Dataf variant.
 * @param uvquality Unvoiced synthesis quality (1..64).
 */
void
mbe_processAmbe3600x2400Framef(float* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                               char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                               int uvquality) {

    *errs = 0;
    *errs2 = 0;
    *errs = mbe_eccAmbe3600x2400C0(ambe_fr);
    mbe_demodulateAmbe3600x2400Data(ambe_fr);
    *errs2 = *errs;
    *errs2 += mbe_eccAmbe3600x2400Data(ambe_fr, ambe_d);

    mbe_processAmbe2400Dataf(aout_buf, errs, errs2, err_str, ambe_d, cur_mp, prev_mp, prev_mp_enhanced, uvquality);
}

/**
 * @brief Process a complete AMBE 3600x2400 frame into 16-bit PCM.
 * @see mbe_processAmbe3600x2400Framef for details.
 */
void
mbe_processAmbe3600x2400Frame(short* aout_buf, int* errs, int* errs2, char* err_str, char ambe_fr[4][24],
                              char ambe_d[49], mbe_parms* cur_mp, mbe_parms* prev_mp, mbe_parms* prev_mp_enhanced,
                              int uvquality) {
    float float_buf[160];

    mbe_processAmbe3600x2400Framef(float_buf, errs, errs2, err_str, ambe_fr, ambe_d, cur_mp, prev_mp, prev_mp_enhanced,
                                   uvquality);
    mbe_floattoshort(float_buf, aout_buf);
}
