// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*
 * ncurses_visualizers.c
 * RTL-SDR visualization panels: constellation, eye diagram, histogram, spectrum
 */

#include <dsd-neo/ui/ncurses_internal.h>
#include <dsd-neo/ui/ncurses_visualizers.h>

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/ui/ui_prims.h>

#include <dsd-neo/platform/curses_compat.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#ifdef USE_RTLSDR
#include <dsd-neo/io/rtl_stream_c.h>
#endif

void
print_constellation_view(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);
#ifdef USE_RTLSDR
    /* Fetch a snapshot of recent I/Q points */
    enum { MAXP = 4096 };

    float buf[(size_t)MAXP * 2];
    int n = rtl_stream_constellation_get(buf, MAXP);

    ui_print_header("Constellation");
    if (n <= 0) {
        ui_print_lborder();
        printw(" (no samples yet)\n");
        attron(COLOR_PAIR(4));
        ui_print_hr();
        attroff(COLOR_PAIR(4));
        return;
    }

    /* Determine grid size from terminal */
    int rows = 24, cols = 80;
    getmaxyx(stdscr, rows, cols);
    int W = cols - 4;
    if (W < 32) {
        W = 32;
    }
    /* Make the constellation a bit taller by default for readability */
    int H = rows / 2;
    if (H < 12) {
        H = 12;
    }

    /* Respect UI toggles */
    int use_unicode = (opts && opts->eye_unicode && ui_unicode_supported());

    /* Local palettes */
    static const char ascii_palette[] = " .:-=+*#%@"; /* 10 levels */
    const int ascii_len = (int)(sizeof(ascii_palette) - 1);
    /* Blocks (eye-style) */
    static const char* block_palette[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"}; /* 9 levels */
    const int block_len = (int)(sizeof(block_palette) / sizeof(block_palette[0]));
    /* Dots of increasing weight/size (preferred for constellation) */
    static const char* dot_palette[] = {" ", "·", "∙", "•", "●", "⬤"}; /* 6 levels */
    const int dot_len = (int)(sizeof(dot_palette) / sizeof(dot_palette[0]));
    int use_dots = 1; /* default to dot style for constellation */

    /* Optional color ramp (blue->cyan->green->yellow->red) */
    static const short color_seq[] = {COLOR_BLUE,    COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW,
                                      COLOR_MAGENTA, COLOR_RED,  COLOR_WHITE};
    const int color_len = (int)(sizeof(color_seq) / sizeof(color_seq[0]));
    const short color_base = 41; /* keep separate from eye's base to avoid clashes */
    const short guide_h_pair = (short)(color_base + 8);
    const short guide_v_pair = (short)(color_base + 9);
    const short guide_x_pair = (short)(color_base + 10);
    static int color_inited = 0;
    if (opts && opts->eye_color && has_colors() && !color_inited) {
        for (int i = 0; i < color_len; i++) {
            init_pair((short)(color_base + i), color_seq[i], COLOR_BLACK);
        }
        init_pair(guide_h_pair, COLOR_YELLOW, COLOR_BLACK);
        init_pair(guide_v_pair, COLOR_CYAN, COLOR_BLACK);
        init_pair(guide_x_pair, COLOR_MAGENTA, COLOR_BLACK);
        color_inited = 1;
    }

    /* Density buffer (reused across frames to avoid malloc/free churn) */
    size_t den_sz = (size_t)H * (size_t)W;
    static unsigned short* s_den = NULL;
    static size_t s_den_cap = 0;
    if (s_den_cap < den_sz) {
        void* nb = realloc(s_den, den_sz * sizeof(unsigned short));
        if (!nb) {
            free(s_den);
            s_den = NULL;
            s_den_cap = 0;
            printw("| (constellation: out of memory)\n");
            ui_print_hr();
            return;
        }
        s_den = (unsigned short*)nb;
        s_den_cap = den_sz;
    }
    unsigned short* den = s_den;
    if (den_sz > 0 && den != NULL) {
        memset(den, 0, den_sz * sizeof(unsigned short));
    }
    if (den == NULL) {
        return;
    }

    /* Dynamic radial scale using high-percentile magnitude (robust to outliers), then smooth. */
    static int s_maxR = 256; /* smoothed scale for radius (~99th percentile of |IQ|) */
    /* Collect absolute I/Q and magnitude values for percentile computation */
    /* Note: float samples are normalized [-1.0, 1.0]; scale to integer range for legacy logic */
    static const float kFloatScale = 16384.0f;
    static int absI[(size_t)MAXP];
    static int absQ[(size_t)MAXP];
    static int magR[(size_t)MAXP];
    for (int k = 0; k < n; k++) {
        float i = buf[(size_t)(k << 1) + 0] * kFloatScale;
        float q = buf[(size_t)(k << 1) + 1] * kFloatScale;
        int ai = (int)lrintf(fabsf(i));
        int aq = (int)lrintf(fabsf(q));
        absI[k] = ai;
        absQ[k] = aq;
        double r2 = (double)i * (double)i + (double)q * (double)q;
        int r = (int)lrint(sqrt(r2));
        magR[k] = r;
    }
    (void)absI;
    (void)absQ;
    /* Compute 99th percentile radius via quickselect (avoid full sort) */
    /* Use 99th percentile (index ~ 0.99*(n-1)) to ignore rare spikes */
    int idxP = (int)lrint(0.99 * (double)(n - 1));
    if (idxP < 0) {
        idxP = 0;
    }
    if (idxP >= n) {
        idxP = n - 1;
    }
    int pR = select_k_int_local(magR, n, idxP);
    /* Avoid zooming into noise; also keep a sane lower bound */
    if (pR < 64) {
        pR = 64;
    }
    /* EMA smoothing (alpha ~0.2) */
    s_maxR = (int)(0.8 * (double)s_maxR + 0.2 * (double)pR);
    if (s_maxR < 64) {
        s_maxR = 64;
    }

    /* Magnitude gate to reduce near-origin clutter */
    double gate = 0.10;
    if (opts) {
        gate = (opts->mod_qpsk == 1) ? (double)opts->const_gate_qpsk : (double)opts->const_gate_other;
        if (gate < 0.0) {
            gate = 0.0;
        }
        if (gate > 0.90) {
            gate = 0.90;
        }
    }
    const double gate2 = gate * gate;

    /* Accumulate density */
    int cx = W / 2, cy = H / 2;
    /* Use equal scale on both axes so circles stay round on wide terminals. */
    int halfX = (W / 2) - 1;
    int halfY = (H / 2) - 1;
    if (halfX < 1) {
        halfX = 1;
    }
    if (halfY < 1) {
        halfY = 1;
    }
    int scale_eq = (halfX < halfY) ? halfX : halfY;
    /* Terminal cell aspect compensation: rows are visually taller than columns.
       Compress vertical mapping to counteract oval appearance (empirical factor). */
    const double y_aspect = 0.55; /* 0.5–0.6 typical; adjust if needed */
    /* Add a small headroom margin so dense clusters don't pin to the border */
    const double outer_margin = 0.92; /* 92% of the square radius */
    /* Define a centered square plotting region so each quadrant is square in rows/cols */
    int x0 = cx - scale_eq;
    int x1 = cx + scale_eq;
    int y0 = cy - scale_eq;
    int y1 = cy + scale_eq;
    unsigned short dmax = 0;
    for (int k = 0; k < n; k++) {
        /* Scale normalized float samples to integer range for legacy logic */
        float fi = buf[(size_t)(k << 1) + 0] * kFloatScale;
        float fq = buf[(size_t)(k << 1) + 1] * kFloatScale;
        /* Compute raw magnitude for gating and optional unit-circle normalization */
        double ii = (double)fi;
        double qq = (double)fq;
        double r = sqrt(ii * ii + qq * qq);
        double rn = r / (double)s_maxR; /* normalized radius for gating */
        if ((rn * rn) < gate2) {
            continue;
        }
        double nx, ny;
        if (opts && opts->const_norm_mode == 1) {
            /* Unit-circle normalization (direction only) */
            if (r <= 1e-9) {
                continue; /* skip degenerate */
            }
            nx = ii / r;
            ny = qq / r;
        } else {
            /* Radial (percentile) normalization */
            nx = ii / (double)s_maxR;
            ny = qq / (double)s_maxR;
        }
        int x = cx + (int)lrint(nx * (double)scale_eq * outer_margin);
        int y = cy - (int)lrint(ny * (double)scale_eq * outer_margin * y_aspect);
        if (x < 0) {
            x = 0;
        }
        if (x >= W) {
            x = W - 1;
        }
        if (y < 0) {
            y = 0;
        }
        if (y >= H) {
            y = H - 1;
        }
        unsigned short* cell = &den[(size_t)y * (size_t)W + (size_t)x];
        if (*cell != 0xFFFF) {
            (*cell)++;
            if (*cell > dmax) {
                dmax = *cell;
            }
        }
    }
    if (dmax == 0) {
        dmax = 1; /* avoid div-by-zero */
    }

    /* Render with overlays and optional color (trim to active density vertically) */
    /* Find first/last rows within [y0,y1] that contain any density. */
    int y_start = y0;
    int y_end = y1;
    if (y_start < 0) {
        y_start = 0;
    }
    if (y_end >= H) {
        y_end = H - 1;
    }
    int y_top = -1, y_bot = -1;
    for (int y = y_start; y <= y_end; y++) {
        int has = 0;
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && x < W) {
                if (den[(size_t)y * (size_t)W + (size_t)x] > 0) {
                    has = 1;
                    break;
                }
            }
        }
        if (has) {
            y_top = y;
            break;
        }
    }
    for (int y = y_end; y >= y_start; y--) {
        int has = 0;
        for (int x = x0; x <= x1; x++) {
            if (x >= 0 && x < W) {
                if (den[(size_t)y * (size_t)W + (size_t)x] > 0) {
                    has = 1;
                    break;
                }
            }
        }
        if (has) {
            y_bot = y;
            break;
        }
    }
    if (y_top >= 0 && y_bot >= y_top) {
        y_start = y_top;
        y_end = y_bot;
    }
    for (int y = y_start; y <= y_end; y++) {
        ui_print_lborder();
        int last_pair = -1;
        for (int x = 0; x < W; x++) {
            /* Determine overlays (restricted to the centered square region) */
            int inside_sq = (x >= x0 && x <= x1 && y >= y0 && y <= y1);
            int is_haxis = inside_sq && (y == cy);
            int is_vaxis = inside_sq && (x == cx);
            int is_diag = 0;
            if (inside_sq && opts && (opts->mod_qpsk == 1)) {
                /* Adjust diagonals to preserve ~45° visually under aspect correction */
                int dx = x - cx;
                int y_d1 = cy + (int)lrint((double)dx * y_aspect);
                int y_d2 = cy - (int)lrint((double)dx * y_aspect);
                is_diag = (y == y_d1) || (y == y_d2);
            }

            unsigned short d = den[(size_t)y * (size_t)W + (size_t)x];
            char ch = ' ';
            int used_guide = 0;

            if (is_haxis || is_vaxis || is_diag) {
                /* Choose overlay char */
                if ((is_haxis && is_vaxis) || (is_vaxis && is_diag) || (is_haxis && is_diag)) {
                    ch = '+';
                } else if (is_haxis) {
                    ch = '-';
                } else if (is_vaxis) {
                    ch = '|';
                } else {
                    ch = (x >= cx) ? '\\' : '/';
                }
                if (opts && opts->eye_color && has_colors()) {
                    short gp = is_diag ? guide_x_pair : (is_haxis ? guide_h_pair : guide_v_pair);
                    if (last_pair >= 0) {
                        attroff(COLOR_PAIR(last_pair));
                        last_pair = -1;
                    }
                    attron(COLOR_PAIR(gp));
                    used_guide = gp;
                }
            } else if (inside_sq && d > 0) {
                /* Density glyph + color */
                double f = (double)d / (double)dmax;
                if (f < 0.0) {
                    f = 0.0;
                }
                if (f > 1.0) {
                    f = 1.0;
                }
                double g = ui_gamma_map01(f); /* gamma brighten via LUT */
                if (opts && opts->eye_color && has_colors()) {
                    int ci = (int)lrint(g * (double)(color_len - 1));
                    if (ci < 0) {
                        ci = 0;
                    }
                    if (ci >= color_len) {
                        ci = color_len - 1;
                    }
                    int pid = color_base + ci;
                    if (pid != last_pair) {
                        if (last_pair >= 0) {
                            attroff(COLOR_PAIR(last_pair));
                        }
                        attron(COLOR_PAIR(pid));
                        last_pair = pid;
                    }
                }
                if (use_unicode) {
                    if (use_dots) {
                        int idx = (int)lrint(g * (double)(dot_len - 1));
                        if (idx < 0) {
                            idx = 0;
                        }
                        if (idx >= dot_len) {
                            idx = dot_len - 1;
                        }
                        ch = 0; /* mark to addstr */
                        addstr(dot_palette[idx]);
                    } else {
                        int idx = (int)lrint(g * (double)(block_len - 1));
                        if (idx < 0) {
                            idx = 0;
                        }
                        if (idx >= block_len) {
                            idx = block_len - 1;
                        }
                        ch = 0; /* mark to addstr */
                        addstr(block_palette[idx]);
                    }
                } else {
                    int idx = (int)lrint(g * (double)(ascii_len - 1));
                    if (idx < 0) {
                        idx = 0;
                    }
                    if (idx >= ascii_len) {
                        idx = ascii_len - 1;
                    }
                    ch = ascii_palette[idx];
                }
            }

            /* Reference cluster centers + quadrant labels (QPSK only) */
            if (inside_sq && opts && (opts->mod_qpsk == 1)) {
                /* Reference points near ~70% radius (independent of mode) */
                double refR = 0.70 * (double)s_maxR;
                int ref_ix[4] = {+1, -1, -1, +1};
                int ref_qx[4] = {+1, +1, -1, -1};
                for (int r = 0; r < 4; r++) {
                    double rii = ref_ix[r] * refR;
                    double rqq = ref_qx[r] * refR;
                    double rx = (rii / (double)s_maxR);
                    double ry = (rqq / (double)s_maxR);
                    int xr = cx + (int)lrint(rx * (double)scale_eq * outer_margin);
                    int yr = cy - (int)lrint(ry * (double)scale_eq * outer_margin * y_aspect);
                    if (xr == x && yr == y) {
                        ch = 'o';
                        if (opts->eye_color && has_colors()) {
                            if (last_pair >= 0) {
                                attroff(COLOR_PAIR(last_pair));
                                last_pair = -1;
                            }
                            attron(COLOR_PAIR(guide_x_pair));
                            used_guide = guide_x_pair;
                        }
                    }
                }
                /* Quadrant labels */
                int qdx = (W / 4);
                int qdy = (H / 4);
                if (y == cy - qdy && x == cx + qdx) {
                    ch = '1';
                } else if (y == cy - qdy && x == cx - qdx) {
                    ch = '2';
                } else if (y == cy + qdy && x == cx - qdx) {
                    ch = '3';
                } else if (y == cy + qdy && x == cx + qdx) {
                    ch = '4';
                }
            }

            if (ch != 0) {
                addch(ch);
            }
            if (used_guide) {
                attroff(COLOR_PAIR(used_guide));
                /* Restore density color if active */
                if (opts && opts->eye_color && has_colors()) {
                    /* Recompute density pair for this cell */
                    if (d > 0) {
                        double f = (double)d / (double)dmax;
                        if (f < 0.0) {
                            f = 0.0;
                        }
                        if (f > 1.0) {
                            f = 1.0;
                        }
                        double g = ui_gamma_map01(f);
                        int ci = (int)lrint(g * (double)(color_len - 1));
                        if (ci < 0) {
                            ci = 0;
                        }
                        if (ci >= color_len) {
                            ci = color_len - 1;
                        }
                        int pid = color_base + ci;
                        attron(COLOR_PAIR(pid));
                        last_pair = pid;
                    }
                }
            }
        }
        if (opts && opts->eye_color && has_colors()) {
            if (last_pair >= 0) {
                attroff(COLOR_PAIR(last_pair));
            }
        }
        printw("\n");
    }

    /* Legend */
    ui_print_lborder();
    printw(" Ref: axes '+', '/'\\'\\' slicer; 'o' cluster refs\n");
    if (use_unicode) {
        if (use_dots) {
            ui_print_lborder();
            printw(" Density: · • ● ⬤  (low -> high)%s\n",
                   (opts && opts->eye_color && has_colors()) ? "; colored" : "");
        } else {
            ui_print_lborder();
            printw(" Density: ▁ ▂ ▃ ▄ ▅ ▆ ▇ █  (low -> high)%s\n",
                   (opts && opts->eye_color && has_colors()) ? "; colored" : "");
        }
    } else {
        ui_print_lborder();
        printw(" Density: . : - = + * # @  (low -> high)%s\n",
               (opts && opts->eye_color && has_colors()) ? "; colored" : "");
    }
    /* Color bar legend (consistent with Eye Diagram) */
    ui_print_lborder();
    printw(" Norm: %s (toggle with 'n')\n", (opts && opts->const_norm_mode) ? "unit-circle" : "radial (p99)");
    if (opts && opts->eye_color && has_colors()) {
        ui_print_lborder();
        addch('\n');
        ui_print_lborder();
        printw(" Color:   ");
        for (int i = 0; i < color_len; i++) {
            attron(COLOR_PAIR((short)(color_base + i)));
            if (use_unicode) {
                addstr("██");
            } else {
                addstr("##");
            }
            attroff(COLOR_PAIR((short)(color_base + i)));
        }
        printw("  low -> high\n");
        ui_print_lborder();
        printw("          ");
        int barw = color_len * 2;
        for (int x = 0; x < barw; x++) {
            if (x == 0 || x == barw / 2 || x == barw - 1) {
                addch('|');
            } else {
                addch(' ');
            }
        }
        printw("\n");
        ui_print_lborder();
        printw("          0%%");
        int pad_mid = barw / 2 - 2;
        for (int i = 0; i < pad_mid; i++) {
            addch(' ');
        }
        printw("50%%");
        int pad_end = barw - (barw / 2 + 2) - 4;
        for (int i = 0; i < pad_end; i++) {
            addch(' ');
        }
        printw("100%%\n");
    }
    attron(COLOR_PAIR(4));
    attron(COLOR_PAIR(4));
    ui_print_hr();
    attroff(COLOR_PAIR(4));
    attroff(COLOR_PAIR(4));
    /* s_den reused; no free */
