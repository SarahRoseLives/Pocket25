// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#ifndef BCH_63_16_HPP_a7b9c2d4e1f83056
#define BCH_63_16_HPP_a7b9c2d4e1f83056

/**
 * @file
 * @brief BCH(63,16,11) encoder/decoder for P25 NID.
 *
 * Implements a binary BCH code that can correct up to 11 bit errors.
 * Used for P25 Phase 1 Network ID (NID) error correction.
 *
 * Based on the algorithms from Simon Rockliff's Reed-Solomon implementation
 * and adapted for binary BCH codes. The BCH code operates over GF(2) but uses
 * GF(2^6) for syndrome calculation and error location.
 *
 * References:
 * - Lin & Costello, "Error Control Coding"
 * - P25 TIA-102.BAAA specification
 */

/**
 * BCH(63,16,11) decoder class.
 *
 * Parameters:
 * - n = 63 = 2^6 - 1 (codeword length in bits)
 * - k = 16 (data bits: 12-bit NAC + 4-bit DUID)
 * - t = 11 (error correction capability)
 * - Uses GF(2^6) with primitive polynomial x^6 + x + 1
 */
class BCH_63_16_11 {
  private:
    static const int MM = 6;  // GF(2^6)
    static const int NN = 63; // n = 2^6 - 1
    static const int KK = 16; // k = data bits
    static const int TT = 11; // t = error correction capability

    int alpha_to[NN + 1]; // antilog table: alpha_to[i] = alpha^i
    int index_of[NN + 1]; // log table: index_of[x] = i where alpha^i = x

    void
    generate_gf() {
        // Primitive polynomial: x^6 + x + 1 -> coefficients [1,1,0,0,0,0,1]
        // Same as used in ReedSolomon_63
        int pp[MM + 1] = {1, 1, 0, 0, 0, 0, 1};

        int mask = 1;
        alpha_to[MM] = 0;
        for (int i = 0; i < MM; i++) {
            alpha_to[i] = mask;
            index_of[alpha_to[i]] = i;
            if (pp[i] != 0) {
                alpha_to[MM] ^= mask;
            }
            mask <<= 1;
        }
        index_of[alpha_to[MM]] = MM;
        mask >>= 1;
        for (int i = MM + 1; i < NN; i++) {
            if (alpha_to[i - 1] >= mask) {
                alpha_to[i] = alpha_to[MM] ^ ((alpha_to[i - 1] ^ mask) << 1);
            } else {
                alpha_to[i] = alpha_to[i - 1] << 1;
            }
            index_of[alpha_to[i]] = i;
        }
        index_of[0] = -1; // log(0) is undefined, use -1 as sentinel
    }

  public:
    BCH_63_16_11() { generate_gf(); }

