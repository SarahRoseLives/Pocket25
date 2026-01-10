// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/io/udp_bind.h>

#include <dsd-neo/core/constants.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

dsd_socket_t
UDPBind(char* hostname, int portno) {
    UNUSED(hostname);

    dsd_socket_t sockfd;
    struct sockaddr_in serveraddr;

    /* socket: create the socket */
    //UDP socket
    sockfd = dsd_socket_create(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

    if (sockfd == DSD_INVALID_SOCKET) {
        fprintf(stderr, "ERROR opening UDP socket\n");
        perror("ERROR opening UDP socket");
        return DSD_INVALID_SOCKET;
    }

    /* build the server's Internet address */
    memset((char*)&serveraddr, 0, sizeof(serveraddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = INADDR_ANY; //INADDR_ANY
    serveraddr.sin_port = htons((uint16_t)portno);

    //Bind socket to listening
    if (dsd_socket_bind(sockfd, (struct sockaddr*)&serveraddr, sizeof(serveraddr)) != 0) {
        perror("ERROR on binding UDP Port");
    }

    //set these for non blocking when no samples to read (very short timeout)
    dsd_socket_set_recv_timeout(sockfd, 1); // 1ms timeout

    return sockfd;
}
