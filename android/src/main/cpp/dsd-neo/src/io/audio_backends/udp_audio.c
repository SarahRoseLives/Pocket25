// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/io/m17_udp.h>
#include <dsd-neo/io/udp_audio.h>
#include <dsd-neo/io/udp_socket_connect.h>

#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/platform/sockets.h>

#include <stdio.h>
#include <string.h>

static struct sockaddr_in address;
static struct sockaddr_in addressA;

void
udp_socket_blaster(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    UNUSED(state);
    int err = 0;

    //listen with:

    //short 8k/2
    //socat stdio udp-listen:23456 | play --buffer 640 -q -b 16 -r 8000 -c2 -t s16 -

    //short 8k/1
    //socat stdio udp-listen:23456 | play --buffer 320 -q -b 16 -r 8000 -c1 -t s16 -

    //float 8k/2
    //socat stdio udp-listen:23456 | play --buffer 1280 -q -e float -b 32 -r 8000 -c2 -t f32 -

    //float 8k/1
    //socat stdio udp-listen:23456 | play --buffer 640 -q -e float -b 32 -r 8000 -c1 -t f32 -

    //send audio or data to socket
    err = dsd_socket_sendto(opts->udp_sockfd, data, nsam, 0, (const struct sockaddr*)&address,
                            sizeof(struct sockaddr_in));
    if (err < 0) {
        fprintf(stderr, "\n UDP SENDTO ERR %d", err);
    }
    if (err >= 0 && (size_t)err < nsam) {
        fprintf(stderr, "\n UDP Underflow %d", err); //I'm not even sure if this is possible
    }
}

int
m17_socket_receiver(dsd_opts* opts, void* data) {
    int err = 0;
    int len = sizeof(address);

    //receive data from socket
    err = dsd_socket_recvfrom(opts->udp_sockfd, data, 1000, 0, (struct sockaddr*)&address,
                              &len); //was MSG_WAITALL, but that seems to be = 256

    return err;
}

//Analog UDP port on +2 of normal open socket
void
udp_socket_blasterA(dsd_opts* opts, dsd_state* state, size_t nsam, void* data) {
    UNUSED(state);
    int err = 0;

    //listen with:

    //short 48k/1
    //socat stdio udp-listen:23456 | play --buffer 1920 -q -b 16 -r 48000 -c1 -t s16 -

    //send audio or data to socket
    err = dsd_socket_sendto(opts->udp_sockfdA, data, nsam, 0, (const struct sockaddr*)&addressA,
                            sizeof(struct sockaddr_in));
    if (err < 0) {
        fprintf(stderr, "\n UDP SENDTO ERR %d",
                err); // return value here is number of bytes sent, or -1 for failure
    }
    if (err >= 0 && (size_t)err < nsam) {
        fprintf(stderr, "\n UDP Underflow %d", err); //I'm not even sure if this is possible
    }
}

int
udp_socket_connect(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    int err = 0;
    opts->udp_sockfd = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (opts->udp_sockfd == DSD_INVALID_SOCKET) {
        fprintf(stderr, " UDP Socket Error\n");
        return -1;
    }

    // Don't think this is needed, but doesn't seem to hurt to keep it here either
    int broadcastEnable = 1;
    err = dsd_socket_setsockopt(opts->udp_sockfd, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    if (err != 0) {
        fprintf(stderr, " UDP Broadcast Set Error %d\n", err);
        return err;
    }

    memset((char*)&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    if (dsd_socket_resolve(opts->udp_hostname, opts->udp_portno, &address) != 0) {
        fprintf(stderr, " UDP address resolve error for %s\n", opts->udp_hostname);
        return -1;
    }

    return 0;
}

int
udp_socket_connectA(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    int err = 0;
    opts->udp_sockfdA = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (opts->udp_sockfdA == DSD_INVALID_SOCKET) {
        fprintf(stderr, " UDP Socket Error\n");
        return -1;
    }

    // Don't think this is needed, but doesn't seem to hurt to keep it here either
    int broadcastEnable = 1;
    err = dsd_socket_setsockopt(opts->udp_sockfdA, SOL_SOCKET, SO_BROADCAST, &broadcastEnable, sizeof(broadcastEnable));
    if (err != 0) {
        fprintf(stderr, " UDP Broadcast Set Error %d\n", err);
        return err;
    }

    memset((char*)&addressA, 0, sizeof(addressA));
    addressA.sin_family = AF_INET;
    //plus 2 to current port assignment for the analog port value
    if (dsd_socket_resolve(opts->udp_hostname, opts->udp_portno + 2, &addressA) != 0) {
        fprintf(stderr, " UDP address resolve error for %s\n", opts->udp_hostname);
        return -1;
    }

    return 0;
}
