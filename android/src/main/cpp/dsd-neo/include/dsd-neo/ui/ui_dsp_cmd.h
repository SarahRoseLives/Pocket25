// SPDX-License-Identifier: GPL-3.0-or-later
/**
 * @file
 * @brief UI DSP runtime command opcodes for `UI_CMD_DSP_OP` payloads.
 */
#pragma once

/** DSP control opcodes understood by the demod thread. */
enum UiDspOp {
    UI_DSP_OP_TOGGLE_CQ = 2,
    UI_DSP_OP_TOGGLE_FLL = 3,
    UI_DSP_OP_TOGGLE_TED = 4,
    UI_DSP_OP_TOGGLE_IQBAL = 5,
    UI_DSP_OP_IQ_DC_TOGGLE = 6,
    UI_DSP_OP_IQ_DC_K_DELTA = 7, // a: delta (+/-)
    UI_DSP_OP_TED_GAIN_SET = 9,  // a: gain
    UI_DSP_OP_C4FM_CLK_CYCLE = 10,
    UI_DSP_OP_C4FM_CLK_SYNC_TOGGLE = 11,
    UI_DSP_OP_FM_AGC_TOGGLE = 12,
    UI_DSP_OP_FM_LIMITER_TOGGLE = 13,
    UI_DSP_OP_FM_AGC_TARGET_DELTA = 14, // a: delta (+/-)
    UI_DSP_OP_FM_AGC_MIN_DELTA = 15,    // a: delta (+/-)
    UI_DSP_OP_FM_AGC_ATTACK_DELTA = 16, // a: delta (+/-)
    UI_DSP_OP_FM_AGC_DECAY_DELTA = 17,  // a: delta (+/-)
    UI_DSP_OP_TUNER_AUTOGAIN_TOGGLE = 18,
};

/**
 * @brief Payload wrapper for DSP opcodes (fields interpreted per opcode).
 */
typedef struct {
    int op;
    int a;
    int b;
    int c;
    int d;
} UiDspPayload;
