// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * p25_frequency.c
 * P25 Channel to Frequency Calculator
 *
 * NXDN Channel to Frequency Calculator
 *
 * LWVMOBILE
 * 2022-11 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/p25/p25_frequency.h>

#include <stdio.h>

// P25 channel → frequency mapping
// - Channel format: 4-bit iden (MSBs) + 12-bit channel number.
// - Frequency calculation per OP25 practice:
//     freq_hz = base[iden]*5 + (step * spacing[iden] * 125)
//   Where:
//     base[iden] is in units of 5 kHz (per IDEN_UP encoding),
//     spacing[iden] is in units of 125 Hz,
//     step = channel_number / slots_per_carrier[type]
// - slots_per_carrier table below is sourced from OP25 and common system behavior.
long int
process_channel_to_freq(dsd_opts* opts, dsd_state* state, int channel) {

    //RX and SU TX frequencies.
    //SU RX = (Base Frequency) + (Channel Number) x (Channel Spacing).

    /*
	Channel Spacing: This is a frequency multiplier for the channel
	number. It is used as a multiplier in other messages that specify
	a channel field value. The channel spacing (kHz) is computed as
	(Channel Spacing) x (0.125 kHz).
	*/

    // Sanitize to 16-bit channel (iden:4 | chan:12)
    uint16_t chan16 = (uint16_t)channel;

    //return 0 if channel value is 0 or 0xFFFF
    if (chan16 == 0) {
        return 0;
    }
    if (chan16 == 0xFFFF) {
        return 0;
    }

    //Note: Base Frequency is calculated as (Base Frequency) x (0.000005 MHz) from the IDEN_UP message.

    long int freq = 0;
    int iden = (chan16 >> 12) & 0xF;
    if (iden < 0 || iden > 15) {
        fprintf(stderr, "\n  P25 FREQ: invalid iden %d", iden);
        return 0;
    }
    int type = state->p25_chan_type[iden] & 0xF;
    // OP25-derived slots-per-carrier by channel type
    static const int slots_per_carrier[16] = {1, 1, 1, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    // Derive division only when TDMA iden is known. This matches OP25, which
    // only divides the channel by tdma when an IDEN_UP_TDMA has populated the
    // table. Without that knowledge, treat channel as FDMA (denom=1).
    int denom = 1;
    if ((state->p25_chan_tdma[iden] & 0x1) != 0) {
        if (type < 0 || type > 15) {
            fprintf(stderr, "\n  P25 FREQ: unknown iden type %d (iden %d)", type, iden);
            return 0;
        }
        denom = slots_per_carrier[type];
        if (denom <= 0) {
            fprintf(stderr, "\n  P25 FREQ: invalid slots/carrier for type %d", type);
            return 0;
        }
    } else {
        // Fallback: if the system is known to carry Phase 2 (TDMA) voice but
        // we have not yet seen an IDEN_UP_TDMA for this iden, assume 2
        // slots/carrier. This avoids early mis-tunes where a TDMA grant
        // arrives before the IDEN table is populated, which would otherwise be
        // treated as FDMA (denom=1).
        if (state->p25_sys_is_tdma == 1) {
            denom = 2;
            if (opts && opts->verbose > 1) {
                fprintf(stderr, "\n  P25 FREQ: iden %d tdma unknown; fallback denom=2 (P2 CC)", iden);
            }
        }
    }
    int step = (chan16 & 0xFFF) / denom;

    //first, check channel map
    if (state->trunk_chan_map[chan16] != 0) {
        freq = state->trunk_chan_map[chan16];
        fprintf(stderr, "\n  P25 FREQ: map ch=0x%04X -> %.6lf MHz", chan16, (double)freq / 1000000.0);
        return freq;
    }

    //if not found, attempt to find it via calculation
    else {
        long base = state->p25_base_freq[iden];
        long spac = state->p25_chan_spac[iden];
        if (base == 0 || spac == 0) {
            fprintf(stderr, "\n  P25 FREQ: missing iden %d params (base=%ld, spac=%ld); refusing tune", iden, base,
                    spac);
            return 0;
        }
        freq = (base * 5) + (step * spac * 125);
        fprintf(stderr, "\n  P25 FREQ: iden=%d type=%d ch=0x%04X -> %.6lf MHz", iden, type, chan16,
                (double)freq / 1000000.0);
        if (opts && opts->verbose > 1) {
            fprintf(stderr, " (base5=%ldHz spac125=%ldHz denom=%d step=%d)", base * 5L, spac * 125L, denom, step);
        }
        // Persist learned mapping so UI can display and future grants can use explicit map
        if (freq != 0) {
            state->trunk_chan_map[chan16] = freq;
        }
        return freq;
    }
}

// Format a short suffix describing the FDMA-equivalent channel and slot for a
// given P25 channel index. Intended to reduce confusion when Phase 2 TDMA uses
// 6.25 kHz channel numbering (e.g., 0x0148) while the Learned Channels list is
// keyed by the 12.5 kHz FDMA channel (e.g., 0x00A4).
//
// Example output (for TDMA): " (FDMA 00A4 S1)"
// For FDMA (denom==1) the suffix is empty to avoid noise.
void
p25_format_chan_suffix(const dsd_state* state, uint16_t chan, int slot_hint, char* out, size_t outsz) {
    if (!out || outsz == 0) {
        return;
    }
    out[0] = '\0';

    if (!state) {
        return;
    }

    int iden = (chan >> 12) & 0xF;
    int raw = chan & 0xFFF;

    // Mirror denom logic from process_channel_to_freq
    int type = state->p25_chan_type[iden] & 0xF;
    static const int slots_per_carrier[16] = {1, 1, 1, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2};
    int denom = 1;
    if ((state->p25_chan_tdma[iden] & 0x1) != 0) {
        if (type >= 0 && type <= 15) {
            denom = slots_per_carrier[type];
        }
    } else if (state->p25_sys_is_tdma == 1) {
        // Conservative fallback when TDMA IDEN not yet learned
        denom = 2;
    }

    if (denom <= 1) {
        // FDMA: nothing to add
        return;
    }

    int fdma = raw / denom;
    int slot = raw % denom;
    if (slot_hint >= 0 && slot_hint < denom) {
        slot = slot_hint;
    }

    // Print only the 12-bit channel index for FDMA to match the Learned list
    // (which commonly shows values like 00A4), and include slot.
    // Display slots as 1-based (S1/S2) to match UI conventions
    snprintf(out, outsz, " (FDMA %04X S%d)", fdma & 0xFFF, slot + 1);
}

void
p25_reset_iden_tables(dsd_state* state) {
    if (!state) {
        return;
    }
    for (int i = 0; i < 16; i++) {
        state->p25_chan_tdma[i] = 0;
        state->p25_chan_tdma_explicit[i] = 0;
        state->p25_chan_type[i] = 0;
        state->p25_chan_spac[i] = 0;
        state->p25_base_freq[i] = 0;
        state->p25_trans_off[i] = 0;
        state->p25_iden_wacn[i] = 0;
        state->p25_iden_sysid[i] = 0;
        state->p25_iden_rfss[i] = 0;
        state->p25_iden_site[i] = 0;
        state->p25_iden_trust[i] = 0; // unknown
    }
}

// Promote any IDENs whose provenance matches the current site to trusted
void
p25_confirm_idens_for_current_site(dsd_state* state) {
    if (!state) {
        return;
    }
    unsigned long long cur_wacn = state->p2_wacn;
    unsigned long long cur_sys = state->p2_sysid;
    unsigned long long cur_rfss = state->p2_rfssid;
    unsigned long long cur_site = state->p2_siteid;
    if (cur_wacn == 0 && cur_sys == 0) {
        return;
    }
    for (int i = 0; i < 16; i++) {
        if (state->p25_iden_trust[i] == 2) {
            continue; // already trusted
        }
        if (state->p25_iden_wacn[i] == 0 && state->p25_iden_sysid[i] == 0) {
            continue;
        }
        if (state->p25_iden_wacn[i] == cur_wacn && state->p25_iden_sysid[i] == cur_sys) {
            // If rfss/site are recorded, require match; else allow
            int rf_ok = (state->p25_iden_rfss[i] == 0 || state->p25_iden_rfss[i] == cur_rfss);
            int st_ok = (state->p25_iden_site[i] == 0 || state->p25_iden_site[i] == cur_site);
            if (rf_ok && st_ok) {
                state->p25_iden_trust[i] = 2; // confirmed
            }
        }
    }
}

long int
nxdn_channel_to_frequency(dsd_opts* opts, dsd_state* state, uint16_t channel) {
    UNUSED(opts);

    long int freq;
    long int base = 0;
    long int step = 0;

    //reworked to include Direct Frequency Assignment if available

    //first, check channel map for imported value, DFA systems most likely won't need an import,
    //unless it has 'system definable' attributes
    if (state->trunk_chan_map[channel] != 0) {
        freq = state->trunk_chan_map[channel];
        fprintf(stderr, "\n  Frequency [%.6lf] MHz", (double)freq / 1000000);
        return (freq);
    }

    //then, let's see if its DFA instead -- 6.5.36
    else if (state->nxdn_rcn == 1) {
        //determine the base frequency in Hz
        if (state->nxdn_base_freq == 1) {
            base = 100000000; //100 MHz
        } else if (state->nxdn_base_freq == 2) {
            base = 330000000; //330 Mhz
        } else if (state->nxdn_base_freq == 3) {
            base = 400000000; //400 Mhz
        } else if (state->nxdn_base_freq == 4) {
            base = 750000000; //750 Mhz
        } else {
            base = 0; //just set to zero, will be system definable most likely and won't be able to calc
        }

        //determine the step value in Hz
        if (state->nxdn_step == 2) {
            step = 1250; //1.25 kHz
        } else if (state->nxdn_step == 3) {
            step = 3125; //3.125 kHz
        } else {
            step = 0; //just set to zero, will be system definable most likely and won't be able to calc
        }

        //if we have a valid base and step, then calc frequency
        //6.5.45. Outbound/Inbound Frequency Number (OFN/IFN)
        if (base && step) {
            freq = base + (channel * step);
            fprintf(stderr, "\n  DFA Frequency [%.6lf] MHz", (double)freq / 1000000);
            // Persist learned mapping for UI visibility and later reuse
            if (freq != 0) {
                state->trunk_chan_map[channel] = freq;
            }
            return (freq);
        } else {
            fprintf(stderr, "\n    Custom DFA Settings -- Unknown Freq;");
            return (0);
        }

    }

    else {
        fprintf(stderr, "\n    Channel not found in import file");
        return (0);
    }
}

long int
nxdn_channel_to_frequency_quiet(dsd_state* state, uint16_t channel) {
    if (!state) {
        return 0;
    }

    // First: imported/learned mapping.
    long int freq = state->trunk_chan_map[channel];
    if (freq != 0) {
        return freq;
    }

    // DFA/RCN=1 mapping (base + step) when broadcast and usable.
    if (state->nxdn_rcn != 1) {
        return 0;
    }

    long int base = 0;
    if (state->nxdn_base_freq == 1) {
        base = 100000000; //100 MHz
    } else if (state->nxdn_base_freq == 2) {
        base = 330000000; //330 Mhz
    } else if (state->nxdn_base_freq == 3) {
        base = 400000000; //400 Mhz
    } else if (state->nxdn_base_freq == 4) {
        base = 750000000; //750 Mhz
    }

    long int step = 0;
    if (state->nxdn_step == 2) {
        step = 1250; //1.25 kHz
    } else if (state->nxdn_step == 3) {
        step = 3125; //3.125 kHz
    }

    if (!base || !step) {
        return 0;
    }

    freq = base + ((long int)channel * step);
    if (freq != 0) {
        state->trunk_chan_map[channel] = freq;
    }
    return freq;
}