    /**
     * Decode a BCH(63,16,11) codeword.
     *
     * @param input Array of 63 chars, each containing a bit (0 or 1).
     *              Bit ordering matches IT++ systematic convention:
     *              data bits in positions 0-15 (MSB first), parity in 16-62.
     * @param output Array of 16 chars to receive corrected data bits.
     * @return true if decoding succeeded (0-11 errors corrected),
     *         false if too many errors to correct.
     */
    bool
    decode(const char* input, char* output) {
        int recd[NN];      // received codeword (working copy)
        int s[2 * TT + 1]; // syndromes

        // Copy input to working array with bit reversal to match IT++ convention
        // IT++ maps: r[j] = rbin(n - j - 1), so input[0] -> recd[62], input[62] -> recd[0]
        for (int i = 0; i < NN; i++) {
            recd[i] = input[NN - 1 - i] ? 1 : 0;
        }

        // Calculate syndromes
        // For BCH, syndrome S_i = sum of r_j * alpha^(i*j) for j=0..n-1
        int syn_error = 0;
        for (int i = 1; i <= 2 * TT; i++) {
            s[i] = 0;
            for (int j = 0; j < NN; j++) {
                if (recd[j]) {
                    s[i] ^= alpha_to[(i * j) % NN];
                }
            }
            if (s[i] != 0) {
                syn_error = 1;
            }
            s[i] = index_of[s[i]]; // convert to index form
        }

        if (!syn_error) {
            // No errors - extract data bits matching IT++ convention
            // Same extraction as the error-corrected case
            for (int i = 0; i < KK; i++) {
                output[i] = (char)recd[NN - 1 - i];
            }
            return true;
        }

        // Berlekamp-Massey algorithm to find error locator polynomial
        // Following the implementation pattern from ReedSolomon.hpp
        int elp[2 * TT + 2][2 * TT]; // error locator polynomial
        int d[2 * TT + 2];           // discrepancy
        int l[2 * TT + 2];           // degree of elp
        int u_lu[2 * TT + 2];        // u - l[u]

        // Initialize
        d[0] = 0;    // index form
        d[1] = s[1]; // index form
        elp[0][0] = 0;
        elp[1][0] = 1;
        for (int i = 1; i < 2 * TT; i++) {
            elp[0][i] = -1;
            elp[1][i] = 0;
        }
        l[0] = 0;
        l[1] = 0;
        u_lu[0] = -1;
        u_lu[1] = 0;

        int u = 0;
        do {
            u++;
            if (d[u] == -1) {
                l[u + 1] = l[u];
                for (int i = 0; i <= l[u]; i++) {
                    elp[u + 1][i] = elp[u][i];
                    // Guard: only index into lookup table for valid values
                    if (elp[u][i] >= 0) {
                        elp[u][i] = index_of[elp[u][i]];
                    }
                }
            } else {
                // Find q with greatest u_lu[q] where d[q] != 0
                int q = u - 1;
                while ((d[q] == -1) && (q > 0)) {
                    q--;
                }
                if (q > 0) {
                    int j = q;
                    do {
                        j--;
                        if ((d[j] != -1) && (u_lu[q] < u_lu[j])) {
                            q = j;
                        }
                    } while (j > 0);
                }

                // Store degree of new elp polynomial
                if (l[u] > l[q] + u - q) {
                    l[u + 1] = l[u];
                } else {
                    l[u + 1] = l[q] + u - q;
                }

                // Form new elp(x)
                for (int i = 0; i < 2 * TT; i++) {
                    elp[u + 1][i] = 0;
                }
                for (int i = 0; i <= l[q]; i++) {
                    if (elp[q][i] != -1) {
                        elp[u + 1][i + u - q] = alpha_to[(d[u] + NN - d[q] + elp[q][i]) % NN];
                    }
                }
                for (int i = 0; i <= l[u]; i++) {
                    elp[u + 1][i] ^= elp[u][i];
                    // Guard: only index into lookup table for valid values
                    if (elp[u][i] >= 0) {
                        elp[u][i] = index_of[elp[u][i]];
                    }
                }
            }
            u_lu[u + 1] = u - l[u + 1];

            // Form (u+1)th discrepancy
            if (u < 2 * TT) {
                if (s[u + 1] != -1) {
                    d[u + 1] = alpha_to[s[u + 1]];
                } else {
                    d[u + 1] = 0;
                }
                for (int i = 1; i <= l[u + 1]; i++) {
                    if ((s[u + 1 - i] != -1) && (elp[u + 1][i] != 0)) {
                        d[u + 1] ^= alpha_to[(s[u + 1 - i] + index_of[elp[u + 1][i]]) % NN];
                    }
                }
                d[u + 1] = index_of[d[u + 1]];
            }
        } while ((u < 2 * TT) && (l[u + 1] <= TT));

        u++;
        if (l[u] > TT) {
            // Too many errors to correct
            return false;
        }

        // Convert elp to index form
        for (int i = 0; i <= l[u]; i++) {
            elp[u][i] = index_of[elp[u][i]];
        }

        // Chien search: find roots of error locator polynomial
        int reg[TT + 1];
        int loc[TT]; // error locations
        int count = 0;

        for (int i = 1; i <= l[u]; i++) {
            reg[i] = elp[u][i];
        }

        for (int i = 1; i <= NN; i++) {
            int q = 1;
            for (int j = 1; j <= l[u]; j++) {
                if (reg[j] != -1) {
                    reg[j] = (reg[j] + j) % NN;
                    q ^= alpha_to[reg[j]];
                }
            }
            if (q == 0) {
                // Found a root
                loc[count] = NN - i;
                count++;
                if (count > TT) {
                    break; // Safety check
                }
            }
        }

        if (count != l[u]) {
            // Number of roots doesn't match degree - decoding failure
            return false;
        }

        // Correct the errors (for binary BCH, just flip the bits)
        for (int i = 0; i < count; i++) {
            if (loc[i] >= 0 && loc[i] < NN) {
                recd[loc[i]] ^= 1;
            }
        }

        // Extract data bits matching IT++ systematic convention:
        // IT++ extracts: m[j] = c[n - k + j] for j=0..k-1 (positions 47-62 in internal order)
        // Then reverses output: mbin(k - j - 1) = bit
        // Combined effect: output[i] = recd[NN - KK + (KK - 1 - i)] = recd[NN - 1 - i]
        for (int i = 0; i < KK; i++) {
            output[i] = (char)recd[NN - 1 - i];
        }

        return true;
    }
};

#endif // BCH_63_16_HPP_a7b9c2d4e1f83056