#else
    ui_print_header("Constellation");
    printw("| (RTL disabled in this build)\n");
    ui_print_hr();
#endif
}

void
print_eye_view(dsd_opts* opts, dsd_state* state) {
#ifdef USE_RTLSDR
    /* Fetch a snapshot of recent I-channel samples and SPS */
    enum { MAXS = 16384 };

    static float buf[(size_t)MAXS];
    int sps = 0;
    int n = rtl_stream_eye_get(buf, MAXS, &sps);
    ui_print_header("Eye Diagram (C4FM/FSK)");
    /* Auto-fallback to ASCII if Unicode likely unsupported */
    static int s_unicode_ready = -1;
    static int s_unicode_warned = 0;
    if (s_unicode_ready < 0) {
        s_unicode_ready = ui_unicode_supported() ? 1 : 0;
    }
    /* Compute effective Unicode use for this frame without mutating opts */
    int use_unicode_ui = (opts && opts->eye_unicode && s_unicode_ready);
    if (opts && opts->eye_unicode && !s_unicode_ready && !s_unicode_warned) {
        printw("| (Unicode block glyphs unsupported; falling back to ASCII)\n");
        s_unicode_warned = 1;
    }
    if (n <= 0 || sps <= 0) {
        ui_print_lborder();
        printw(" (no samples or SPS)\n");
        attron(COLOR_PAIR(4));
        ui_print_hr();
        attroff(COLOR_PAIR(4));
        return;
    }
    /* Grid size adaptive */
    int rows = 24, cols = 80;
    getmaxyx(stdscr, rows, cols);
    int W = cols - 4;
    if (W < 32) {
        W = 32;
    }
    int H = rows / 3;
    if (H < 12) {
        H = 12;
    }
    /* Density buffer sized to current grid (reuse across frames) */
    size_t den_sz = (size_t)H * (size_t)W;
    static unsigned short* s_den_eye = NULL;
    static size_t s_den_eye_cap = 0;
    if (s_den_eye_cap < den_sz) {
        void* nb = realloc(s_den_eye, den_sz * sizeof(unsigned short));
        if (!nb) {
            free(s_den_eye);
            s_den_eye = NULL;
            s_den_eye_cap = 0;
            printw("| (eye: out of memory)\n");
            ui_print_hr();
            return;
        }
        s_den_eye = (unsigned short*)nb;
        s_den_eye_cap = den_sz;
    }
    unsigned short* den = s_den_eye;
    if (den_sz > 0 && den != NULL) {
        memset(den, 0, den_sz * sizeof(unsigned short));
    }
    if (den == NULL) {
        return;
    }
    int mid = H / 2;
    /* Normalize peak with EMA for stability */
    /* Note: float samples are normalized [-1.0, 1.0]; scale to integer range for legacy logic */
    static const float kEyeFloatScale = 16384.0f;
    static int s_peak = 256;
    int peak = 1;
    for (int i = 0; i < n; i++) {
        float v = buf[i] * kEyeFloatScale;
        int a = (int)lrintf(fabsf(v));
        if (a > peak) {
            peak = a;
        }
    }
    if (peak < 64) {
        peak = 64;
    }
    s_peak = (int)(0.8 * (double)s_peak + 0.2 * (double)peak);
    if (s_peak < 64) {
        s_peak = 64;
    }
    /* Build quartiles for reference levels */
    int step_ds = (n > 8192) ? (n / 8192) : 1;
    int m = (n + step_ds - 1) / step_ds;
    if (m > 8192) {
        m = 8192;
    }
    static int qvals[8192];
    int vi = 0;
    for (int i = 0; i < n && vi < m; i += step_ds) {
        qvals[vi++] = (int)lrintf(buf[i] * kEyeFloatScale);
    }
    m = vi;
    if (m < 8) {
        qvals[0] = -s_peak;
        qvals[1] = s_peak;
        m = 2;
    }
    /* Quartiles via quickselect */
    int idx1 = (int)((size_t)m / 4);
    int idx2 = (int)((size_t)m / 2);
    int idx3 = (int)((size_t)(3 * (size_t)m) / 4);
    int q2 = select_k_int_local(qvals, m, idx2);
    int q1 = select_k_int_local(qvals, idx2, idx1); /* select within lower half */
    int q3 = select_k_int_local(qvals + idx2 + 1, m - (idx2 + 1), idx3 - (idx2 + 1));
    /* Accumulate density by folding modulo 2 symbols */
    int two_sps = 2 * sps;
    if (two_sps < 8) {
        two_sps = 8;
    }
    for (int i = 0; i < n; i++) {
        double v = ((double)buf[i] * (double)kEyeFloatScale) / (double)s_peak;
        if (v > 1.0) {
            v = 1.0;
        }
        if (v < -1.0) {
            v = -1.0;
        }
        int y = mid - (int)lrint(v * ((double)H / 2.0 - 1.0));
        if (y < 0) {
            y = 0;
        }
        if (y >= H) {
            y = H - 1;
        }
        int phase = i % two_sps;
        int x = (int)lrint(((double)phase / (double)(two_sps - 1)) * (double)(W - 1));
        if (x < 0) {
            x = 0;
        }
        if (x >= W) {
            x = W - 1;
        }
        size_t di = (size_t)y * (size_t)W + (size_t)x;
        if (den[di] < 65535) {
            den[di]++;
        }
    }
    /* Determine max density for mapping */
    unsigned short dmax = 1;
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            unsigned short dv = den[(size_t)y * (size_t)W + (size_t)x];
            if (dv > dmax) {
                dmax = dv;
            }
        }
    }
    /* Map density to a higher-contrast palette (ASCII or Unicode) and overlay guides */
    /* ASCII palette (low -> high density) */
    static const char ascii_palette[] = " .:-=+*#%@"; /* 10 levels */
    const int ascii_len = (int)(sizeof(ascii_palette) - 1);
    /* Unicode block palette (UTF-8), width 1 per glyph */
    static const char* uni_palette[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"}; /* 9 levels */
    const int uni_len = (int)(sizeof(uni_palette) / sizeof(uni_palette[0]));

    /* Optional color density mapping */
    static int color_inited = 0;
    static const short color_seq[] = {COLOR_BLUE,    COLOR_CYAN, COLOR_GREEN, COLOR_YELLOW,
                                      COLOR_MAGENTA, COLOR_RED,  COLOR_WHITE};
    const int color_len = (int)(sizeof(color_seq) / sizeof(color_seq[0]));
    const short color_base = 21; /* avoid clashing with existing pairs */
    /* Guide color pairs (horizontal, vertical, cross) */
    const short guide_h_pair = (short)(color_base + 8);
    const short guide_v_pair = (short)(color_base + 9);
    const short guide_x_pair = (short)(color_base + 10);
    if (opts->eye_color && has_colors() && !color_inited) {
        for (int i = 0; i < color_len; i++) {
            init_pair((short)(color_base + i), color_seq[i], COLOR_BLACK);
        }
        init_pair(guide_h_pair, COLOR_YELLOW, COLOR_BLACK);
        init_pair(guide_v_pair, COLOR_CYAN, COLOR_BLACK);
        init_pair(guide_x_pair, COLOR_MAGENTA, COLOR_BLACK);
        color_inited = 1;
    }

    /* Compute reference rows for quartiles (approximate C4FM levels) */
    int yq1 = mid - (int)lrint(((double)q1 / (double)s_peak) * ((double)H / 2.0 - 1.0));
    if (yq1 < 0) {
        yq1 = 0;
    }
    if (yq1 >= H) {
        yq1 = H - 1;
    }
    int yq2 = mid - (int)lrint(((double)q2 / (double)s_peak) * ((double)H / 2.0 - 1.0));
    if (yq2 < 0) {
        yq2 = 0;
    }
    if (yq2 >= H) {
        yq2 = H - 1;
    }
    int yq3 = mid - (int)lrint(((double)q3 / (double)s_peak) * ((double)H / 2.0 - 1.0));
    if (yq3 < 0) {
        yq3 = 0;
    }
    if (yq3 >= H) {
        yq3 = H - 1;
    }
    /* Symbol boundary columns (phase 0, 1 symbol, 2 symbols) */
    int xb0 = 0;
    int xb1 = (int)lrint(((double)sps / (double)(two_sps - 1)) * (double)(W - 1));
    if (xb1 < 0) {
        xb1 = 0;
    }
    if (xb1 >= W) {
        xb1 = W - 1;
    }
    int xb2 = W - 1;

    /* Draw with overlays */
    for (int y = 0; y < H; y++) {
        ui_print_lborder();
        int last_pair = -1;
        for (int x = 0; x < W; x++) {
            unsigned short d = den[(size_t)y * (size_t)W + (size_t)x];
            char ch = ' ';
            if (d > 0) {
                /* Gamma to brighten low densities */
                double f = (double)d / (double)dmax;
                if (f < 0.0) {
                    f = 0.0;
                }
                if (f > 1.0) {
                    f = 1.0;
                }
                double g = ui_gamma_map01(f); /* gamma = 0.5 via LUT */
                if (opts->eye_unicode) {
                    int idx = (int)lrint(g * (double)(uni_len - 1));
                    if (idx < 0) {
                        idx = 0;
                    }
                    if (idx >= uni_len) {
                        idx = uni_len - 1;
                    }
                    (void)idx; // reserve for overlays using density index
                    /* Color mapping (based on g) */
                    if (opts->eye_color && has_colors()) {
                        int ci = (int)lrint(g * (double)(color_len - 1));
                        if (ci < 0) {
                            ci = 0;
                        }
                        if (ci >= color_len) {
                            ci = color_len - 1;
                        }
                        int pid = color_base + ci;
                        if (pid != last_pair) {
                            if (last_pair >= 0) {
                                attroff(COLOR_PAIR(last_pair));
                            }
                            attron(COLOR_PAIR(pid));
                            last_pair = pid;
                        }
                    }
                    /* Unicode draw below unless overlays apply */
                    ch = 0; /* marker to indicate we'll addstr() later */
                    /* overlays handled further below */
                    /* store density index in x-local via idx variable */
                    /* reuse idx below if not overridden */
                    /* To keep scope, repeat calculation */
                    ;
                } else {
                    int idx = (int)lrint(g * (double)(ascii_len - 1));
                    if (idx < 0) {
                        idx = 0;
                    }
                    if (idx >= ascii_len) {
                        idx = ascii_len - 1;
                    }
                    if (opts->eye_color && has_colors()) {
                        int ci = (int)lrint(g * (double)(color_len - 1));
                        if (ci < 0) {
                            ci = 0;
                        }
                        if (ci >= color_len) {
                            ci = color_len - 1;
                        }
                        int pid = color_base + ci;
                        if (pid != last_pair) {
                            if (last_pair >= 0) {
                                attroff(COLOR_PAIR(last_pair));
                            }
                            attron(COLOR_PAIR(pid));
                            last_pair = pid;
                        }
                    }
                    ch = ascii_palette[idx];
                }
            }
            /* Determine overlays */
            int is_hline = (y == yq1 || y == yq2 || y == yq3);
            int is_vline = (x == xb0 || x == xb1 || x == xb2);
            int used_guide = 0;
            if (is_hline || is_vline) {
                /* Choose overlay character */
                if (is_hline && is_vline) {
                    ch = '+';
                } else if (is_hline) {
                    ch = (ch == ' ' || ch == '.' || ch == ':') ? '-' : '=';
                } else {
                    ch = (ch == ' ' || ch == '.' || ch == ':' || ch == '-') ? '|' : '+';
                }
                /* Apply guide colors if enabled */
                if (opts->eye_color && has_colors()) {
                    short gp = is_hline && is_vline ? guide_x_pair : (is_hline ? guide_h_pair : guide_v_pair);
                    if (last_pair >= 0) {
                        attroff(COLOR_PAIR(last_pair));
                        last_pair = -1; /* force reapply after */
                    }
                    attron(COLOR_PAIR(gp));
                    used_guide = gp;
                }
            }
            if (use_unicode_ui && ch == 0 && !used_guide) {
                /* Redetermine density index for unicode and print glyph */
                unsigned short d2 = den[(size_t)y * (size_t)W + (size_t)x];
                double f2 = (double)d2 / (double)dmax;
                if (f2 < 0.0) {
                    f2 = 0.0;
                }
                if (f2 > 1.0) {
                    f2 = 1.0;
                }
                double g2 = ui_gamma_map01(f2);
                int uidx = (int)lrint(g2 * (double)(uni_len - 1));
                if (uidx < 0) {
                    uidx = 0;
                }
                if (uidx >= uni_len) {
                    uidx = uni_len - 1;
                }
                addstr(uni_palette[uidx]);
            } else {
                addch(ch);
            }
            /* If we used guide color, restore previous density color */
            if (used_guide) {
                attroff(COLOR_PAIR(used_guide));
                /* Re-enable density color if was active */
                if (opts->eye_color && has_colors()) {
                    /* recompute f to restore approximate density color */
                    double f = (double)d / (double)dmax;
                    if (f < 0.0) {
                        f = 0.0;
                    }
                    if (f > 1.0) {
                        f = 1.0;
                    }
                    double g = ui_gamma_map01(f);
                    int ci = (int)lrint(g * (double)(color_len - 1));
                    if (ci < 0) {
                        ci = 0;
                    }
                    if (ci >= color_len) {
                        ci = color_len - 1;
                    }
                    int pid = color_base + ci;
                    attron(COLOR_PAIR(pid));
                    last_pair = pid;
                }
            }
        }
        if (opts->eye_color && has_colors()) {
            if (last_pair >= 0) {
                attroff(COLOR_PAIR(last_pair));
            }
        }
        addch('\n');
    }
    /* Legend + reference info */
    ui_print_lborder();
    printw(" Ref: '-' Q1/Q3, '=' median; '|' edges; '+' crossings\n");
    if (use_unicode_ui) {
        ui_print_lborder();
        printw(" Density: ▁ ▂ ▃ ▄ ▅ ▆ ▇ █  (low -> high)%s\n", (opts->eye_color && has_colors()) ? "; colored" : "");
    } else {
        ui_print_lborder();
        printw(" Density: . : - = + * # @  (low -> high)%s\n", (opts->eye_color && has_colors()) ? "; colored" : "");
    }
    if (opts->eye_color && has_colors()) {
        ui_print_lborder();
        addch('\n');
        /* Show a color bar legend for density mapping */
        ui_print_lborder();
        printw(" Color:   ");
        for (int i = 0; i < color_len; i++) {
            attron(COLOR_PAIR((short)(color_base + i)));
            if (use_unicode_ui) {
                addstr("██");
            } else {
                addstr("##");
            }
            attroff(COLOR_PAIR((short)(color_base + i)));
        }
        printw("  low -> high\n");
        /* Ticks under the color bar: 0%, 50%, 100% */
        ui_print_lborder();
        printw("          ");
        int barw = color_len * 2;
        for (int x = 0; x < barw; x++) {
            if (x == 0 || x == barw / 2 || x == barw - 1) {
                addch('|');
            } else {
                addch(' ');
            }
        }
        printw("\n");
        ui_print_lborder();
        printw("          0%%");
        int pad_mid = barw / 2 - 2;
        for (int i = 0; i < pad_mid; i++) {
            addch(' ');
        }
        printw("50%%");
        int pad_end = barw - (barw / 2 + 2) - 4;
        for (int i = 0; i < pad_end; i++) {
            addch(' ');
        }
        printw("100%%\n");
    }
    /* Prefer post-filter demod SNR when available (only for confirmed C4FM) */
    double snr_db = -1.0;
    int is_c4fm = (opts->mod_c4fm == 1);
    if (state) {
        is_c4fm = is_c4fm && (state->rf_mod == 0);
    }
#ifdef USE_RTLSDR
    if (is_c4fm) {
        snr_db = rtl_stream_get_snr_c4fm();
    }
#endif
    if (is_c4fm && snr_db < -20.0) {
        /* Fallback: quick estimate using current buffer */
        if (sps > 0 && n > 100) {
            int c1 = sps / 2;
            int c2 = (3 * sps) / 2;
            int win = sps / 10;
            if (win < 1) {
                win = 1;
            }
            long long cnt[4] = {0, 0, 0, 0};
            double sum[4] = {0, 0, 0, 0};
            for (int i = 0; i < n; i++) {
                int phase = i % two_sps;
                int inwin = (abs(phase - c1) <= win) || (abs(phase - c2) <= win);
                if (!inwin) {
                    continue;
                }
                int v = (int)buf[i];
                int b = (v <= q1) ? 0 : (v <= q2) ? 1 : (v <= q3) ? 2 : 3;
                cnt[b]++;
                sum[b] += (double)v;
            }
            double mu[4] = {0, 0, 0, 0};
            long long total = 0;
            for (int b = 0; b < 4; b++) {
                if (cnt[b] > 0) {
                    mu[b] = sum[b] / (double)cnt[b];
                }
                total += cnt[b];
            }
            if (total > 50 && cnt[0] && cnt[1] && cnt[2] && cnt[3]) {
                double nsum = 0.0;
                for (int i = 0; i < n; i++) {
                    int phase = i % two_sps;
                    int inwin = (abs(phase - c1) <= win) || (abs(phase - c2) <= win);
                    if (!inwin) {
                        continue;
                    }
                    int v = (int)buf[i];
                    int b = (v <= q1) ? 0 : (v <= q2) ? 1 : (v <= q3) ? 2 : 3;
                    double e = (double)v - mu[b];
                    nsum += e * e;
                }
                double noise_var = nsum / (double)total;
                double mu_all = 0.0;
                for (int b = 0; b < 4; b++) {
                    mu_all += mu[b] * (double)cnt[b] / (double)total;
                }
                double ssum = 0.0;
                for (int b = 0; b < 4; b++) {
                    double d = mu[b] - mu_all;
                    ssum += (double)cnt[b] * d * d;
                }
                double sig_var = ssum / (double)total;
                if (noise_var > 1e-9 && sig_var > 1e-9) {
                    /* Apply same dynamic C4FM calibration as radio path (accounts for BW/rate). */
#ifdef USE_RTLSDR
                    double bias = rtl_stream_get_snr_bias_c4fm();
#else
                    double bias = 8.0; /* fallback: typical C4FM bias */
#endif
                    snr_db = 10.0 * log10(sig_var / noise_var) - bias;
                }
            }
        }
    }
    if (is_c4fm && snr_db > -50.0) {
        ui_print_lborder();
        printw(" Rows: Q1=%d  Median=%d  Q3=%d   SPS=%d  SNR=%.1f dB\n", yq1, yq2, yq3, sps, snr_db);
    } else {
        ui_print_lborder();
        printw(" Rows: Q1=%d  Median=%d  Q3=%d   SPS=%d  SNR=n/a\n", yq1, yq2, yq3, sps);
    }
    attron(COLOR_PAIR(4));
    ui_print_hr();
    attroff(COLOR_PAIR(4));
    /* den buffer reused across frames; do not free here */
#else
    ui_print_header("Eye Diagram");
    printw("| (RTL disabled in this build)\n");
    ui_print_hr();
#endif
}

