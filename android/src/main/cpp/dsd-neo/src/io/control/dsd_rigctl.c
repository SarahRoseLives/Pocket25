// SPDX-License-Identifier: ISC
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */
/*-------------------------------------------------------------------------------
 * dsd_rigctl.c
 * Simple RIGCTL Client for DSD (remote control of GQRX, SDR++, etc)
 *
 * Portions from https://github.com/neural75/gqrx-scanner
 *
 * LWVMOBILE
 * 2022-10 DSD-FME Florida Man Edition
 *-----------------------------------------------------------------------------*/

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/io/control.h>
#include <dsd-neo/io/rigctl_client.h>
#include <dsd-neo/io/rtl_stream_c.h>
#include <dsd-neo/platform/sockets.h>
#include <dsd-neo/platform/timing.h>
#include <dsd-neo/runtime/config.h>
#include <dsd-neo/runtime/log.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BUFSIZE        1024
#define FREQ_MAX       4096
#define SAVED_FREQ_MAX 1000
#define TAG_MAX        100

/**
 * @brief Establish a TCP RIGCTL connection to the given host/port.
 *
 * Resolves the hostname, opens a TCP socket, and applies a short receive
 * timeout so control I/O cannot wedge the application.
 *
 * @param hostname Target host (IPv4/hostname).
 * @param portno Target port number.
 * @return Socket FD on success; 0 on resolution/connection failure.
 */
dsd_socket_t
Connect(char* hostname, int portno) {
    dsd_socket_t sockfd;
    struct sockaddr_in serveraddr;

    /* socket: create the socket */
    sockfd = dsd_socket_create(AF_INET, SOCK_STREAM, 0);
    if (sockfd == DSD_INVALID_SOCKET) {
        LOG_ERROR("ERROR opening socket\n");
        perror("ERROR opening socket");
        return DSD_INVALID_SOCKET;
    }

    /* Resolve hostname and build the server's Internet address */
    if (dsd_socket_resolve(hostname, portno, &serveraddr) != 0) {
        LOG_ERROR("ERROR, no such host as %s\n", hostname);
        dsd_socket_close(sockfd);
        return DSD_INVALID_SOCKET; //check on other end and configure pulse input
    }

    /* connect: create a connection with the server */
    if (dsd_socket_connect(sockfd, (const struct sockaddr*)&serveraddr, sizeof(serveraddr)) != 0) {
        LOG_ERROR("ERROR connecting socket\n");
        dsd_socket_close(sockfd);
        return DSD_INVALID_SOCKET;
    }

    /* Apply small receive timeout so control I/O can't wedge the app. Default 1500ms. */
    {
        int to_ms = 1500;
        const dsdneoRuntimeConfig* cfg = dsd_neo_get_config();
        if (!cfg) {
            dsd_neo_config_init(NULL);
            cfg = dsd_neo_get_config();
        }
        if (cfg) {
            to_ms = cfg->rigctl_rcvtimeo_ms;
            if (!cfg->rigctl_rcvtimeo_is_set && cfg->tcp_rcvtimeo_is_set) {
                to_ms = cfg->tcp_rcvtimeo_ms;
            }
        }
        (void)dsd_socket_set_recv_timeout(sockfd, (unsigned int)to_ms);
        int nodelay = 1;
        (void)dsd_socket_setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));
    }

    return sockfd;
}

/**
 * @brief Send a null-terminated RIGCTL command.
 *
 * Writes the buffer contents and treats short writes as errors.
 *
 * @param sockfd Connected socket FD.
 * @param buf Command buffer (null-terminated).
 * @return true on success; false on error.
 */
bool
Send(dsd_socket_t sockfd, char* buf) {
    int n;

    n = dsd_socket_send(sockfd, buf, strlen(buf), 0);
    if (n < 0) {
        // Non-fatal: allow control plane hiccups without exiting
        return false;
    }
    return true;
}

/**
 * @brief Receive a RIGCTL response into the provided buffer.
 *
 * Reads up to BUFSIZE bytes; on timeout/error, zeroes the buffer and returns
 * false.
 *
 * @param sockfd Connected socket FD.
 * @param buf Buffer to fill (must be at least BUFSIZE+1 bytes).
 * @return true on success; false on timeout/error.
 */
bool
Recv(dsd_socket_t sockfd, char* buf) {
    int n;

    n = dsd_socket_recv(sockfd, buf, BUFSIZE, 0);
    if (n <= 0) {
        // Timeout or error: treat as soft failure so callers can continue
        if (buf) {
            buf[0] = '\0';
        }
        return false;
    }
    buf[n] = '\0';
    return true;
}

