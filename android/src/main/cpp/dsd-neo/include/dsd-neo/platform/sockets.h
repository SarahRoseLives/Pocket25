// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

/**
 * @file
 * @brief Cross-platform socket abstraction for DSD-neo.
 *
 * Provides a unified API for BSD sockets (POSIX) and Winsock2 (Windows).
 */

#include <dsd-neo/platform/platform.h>

#if DSD_PLATFORM_WIN_NATIVE
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET dsd_socket_t;
#define DSD_INVALID_SOCKET INVALID_SOCKET
#define DSD_SOCKET_ERROR   SOCKET_ERROR
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int dsd_socket_t;
#define DSD_INVALID_SOCKET (-1)
#define DSD_SOCKET_ERROR   (-1)
#endif

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize socket subsystem.
 *
 * Must be called before any socket operations on Windows.
 * No-op on POSIX systems.
 *
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_init(void);

/**
 * @brief Cleanup socket subsystem.
 *
 * Should be called at program termination on Windows.
 * No-op on POSIX systems.
 */
void dsd_socket_cleanup(void);

/**
 * @brief Create a socket.
 *
 * @param domain    Address family (AF_INET, AF_INET6).
 * @param type      Socket type (SOCK_STREAM, SOCK_DGRAM).
 * @param protocol  Protocol (IPPROTO_TCP, IPPROTO_UDP, 0).
 * @return Socket handle, or DSD_INVALID_SOCKET on error.
 */
dsd_socket_t dsd_socket_create(int domain, int type, int protocol);

/**
 * @brief Close a socket.
 *
 * @param sock      Socket to close.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_close(dsd_socket_t sock);

/**
 * @brief Bind socket to address.
 *
 * @param sock      Socket handle.
 * @param addr      Address structure.
 * @param addrlen   Size of address structure.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_bind(dsd_socket_t sock, const struct sockaddr* addr, int addrlen);

/**
 * @brief Listen for connections on a socket.
 *
 * @param sock      Socket handle.
 * @param backlog   Maximum length of pending connection queue.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_listen(dsd_socket_t sock, int backlog);

/**
 * @brief Accept a connection on a socket.
 *
 * @param sock      Socket handle.
 * @param addr      Address structure for client (output, may be NULL).
 * @param addrlen   Address length (in/out, may be NULL).
 * @return New socket handle, or DSD_INVALID_SOCKET on error.
 */
dsd_socket_t dsd_socket_accept(dsd_socket_t sock, struct sockaddr* addr, int* addrlen);

/**
 * @brief Connect socket to remote address.
 *
 * @param sock      Socket handle.
 * @param addr      Remote address.
 * @param addrlen   Size of address structure.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_connect(dsd_socket_t sock, const struct sockaddr* addr, int addrlen);

/**
 * @brief Send data on a connected socket.
 *
 * @param sock      Socket handle.
 * @param buf       Data buffer.
 * @param len       Buffer length.
 * @param flags     Send flags.
 * @return Number of bytes sent, or negative on error.
 */
int dsd_socket_send(dsd_socket_t sock, const void* buf, size_t len, int flags);

/**
 * @brief Send data to a specific address (UDP).
 *
 * @param sock      Socket handle.
 * @param buf       Data buffer.
 * @param len       Buffer length.
 * @param flags     Send flags.
 * @param dest_addr Destination address.
 * @param addrlen   Size of address structure.
 * @return Number of bytes sent, or negative on error.
 */
int dsd_socket_sendto(dsd_socket_t sock, const void* buf, size_t len, int flags, const struct sockaddr* dest_addr,
                      int addrlen);

/**
 * @brief Receive data from a connected socket.
 *
 * @param sock      Socket handle.
 * @param buf       Buffer for received data.
 * @param len       Buffer length.
 * @param flags     Receive flags.
 * @return Number of bytes received, 0 on connection closed, negative on error.
 */
int dsd_socket_recv(dsd_socket_t sock, void* buf, size_t len, int flags);

/**
 * @brief Receive data from any sender (UDP).
 *
 * @param sock      Socket handle.
 * @param buf       Buffer for received data.
 * @param len       Buffer length.
 * @param flags     Receive flags.
 * @param src_addr  Source address (output, may be NULL).
 * @param addrlen   Address length (in/out, may be NULL).
 * @return Number of bytes received, negative on error.
 */
int dsd_socket_recvfrom(dsd_socket_t sock, void* buf, size_t len, int flags, struct sockaddr* src_addr, int* addrlen);

/**
 * @brief Set socket option.
 *
 * @param sock      Socket handle.
 * @param level     Option level (SOL_SOCKET, IPPROTO_TCP, etc.).
 * @param optname   Option name.
 * @param optval    Option value.
 * @param optlen    Option value length.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_setsockopt(dsd_socket_t sock, int level, int optname, const void* optval, int optlen);

/**
 * @brief Get socket option.
 *
 * @param sock      Socket handle.
 * @param level     Option level (SOL_SOCKET, IPPROTO_TCP, etc.).
 * @param optname   Option name.
 * @param optval    Option value buffer.
 * @param optlen    Option value length (in/out).
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_getsockopt(dsd_socket_t sock, int level, int optname, void* optval, int* optlen);

/**
 * @brief Shutdown socket for reading, writing, or both.
 *
 * @param sock      Socket handle.
 * @param how       SHUT_RD, SHUT_WR, or SHUT_RDWR.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_shutdown(dsd_socket_t sock, int how);

/**
 * @brief Get last socket error code.
 *
 * @return Error code (errno on POSIX, WSAGetLastError on Windows).
 */
int dsd_socket_get_error(void);

/**
 * @brief Set socket to non-blocking mode.
 *
 * @param sock      Socket handle.
 * @param nonblock  1 for non-blocking, 0 for blocking.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_set_nonblocking(dsd_socket_t sock, int nonblock);

/**
 * @brief Set socket receive timeout.
 *
 * @param sock          Socket handle.
 * @param timeout_ms    Timeout in milliseconds (0 = no timeout).
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_set_recv_timeout(dsd_socket_t sock, unsigned int timeout_ms);

/**
 * @brief Set socket send timeout.
 *
 * @param sock          Socket handle.
 * @param timeout_ms    Timeout in milliseconds (0 = no timeout).
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_set_send_timeout(dsd_socket_t sock, unsigned int timeout_ms);

/**
 * @brief Resolve hostname to address.
 *
 * @param hostname  Hostname to resolve.
 * @param port      Port number.
 * @param addr      Output sockaddr_in structure.
 * @return 0 on success, non-zero on failure.
 */
int dsd_socket_resolve(const char* hostname, int port, struct sockaddr_in* addr);

/* Shutdown constants (may not be defined on Windows) */
#ifndef SHUT_RD
#define SHUT_RD   0
#define SHUT_WR   1
#define SHUT_RDWR 2
#endif

#ifdef __cplusplus
}
#endif
