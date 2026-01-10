// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/platform/sockets.h>

#if DSD_PLATFORM_WIN_NATIVE

#include <errno.h>

/* Winsock initialization tracking */
static int s_wsa_initialized = 0;

int
dsd_socket_init(void) {
    if (s_wsa_initialized) {
        return 0;
    }

    WSADATA wsa;
    int result = WSAStartup(MAKEWORD(2, 2), &wsa);
    if (result != 0) {
        return result;
    }
    s_wsa_initialized = 1;
    return 0;
}

void
dsd_socket_cleanup(void) {
    if (s_wsa_initialized) {
        WSACleanup();
        s_wsa_initialized = 0;
    }
}

dsd_socket_t
dsd_socket_create(int domain, int type, int protocol) {
    /* Ensure Winsock is initialized */
    if (!s_wsa_initialized) {
        if (dsd_socket_init() != 0) {
            return DSD_INVALID_SOCKET;
        }
    }
    return socket(domain, type, protocol);
}

int
dsd_socket_close(dsd_socket_t sock) {
    return closesocket(sock);
}

int
dsd_socket_bind(dsd_socket_t sock, const struct sockaddr* addr, int addrlen) {
    return bind(sock, addr, addrlen);
}

int
dsd_socket_listen(dsd_socket_t sock, int backlog) {
    return listen(sock, backlog);
}

dsd_socket_t
dsd_socket_accept(dsd_socket_t sock, struct sockaddr* addr, int* addrlen) {
    return accept(sock, addr, addrlen);
}

int
dsd_socket_connect(dsd_socket_t sock, const struct sockaddr* addr, int addrlen) {
    return connect(sock, addr, addrlen);
}

int
dsd_socket_send(dsd_socket_t sock, const void* buf, size_t len, int flags) {
    return send(sock, (const char*)buf, (int)len, flags);
}

int
dsd_socket_sendto(dsd_socket_t sock, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr,
                  int addrlen) {
    return sendto(sock, (const char*)buf, (int)len, flags, dest_addr, addrlen);
}

int
dsd_socket_recv(dsd_socket_t sock, void* buf, size_t len, int flags) {
    return recv(sock, (char*)buf, (int)len, flags);
}

int
dsd_socket_recvfrom(dsd_socket_t sock, void* buf, size_t len, int flags, struct sockaddr* src_addr, int* addrlen) {
    return recvfrom(sock, (char*)buf, (int)len, flags, src_addr, addrlen);
}

int
dsd_socket_setsockopt(dsd_socket_t sock, int level, int optname, const void* optval, int optlen) {
    return setsockopt(sock, level, optname, (const char*)optval, optlen);
}

int
dsd_socket_getsockopt(dsd_socket_t sock, int level, int optname, void* optval, int* optlen) {
    if (!optlen) {
        WSASetLastError(WSAEINVAL);
        return DSD_SOCKET_ERROR;
    }
    return getsockopt(sock, level, optname, (char*)optval, optlen);
}

int
dsd_socket_shutdown(dsd_socket_t sock, int how) {
    /* Map POSIX constants to Windows constants */
    int win_how;
    switch (how) {
        case SHUT_RD: win_how = SD_RECEIVE; break;
        case SHUT_WR: win_how = SD_SEND; break;
        case SHUT_RDWR: win_how = SD_BOTH; break;
        default: return -1;
    }
    return shutdown(sock, win_how);
}

int
dsd_socket_get_error(void) {
    return WSAGetLastError();
}

int
dsd_socket_set_nonblocking(dsd_socket_t sock, int nonblock) {
    u_long mode = nonblock ? 1 : 0;
    return ioctlsocket(sock, FIONBIO, &mode);
}

int
dsd_socket_set_recv_timeout(dsd_socket_t sock, unsigned int timeout_ms) {
    /* Windows uses DWORD (milliseconds) for socket timeouts */
    DWORD tv = timeout_ms;
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
}

int
dsd_socket_set_send_timeout(dsd_socket_t sock, unsigned int timeout_ms) {
    /* Windows uses DWORD (milliseconds) for socket timeouts */
    DWORD tv = timeout_ms;
    return setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, (const char*)&tv, sizeof(tv));
}

int
dsd_socket_resolve(const char* hostname, int port, struct sockaddr_in* addr) {
    if (!hostname || !addr) {
        return -1;
    }

    /* Ensure Winsock is initialized */
    if (!s_wsa_initialized) {
        if (dsd_socket_init() != 0) {
            return -1;
        }
    }

    memset(addr, 0, sizeof(*addr));
    addr->sin_family = AF_INET;
    addr->sin_port = htons((u_short)port);

    /* Try numeric address first */
    addr->sin_addr.s_addr = inet_addr(hostname);
    if (addr->sin_addr.s_addr != INADDR_NONE) {
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

#endif /* DSD_PLATFORM_WIN_NATIVE */
