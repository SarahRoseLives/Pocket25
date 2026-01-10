// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/sockets.h>

#if !DSD_PLATFORM_WIN_NATIVE

#include <errno.h>
#include <string.h>
#include <sys/time.h>

int
dsd_socket_init(void) {
    /* No-op on POSIX */
    return 0;
}

void
dsd_socket_cleanup(void) {
    /* No-op on POSIX */
}

dsd_socket_t
dsd_socket_create(int domain, int type, int protocol) {
    return socket(domain, type, protocol);
}

int
dsd_socket_close(dsd_socket_t sock) {
    return close(sock);
}

int
dsd_socket_bind(dsd_socket_t sock, const struct sockaddr* addr, int addrlen) {
    return bind(sock, addr, (socklen_t)addrlen);
}

int
dsd_socket_listen(dsd_socket_t sock, int backlog) {
    return listen(sock, backlog);
}

dsd_socket_t
dsd_socket_accept(dsd_socket_t sock, struct sockaddr* addr, int* addrlen) {
    socklen_t slen = addrlen ? (socklen_t)*addrlen : 0;
    dsd_socket_t result = accept(sock, addr, addrlen ? &slen : NULL);
    if (addrlen) {
        *addrlen = (int)slen;
    }
    return result;
}

int
dsd_socket_connect(dsd_socket_t sock, const struct sockaddr* addr, int addrlen) {
    return connect(sock, addr, (socklen_t)addrlen);
}

int
dsd_socket_send(dsd_socket_t sock, const void* buf, size_t len, int flags) {
    return (int)send(sock, buf, len, flags);
}

int
dsd_socket_sendto(dsd_socket_t sock, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr,
                  int addrlen) {
    return (int)sendto(sock, buf, len, flags, dest_addr, (socklen_t)addrlen);
}

int
dsd_socket_recv(dsd_socket_t sock, void* buf, size_t len, int flags) {
    return (int)recv(sock, buf, len, flags);
}

int
dsd_socket_recvfrom(dsd_socket_t sock, void* buf, size_t len, int flags, struct sockaddr* src_addr, int* addrlen) {
    socklen_t slen = addrlen ? (socklen_t)*addrlen : 0;
    int result = (int)recvfrom(sock, buf, len, flags, src_addr, addrlen ? &slen : NULL);
    if (addrlen) {
        *addrlen = (int)slen;
    }
    return result;
}

int
dsd_socket_setsockopt(dsd_socket_t sock, int level, int optname, const void* optval, int optlen) {
    return setsockopt(sock, level, optname, optval, (socklen_t)optlen);
}

int
dsd_socket_getsockopt(dsd_socket_t sock, int level, int optname, void* optval, int* optlen) {
    if (!optlen) {
        errno = EINVAL;
        return -1;
    }

    socklen_t slen = (socklen_t)*optlen;
    int result = getsockopt(sock, level, optname, optval, &slen);
    if (result == 0) {
        *optlen = (int)slen;
    }
    return result;
}

int
dsd_socket_shutdown(dsd_socket_t sock, int how) {
    return shutdown(sock, how);
}

int
dsd_socket_get_error(void) {
    return errno;
}

int
dsd_socket_set_nonblocking(dsd_socket_t sock, int nonblock) {
    int flags = fcntl(sock, F_GETFL, 0);
    if (flags < 0) {
        return -1;
    }
    if (nonblock) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }
    return fcntl(sock, F_SETFL, flags);
}

int
dsd_socket_set_recv_timeout(dsd_socket_t sock, unsigned int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (suseconds_t)(timeout_ms % 1000U) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
}

int
dsd_socket_set_send_timeout(dsd_socket_t sock, unsigned int timeout_ms) {
    struct timeval tv;
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (suseconds_t)(timeout_ms % 1000U) * 1000;
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
}

int
dsd_socket_resolve(const char* hostname, int port, struct sockaddr_in* addr) {
    if (!hostname || !addr) {
        return -1;
    }

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((uint16_t)port);

    /* Try numeric address first */
    if (inet_pton(AF_INET, hostname, &addr->sin_addr) == 1) {
        return 0;
    }

    /* Fall back to DNS lookup */
    struct hostent* he = gethostbyname(hostname);
    if (!he || !he->h_addr_list[0]) {
        return -1;
    }

    memcpy(&addr->sin_addr, he->h_addr_list[0], sizeof(addr->sin_addr));
    return 0;
}

#endif /* !DSD_PLATFORM_WIN_NATIVE */
