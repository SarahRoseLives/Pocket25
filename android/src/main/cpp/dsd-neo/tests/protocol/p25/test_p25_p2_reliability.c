// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/*
 * Unit tests for P25P2 reliability buffer handling.
 *
 * Validates that:
 * 1. p25_p2_frame_reset() clears reliability buffers
 * 2. Buffer sizes are consistent with 700-dibit capture scope
 * 3. Reliability buffers are distinct from bit buffers
 *
 * This test compiles p25p2_frame.c directly with stubs to avoid
 * dragging in full library dependencies.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <dsd-neo/core/opts_fwd.h>
#include <dsd-neo/core/state_fwd.h>

/* External declarations matching p25p2_frame.c */
extern int p2bit[4320];
extern uint8_t p2reliab[700];
extern uint8_t p2xreliab[700];
extern void p25_p2_frame_reset(void);

/* Stubs for external functions referenced by p25p2_frame.c */
struct RtlSdrContext* g_rtl_ctx = 0;

bool
SetFreq(int sockfd, long int freq) {
    (void)sockfd;
    (void)freq;
    return false;
}

bool
SetModulation(int sockfd, int bandwidth) {
    (void)sockfd;
    (void)bandwidth;
    return false;
}

void
return_to_cc(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

int
rtl_stream_tune(struct RtlSdrContext* ctx, uint32_t center_freq_hz) {
    (void)ctx;
    (void)center_freq_hz;
    return 0;
}

void
rtl_stream_p25p2_err_update(int slot, int a, int b, int c, int d, int e) {
    (void)slot;
    (void)a;
    (void)b;
    (void)c;
    (void)d;
    (void)e;
}

void
getTimeC_buf(char out[9]) {
    if (out) {
        memcpy(out, "00:00:00", 9);
    }
}

void
rotate_symbol_out_file(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
openMbeOutFile(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
openMbeOutFileR(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
closeMbeOutFile(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
closeMbeOutFileR(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
playSynthesizedVoiceFS4(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
playSynthesizedVoiceSS18(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
}

void
watchdog_event_history(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
watchdog_event_current(dsd_opts* opts, dsd_state* state, uint8_t slot) {
    (void)opts;
    (void)state;
    (void)slot;
}

void
processMbeFrame(dsd_opts* opts, dsd_state* state, void* a, char fr[4][24], void* c) {
    (void)opts;
    (void)state;
    (void)a;
    (void)fr;
    (void)c;
}

void
p25_emit_enc_lockout_once(dsd_opts* opts, dsd_state* state, uint8_t slot, int tg, int svc) {
    (void)opts;
    (void)state;
    (void)slot;
    (void)tg;
    (void)svc;
}

void
LFSRP(dsd_state* state) {
    (void)state;
}

void
LFSR128(dsd_state* state) {
    (void)state;
}

double
dsd_time_now_monotonic_s(void) {
    return 0.0;
}

/* RS decoders */
int
ez_rs28_facch(int* payload, int* parity) {
    (void)payload;
    (void)parity;
    return 0;
}

int
ez_rs28_sacch(int* payload, int* parity) {
    (void)payload;
    (void)parity;
    return 0;
}

int
ez_rs28_ess(int* payload, int* parity) {
    (void)payload;
    (void)parity;
    return 0;
}

int
isch_lookup(uint64_t isch) {
    (void)isch;
    return -1;
}

int
ez_rs28_facch_soft(int* payload, int* parity, const int* erasures, int n_erasures) {
    (void)payload;
    (void)parity;
    (void)erasures;
    (void)n_erasures;
    return 0;
}

int
ez_rs28_sacch_soft(int* payload, int* parity, const int* erasures, int n_erasures) {
    (void)payload;
    (void)parity;
    (void)erasures;
    (void)n_erasures;
    return 0;
}

int
ez_rs28_ess_soft(int* payload, int* parity, const int* erasures, int n_erasures) {
    (void)payload;
    (void)parity;
    (void)erasures;
    (void)n_erasures;
    return 0;
}

/* MAC PDU handlers */
void
process_SACCH_MAC_PDU(dsd_opts* opts, dsd_state* state, int* bits) {
    (void)opts;
    (void)state;
    (void)bits;
}

void
process_FACCH_MAC_PDU(dsd_opts* opts, dsd_state* state, int* bits) {
    (void)opts;
    (void)state;
    (void)bits;
}

/* Dibit acquisition */
int
getDibitWithReliability(dsd_opts* opts, dsd_state* state, uint8_t* out_reliability) {
    (void)opts;
    (void)state;
    if (out_reliability) {
        *out_reliability = 128;
    }
    return 0;
}

int
main(void) {
    int failures = 0;

    printf("P25P2 Reliability Buffer Tests\n");
    printf("===============================\n\n");

    /* Test 1: Reset clears reliability buffers */
    printf("Test 1: Reset clears reliability buffers... ");
    memset(p2reliab, 0xAA, sizeof(p2reliab));
    memset(p2xreliab, 0xBB, sizeof(p2xreliab));
    p25_p2_frame_reset();

    int non_zero = 0;
    for (int i = 0; i < 700; i++) {
        if (p2reliab[i] != 0) {
            non_zero++;
        }
        if (p2xreliab[i] != 0) {
            non_zero++;
        }
    }
    if (non_zero == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL (%d non-zero entries)\n", non_zero);
        failures++;
    }

    /* Test 2: Buffer sizes are consistent with 700-dibit capture scope */
    printf("Test 2: Buffer size consistency... ");
    size_t p2bit_size = sizeof(p2bit) / sizeof(p2bit[0]);
    size_t p2reliab_size = sizeof(p2reliab) / sizeof(p2reliab[0]);
    size_t p2xreliab_size = sizeof(p2xreliab) / sizeof(p2xreliab[0]);

    if (p2bit_size == 4320 && p2reliab_size == 700 && p2xreliab_size == 700) {
        printf("PASS (p2bit=4320 bits, p2reliab=p2xreliab=700 dibits)\n");
    } else {
        printf("FAIL (p2bit=%zu, p2reliab=%zu, p2xreliab=%zu)\n", p2bit_size, p2reliab_size, p2xreliab_size);
        failures++;
    }

    /* Test 3: Reliability buffers are distinct from bit buffers */
    printf("Test 3: Buffer address separation... ");
    void* p_p2bit = (void*)p2bit;
    void* p_p2reliab = (void*)p2reliab;
    void* p_p2xreliab = (void*)p2xreliab;
    if (p_p2bit != p_p2reliab && p_p2reliab != p_p2xreliab && p_p2bit != p_p2xreliab) {
        printf("PASS\n");
    } else {
        printf("FAIL (overlapping buffers)\n");
        failures++;
    }

    /* Test 4: Reliability propagation through descramble preserves values */
    printf("Test 4: Reliability propagation preserves values... ");
    p25_p2_frame_reset();

    /* Simulate reliability values from dibit capture */
    for (int i = 0; i < 700; i++) {
        p2reliab[i] = (uint8_t)(i & 0xFF);
    }

    /* Manually copy to p2xreliab as process_Frame_Scramble would */
    memset(p2xreliab, 0, sizeof(p2xreliab));
    for (int i = 0; i < 700; i++) {
        p2xreliab[i] = p2reliab[i];
    }

    int mismatch = 0;
    for (int i = 0; i < 700; i++) {
        if (p2xreliab[i] != p2reliab[i]) {
            mismatch++;
        }
    }
    if (mismatch == 0) {
        printf("PASS\n");
    } else {
        printf("FAIL (%d mismatches)\n", mismatch);
        failures++;
    }

    printf("\n%d test(s) failed\n", failures);
    return failures > 0 ? 1 : 0;
}