/**
 * @brief Query current tuned frequency via RIGCTL.
 *
 * Issues the "f" command and parses the returned frequency in Hz.
 *
 * @param sockfd Connected RIGCTL socket.
 * @return Current frequency in Hz; 0 on error/unknown.
 */
long int
GetCurrentFreq(dsd_socket_t sockfd) {
    long int freq = 0;
    char buf[BUFSIZE];
    char* ptr;
    char* token;

    Send(sockfd, "f\n");
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0) {
        return freq;
    }

    token = strtok(buf, "\n");
    freq = strtol(token, &ptr, 10);
    // fprintf (stderr, "\nRIGCTL VFO Freq: [%ld]\n", freq);
    return freq;
}

/**
 * @brief Set center frequency on the connected RIGCTL peer.
 *
 * Caches the last request per socket to avoid redundant I/O.
 *
 * @param sockfd Connected RIGCTL socket.
 * @param freq Desired frequency in Hz.
 * @return true on success; false on failure.
 */
bool
SetFreq(dsd_socket_t sockfd, long int freq) {
    static dsd_socket_t s_last_sockfd = DSD_INVALID_SOCKET;
    static long int s_last_freq = LONG_MIN;
    if (sockfd == s_last_sockfd && freq == s_last_freq) {
        return true; // no change; skip I/O
    }
    char buf[BUFSIZE];

    snprintf(buf, sizeof buf, "F %ld\n", freq);
    Send(sockfd, buf);
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1\n") == 0) { //sdr++ has a linebreak here, is that in all versions of the protocol?
        return false;
    }

    s_last_sockfd = sockfd;
    s_last_freq = freq;
    return true;
}

/**
 * @brief Set modulation/bandwidth on the RIGCTL peer.
 *
 * Sends both the SDR++-specific "NFM" token and the generic "FM" token as a
 * fallback. Requests are cached to skip redundant updates.
 *
 * @param sockfd Connected RIGCTL socket.
 * @param bandwidth Target bandwidth in Hz.
 * @return true on success; false on failure.
 */
bool
SetModulation(dsd_socket_t sockfd, int bandwidth) {
    static dsd_socket_t s_last_sockfd = DSD_INVALID_SOCKET;
    static int s_last_bw = INT_MIN;
    if (sockfd == s_last_sockfd && bandwidth == s_last_bw) {
        return true; // unchanged
    }
    char buf[BUFSIZE];
    //the bandwidth is now a user/system based configurable variable
    snprintf(
        buf, sizeof buf, "M NFM %d\n",
        bandwidth); //SDR++ has changed the token from FM to NFM, even if Ryzerth fixes it later, users may still have an older version
    Send(sockfd, buf);
    Recv(sockfd, buf);

    //if it fails the first time, send the other token instead
    if (strcmp(buf, "RPRT 1\n") == 0) //sdr++ has a linebreak here, is that in all versions of the protocol?
    {
        snprintf(buf, sizeof buf, "M FM %d\n", bandwidth); //anything not SDR++
        Send(sockfd, buf);
        Recv(sockfd, buf);
    }

    if (strcmp(buf, "RPRT 1\n") == 0) {
        return false;
    }

    s_last_sockfd = sockfd;
    s_last_bw = bandwidth;
    return true;
}

/**
 * @brief Read current signal level from the peer.
 *
 * Issues the "l" command and parses the reported dB level with one decimal
 * place of precision.
 *
 * @param sockfd Connected RIGCTL socket.
 * @param dB [out] Parsed signal level.
 * @return true on success; false on error or zero reading.
 */
bool
GetSignalLevel(dsd_socket_t sockfd, double* dB) {
    char buf[BUFSIZE];

    Send(sockfd, "l\n");
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0) {
        return false;
    }

    sscanf(buf, "%lf", dB);
    *dB = round((*dB) * 10) / 10;

    if (*dB == 0.0) {
        return false;
    }
    return true;
}

/**
 * @brief Query squelch level in dB from the peer.
 * @param sockfd Connected RIGCTL socket.
 * @param dB [out] Parsed squelch level.
 * @return true on success; false on error.
 */
bool
GetSquelchLevel(dsd_socket_t sockfd, double* dB) {
    char buf[BUFSIZE];

    Send(sockfd, "l SQL\n");
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0) {
        return false;
    }

    sscanf(buf, "%lf", dB);
    *dB = round((*dB) * 10) / 10;

    return true;
}

/**
 * @brief Set squelch level on the peer in dB.
 * @param sockfd Connected RIGCTL socket.
 * @param dB Desired squelch level.
 * @return true on success; false on failure.
 */
