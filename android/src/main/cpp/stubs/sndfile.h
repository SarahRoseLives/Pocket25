// Stub sndfile.h for Android NDK builds
// libsndfile is not available on Android - this provides stub declarations

#ifndef SNDFILE_STUB_H
#define SNDFILE_STUB_H

#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

// Types
typedef struct sf_private_tag SNDFILE;

typedef int64_t sf_count_t;

typedef struct SF_INFO {
    sf_count_t frames;
    int samplerate;
    int channels;
    int format;
    int sections;
    int seekable;
} SF_INFO;

// Format constants
#define SF_FORMAT_WAV       0x010000
#define SF_FORMAT_RAW       0x040000
#define SF_FORMAT_PCM_16    0x0002
#define SF_FORMAT_PCM_S8    0x0001
#define SF_FORMAT_PCM_24    0x0003
#define SF_FORMAT_PCM_32    0x0004
#define SF_FORMAT_FLOAT     0x0006
#define SF_ENDIAN_LITTLE    0x10000000
#define SF_ENDIAN_BIG       0x20000000

// Mode constants
#define SFM_READ    0x10
#define SFM_WRITE   0x20
#define SFM_RDWR    0x30

// Stub functions - all return failure/no-op
static inline SNDFILE* sf_open(const char *path, int mode, SF_INFO *sfinfo) {
    (void)path; (void)mode; (void)sfinfo;
    return NULL;
}

static inline SNDFILE* sf_open_fd(int fd, int mode, SF_INFO *sfinfo, int close_desc) {
    (void)fd; (void)mode; (void)sfinfo; (void)close_desc;
    return NULL;
}

static inline int sf_close(SNDFILE *sndfile) {
    (void)sndfile;
    return 0;
}

static inline sf_count_t sf_read_short(SNDFILE *sndfile, short *ptr, sf_count_t items) {
    (void)sndfile; (void)ptr; (void)items;
    return 0;
}

static inline sf_count_t sf_write_short(SNDFILE *sndfile, const short *ptr, sf_count_t items) {
    (void)sndfile; (void)ptr; (void)items;
    return 0;
}

static inline sf_count_t sf_read_float(SNDFILE *sndfile, float *ptr, sf_count_t items) {
    (void)sndfile; (void)ptr; (void)items;
    return 0;
}

static inline sf_count_t sf_write_float(SNDFILE *sndfile, const float *ptr, sf_count_t items) {
    (void)sndfile; (void)ptr; (void)items;
    return 0;
}

static inline sf_count_t sf_readf_short(SNDFILE *sndfile, short *ptr, sf_count_t frames) {
    (void)sndfile; (void)ptr; (void)frames;
    return 0;
}

static inline sf_count_t sf_writef_short(SNDFILE *sndfile, const short *ptr, sf_count_t frames) {
    (void)sndfile; (void)ptr; (void)frames;
    return 0;
}

static inline sf_count_t sf_seek(SNDFILE *sndfile, sf_count_t frames, int whence) {
    (void)sndfile; (void)frames; (void)whence;
    return -1;
}

static inline const char* sf_strerror(SNDFILE *sndfile) {
    (void)sndfile;
    return "libsndfile not available on Android";
}

static inline int sf_error(SNDFILE *sndfile) {
    (void)sndfile;
    return 1; // Always error since we're stubbed
}

static inline int sf_format_check(const SF_INFO *info) {
    (void)info;
    return 0; // Format not supported
}

static inline sf_count_t sf_read_raw(SNDFILE *sndfile, void *ptr, sf_count_t bytes) {
    (void)sndfile; (void)ptr; (void)bytes;
    return 0;
}

static inline sf_count_t sf_write_raw(SNDFILE *sndfile, const void *ptr, sf_count_t bytes) {
    (void)sndfile; (void)ptr; (void)bytes;
    return 0;
}

// dsd-neo custom extension function stub
static inline void sf_write_sync(SNDFILE *sndfile) {
    (void)sndfile;
}

#ifdef __cplusplus
}
#endif

#endif // SNDFILE_STUB_H
