// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * Copyright (C) 2010 DSD Author
 * GPG Key ID: 0x3F1D7FD0 (74EF 430D F7F2 0A48 FCE6  F630 FAA2 635D 3F1D 7FD0)
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES WITH
 * REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY
 * AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR ANY SPECIAL, DIRECT,
 * INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM
 * LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE
 * OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/dibit.h>
#include <dsd-neo/core/dsd_time.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/synctype_ids.h>
#include <dsd-neo/dsp/p25p1_heuristics.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>
#include <dsd-neo/protocol/p25/p25_lfsr.h>
#include <dsd-neo/protocol/p25/p25_trunk_sm.h>
#include <dsd-neo/protocol/p25/p25p1_check_hdu.h>
#include <dsd-neo/protocol/p25/p25p1_hdu.h>
#include <dsd-neo/protocol/p25/p25p1_soft.h>
#include <dsd-neo/runtime/colors.h>
#include <dsd-neo/runtime/p25_optional_hooks.h>

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/**
 * Read a dibit and retrieve both analog signal value and reliability.
 *
 * @param opts      Decoder options.
 * @param state     Decoder state.
 * @param analog    Output: analog signal value (for heuristics).
 * @param reliab    Output: reliability 0-255 (for soft decode).
 * @return The dibit value (0-3).
 *
 * Implementation note: Reliability is read from dmr_reliab_p[-1] because
 * dmr_compute_reliability() advances the pointer AFTER writing. Guard against
 * buffer wrap using the same bounds as the writer (wraps at +900000, starts
 * at +200). Only read when rp lies in [dmr_reliab_buf+200, dmr_reliab_buf+900000).
 */
static int
get_dibit_analog_and_reliability(dsd_opts* opts, dsd_state* state, int* analog, int* reliab) {
    int dibit = get_dibit_and_analog_signal(opts, state, analog);

    int r = 255;
    if (state->dmr_reliab_p != NULL && state->dmr_reliab_buf != NULL) {
        uint8_t* rp = state->dmr_reliab_p - 1;
        if (rp >= state->dmr_reliab_buf + 200 && rp < state->dmr_reliab_buf + 1000000) {
            r = (int)(*rp);
        }
    }

    if (r < 0) {
        r = 0;
    }
    if (r > 255) {
        r = 255;
    }
    *reliab = r;

    return dibit;
}

int
read_dibit(dsd_opts* opts, dsd_state* state, char* output, int* status_count, int* analog_signal, int* did_read_status,
           int* reliab) {
    int dibit;
    int status;
    int r;
    UNUSED(status);

    if (*status_count == 35) {

#ifdef TRACE_DSD
        char prev_prefix = state->debug_prefix;
        state->debug_prefix = 's';
#endif

        // Status bits now (unused)
        (void)getDibit(opts, state);
        // TODO: do something useful with the status bits...
        if (did_read_status != NULL) {
            *did_read_status = 1;
        }
        *status_count = 1;

#ifdef TRACE_DSD
        state->debug_prefix = prev_prefix;
#endif

    } else {
        if (did_read_status != NULL) {
            *did_read_status = 0;
        }
        (*status_count)++;
    }

    dibit = get_dibit_analog_and_reliability(opts, state, analog_signal, &r);
    if (reliab != NULL) {
        *reliab = r;
    }
    output[0] = (1 & (dibit >> 1)); // bit 1
    output[1] = (1 & dibit);        // bit 0

    return dibit;
}

void
read_dibit_update_analog_data(dsd_opts* opts, dsd_state* state, char* output, unsigned int count, int* status_count,
                              AnalogSignal* analog_signal_array, int* analog_signal_index) {
    unsigned int i;

    for (i = 0; i < count; i += 2) {
        // We read two bits on each call
        int analog_signal;
        int did_read_status;
        int reliab;
        int dibit;

        dibit = read_dibit(opts, state, output + i, status_count, &analog_signal, &did_read_status, &reliab);

        if (analog_signal_array != NULL) {
            // Fill up the AnalogSignal struct
            analog_signal_array[*analog_signal_index].value = analog_signal;
            analog_signal_array[*analog_signal_index].dibit = dibit;
            analog_signal_array[*analog_signal_index].sequence_broken = did_read_status;
            analog_signal_array[*analog_signal_index].reliab = reliab;
            (*analog_signal_index)++;
        }
    }
}

