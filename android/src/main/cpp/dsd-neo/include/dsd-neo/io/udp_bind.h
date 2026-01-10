// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#pragma once

#include <dsd-neo/platform/sockets.h>

#ifdef __cplusplus
extern "C" {
#endif

dsd_socket_t UDPBind(char* hostname, int portno);

#ifdef __cplusplus
}
#endif
