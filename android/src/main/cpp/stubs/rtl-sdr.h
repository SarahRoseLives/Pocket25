// rtl-sdr.h - Header router for Android builds
// When NATIVE_RTLSDR_ENABLED is defined, this routes to the real librtlsdr headers.
// Otherwise, it includes the stub implementation for rtl_tcp-only mode.

#ifndef RTL_SDR_ROUTER_H
#define RTL_SDR_ROUTER_H

#ifdef NATIVE_RTLSDR_ENABLED
// Use real librtlsdr-android headers
#include <rtl-sdr-android.h>
#else
// Use stub implementation
#include "rtl-sdr-stub.h"
#endif

#endif // RTL_SDR_ROUTER_H