void
print_fsk_hist_view(void) {
#ifdef USE_RTLSDR
    enum { MAXS = 8192 };

    static float buf[(size_t)MAXS];
    int sps = 0;
    int n = rtl_stream_eye_get(buf, MAXS, &sps);
    ui_print_header("FSK 4-Level Histogram");
    if (n <= 0) {
        ui_print_lborder();
        printw(" (no samples)\n");
        attron(COLOR_PAIR(4));
        ui_print_hr();
        attroff(COLOR_PAIR(4));
        return;
    }
    /* Compute peak and mean DC offset */
    /* Note: float samples are normalized [-1.0, 1.0]; scale to integer range for legacy logic */
    static const float kHistFloatScale = 16384.0f;
    int peak = 1;
    double sum = 0.0;
    for (int i = 0; i < n; i++) {
        float v = buf[i] * kHistFloatScale;
        int a = (int)lrintf(fabsf(v));
        if (a > peak) {
            peak = a;
        }
        sum += v;
    }
    if (peak < 64) {
        peak = 64;
    }
    double dc_norm = (double)sum / (double)n / (double)peak; /* ~[-1,1] */

    /* Adaptive quartile thresholds over recent I-channel samples. */
    /* Downsample set for faster sort if needed */
    int step = (n > 4096) ? (n / 4096) : 1;
    int m = (n + step - 1) / step;
    if (m < 8) {
        m = n, step = 1; /* ensure adequate sample count */
    }
    /* Copy sampled values into a temp array for sorting */
    static int vals[8192];
    if (m > 8192) {
        m = 8192;
    }
    int vi = 0;
    for (int i = 0; i < n && vi < m; i += step) {
        vals[vi++] = (int)lrintf(buf[i] * kHistFloatScale);
    }
    m = vi;
    /* Quartiles via quickselect */
    int idx1 = (int)((size_t)m / 4);
    int idx2 = (int)((size_t)m / 2);
    int idx3 = (int)((size_t)(3 * (size_t)m) / 4);
    int q2 = select_k_int_local(vals, m, idx2);
    int q1 = select_k_int_local(vals, idx2, idx1);
    int q3 = select_k_int_local(vals + idx2 + 1, m - (idx2 + 1), idx3 - (idx2 + 1));
    /* Bin using quartile boundaries */
    int64_t bin[4] = {0, 0, 0, 0};
    for (int i = 0; i < n; i++) {
        int v = (int)lrintf(buf[i] * kHistFloatScale);
        int b = 0;
        if (v <= q1) {
            b = 0;
        } else if (v <= q2) {
            b = 1;
        } else if (v <= q3) {
            b = 2;
        } else {
            b = 3;
        }
        bin[b]++;
    }
    /* Draw quartile ruler across value span (min..max) */
    int minv = vals[0];
    int maxv = vals[m - 1];
    if (maxv == minv) {
        maxv = minv + 1; /* avoid div-by-zero */
    }

    enum { WR = 60 };

    char ruler[WR];
    for (int x = 0; x < WR; x++) {
        ruler[x] = '-';
    }
    int p1 = (int)lrint(((double)(q1 - minv) / (double)(maxv - minv)) * (double)(WR - 1));
    int p2 = (int)lrint(((double)(q2 - minv) / (double)(maxv - minv)) * (double)(WR - 1));
    int p3 = (int)lrint(((double)(q3 - minv) / (double)(maxv - minv)) * (double)(WR - 1));
    if (p1 < 0) {
        p1 = 0;
    }
    if (p1 >= WR) {
        p1 = WR - 1;
    }
    if (p2 < 0) {
        p2 = 0;
    }
    if (p2 >= WR) {
        p2 = WR - 1;
    }
    if (p3 < 0) {
        p3 = 0;
    }
    if (p3 >= WR) {
        p3 = WR - 1;
    }
    ruler[p1] = '|';
    ruler[p2] = '+'; /* median */
    ruler[p3] = '|';
    ui_print_lborder();
    printw(" Ruler:  ");
    for (int x = 0; x < WR; x++) {
        addch(ruler[x]);
    }
    printw("  (Q1='|', Median='+', Q3='|')\n");

    /* Draw bars */
    const int W = 60;
    int64_t maxc = 1;
    for (int i = 0; i < 4; i++) {
        if (bin[i] > maxc) {
            maxc = bin[i];
        }
    }
    const char* labels[4] = {"L3(-)", "L1(-)", "L1(+)", "L3(+)"};
    ui_print_lborder();
    printw(" DC Offset: %+0.2f%% of full-scale\n", dc_norm * 100.0);
    for (int i = 0; i < 4; i++) {
        int w = (int)((double)bin[i] / (double)maxc * (double)W + 0.5);
        if (w < 0) {
            w = 0;
        }
        if (w > W) {
            w = W;
        }
        ui_print_lborder();
        printw(" %-6s ", labels[i]);
        for (int x = 0; x < w; x++) {
            addch('#');
        }
        for (int x = w; x < W; x++) {
            addch(' ');
        }
        printw(" %lld\n", (long long)bin[i]);
    }
    attron(COLOR_PAIR(4));
    ui_print_hr();
    attroff(COLOR_PAIR(4));
#else
    ui_print_header("FSK 4-Level Histogram");
    printw("| (RTL disabled in this build)\n");
    ui_print_hr();
#endif
}

