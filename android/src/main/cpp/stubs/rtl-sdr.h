// Stub rtl-sdr.h for Android builds
// This provides the necessary types and function declarations for rtl_tcp support
// without requiring the actual librtlsdr USB library

#ifndef RTL_SDR_STUB_H
#define RTL_SDR_STUB_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque device handle
typedef struct rtlsdr_dev rtlsdr_dev_t;

// Tuner types
enum rtlsdr_tuner {
    RTLSDR_TUNER_UNKNOWN = 0,
    RTLSDR_TUNER_E4000 = 1,
    RTLSDR_TUNER_FC0012 = 2,
    RTLSDR_TUNER_FC0013 = 3,
    RTLSDR_TUNER_FC2580 = 4,
    RTLSDR_TUNER_R820T = 5,
    RTLSDR_TUNER_R828D = 6
};

// Callback for async read
typedef void (*rtlsdr_read_async_cb_t)(unsigned char *buf, uint32_t len, void *ctx);

// Device enumeration and info
static inline uint32_t rtlsdr_get_device_count(void) { return 0; }
static inline const char *rtlsdr_get_device_name(uint32_t index) { (void)index; return "stub"; }
static inline int rtlsdr_get_device_usb_strings(uint32_t index, char *manufact, char *product, char *serial) {
    (void)index; (void)manufact; (void)product; (void)serial;
    return -1;
}
static inline int rtlsdr_get_index_by_serial(const char *serial) { (void)serial; return -1; }

// Open/close
static inline int rtlsdr_open(rtlsdr_dev_t **dev, uint32_t index) {
    (void)dev; (void)index;
    return -1; // No USB devices on Android
}
static inline int rtlsdr_close(rtlsdr_dev_t *dev) { (void)dev; return 0; }

// Configuration
static inline int rtlsdr_set_xtal_freq(rtlsdr_dev_t *dev, uint32_t rtl_freq, uint32_t tuner_freq) {
    (void)dev; (void)rtl_freq; (void)tuner_freq;
    return -1;
}
static inline int rtlsdr_get_xtal_freq(rtlsdr_dev_t *dev, uint32_t *rtl_freq, uint32_t *tuner_freq) {
    (void)dev; (void)rtl_freq; (void)tuner_freq;
    return -1;
}
static inline int rtlsdr_get_usb_strings(rtlsdr_dev_t *dev, char *manufact, char *product, char *serial) {
    (void)dev; (void)manufact; (void)product; (void)serial;
    return -1;
}
static inline int rtlsdr_write_eeprom(rtlsdr_dev_t *dev, uint8_t *data, uint8_t offset, uint16_t len) {
    (void)dev; (void)data; (void)offset; (void)len;
    return -1;
}
static inline int rtlsdr_read_eeprom(rtlsdr_dev_t *dev, uint8_t *data, uint8_t offset, uint16_t len) {
    (void)dev; (void)data; (void)offset; (void)len;
    return -1;
}

// Frequency
static inline int rtlsdr_set_center_freq(rtlsdr_dev_t *dev, uint32_t freq) { (void)dev; (void)freq; return -1; }
static inline uint32_t rtlsdr_get_center_freq(rtlsdr_dev_t *dev) { (void)dev; return 0; }

// Offset tuning
static inline int rtlsdr_set_offset_tuning(rtlsdr_dev_t *dev, int on) { (void)dev; (void)on; return -1; }
static inline int rtlsdr_get_offset_tuning(rtlsdr_dev_t *dev) { (void)dev; return 0; }

// Tuner type
static inline enum rtlsdr_tuner rtlsdr_get_tuner_type(rtlsdr_dev_t *dev) { (void)dev; return RTLSDR_TUNER_UNKNOWN; }

// Gain
static inline int rtlsdr_get_tuner_gains(rtlsdr_dev_t *dev, int *gains) { (void)dev; (void)gains; return 0; }
static inline int rtlsdr_set_tuner_gain(rtlsdr_dev_t *dev, int gain) { (void)dev; (void)gain; return -1; }
static inline int rtlsdr_get_tuner_gain(rtlsdr_dev_t *dev) { (void)dev; return 0; }
static inline int rtlsdr_set_tuner_if_gain(rtlsdr_dev_t *dev, int stage, int gain) {
    (void)dev; (void)stage; (void)gain;
    return -1;
}
static inline int rtlsdr_set_tuner_gain_mode(rtlsdr_dev_t *dev, int manual) { (void)dev; (void)manual; return -1; }
static inline int rtlsdr_set_tuner_bandwidth(rtlsdr_dev_t *dev, uint32_t bw) { (void)dev; (void)bw; return -1; }

// Sample rate
static inline int rtlsdr_set_sample_rate(rtlsdr_dev_t *dev, uint32_t rate) { (void)dev; (void)rate; return -1; }
static inline uint32_t rtlsdr_get_sample_rate(rtlsdr_dev_t *dev) { (void)dev; return 0; }

// Test mode
static inline int rtlsdr_set_testmode(rtlsdr_dev_t *dev, int on) { (void)dev; (void)on; return -1; }

// AGC
static inline int rtlsdr_set_agc_mode(rtlsdr_dev_t *dev, int on) { (void)dev; (void)on; return -1; }

// Direct sampling
static inline int rtlsdr_set_direct_sampling(rtlsdr_dev_t *dev, int on) { (void)dev; (void)on; return -1; }
static inline int rtlsdr_get_direct_sampling(rtlsdr_dev_t *dev) { (void)dev; return 0; }

// PPM error
static inline int rtlsdr_set_freq_correction(rtlsdr_dev_t *dev, int ppm) { (void)dev; (void)ppm; return -1; }
static inline int rtlsdr_get_freq_correction(rtlsdr_dev_t *dev) { (void)dev; return 0; }

// Bias tee
static inline int rtlsdr_set_bias_tee(rtlsdr_dev_t *dev, int on) { (void)dev; (void)on; return -1; }

// Streaming
static inline int rtlsdr_reset_buffer(rtlsdr_dev_t *dev) { (void)dev; return -1; }
static inline int rtlsdr_read_sync(rtlsdr_dev_t *dev, void *buf, int len, int *n_read) {
    (void)dev; (void)buf; (void)len; (void)n_read;
    return -1;
}
static inline int rtlsdr_wait_async(rtlsdr_dev_t *dev, rtlsdr_read_async_cb_t cb, void *ctx) {
    (void)dev; (void)cb; (void)ctx;
    return -1;
}
static inline int rtlsdr_read_async(rtlsdr_dev_t *dev, rtlsdr_read_async_cb_t cb, void *ctx, uint32_t buf_num, uint32_t buf_len) {
    (void)dev; (void)cb; (void)ctx; (void)buf_num; (void)buf_len;
    return -1;
}
static inline int rtlsdr_cancel_async(rtlsdr_dev_t *dev) { (void)dev; return -1; }

#ifdef __cplusplus
}
#endif

#endif // RTL_SDR_STUB_H