void
read_word(dsd_opts* opts, dsd_state* state, char* word, unsigned int length, int* status_count,
          AnalogSignal* analog_signal_array, int* analog_signal_index) {
    read_dibit_update_analog_data(opts, state, word, length, status_count, analog_signal_array, analog_signal_index);
}

void
read_golay24_parity(dsd_opts* opts, dsd_state* state, char* parity, int* status_count,
                    AnalogSignal* analog_signal_array, int* analog_signal_index) {
    read_dibit_update_analog_data(opts, state, parity, 12, status_count, analog_signal_array, analog_signal_index);
}

void
read_hamm_parity(dsd_opts* opts, dsd_state* state, char* parity, int* status_count, AnalogSignal* analog_signal_array,
                 int* analog_signal_index) {
    // Read 2 dibits = read 4 bits.
    read_dibit_update_analog_data(opts, state, parity, 4, status_count, analog_signal_array, analog_signal_index);
}

/**
 * Corrects a hex (6 bit) word using the Golay 24 FEC.
 * Uses soft decode if hard decode fails and reliability info is available.
 *
 * @param state Decoder state for error tracking.
 * @param hex   The 6-bit data word (modified in place).
 * @param parity The 12-bit parity word.
 * @param analog_signal_array AnalogSignal array for this hex word (9 dibits: 3 data + 6 parity).
 *                            May be NULL to disable soft decode.
 */
static void
correct_hex_word(dsd_opts* opts, dsd_state* state, char* hex, char* parity, const AnalogSignal* analog_signal_array) {
    int fixed_errors;
    int irrecoverable_errors;

    irrecoverable_errors = check_and_fix_golay_24_6(hex, parity, &fixed_errors);

    state->debug_header_errors += fixed_errors;

    if (irrecoverable_errors != 0 && analog_signal_array != NULL && opts->p25_p1_soft_voice) {
        /* Hard decode failed - try soft decode using reliability info.
         * The analog_signal_array contains 9 dibits:
         *   [0..2] = 3 dibits for 6 data bits
         *   [3..8] = 6 dibits for 12 parity bits
         * Extract per-bit reliability by taking dibit reliability for both bits.
         */
        int reliab[18];
        int idx = 0;

        /* Data bits: 3 dibits -> 6 bits */
        for (int d = 0; d < 3; d++) {
            int r = analog_signal_array[d].reliab;
            reliab[idx++] = r; /* bit 0 of dibit */
            reliab[idx++] = r; /* bit 1 of dibit */
        }
        /* Parity bits: 6 dibits -> 12 bits */
        for (int d = 3; d < 9; d++) {
            int r = analog_signal_array[d].reliab;
            reliab[idx++] = r;
            reliab[idx++] = r;
        }

        int soft_fixed = 0;
        int soft_result = check_and_fix_golay_24_6_soft(hex, parity, reliab, &soft_fixed);
        if (soft_result == 0) {
            /* Soft decode succeeded */
            state->debug_header_errors += soft_fixed;
            irrecoverable_errors = 0;
        }
    }

    if (irrecoverable_errors != 0) {
        state->debug_header_critical_errors++;
    }
}

/**
 * Reads an hex word, its parity bits and attempts to error correct it using the Golay24 algorithm.
 */
static void
read_and_correct_hex_word(dsd_opts* opts, dsd_state* state, char* hex, int* status_count,
                          AnalogSignal* analog_signal_array, int* analog_signal_index) {
    char parity[12];

    /* Remember where this hex word's analog signals start */
    int start_index = *analog_signal_index;

    // Read the hex word
    read_word(opts, state, hex, 6, status_count, analog_signal_array, analog_signal_index);
    // Read the parity
    read_golay24_parity(opts, state, parity, status_count, analog_signal_array, analog_signal_index);

    // Use the Golay24 FEC to correct it. This call modifies the content of hex to fix it, hopefully.
    // Pass the analog signal array starting at this hex word for soft decode support.
    const AnalogSignal* hex_analog = (analog_signal_array != NULL) ? &analog_signal_array[start_index] : NULL;
    correct_hex_word(opts, state, hex, parity, hex_analog);
}