void
print_spectrum_view(dsd_opts* opts) {
#ifdef USE_RTLSDR
    int nfft = rtl_stream_spectrum_get_size();
    if (nfft < 64) {
        nfft = 64;
    }
    if (nfft > 1024) {
        nfft = 1024;
    }
    static float bins_static[1024];
    float* bins = bins_static; /* nfft clamped to <= 1024 above */
    int rate = 0;
    int n = rtl_stream_spectrum_get(bins, nfft, &rate);
    ui_print_header("Spectrum Analyzer");
    if (n <= 0) {
        printw("| (no spectrum yet)\n");
        ui_print_hr();
        return;
    }
    int rows = 24, cols = 80;
    getmaxyx(stdscr, rows, cols);
    int W = cols - 4;
    if (W < 32) {
        W = 32;
    }
    int H = rows / 3;
    if (H < 10) {
        H = 10;
    }
    /* Downsample or upsample bins to match width W */
    static float col[(size_t)2048];
    if (W > (int)(sizeof col / sizeof col[0])) {
        W = (int)(sizeof col / sizeof col[0]);
    }
    if (n >= W) {
        for (int x = 0; x < W; x++) {
            int i0 = (int)((long long)x * n / W);
            int i1 = (int)((long long)(x + 1) * n / W);
            if (i1 <= i0) {
                i1 = i0 + 1;
            }
            if (i1 > n) {
                i1 = n;
            }
            /* Use max within the column to preserve narrow peaks */
            float s = -1e9f;
            for (int i = i0; i < i1; i++) {
                if (bins[i] > s) {
                    s = bins[i];
                }
            }
            col[x] = s;
        }
    } else {
        for (int x = 0; x < W; x++) {
            int src = (int)((long long)x * n / W);
            if (src < 0) {
                src = 0;
            }
            if (src >= n) {
                src = n - 1;
            }
            col[x] = bins[src];
        }
    }
    /* Auto-scale dB floor to 60 dB span around recent max */
    float vmax = -1e9f;
    for (int x = 0; x < W; x++) {
        if (col[x] > vmax) {
            vmax = col[x];
        }
    }
    float vmin = vmax - 60.0f;
    float span = (vmax - vmin);
    if (span < 1.0f) {
        span = 1.0f;
    }

    int use_unicode = (opts && opts->eye_unicode && ui_unicode_supported());
#ifdef PRETTY_COLORS
    const short C_GOOD = 11, C_MOD = 12, C_POOR = 13;
#endif
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float v = col[x];
            if (v < vmin) {
                v = vmin;
            }
            if (v > vmax) {
                v = vmax;
            }
            float t = (v - vmin) / span; /* 0..1 */
            int h = (int)lrint(t * (H - 1));
            int filled = (H - 1 - y) <= h;
#ifdef PRETTY_COLORS
            /* Color by relative height bands */
            short cp = (t < 0.33f) ? C_POOR : (t < 0.66f) ? C_MOD : C_GOOD;
            if (opts && opts->eye_color && has_colors()) {
                attron(COLOR_PAIR(cp));
            }
#endif
            if (filled) {
                if (use_unicode) {
                    addstr("█");
                } else {
                    addch('#');
                }
            } else {
                addch(' ');
            }
#ifdef PRETTY_COLORS
            if (opts && opts->eye_color && has_colors()) {
                attroff(COLOR_PAIR(cp));
            }
#endif
        }
        addch('\n');
    }
    /* Legend */
    float span_hz = (float)rate;
    int nfft2 = rtl_stream_spectrum_get_size();
    const char* df = use_unicode ? "\xCE\x94"
                                   "f"
                                 : "df";
    ui_print_lborder();
    printw(" Span: %.1f kHz  %s(FFT): %.1f Hz  %s(col): %.1f Hz  FFT: %d  "
           "Glyphs: %s%s\n",
           span_hz / 1000.0f, df, (rate > 0 && nfft2 > 0) ? (span_hz / (float)nfft2) : 0.0f, df,
           (rate > 0 && W > 0) ? (span_hz / (float)W) : 0.0f, nfft2, use_unicode ? "Unicode" : "ASCII",
           (opts && opts->eye_color && has_colors()) ? "; colored" : "");
    /* Frequency ticks around DC */
    ui_print_lborder();
    printw(" Freq: -%.1fk   0   +%.1fk\n", (span_hz * 0.5f) / 1000.0f, (span_hz * 0.5f) / 1000.0f);
    /* Amplitude scale relative to current peak */
    ui_print_lborder();
    printw(" Scale: top=%.1f dB  floor=%.1f dB (relative)\n", vmax, vmin);
#ifdef PRETTY_COLORS
    if (opts && opts->eye_color && has_colors()) {
        printw("| Color:   ");
        attron(COLOR_PAIR(C_POOR));
        if (use_unicode) {
            addstr("██");
        } else {
            addstr("##");
        }
        attroff(COLOR_PAIR(C_POOR));
        printw(" low  ");
        attron(COLOR_PAIR(C_MOD));
        if (use_unicode) {
            addstr("██");
        } else {
            addstr("##");
        }
        attroff(COLOR_PAIR(C_MOD));
        printw(" mid  ");
        attron(COLOR_PAIR(C_GOOD));
        if (use_unicode) {
            addstr("██");
        } else {
            addstr("##");
        }
        attroff(COLOR_PAIR(C_GOOD));
        printw(" high\n");
    }
#endif
    ui_print_hr();
#else
    ui_print_header("Spectrum Analyzer");
    printw("| (RTL disabled in this build)\n");
    ui_print_hr();
#endif
}
