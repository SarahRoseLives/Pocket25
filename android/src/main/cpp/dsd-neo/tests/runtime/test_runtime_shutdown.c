// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2025 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

#include <dsd-neo/core/cleanup.h>
#include <dsd-neo/runtime/exitflag.h>
#include <dsd-neo/runtime/shutdown.h>

#include <stdint.h>

#if !defined(_WIN32)
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

int
main(void) {
    exitflag = 0;
    dsd_request_shutdown(NULL, NULL);
    if (exitflag != 1) {
        return 1;
    }

    exitflag = 0;

#if defined(_WIN32)
    cleanupAndExit(NULL, NULL);
    if (exitflag != 1) {
        return 2;
    }
    return 0;
#else
    int pipe_fds[2] = {-1, -1};
    if (pipe(pipe_fds) != 0) {
        return 3;
    }

    pid_t pid = fork();
    if (pid < 0) {
        (void)close(pipe_fds[0]);
        (void)close(pipe_fds[1]);
        return 4;
    }

    if (pid == 0) {
        (void)close(pipe_fds[0]);

        uint8_t marker = 0xA5;
        (void)write(pipe_fds[1], &marker, sizeof(marker));

        cleanupAndExit(NULL, NULL);

        uint8_t flag = exitflag;
        (void)write(pipe_fds[1], &flag, sizeof(flag));

        marker = 0x5A;
        (void)write(pipe_fds[1], &marker, sizeof(marker));

        (void)close(pipe_fds[1]);
        _exit(0);
    }

    (void)close(pipe_fds[1]);

    uint8_t got[3] = {0, 0, 0};
    size_t total = 0;
    while (total < sizeof(got)) {
        ssize_t r = read(pipe_fds[0], got + total, sizeof(got) - total);
        if (r <= 0) {
            break;
        }
        total += (size_t)r;
    }
    (void)close(pipe_fds[0]);
    if (total != sizeof(got) || got[0] != 0xA5 || got[1] != 1 || got[2] != 0x5A) {
        return 5;
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        return 6;
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return 7;
    }

    return 0;
#endif
}
