// SPDX-License-Identifier: ISC
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
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/core/sync_patterns.h>
#include <dsd-neo/fec/block_codes.h>
#include <dsd-neo/protocol/dmr/dmr.h>
#include <dsd-neo/protocol/dmr/dmr_trunk_sm.h>
#include <dsd-neo/runtime/colors.h>
#ifdef USE_RTLSDR
#include <dsd-neo/runtime/rtl_stream_metrics_hooks.h>
#endif

#include <stdio.h>
#include <string.h>

void
dmr_data_sync(dsd_opts* opts, dsd_state* state) {

    int i, dibit;
    int* dibit_p;
    char sync[25];
    char syncdata[48];
    uint8_t cachdata[25];
    UNUSED(syncdata);

    uint8_t burst;
    char info[196];
    uint8_t rel98[98];
    unsigned char SlotType[20];
    unsigned int SlotTypeOk;
    // CACH handler called for side effects; ignore return

    int cachInterleave[24] = {0, 7, 8, 9, 1, 10, 11, 12, 2, 13, 14, 15, 3, 16, 4, 17, 18, 19, 5, 20, 21, 22, 6, 23};

    dibit_p = state->dmr_payload_p - 90;
    uint8_t* rel_p = NULL;
    if (state->dmr_reliab_buf && state->dmr_reliab_p) {
        rel_p = state->dmr_reliab_p - 90;
    }

    //collect cach and de-interleave
    for (i = 0; i < 12; i++) {
        dibit = *dibit_p;
        dibit_p++;
        if (rel_p) {
            rel_p++;
        }
        if (opts->inverted_dmr == 1) {
            dibit = (dibit ^ 2);
        }
        if (state->dmr_stereo == 1) {
            dibit = (int)state->dmr_stereo_payload[i];
        } else {
            state->dmr_stereo_payload[i] = dibit;
        }

        cachdata[cachInterleave[((size_t)i * 2)]] = (1 & (dibit >> 1)); // bit 1
        cachdata[cachInterleave[((size_t)i * 2) + 1]] = (1 & dibit);    // bit 0
    }

    //seperate tact bits from cach
    uint8_t tact_bits[7];
    for (i = 0; i < 7; i++) {
        tact_bits[i] = cachdata[i];
    }

    //run hamming on tact bits
    int cach_okay = -1;
    if (Hamming_7_4_decode(tact_bits)) {
        cach_okay = 1;
    } else {
        cach_okay = -1;
        SlotTypeOk = 0;
        goto END;
    }

    state->currentslot = tact_bits[1];

    //in the future, maybe we will remove the hard set on this
    if (state->dmr_ms_mode == 1) {
        state->currentslot = 0;
    }

    // Current slot - First half - Data Payload - 1st part
    for (i = 0; i < 49; i++) {
        dibit = *dibit_p;
        dibit_p++;
        if (rel_p) {
            rel98[i] = *rel_p;
            rel_p++;
        } else {
            rel98[i] = 200;
        }
        if (opts->inverted_dmr == 1) {
            dibit = (dibit ^ 2);
        }
        if (state->dmr_stereo == 1) {
            dibit = (int)state->dmr_stereo_payload[i + 12];
        } else {
            state->dmr_stereo_payload[i + 12] = dibit;
        }
        info[((size_t)2) * i] = (1 & (dibit >> 1)); // bit 1
        info[((size_t)2 * i) + 1] = (1 & dibit);    // bit 0
    }

    // slot type
    dibit = *dibit_p;
    dibit_p++;
    if (rel_p) {
        rel_p++;
    }
    if (opts->inverted_dmr == 1) {
        dibit = (dibit ^ 2);
    }
    if (state->dmr_stereo == 1) {
        dibit = (int)state->dmr_stereo_payload[61];
    } else {
        state->dmr_stereo_payload[61] = dibit;
    }

    SlotType[0] = (1 & (dibit >> 1)); // bit 1
    SlotType[1] = (1 & dibit);        // bit 0

    dibit = *dibit_p;
    dibit_p++;
    if (rel_p) {
        rel_p++;
    }
    if (opts->inverted_dmr == 1) {
        dibit = (dibit ^ 2);
    }
    if (state->dmr_stereo == 1) {
        dibit = (int)state->dmr_stereo_payload[62];
    } else {
        state->dmr_stereo_payload[62] = dibit;
    }

    SlotType[2] = (1 & (dibit >> 1)); // bit 1
    SlotType[3] = (1 & dibit);        // bit 0

    dibit = *dibit_p;
    dibit_p++;
    if (rel_p) {
        rel_p++;
    }
    if (opts->inverted_dmr == 1) {
        dibit = (dibit ^ 2);
    }
    if (state->dmr_stereo == 1) //state
    {
        dibit = (int)state->dmr_stereo_payload[63];
    } else {
        state->dmr_stereo_payload[63] = dibit;
    }

    SlotType[4] = (1 & (dibit >> 1)); // bit 1
    SlotType[5] = (1 & dibit);        // bit 0

    dibit = *dibit_p;
    dibit_p++;
    if (rel_p) {
        rel_p++;
    }
    if (opts->inverted_dmr == 1) {
        dibit = (dibit ^ 2);
    }
    if (state->dmr_stereo == 1) //state
    {
        dibit = (int)state->dmr_stereo_payload[64];
    } else {
        state->dmr_stereo_payload[64] = dibit;
    }

    SlotType[6] = (1 & (dibit >> 1)); // bit 1
    SlotType[7] = (1 & dibit);        // bit 0

    // Parity bit
    dibit = *dibit_p;
    dibit_p++;
    if (opts->inverted_dmr == 1) {
        dibit = (dibit ^ 2);
    }
    if (state->dmr_stereo == 1) {
        dibit = (int)state->dmr_stereo_payload[65];
    } else {
        state->dmr_stereo_payload[65] = dibit;
    }
    SlotType[8] = (1 & (dibit >> 1)); // bit 1
    SlotType[9] = (1 & dibit);        // bit 0

    // signaling data or sync
    for (i = 0; i < 24; i++) {
        dibit = *dibit_p;
        dibit_p++;
        if (rel_p) {
            rel_p++;
        }
        if (opts->inverted_dmr == 1) {
            dibit = (dibit ^ 2);
        }
        if (state->dmr_stereo == 1) {
            dibit = (int)state->dmr_stereo_payload[i + 66];
        } else {
            state->dmr_stereo_payload[i + 66] = dibit;
        }

        syncdata[((size_t)2) * i] = (1 & (dibit >> 1)); // bit 1
        syncdata[((size_t)2 * i) + 1] = (1 & dibit);    // bit 0
        sync[i] = (dibit | 1) + 48;
    }
    sync[24] = 0;

    if ((strcmp(sync, DMR_BS_DATA_SYNC) == 0)) {
        if (state->currentslot == 0) {
            sprintf(state->slot1light, "[slot1]");
            sprintf(state->slot2light, " slot2 ");
        } else {
            sprintf(state->slot1light, " slot1 ");
            sprintf(state->slot2light, "[slot2]");
        }
    }

    else if (strcmp(sync, DMR_DIRECT_MODE_TS1_DATA_SYNC) == 0) {
        state->currentslot = 0;
        sprintf(state->slot1light, "[sLoT1]");
        sprintf(state->slot2light, "[DMODE]");
    }

    else if (strcmp(sync, DMR_DIRECT_MODE_TS2_DATA_SYNC) == 0) {
        state->currentslot = 1;
        sprintf(state->slot1light, "[DMODE]");
        sprintf(state->slot2light, "[sLoT2]");
    }

    if (state->dmr_ms_mode == 0) {
        fprintf(stderr, "%s %s ", state->slot1light, state->slot2light);
    }

    // Slot type - Second part - Parity bit
    for (i = 0; i < 5; i++) {
        if (state->dmr_stereo == 0) {
            int symbol = 0;
            dibit = get_dibit_and_analog_signal(opts, state, &symbol);
            state->dmr_stereo_payload[i + 90] = dibit;
            // store reliab for stereo cache
            int rel = 0;
            if (symbol > state->umid) {
                int span = state->max - state->umid;
                if (span < 1) {
                    span = 1;
                }
                rel = (symbol - state->umid) * 255 / span;
            } else if (symbol > state->center) {
                int d1 = symbol - state->center;
                int d2 = state->umid - symbol;
                int span = state->umid - state->center;
                if (span < 1) {
                    span = 1;
                }
                int m = d1 < d2 ? d1 : d2;
                rel = (m * 510) / span;
            } else if (symbol >= state->lmid) {
                int d1 = state->center - symbol;
                int d2 = symbol - state->lmid;
                int span = state->center - state->lmid;
                if (span < 1) {
                    span = 1;
                }
                int m = d1 < d2 ? d1 : d2;
                rel = (m * 510) / span;
            } else {
                int span = state->lmid - state->min;
                if (span < 1) {
                    span = 1;
                }
                rel = (state->lmid - symbol) * 255 / span;
            }
            if (rel < 0) {
                rel = 0;
            }
            if (rel > 255) {
                rel = 255;
            }
#ifdef USE_RTLSDR
            double snr_db = dsd_rtl_stream_metrics_hook_snr_c4fm_db();
            if (snr_db < -50.0) {
                snr_db = dsd_rtl_stream_metrics_hook_snr_c4fm_eye_db();
            }
            int w256 = 0;
            /* After C4FM SNR bias removal (~8 dB), shift window from [-5,20] to [-13,12]. */
            if (snr_db > -13.0) {
                if (snr_db >= 12.0) {
                    w256 = 255;
                } else {
                    double w = (snr_db + 13.0) / 25.0;
                    if (w < 0.0) {
                        w = 0.0;
                    }
                    if (w > 1.0) {
                        w = 1.0;
                    }
                    w256 = (int)(w * 255.0 + 0.5);
                }
            }
            int scale_num = 204 + (w256 >> 2);
            int scaled = (rel * scale_num) >> 8;
            if (scaled > 255) {
                scaled = 255;
            }
            if (scaled < 0) {
                scaled = 0;
            }
            rel = scaled;
#endif
            state->dmr_stereo_reliab[i + 90] = (uint8_t)rel;
        }
        if (state->dmr_stereo == 1) {
            dibit = (int)state->dmr_stereo_payload[i + 90];
        }
        SlotType[(i * 2) + 10] = (1 & (dibit >> 1)); // bit 1
        SlotType[(i * 2) + 11] = (1 & dibit);        // bit 0
    }

    /* Check and correct the SlotType (apply Golay(20,8) FEC check) */

    // golay (20,8) hamming-weight of 6 reliably corrects at most 2 bit-errors
    if (Golay_20_8_decode(SlotType)) {
        SlotTypeOk = 1;
    } else {
        SlotTypeOk = 0;
        goto END;
    }

    state->color_code = (SlotType[0] << 3) + (SlotType[1] << 2) + (SlotType[2] << 1) + (SlotType[3] << 0);
    state->color_code_ok = SlotTypeOk;

    //not sure why I still have two variables for this, need to look and see what state->color_code still ties into
    if (SlotTypeOk == 1) {
        state->dmr_color_code = state->color_code;
    }

    /* Reconstitute the burst type */
    burst = (uint8_t)((SlotType[4] << 3) + (SlotType[5] << 2) + (SlotType[6] << 1) + SlotType[7]);
    if (state->currentslot == 0) {
        state->dmrburstL = burst;
    }
    if (state->currentslot == 1) {
        state->dmrburstR = burst;
    }

    // Emit data sync to trunk SM for data burst types (DATA header, R12D, R34D, R1_D)
    // This keeps the channel active when trunk_tune_data_calls is enabled
    if (burst == 6 || burst == 7 || burst == 8 || burst == 10) {
        dmr_sm_emit_data_sync(opts, state, state->currentslot);
    }

    // Current slot - Second Half - Data Payload - 2nd part
    for (i = 0; i < 49; i++) {
        if (state->dmr_stereo == 0) {
            int symbol = 0;
            dibit = get_dibit_and_analog_signal(opts, state, &symbol);
            state->dmr_stereo_payload[i + 95] = dibit;
            int rel = 0;
            if (symbol > state->umid) {
                int span = state->max - state->umid;
                if (span < 1) {
                    span = 1;
                }
                rel = (symbol - state->umid) * 255 / span;
            } else if (symbol > state->center) {
                int d1 = symbol - state->center;
                int d2 = state->umid - symbol;
                int span = state->umid - state->center;
                if (span < 1) {
                    span = 1;
                }
                int m = d1 < d2 ? d1 : d2;
                rel = (m * 510) / span;
            } else if (symbol >= state->lmid) {
                int d1 = state->center - symbol;
                int d2 = symbol - state->lmid;
                int span = state->center - state->lmid;
                if (span < 1) {
                    span = 1;
                }
                int m = d1 < d2 ? d1 : d2;
                rel = (m * 510) / span;
            } else {
                int span = state->lmid - state->min;
                if (span < 1) {
                    span = 1;
                }
                rel = (state->lmid - symbol) * 255 / span;
            }
            if (rel < 0) {
                rel = 0;
            }
            if (rel > 255) {
                rel = 255;
            }
#ifdef USE_RTLSDR
            double snr_db = dsd_rtl_stream_metrics_hook_snr_c4fm_db();
            if (snr_db < -50.0) {
                snr_db = dsd_rtl_stream_metrics_hook_snr_c4fm_eye_db();
            }
            int w256 = 0;
            /* After C4FM SNR bias removal (~8 dB), shift window from [-5,20] to [-13,12]. */
            if (snr_db > -13.0) {
                if (snr_db >= 12.0) {
                    w256 = 255;
                } else {
                    double w = (snr_db + 13.0) / 25.0;
                    if (w < 0.0) {
                        w = 0.0;
                    }
                    if (w > 1.0) {
                        w = 1.0;
                    }
                    w256 = (int)(w * 255.0 + 0.5);
                }
            }
            int scale_num = 204 + (w256 >> 2);
            int scaled = (rel * scale_num) >> 8;
            if (scaled > 255) {
                scaled = 255;
            }
            if (scaled < 0) {
                scaled = 0;
            }
            rel = scaled;
#endif
            rel98[i + 49] = (uint8_t)rel;
            state->dmr_stereo_reliab[i + 95] = (uint8_t)rel;
        }

        if (state->dmr_stereo == 1) {
            dibit = (int)state->dmr_stereo_payload[i + 95];
            rel98[i + 49] = state->dmr_stereo_reliab[i + 95];
        }

        info[(2 * i) + 98] = (1 & (dibit >> 1)); // bit 1
        info[(2 * i) + 99] = (1 & dibit);        // bit 0
    }

    dmr_data_burst_handler_ex(opts, state, (uint8_t*)info, burst, rel98);

    //don't run cach on simplex or mono
    if (state->dmr_ms_mode == 0 && opts->dmr_mono == 0) {
        (void)dmr_cach(opts, state, cachdata);
    }

    //ending line break
    fprintf(stderr, "\n");

END:
    if (SlotTypeOk == 0 || cach_okay != 1) {
        fprintf(stderr, "%s", KRED);
        fprintf(stderr, "| CACH/Burst FEC ERR");
        fprintf(stderr, "%s", KNRM);
        fprintf(stderr, "\n");
        dmr_reset_blocks(opts, state); //failsafe to reset all data header and blocks when bad tact or slottype
    }

    // Skip cach (24 bit = 12 dibit) and next slot 1st half (98 + 10 bit = 49 + 5 dibit)
    if (state->dmr_stereo == 0) {
        skipDibit(opts, state, 12 + 49 + 5);
    }

#define CON_TUNEAWAY //disable if any unlock issues noted on the logic

#ifdef CON_TUNEAWAY
    //simplified, if IDLE condition, drop cc and vc sync time so when RF channel tears down, we go back much quicker
    //NOTE: This was adopted, because going back to the CC early and then to be sent back to the channel grant,
    //only for it to tear down and being sent back to the CC again isn't very efficient for trunk tracking
    //NOTE: This will still leave the tuner in the 'locked' state when tuning a voice channel grant on the CC,
    //and will remain locked until a new voice channel grant is received, but its just asthetic, trying to fix it
    //is too much of a hassle and causes other issues like CC hunting, etc.
    if (opts->trunk_enable == 1 && opts->p25_is_tuned == 1 && state->is_con_plus == 1) {
        int clear = 0;
        //IF both slots currently signalling IDLE
        if (state->dmrburstL == 9 && state->dmrburstR == 9) {
            clear = 1;
        }
        if (clear == 1) {
            state->last_cc_sync_time = 0;
            state->last_cc_sync_time_m = 0.0;
            state->last_vc_sync_time = 0;
            state->last_vc_sync_time_m = 0.0;
        }
    }
#endif
}
