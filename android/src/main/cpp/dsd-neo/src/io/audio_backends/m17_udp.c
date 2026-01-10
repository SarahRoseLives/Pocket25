// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/io/m17_udp.h>

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/platform/sockets.h>

#include <stdio.h>
#include <string.h>

static struct sockaddr_in addressM17;

int
udp_socket_connectM17(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    int err = 0;
    opts->m17_udp_sock = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (opts->m17_udp_sock == DSD_INVALID_SOCKET) {
        fprintf(stderr, " UDP Socket Error\n");
        return -1;
    }

    // Don't think this is needed, but doesn't seem to hurt to keep it here either
    int broadcastEnable = 1;
    err =
        dsd_socket_setsockopt(opts->m17_udp_sock, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    if (err != 0) {
        fprintf(stderr, " UDP Broadcast Set Error %d\n", err);
        return err;
    }

    memset((char*)&addressM17, 0, sizeof(addressM17));
    addressM17.sin_family = AF_INET;
    if (dsd_socket_resolve(opts->m17_hostname, opts->m17_portno, &addressM17) != 0) {
        fprintf(stderr, " UDP address resolve error for %s\n", opts->m17_hostname);
        return -1;
    }

    return 0;
}

int
m17_socket_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    UNUSED(state);
    int err = 0;

    //See notes in m17.c on line ~3395 regarding usage

    //send audio or data to socket
    err = dsd_socket_sendto(opts->m17_udp_sock, data, nsam, 0, (const struct sockaddr*)&addressM17,
                            sizeof(struct sockaddr_in));
    //RETURN Value should be ACKN or NACK, or PING, or PONG

    return (err);
}