bool
SetSquelchLevel(dsd_socket_t sockfd, double dB) {
    char buf[BUFSIZE];

    snprintf(buf, sizeof buf, "L SQL %f\n", dB);
    Send(sockfd, buf);
    Recv(sockfd, buf);

    if (strcmp(buf, "RPRT 1") == 0) {
        return false;
    }

    return true;
}

//
// GetSignalLevelEx
// Get a bunch of sample with some delay and calculate the mean value
//
/**
 * @brief Average multiple signal level samples with a short delay between reads.
 *
 * Intended to smooth noisy signal reports when deciding on squelch or tuning
 * actions.
 *
 * @param sockfd Connected RIGCTL socket.
 * @param dB [out] Averaged signal level.
 * @param n_samp Number of samples to average.
 * @return true when sampling completed (errors are tolerated in the average).
 */
bool
GetSignalLevelEx(dsd_socket_t sockfd, double* dB, int n_samp) {
    double temp_level;
    *dB = 0;
    int errors = 0;
    for (int i = 0; i < n_samp; i++) {
        if (GetSignalLevel(sockfd, &temp_level)) {
            *dB = *dB + temp_level;
        } else {
            errors++;
        }
        dsd_sleep_ms(1);
    }
    *dB = *dB / (n_samp - errors);
    return true;
}

//going to leave this function available, even if completely switched over to rtl_dev_tune now, may be useful in the future
/**
 * @brief Tune RTL devices via legacy UDP command flow.
 *
 * Writes a 5-byte tuning command to the configured RTL UDP port on localhost.
 * Caches the last frequency to avoid redundant transmissions.
 *
 * @param opts Decoder options (supplies UDP port and caches freq).
 * @param state Decoder state (unused).
 * @param frequency Desired center frequency in Hz.
 */
void
rtl_udp_tune(dsd_opts* opts, dsd_state* state, long int frequency) {
    UNUSED(state);
    static long int s_last_udp_freq = LONG_MIN;
    if (frequency == s_last_udp_freq) {
        return; // unchanged
    }
    dsd_socket_t handle;
    unsigned short udp_port = opts->rtl_udp_port;
    char data[5] = {0}; //data buffer size is 5 for UDP frequency tuning
    struct sockaddr_in tune_addr;

    uint32_t new_freq = (uint32_t)frequency;
    opts->rtlsdr_center_freq = new_freq; //for ncurses terminal display after rtl is started up

    handle = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (handle == DSD_INVALID_SOCKET) {
        return; // failed to create socket
    }

    data[0] = 0;
    data[1] = (char)(new_freq & 0xFF);
    data[2] = (char)((new_freq >> 8) & 0xFF);
    data[3] = (char)((new_freq >> 16) & 0xFF);
    data[4] = (char)((new_freq >> 24) & 0xFF);

    memset((char*)&tune_addr, 0, sizeof(tune_addr));
    tune_addr.sin_family = AF_INET;
    dsd_socket_resolve("127.0.0.1", udp_port, &tune_addr); //make user configurable later
    (void)dsd_socket_sendto(handle, data, 5, 0, (const struct sockaddr*)&tune_addr, sizeof(struct sockaddr_in));

    dsd_socket_close(handle); //close socket after sending.
    s_last_udp_freq = frequency;
}

/**
 * @brief Set tuner frequency via io/control API (simple tune without trunking bookkeeping).
 *
 * This is the canonical way for UI and non-trunking code to request frequency changes.
 * It handles both RTL-SDR and rigctl backends but does NOT update trunking state fields
 * or perform modulation resets. For trunking voice/CC tuning, use
 * dsd_trunk_tuning_hook_tune_to_freq() or dsd_trunk_tuning_hook_tune_to_cc() instead.
 *
 * @param opts Decoder options with tuning configuration.
 * @param state Decoder state (required for RTL tuning, may be NULL for rigctl-only).
 * @param freq Target frequency in Hz.
 * @return 0 on success, -1 on error.
 */
int
io_control_set_freq(dsd_opts* opts, dsd_state* state, long int freq) {
    if (!opts || freq <= 0) {
        return -1;
    }

    LOG_INFO("io_control: tune to %ld Hz\n", freq);

    // Update cached frequency for display/tracking
    opts->rtlsdr_center_freq = (uint32_t)freq;

    if (opts->use_rigctl == 1) {
        if (opts->setmod_bw != 0) {
            SetModulation(opts->rigctl_sockfd, opts->setmod_bw);
        }
        SetFreq(opts->rigctl_sockfd, freq);
    } else if (opts->audio_in_type == AUDIO_IN_RTL) {
#ifdef USE_RTLSDR
        if (state && state->rtl_ctx) {
            rtl_stream_tune(state->rtl_ctx, (uint32_t)freq);
        }
#endif
    }
    return 0;
}