/**
 * Uses the information from a corrected sequence of hex words to update the AnalogSignal data.
 * The proper Golay 24 parity is calculated from the corrected hex word so we can also fix the Golay parity
 * that we read originally from the signal.
 * \param corrected_hex_data Pointer to a sequence of hex words that has been error corrected and therefore
 * we trust it's correct. Typically this are hex words that has been decoded successfully using a
 * Reed-Solomon variant.
 * \param hex_count The number of hex words in the sequence.
 * \param analog_signal_array A pointer to the AnalogSignal information for the sequence of hex words.
 */
static void
correct_golay_dibits_6(char* corrected_hex_data, int hex_count, AnalogSignal* analog_signal_array) {
    int i, j;
    int analog_signal_index;
    int dibit;
    char parity[12];

    analog_signal_index = 0;

    for (i = hex_count - 1; i >= 0; i--) {
        for (j = 0; j < 6; j += 2) // 3 iterations -> 3 dibits
        {
            // Given the bits, calculates the dibit
            dibit = (corrected_hex_data[i * 6 + j] << 1) | corrected_hex_data[i * 6 + j + 1];
            // Now we know the dibit we should have read from the signal
            analog_signal_array[analog_signal_index].corrected_dibit = dibit;

#ifdef HEURISTICS_DEBUG
            if (analog_signal_array[analog_signal_index].dibit != dibit) {
                fprintf(stderr, "HDU data word corrected from %i to %i, analog value %i\n",
                        analog_signal_array[analog_signal_index].dibit, dibit,
                        analog_signal_array[analog_signal_index].value);
            }
#endif

            analog_signal_index++;
        }

        // Calculate the Golay 24 parity for the corrected hex word
        ptrdiff_t off = (ptrdiff_t)i * 6;
        encode_golay_24_6(corrected_hex_data + off, parity);

        // Now we know the parity we should have read from the signal. Use this information
        for (j = 0; j < 12; j += 2) // 6 iterations -> 6 dibits
        {
            // Given the bits, calculates the dibit
            dibit = (parity[j] << 1) | parity[j + 1];
            // Now we know the dibit we should have read from the signal
            analog_signal_array[analog_signal_index].corrected_dibit = dibit;

#ifdef HEURISTICS_DEBUG
            if (analog_signal_array[analog_signal_index].dibit != dibit) {
                fprintf(stderr, "HDU parity corrected from %i to %i, analog value %i\n",
                        analog_signal_array[analog_signal_index].dibit, dibit,
                        analog_signal_array[analog_signal_index].value);
            }
#endif

            analog_signal_index++;
        }
    }
}

/**
 * The important method that processes a full P25 HD unit.
 */
