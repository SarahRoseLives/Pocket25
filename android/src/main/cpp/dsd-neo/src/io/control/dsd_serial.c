// SPDX-License-Identifier: ISC
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/platform/platform.h>
#include <dsd-neo/runtime/log.h>

#include <stdio.h>
#include <stdlib.h>

#if DSD_PLATFORM_WIN_NATIVE
/* Windows: Serial port support is stubbed for now. */

/**
 * @brief Open and configure the outbound serial port used for radio control.
 *
 * Windows stub - serial port support not yet implemented.
 *
 * @param opts Decoder options containing serial configuration.
 * @param state Decoder state (unused).
 */
void
openSerial(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);
    LOG_ERROR("Serial port control is not yet supported on Windows.\n");
    LOG_ERROR("Requested port: %s, baud: %d\n", opts->serial_dev, opts->serial_baud);
    opts->serial_fd = -1;
}

/**
 * @brief Resume scanning on the attached serial-controlled receiver.
 *
 * Windows stub - serial port support not yet implemented.
 *
 * @param opts Decoder options containing serial FD.
 * @param state Decoder state to update.
 */
void
resumeScan(dsd_opts* opts, dsd_state* state) {
    UNUSED(opts);
    state->numtdulc = 0;
}

#else /* POSIX */

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

/**
 * @brief Open and configure the outbound serial port used for radio control.
 *
 * Applies baud rate and 8N1 framing and stores the resulting file descriptor
 * in `opts->serial_fd`. On failure to open the port, logs and returns with
 * `opts->serial_fd` set to -1.
 *
 * @param opts Decoder options containing serial configuration.
 * @param state Decoder state (unused).
 */
void
openSerial(dsd_opts* opts, dsd_state* state) {
    UNUSED(state);

    struct termios tty;
    speed_t baud;

    fprintf(stderr, "Opening serial port %s and setting baud to %i\n", opts->serial_dev, opts->serial_baud);
    opts->serial_fd = -1;
    int fd = open(opts->serial_dev, O_WRONLY);
    if (fd == -1) {
        LOG_ERROR("Error, couldn't open %s\n", opts->serial_dev);
        return;
    }
    opts->serial_fd = fd;

    tty.c_cflag = 0;

    baud = B115200;
    switch (opts->serial_baud) {
        case 1200: baud = B1200; break;
        case 2400: baud = B2400; break;
        case 4800: baud = B4800; break;
        case 9600: baud = B9600; break;
        case 19200: baud = B19200; break;
        case 38400: baud = B38400; break;
        case 57600: baud = B57600; break;
        case 115200: baud = B115200; break;
        case 230400: baud = B230400; break;
        default:
            if (opts->serial_baud > 0) {
                LOG_WARN("Unsupported baud rate %d; defaulting to 115200", opts->serial_baud);
            }
            break;
    }
    if (opts->serial_baud > 0) {
        cfsetospeed(&tty, baud);
        cfsetispeed(&tty, baud);
    }

    tty.c_cflag |= (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag = IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cc[VMIN] = 1;
    tty.c_cc[VTIME] = 5;

    tcsetattr(opts->serial_fd, TCSANOW, &tty);
}

/**
 * @brief Resume scanning on the attached serial-controlled receiver.
 *
 * Issues the device-specific command sequence when the serial FD is valid and
 * resets TDMA link counters in the decoder state.
 *
 * @param opts Decoder options containing serial FD.
 * @param state Decoder state to update.
 */
void
resumeScan(dsd_opts* opts, dsd_state* state) {

    char cmd[16];

    if (opts->serial_fd > 0) {
        snprintf(cmd, sizeof cmd, "\rKEY00\r");
        ssize_t written = write(opts->serial_fd, cmd, 7);
        if (written != 7) {
            LOG_WARN("resumeScan: sent %zd/7 bytes on serial FD", written);
        }
        cmd[0] = 2;
        cmd[1] = 75;
        cmd[2] = 15;
        cmd[3] = 3;
        cmd[4] = 93;
        cmd[5] = 0;
        written = write(opts->serial_fd, cmd, 5);
        if (written != 5) {
            LOG_WARN("resumeScan: sent %zd/5 bytes on serial FD", written);
        }
        state->numtdulc = 0;
    }
}

#endif /* DSD_PLATFORM_WIN_NATIVE */