void
processHDU(dsd_opts* opts, dsd_state* state) {
    state->p25_p1_duid_hdu++;
    P25Heuristics* heur = (state->synctype == DSD_SYNC_P25P1_NEG) ? &state->inv_p25_heuristics : &state->p25_heuristics;

    // Defer last_vc_sync_time refresh until after FEC success to avoid
    // extending hangtime due to false HDU decodes during signal loss.

    //push current slot to 0, just in case swapping p2 to p1
    //or stale slot value from p2 and then decoding a pdu
    state->currentslot = 0;

    uint8_t mi[73];
    char mfid[9], algid[9], kid[17], tgid[17];
    int i, j;
    int algidhex, kidhex;
    char hex[6];
    int status_count;
    int status;
    unsigned long long int mihex1, mihex2, mihex3;
    char hex_data[20][6];   // Data in hex-words (6 bit words). A total of 20 hex words.
    char hex_parity[16][6]; // Parity of the data, again in hex-word format. A total of 16 parity hex words.
    UNUSED4(mfid, tgid, status, mihex3);

    int irrecoverable_errors;

    AnalogSignal analog_signal_array[20 * (3 + 6) + 16 * (3 + 6)] = {0};
    int analog_signal_index;

    analog_signal_index = 0;

    // we skip the status dibits that occur every 36 symbols
    // the next status symbol comes in 14 dibits from here
    // so we start counter at 36-14-1 = 21
    status_count = 21;

    // Read 20 hex words, correct them using their Golay 24 parity data.
    for (i = 19; i >= 0; i--) {
        read_and_correct_hex_word(opts, state, hex, &status_count, analog_signal_array, &analog_signal_index);
        // Store the corrected hex word into the hex_data store:
        for (j = 0; j < 6; j++) {
            hex_data[i][j] = hex[j];
        }
    }

    // Read the 16 parity hex word. These are used to FEC the 20 hex words using Reed-Solomon.
    for (i = 15; i >= 0; i--) {
        read_and_correct_hex_word(opts, state, hex, &status_count, analog_signal_array, &analog_signal_index);
        // Store the corrected hex word into the hex_parity store:
        for (j = 0; j < 6; j++) {
            hex_parity[i][j] = hex[j];
        }
    }
    // Don't forget to mark the first element as the start of a new sequence
    analog_signal_array[0].sequence_broken = 1;

    // Use the Reed-Solomon algorithm to correct the data. hex_data is modified in place
    irrecoverable_errors = check_and_fix_redsolomon_36_20_17((char*)hex_data, (char*)hex_parity);
    if (irrecoverable_errors != 0) {
        state->p25_p1_voice_fec_err++;
        // The hex words failed the Reed-Solomon check. There were too many errors. Still we can use this
        // information to update an estimate of the BER.
        state->debug_header_critical_errors++;

        // We can correct (17-1)/2 = 8 errors. If we failed, it means that there were more than 8 errors in
        // these 20+16 words. But take into account that each hex word was already error corrected with
        // Golay 24, which can correct 3 bits on each sequence of (6+12) bits. We could say that there were
        // 9 errors of 4 bits.
        update_error_stats(heur, 20 * 6 + 16 * 6, 9 * 4);
    } else {
        state->p25_p1_voice_fec_ok++;
        // Passed FEC checks: mark recent activity for trunk hangtime tracking.
        state->last_vc_sync_time = time(NULL);
        state->last_vc_sync_time_m = dsd_time_now_monotonic_s();
        // The hex words passed the Reed-Solomon check. This means that very likely they are correct and we
        // can trust that the digitizer did a good job with them. In other words, each analog value was
        // correctly assigned to a dibit. This is extremely useful information for the digitizer and we are
        // going to exploit it.
        char fixed_parity[16 * 6];

        // Correct the dibits that we did read according with the newly corrected hex_data values
        correct_golay_dibits_6((char*)hex_data, 20, analog_signal_array);

        // Generate again the Reed-Solomon parity for the corrected data
        encode_reedsolomon_36_20_17((char*)hex_data, fixed_parity);

        // Correct the dibits that we read according with the corrected parity values
        ptrdiff_t hoff = (ptrdiff_t)20 * (3 + 6);
        correct_golay_dibits_6(fixed_parity, 16, analog_signal_array + hoff);

        // Now we have a bunch of dibits (composed of data and parity of different kinds) that we trust are all
        // correct. We also keep a record of the analog values from where each dibit is coming from.
        // This information is gold for the heuristics module.
        contribute_to_heuristics(state->rf_mod, heur, analog_signal_array, 20 * (3 + 6) + 16 * (3 + 6));
    }

    // Now put the corrected data on the DSD structures

    mi[72] = 0;
    mfid[8] = 0;
    algid[8] = 0;
    kid[16] = 0;
    tgid[16] = 0;

    mi[0] = hex_data[19][0] + '0';
    mi[1] = hex_data[19][1] + '0';
    mi[2] = hex_data[19][2] + '0';
    mi[3] = hex_data[19][3] + '0';
    mi[4] = hex_data[19][4] + '0';
    mi[5] = hex_data[19][5] + '0';

    mi[6] = hex_data[18][0] + '0';
    mi[7] = hex_data[18][1] + '0';
    mi[8] = hex_data[18][2] + '0';
    mi[9] = hex_data[18][3] + '0';
    mi[10] = hex_data[18][4] + '0';
    mi[11] = hex_data[18][5] + '0';

    mi[12] = hex_data[17][0] + '0';
    mi[13] = hex_data[17][1] + '0';
    mi[14] = hex_data[17][2] + '0';
    mi[15] = hex_data[17][3] + '0';
    mi[16] = hex_data[17][4] + '0';
    mi[17] = hex_data[17][5] + '0';

    mi[18] = hex_data[16][0] + '0';
    mi[19] = hex_data[16][1] + '0';
    mi[20] = hex_data[16][2] + '0';
    mi[21] = hex_data[16][3] + '0';
    mi[22] = hex_data[16][4] + '0';
    mi[23] = hex_data[16][5] + '0';

    mi[24] = hex_data[15][0] + '0';
    mi[25] = hex_data[15][1] + '0';
    mi[26] = hex_data[15][2] + '0';
    mi[27] = hex_data[15][3] + '0';
    mi[28] = hex_data[15][4] + '0';
    mi[29] = hex_data[15][5] + '0';

    mi[30] = hex_data[14][0] + '0';
    mi[31] = hex_data[14][1] + '0';
    mi[32] = hex_data[14][2] + '0';
    mi[33] = hex_data[14][3] + '0';
    mi[34] = hex_data[14][4] + '0';
    mi[35] = hex_data[14][5] + '0';

    mi[36] = hex_data[13][0] + '0';
    mi[37] = hex_data[13][1] + '0';
    mi[38] = hex_data[13][2] + '0';
    mi[39] = hex_data[13][3] + '0';
    mi[40] = hex_data[13][4] + '0';
    mi[41] = hex_data[13][5] + '0';

    mi[42] = hex_data[12][0] + '0';
    mi[43] = hex_data[12][1] + '0';
    mi[44] = hex_data[12][2] + '0';
    mi[45] = hex_data[12][3] + '0';
    mi[46] = hex_data[12][4] + '0';
    mi[47] = hex_data[12][5] + '0';

    mi[48] = hex_data[11][0] + '0';
    mi[49] = hex_data[11][1] + '0';
    mi[50] = hex_data[11][2] + '0';
    mi[51] = hex_data[11][3] + '0';
    mi[52] = hex_data[11][4] + '0';
    mi[53] = hex_data[11][5] + '0';

    mi[54] = hex_data[10][0] + '0';
    mi[55] = hex_data[10][1] + '0';
    mi[56] = hex_data[10][2] + '0';
    mi[57] = hex_data[10][3] + '0';
    mi[58] = hex_data[10][4] + '0';
    mi[59] = hex_data[10][5] + '0';

    mi[60] = hex_data[9][0] + '0';
    mi[61] = hex_data[9][1] + '0';
    mi[62] = hex_data[9][2] + '0';
    mi[63] = hex_data[9][3] + '0';
    mi[64] = hex_data[9][4] + '0';
    mi[65] = hex_data[9][5] + '0';

    mi[66] = hex_data[8][0] + '0';
    mi[67] = hex_data[8][1] + '0';
    mi[68] = hex_data[8][2] + '0';
    mi[69] = hex_data[8][3] + '0';
    mi[70] = hex_data[8][4] + '0';
    mi[71] = hex_data[8][5] + '0';

    mfid[0] = hex_data[7][0] + '0';
    mfid[1] = hex_data[7][1] + '0';
    mfid[2] = hex_data[7][2] + '0';
    mfid[3] = hex_data[7][3] + '0';
    mfid[4] = hex_data[7][4] + '0';
    mfid[5] = hex_data[7][5] + '0';

    mfid[6] = hex_data[6][0] + '0';
    mfid[7] = hex_data[6][1] + '0';
    algid[0] = hex_data[6][2] + '0'; // The important algorithm ID. This indicates whether the data is
    algid[1] = hex_data[6][3] + '0'; // encrypted and if so what is the encryption algorithm used.
    algid[2] = hex_data[6][4] + '0'; // A code 0x80 here means that the data is unencrypted.
    algid[3] = hex_data[6][5] + '0';

    algid[4] = hex_data[5][0] + '0';
    algid[5] = hex_data[5][1] + '0';
    algid[6] = hex_data[5][2] + '0';
    algid[7] = hex_data[5][3] + '0';
    kid[0] = hex_data[5][4] + '0';
    kid[1] = hex_data[5][5] + '0';

    kid[2] = hex_data[4][0] + '0'; // The encryption key ID
    kid[3] = hex_data[4][1] + '0';
    kid[4] = hex_data[4][2] + '0';
    kid[5] = hex_data[4][3] + '0';
    kid[6] = hex_data[4][4] + '0';
    kid[7] = hex_data[4][5] + '0';

    kid[8] = hex_data[3][0] + '0';
    kid[9] = hex_data[3][1] + '0';
    kid[10] = hex_data[3][2] + '0';
    kid[11] = hex_data[3][3] + '0';
    kid[12] = hex_data[3][4] + '0';
    kid[13] = hex_data[3][5] + '0';

    kid[14] = hex_data[2][0] + '0';
    kid[15] = hex_data[2][1] + '0';
    tgid[0] = hex_data[2][2] + '0'; // Talk group ID
    tgid[1] = hex_data[2][3] + '0';
    tgid[2] = hex_data[2][4] + '0';
    tgid[3] = hex_data[2][5] + '0';

    tgid[4] = hex_data[1][0] + '0';
    tgid[5] = hex_data[1][1] + '0';
    tgid[6] = hex_data[1][2] + '0';
    tgid[7] = hex_data[1][3] + '0';
    tgid[8] = hex_data[1][4] + '0';
    tgid[9] = hex_data[1][5] + '0';

    tgid[10] = hex_data[0][0] + '0';
    tgid[11] = hex_data[0][1] + '0';
    tgid[12] = hex_data[0][2] + '0';
    tgid[13] = hex_data[0][3] + '0';
    tgid[14] = hex_data[0][4] + '0';
    tgid[15] = hex_data[0][5] + '0';

    state->p25kid = strtol(kid, NULL, 2);

    skipDibit(opts, state, 5);
    (void)getDibit(opts, state);
    //TODO: Do something useful with the status bits...

    algidhex = strtol(algid, NULL, 2);
    kidhex = strtol(kid, NULL, 2);
    mihex1 = (unsigned long long int)ConvertBitIntoBytes(&mi[0], 32);
    mihex2 = (unsigned long long int)ConvertBitIntoBytes(&mi[32], 32);
    mihex3 = (unsigned long long int)ConvertBitIntoBytes(&mi[64], 8);

    //reset dropbytes - skip first 11 for LCW
    state->dropL = 267;

    //set vc counter to 0
    state->p25vc = 0;

    if (irrecoverable_errors == 0) {

        fprintf(stderr, "%s", KYEL);
        fprintf(stderr, " HDU  ALG ID: 0x%02X KEY ID: 0x%04X MI: 0x%08llX%08llX", algidhex, kidhex, mihex1, mihex2);
        state->payload_algid = algidhex;
        state->payload_keyid = kidhex;
        if (mihex3) {
            fprintf(stderr, "-%02llX", mihex3);
        }
        if (state->R != 0
            && (state->payload_algid == 0xAA || state->payload_algid == 0x81 || state->payload_algid == 0x9F)) {
            fprintf(stderr, " Key: %010llX", state->R);
            opts->unmute_encrypted_p25 = 1;
        } else if ((state->payload_algid == 0x84 || state->payload_algid == 0x89) && state->aes_key_loaded[0] == 1) {
            fprintf(stderr, "\n ");
            fprintf(stderr, "%s", KYEL);
            fprintf(stderr, "Key: %016llX %016llX ", state->A1[0], state->A2[0]);
            if (state->payload_algid == 0x84) {
                fprintf(stderr, "%016llX %016llX", state->A3[0], state->A4[0]);
            }
            fprintf(stderr, "%s ", KNRM);
            opts->unmute_encrypted_p25 = 1;
        } else if (state->payload_algid != 0 && state->payload_algid != 0x80) {
            //may want to mute this again, or may not want to
            opts->unmute_encrypted_p25 = 0;
        }
        fprintf(stderr, "%s", KNRM);
        //only use 64 MSB, trailing 8 bits aren't used, so no mihex3
        state->payload_miP = (mihex1 << 32) | (mihex2);

        if (state->payload_algid != 0x80 && state->payload_algid != 0x0) {
            fprintf(stderr, "%s", KRED);
            fprintf(stderr, " ENC");
            fprintf(stderr, "%s", KNRM);
        }

        fprintf(stderr, "\n");

        //expand 64-bit MI to 128-bit for AES
        if (state->payload_algid == 0x84 || state->payload_algid == 0x89) {
            LFSR128(state);
            fprintf(stderr, "\n");
        }

        //xl, we need to know if the ESS is from HDU, or LDU2
        state->xl_is_hdu = 1;

        // Early ENC lockout for P25 Phase 1: if trunking ENC lockout is
        // enabled and the call is encrypted without a usable key, terminate
        // immediately and return to the control channel. This avoids sitting
        // on an encrypted VC waiting for later LDUs.
        if (opts->p25_trunk == 1 && opts->p25_is_tuned == 1 && opts->trunk_tune_enc_calls == 0) {
            int alg = state->payload_algid;
            int have_key = 0;
            if (((alg == 0xAA || alg == 0x81 || alg == 0x9F) && state->R != 0)
                || ((alg == 0x84 || alg == 0x89) && state->aes_key_loaded[0] == 1)) {
                have_key = 1;
            }
            int enc_suspect = (alg != 0 && alg != 0x80 && have_key == 0);
            if (enc_suspect) {
                // Clear V XTRA fields immediately to avoid UI stale display
                state->payload_algid = 0;
                state->payload_keyid = 0;
                state->payload_miP = 0ULL;
                // Optional: mark TG as ENC LO for visibility when known
                int ttg = state->lasttg;
                if (ttg != 0) {
                    int idx = -1;
                    for (unsigned int xx = 0; xx < state->group_tally; xx++) {
                        if (state->group_array[xx].groupNumber == (unsigned long)ttg) {
                            idx = (int)xx;
                            break;
                        }
                    }
                    if (idx >= 0) {
                        snprintf(state->group_array[idx].groupMode, sizeof state->group_array[idx].groupMode, "%s",
                                 "DE");
                    } else if (state->group_tally
                               < (unsigned)(sizeof(state->group_array) / sizeof(state->group_array[0]))) {
                        state->group_array[state->group_tally].groupNumber = ttg;
                        sprintf(state->group_array[state->group_tally].groupMode, "%s", "DE");
                        sprintf(state->group_array[state->group_tally].groupName, "%s", "ENC LO");
                        state->group_tally++;
                    }
                    sprintf(state->event_history_s[0].Event_History_Items[0].internal_str,
                            "Target: %d; has been locked out; Encryption Lock Out Enabled.", ttg);
                    dsd_p25_optional_hook_watchdog_event_current(opts, state, 0);
                    // Immediately log and push this lockout event so it is not delayed
                    if (opts->event_out_file[0] != 0) {
                        dsd_p25_optional_hook_write_event_to_log_file(
                            opts, state, 0, /*swrite*/ 0,
                            state->event_history_s[0].Event_History_Items[0].event_string);
                    }
                    dsd_p25_optional_hook_push_event_history(&state->event_history_s[0]);
                    dsd_p25_optional_hook_init_event_history(&state->event_history_s[0], 0, 1);
                }
                // Also clear banner to avoid stale "Group Encrypted" on UI
                snprintf(state->call_string[0], sizeof state->call_string[0], "%s", "                     ");
                fprintf(stderr, " No Enc Following on P25p1 Trunking (HDU); Return to CC; \n");
                state->p25_sm_force_release = 1;
                p25_sm_on_release(opts, state);
            }
        }

    } else {
        fprintf(stderr, "%s", KRED);
        fprintf(stderr, " HDU FEC ERR \n");
        fprintf(stderr, "%s", KNRM);
    }

    //reset gain
    if (opts->floating_point == 1) {
        state->aout_gain = opts->audio_gain;
    }
}
