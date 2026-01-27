#include <jni.h>
#include <android/log.h>
#include <string>
#include <pthread.h>
#include <cstdio>
#include <unistd.h>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#define LOG_TAG "DSD-Flutter"
#define LOG_TAG_OUTPUT "DSD-Output"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

extern "C" {
#include <dsd-neo/core/init.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/engine/engine.h>
#include <dsd-neo/runtime/exitflag.h>
// Forward declare to avoid C++ incompatibility with headers
void p25_sm_init(dsd_opts* opts, dsd_state* state);
void p25_reset_iden_tables(dsd_state* state);
int dsd_rtl_stream_tune(dsd_opts* opts, long int frequency);
}

// Native RTL-SDR USB support (when enabled)
#ifdef NATIVE_RTLSDR_ENABLED
#include <rtl-sdr.h>
#include <rtl-sdr-android.h>
#include <dsd-neo/io/rtl_device.h>
#include <dsd-neo/io/rtl_stream_c.h>
#endif

// Global context
static dsd_opts* g_opts = nullptr;
static dsd_state* g_state = nullptr;
static JavaVM* g_jvm = nullptr;
static pthread_t g_engine_thread;
static pthread_t g_stderr_thread;
static pthread_t g_poll_thread;
static bool g_engine_running = false;
static int g_stderr_pipe[2] = {-1, -1};
static pthread_t g_hackrf_tcp_server_thread;
static int g_hackrf_tcp_server_sock = -1;
static int g_hackrf_tcp_client_sock = -1;
static bool g_hackrf_tcp_server_running = false;
static bool g_hackrf_mode = false;
static jclass g_plugin_class = nullptr;
static jmethodID g_send_output_method = nullptr;
static jmethodID g_send_call_event_method = nullptr;
static jmethodID g_send_site_event_method = nullptr;
static jmethodID g_send_signal_event_method = nullptr;
static jmethodID g_send_network_event_method = nullptr;
static jmethodID g_send_patch_event_method = nullptr;
static jmethodID g_send_ga_event_method = nullptr;
static jmethodID g_send_aff_event_method = nullptr;

// Last known call state for change detection
static int g_last_tg = 0;
static int g_last_src = 0;

// Last known signal state for change detection
static unsigned int g_last_tsbk_ok = 0;
static unsigned int g_last_tsbk_err = 0;
static int g_last_synctype = -1;
static int g_last_carrier = 0;

// Last known network state for change detection
static int g_last_nb_count = 0;
static int g_last_patch_count = 0;
static int g_last_ga_count = 0;
static int g_last_aff_count = 0;

// ============================================================================
// Talkgroup Filtering (Whitelist/Blacklist)
// ============================================================================

#include <set>
#include <mutex>
#include <atomic>

enum FilterMode {
    FILTER_MODE_DISABLED = 0,  // No filtering - hear all calls
    FILTER_MODE_WHITELIST = 1, // Only hear whitelisted talkgroups
    FILTER_MODE_BLACKLIST = 2  // Hear all except blacklisted talkgroups
};

static FilterMode g_filter_mode = FILTER_MODE_DISABLED;
static std::set<int> g_filter_talkgroups;
static std::mutex g_filter_mutex;
static bool g_audio_enabled_by_user = true;  // Track user's audio preference
static bool g_audio_muted_by_filter = false; // Track if filter muted audio

// Custom DSD command arguments
static std::string g_custom_args;
static std::mutex g_custom_args_mutex;

// Retune freeze - temporarily block auto-retunes during system switch
static std::atomic<bool> g_retune_freeze{false};

// Check if a talkgroup should be heard based on filter settings
static bool should_hear_talkgroup(int tg) {
    std::lock_guard<std::mutex> lock(g_filter_mutex);
    
    if (g_filter_mode == FILTER_MODE_DISABLED) {
        return true;
    }
    
    bool in_list = g_filter_talkgroups.find(tg) != g_filter_talkgroups.end();
    
    if (g_filter_mode == FILTER_MODE_WHITELIST) {
        return in_list;  // Only hear if in whitelist
    } else { // FILTER_MODE_BLACKLIST
        return !in_list; // Hear unless in blacklist
    }
}

// Update audio output state based on filter
static void update_audio_for_talkgroup(int tg) {
    if (!g_opts) return;
    
    bool should_hear = should_hear_talkgroup(tg);
    
    if (should_hear && g_audio_enabled_by_user) {
        if (g_audio_muted_by_filter) {
            g_opts->audio_out = 1;
            g_audio_muted_by_filter = false;
            LOGI("Audio unmuted for TG %d", tg);
        }
    } else if (!should_hear) {
        if (!g_audio_muted_by_filter && g_opts->audio_out) {
            g_opts->audio_out = 0;
            g_audio_muted_by_filter = true;
            LOGI("Audio muted for filtered TG %d", tg);
        }
    }
}

// Public function to check if voice channel should be followed for a talkgroup
// Used by DSD-neo to skip voice channel grants for filtered talkgroups
extern "C" int dsd_flutter_should_follow_tg(int tg) {
    return should_hear_talkgroup(tg) ? 1 : 0;
}

// Last known site state for change detection
static unsigned long long g_last_wacn = 0;
static unsigned long long g_last_siteid = 0;
static unsigned long long g_last_rfssid = 0;
static int g_last_nac = 0;

// Helper function to sanitize string for UTF-8 conversion
// Replaces invalid UTF-8 bytes with '?' to prevent JNI crashes
static std::string sanitize_for_utf8(const char* text) {
    if (!text) return "";
    
    std::string result;
    result.reserve(strlen(text));
    
    const unsigned char* bytes = reinterpret_cast<const unsigned char*>(text);
    size_t len = strlen(text);
    
    for (size_t i = 0; i < len; i++) {
        unsigned char c = bytes[i];
        
        // ASCII range (0x00-0x7F) - always valid
        if (c < 0x80) {
            result += c;
        }
        // Start of 2-byte sequence (0xC0-0xDF)
        else if (c >= 0xC0 && c <= 0xDF && i + 1 < len) {
            unsigned char c2 = bytes[i + 1];
            if ((c2 & 0xC0) == 0x80) {
                result += c;
                result += c2;
                i += 1;
            } else {
                result += '?';  // Invalid continuation byte
            }
        }
        // Start of 3-byte sequence (0xE0-0xEF)
        else if (c >= 0xE0 && c <= 0xEF && i + 2 < len) {
            unsigned char c2 = bytes[i + 1];
            unsigned char c3 = bytes[i + 2];
            if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80) {
                result += c;
                result += c2;
                result += c3;
                i += 2;
            } else {
                result += '?';  // Invalid continuation bytes
            }
        }
        // Start of 4-byte sequence (0xF0-0xF7)
        else if (c >= 0xF0 && c <= 0xF7 && i + 3 < len) {
            unsigned char c2 = bytes[i + 1];
            unsigned char c3 = bytes[i + 2];
            unsigned char c4 = bytes[i + 3];
            if ((c2 & 0xC0) == 0x80 && (c3 & 0xC0) == 0x80 && (c4 & 0xC0) == 0x80) {
                result += c;
                result += c2;
                result += c3;
                result += c4;
                i += 3;
            } else {
                result += '?';  // Invalid continuation bytes
            }
        }
        // Invalid UTF-8 byte - replace with '?'
        else {
            result += '?';
        }
    }
    
    return result;
}

// Send output text to Flutter via JNI callback
static void send_to_flutter(const char* text) {
    if (!g_jvm || !g_plugin_class || !g_send_output_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    // Sanitize the input string to ensure valid UTF-8
    std::string sanitized = sanitize_for_utf8(text);
    jstring jtext = env->NewStringUTF(sanitized.c_str());
    if (jtext) {
        env->CallStaticVoidMethod(g_plugin_class, g_send_output_method, jtext);
        env->DeleteLocalRef(jtext);
    }
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send structured call event to Flutter
static void send_call_event_to_flutter(
    int eventType,      // 0=call_start, 1=call_update, 2=call_end
    int talkgroup,
    int sourceId,
    int nac,
    const char* callType,
    bool isEncrypted,
    bool isEmergency,
    const char* algName,
    int slot,
    double frequency,
    const char* systemName,
    const char* groupName,
    const char* sourceName
) {
    if (!g_jvm || !g_plugin_class || !g_send_call_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    jstring jCallType = env->NewStringUTF(callType ? callType : "");
    jstring jAlgName = env->NewStringUTF(algName ? algName : "");
    jstring jSystemName = env->NewStringUTF(systemName ? systemName : "");
    jstring jGroupName = env->NewStringUTF(groupName ? groupName : "");
    jstring jSourceName = env->NewStringUTF(sourceName ? sourceName : "");
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_call_event_method,
        (jint)eventType,
        (jint)talkgroup,
        (jint)sourceId,
        (jint)nac,
        jCallType,
        (jboolean)isEncrypted,
        (jboolean)isEmergency,
        jAlgName,
        (jint)slot,
        (jdouble)frequency,
        jSystemName,
        jGroupName,
        jSourceName
    );
    
    env->DeleteLocalRef(jCallType);
    env->DeleteLocalRef(jAlgName);
    env->DeleteLocalRef(jSystemName);
    env->DeleteLocalRef(jGroupName);
    env->DeleteLocalRef(jSourceName);
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send site/system details to Flutter
static void send_site_event_to_flutter(
    unsigned long long wacn,
    unsigned long long siteId,
    unsigned long long rfssId,
    unsigned long long systemId,
    int nac
) {
    if (!g_jvm || !g_plugin_class || !g_send_site_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_site_event_method,
        (jlong)wacn,
        (jlong)siteId,
        (jlong)rfssId,
        (jlong)systemId,
        (jint)nac
    );
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send signal quality metrics to Flutter
static void send_signal_event_to_flutter(
    unsigned int tsbkOk,
    unsigned int tsbkErr,
    int synctype,
    bool hasCarrier,
    bool hasSync
) {
    if (!g_jvm || !g_plugin_class || !g_send_signal_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_signal_event_method,
        (jint)tsbkOk,
        (jint)tsbkErr,
        (jint)synctype,
        (jboolean)hasCarrier,
        (jboolean)hasSync
    );
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send neighbor sites event to Flutter
static void send_neighbor_event_to_flutter(
    int neighborCount,
    const long int* neighborFreqs,
    const time_t* neighborLastSeen
) {
    if (!g_jvm || !g_plugin_class || !g_send_network_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    // Convert neighbor frequencies to Java long array
    jlongArray jNeighborFreqs = env->NewLongArray(neighborCount);
    if (jNeighborFreqs && neighborCount > 0) {
        jlong* freqs = new jlong[neighborCount];
        for (int i = 0; i < neighborCount; i++) {
            freqs[i] = (jlong)neighborFreqs[i];
        }
        env->SetLongArrayRegion(jNeighborFreqs, 0, neighborCount, freqs);
        delete[] freqs;
    }
    
    // Convert last seen times to Java long array
    jlongArray jLastSeen = env->NewLongArray(neighborCount);
    if (jLastSeen && neighborCount > 0) {
        jlong* times = new jlong[neighborCount];
        for (int i = 0; i < neighborCount; i++) {
            times[i] = (jlong)neighborLastSeen[i];
        }
        env->SetLongArrayRegion(jLastSeen, 0, neighborCount, times);
        delete[] times;
    }
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_network_event_method,
        (jint)neighborCount,
        jNeighborFreqs,
        jLastSeen
    );
    
    if (jNeighborFreqs) env->DeleteLocalRef(jNeighborFreqs);
    if (jLastSeen) env->DeleteLocalRef(jLastSeen);
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send patch event to Flutter
static void send_patch_event_to_flutter(
    int patchCount,
    const uint16_t* sgids,
    const uint8_t* isPatch,
    const uint8_t* active,
    const time_t* lastUpdate,
    const uint8_t* wgidCounts,
    const uint16_t wgids[][8],
    const uint8_t* wuidCounts,
    const uint32_t wuids[][8],
    const uint16_t* keys,
    const uint8_t* algs,
    const uint8_t* keyValid
) {
    if (!g_jvm || !g_plugin_class || !g_send_patch_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    // Create Java arrays for patch data
    jintArray jSgids = env->NewIntArray(patchCount);
    jbooleanArray jIsPatch = env->NewBooleanArray(patchCount);
    jbooleanArray jActive = env->NewBooleanArray(patchCount);
    jlongArray jLastUpdate = env->NewLongArray(patchCount);
    jintArray jWgidCounts = env->NewIntArray(patchCount);
    jintArray jWuidCounts = env->NewIntArray(patchCount);
    jintArray jKeys = env->NewIntArray(patchCount);
    jintArray jAlgs = env->NewIntArray(patchCount);
    jbooleanArray jKeyValid = env->NewBooleanArray(patchCount);
    
    if (patchCount > 0) {
        jint* sgidsBuf = new jint[patchCount];
        jboolean* isPatchBuf = new jboolean[patchCount];
        jboolean* activeBuf = new jboolean[patchCount];
        jlong* lastUpdateBuf = new jlong[patchCount];
        jint* wgidCountsBuf = new jint[patchCount];
        jint* wuidCountsBuf = new jint[patchCount];
        jint* keysBuf = new jint[patchCount];
        jint* algsBuf = new jint[patchCount];
        jboolean* keyValidBuf = new jboolean[patchCount];
        
        for (int i = 0; i < patchCount; i++) {
            sgidsBuf[i] = sgids[i];
            isPatchBuf[i] = isPatch[i] != 0;
            activeBuf[i] = active[i] != 0;
            lastUpdateBuf[i] = lastUpdate[i];
            wgidCountsBuf[i] = wgidCounts[i];
            wuidCountsBuf[i] = wuidCounts[i];
            keysBuf[i] = keys[i];
            algsBuf[i] = algs[i];
            keyValidBuf[i] = keyValid[i] != 0;
        }
        
        env->SetIntArrayRegion(jSgids, 0, patchCount, sgidsBuf);
        env->SetBooleanArrayRegion(jIsPatch, 0, patchCount, isPatchBuf);
        env->SetBooleanArrayRegion(jActive, 0, patchCount, activeBuf);
        env->SetLongArrayRegion(jLastUpdate, 0, patchCount, lastUpdateBuf);
        env->SetIntArrayRegion(jWgidCounts, 0, patchCount, wgidCountsBuf);
        env->SetIntArrayRegion(jWuidCounts, 0, patchCount, wuidCountsBuf);
        env->SetIntArrayRegion(jKeys, 0, patchCount, keysBuf);
        env->SetIntArrayRegion(jAlgs, 0, patchCount, algsBuf);
        env->SetBooleanArrayRegion(jKeyValid, 0, patchCount, keyValidBuf);
        
        delete[] sgidsBuf;
        delete[] isPatchBuf;
        delete[] activeBuf;
        delete[] lastUpdateBuf;
        delete[] wgidCountsBuf;
        delete[] wuidCountsBuf;
        delete[] keysBuf;
        delete[] algsBuf;
        delete[] keyValidBuf;
    }
    
    // Convert 2D arrays - flatten WGIDs and WUIDs
    jintArray jWgids = nullptr;
    jintArray jWuids = nullptr;
    
    if (patchCount > 0) {
        jWgids = env->NewIntArray(patchCount * 8);
        jWuids = env->NewIntArray(patchCount * 8);
        
        jint* wgidsBuf = new jint[patchCount * 8];
        jint* wuidsBuf = new jint[patchCount * 8];
        
        for (int i = 0; i < patchCount; i++) {
            for (int j = 0; j < 8; j++) {
                wgidsBuf[i * 8 + j] = wgids[i][j];
                wuidsBuf[i * 8 + j] = wuids[i][j];
            }
        }
        
        env->SetIntArrayRegion(jWgids, 0, patchCount * 8, wgidsBuf);
        env->SetIntArrayRegion(jWuids, 0, patchCount * 8, wuidsBuf);
        
        delete[] wgidsBuf;
        delete[] wuidsBuf;
    }
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_patch_event_method,
        (jint)patchCount, jSgids, jIsPatch, jActive, jLastUpdate,
        jWgidCounts, jWgids, jWuidCounts, jWuids,
        jKeys, jAlgs, jKeyValid
    );
    
    if (jSgids) env->DeleteLocalRef(jSgids);
    if (jIsPatch) env->DeleteLocalRef(jIsPatch);
    if (jActive) env->DeleteLocalRef(jActive);
    if (jLastUpdate) env->DeleteLocalRef(jLastUpdate);
    if (jWgidCounts) env->DeleteLocalRef(jWgidCounts);
    if (jWuidCounts) env->DeleteLocalRef(jWuidCounts);
    if (jWgids) env->DeleteLocalRef(jWgids);
    if (jWuids) env->DeleteLocalRef(jWuids);
    if (jKeys) env->DeleteLocalRef(jKeys);
    if (jAlgs) env->DeleteLocalRef(jAlgs);
    if (jKeyValid) env->DeleteLocalRef(jKeyValid);
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send group attachment event to Flutter
static void send_ga_event_to_flutter(
    int gaCount,
    const uint32_t* rids,
    const uint16_t* tgs,
    const time_t* lastSeen
) {
    if (!g_jvm || !g_plugin_class || !g_send_ga_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    jlongArray jRids = env->NewLongArray(gaCount);
    jintArray jTgs = env->NewIntArray(gaCount);
    jlongArray jLastSeen = env->NewLongArray(gaCount);
    
    if (gaCount > 0) {
        jlong* ridsBuf = new jlong[gaCount];
        jint* tgsBuf = new jint[gaCount];
        jlong* lastSeenBuf = new jlong[gaCount];
        
        for (int i = 0; i < gaCount; i++) {
            ridsBuf[i] = rids[i];
            tgsBuf[i] = tgs[i];
            lastSeenBuf[i] = lastSeen[i];
        }
        
        env->SetLongArrayRegion(jRids, 0, gaCount, ridsBuf);
        env->SetIntArrayRegion(jTgs, 0, gaCount, tgsBuf);
        env->SetLongArrayRegion(jLastSeen, 0, gaCount, lastSeenBuf);
        
        delete[] ridsBuf;
        delete[] tgsBuf;
        delete[] lastSeenBuf;
    }
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_ga_event_method,
        (jint)gaCount, jRids, jTgs, jLastSeen
    );
    
    if (jRids) env->DeleteLocalRef(jRids);
    if (jTgs) env->DeleteLocalRef(jTgs);
    if (jLastSeen) env->DeleteLocalRef(jLastSeen);
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Send affiliation event to Flutter
static void send_aff_event_to_flutter(
    int affCount,
    const uint32_t* rids,
    const time_t* lastSeen
) {
    if (!g_jvm || !g_plugin_class || !g_send_aff_event_method) return;
    
    JNIEnv* env = nullptr;
    bool attached = false;
    
    int status = g_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (status == JNI_EDETACHED) {
        if (g_jvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            attached = true;
        } else {
            return;
        }
    } else if (status != JNI_OK) {
        return;
    }
    
    jlongArray jRids = env->NewLongArray(affCount);
    jlongArray jLastSeen = env->NewLongArray(affCount);
    
    if (affCount > 0) {
        jlong* ridsBuf = new jlong[affCount];
        jlong* lastSeenBuf = new jlong[affCount];
        
        for (int i = 0; i < affCount; i++) {
            ridsBuf[i] = rids[i];
            lastSeenBuf[i] = lastSeen[i];
        }
        
        env->SetLongArrayRegion(jRids, 0, affCount, ridsBuf);
        env->SetLongArrayRegion(jLastSeen, 0, affCount, lastSeenBuf);
        
        delete[] ridsBuf;
        delete[] lastSeenBuf;
    }
    
    env->CallStaticVoidMethod(g_plugin_class, g_send_aff_event_method,
        (jint)affCount, jRids, jLastSeen
    );
    
    if (jRids) env->DeleteLocalRef(jRids);
    if (jLastSeen) env->DeleteLocalRef(jLastSeen);
    
    if (attached) {
        g_jvm->DetachCurrentThread();
    }
}

// Poll thread - checks state for call and site changes
static void* poll_thread_func(void* arg) {
    LOGI("Poll thread started");
    
    while (g_engine_running && g_state) {
        // Check for call state changes
        int tg = g_state->lasttg;
        int src = g_state->lastsrc;
        int nac = g_state->nac;
        int slot = g_state->currentslot;
        
        // Detect call changes
        if (tg != g_last_tg || src != g_last_src) {
            if (tg != 0 || src != 0) {
                // New or updated call - apply talkgroup filter
                update_audio_for_talkgroup(tg);
                
                // New or updated call
                const char* callType = "Group";
                if (g_state->gi[0] == 1) {
                    callType = "Private";
                }
                
                bool isEncrypted = false;
                bool isEmergency = g_state->p25_call_emergency[0] != 0;
                
                // Get group/source names from call_string if available
                const char* groupName = "";
                const char* sourceName = "";
                
                int eventType = (g_last_tg == 0 && g_last_src == 0) ? 0 : 1; // 0=start, 1=update
                
                // Add isFiltered flag to indicate if audio is muted
                bool isFiltered = !should_hear_talkgroup(tg);
                
                LOGI("Call event: type=%d tg=%d src=%d nac=0x%X slot=%d filtered=%d", 
                     eventType, tg, src, nac, slot, isFiltered);
                
                send_call_event_to_flutter(
                    eventType,
                    tg,
                    src,
                    nac,
                    callType,
                    isEncrypted,
                    isEmergency,
                    "",  // alg name
                    slot,
                    0.0, // frequency
                    "",  // system name
                    groupName,
                    sourceName
                );
            } else if (g_last_tg != 0 || g_last_src != 0) {
                // Call ended - restore audio if it was muted by filter
                if (g_audio_muted_by_filter && g_audio_enabled_by_user && g_opts) {
                    g_opts->audio_out = 1;
                    g_audio_muted_by_filter = false;
                    LOGI("Audio restored after filtered call ended");
                }
                
                // Call ended
                LOGI("Call ended: was tg=%d src=%d", g_last_tg, g_last_src);
                send_call_event_to_flutter(
                    2,  // call_end
                    g_last_tg,
                    g_last_src,
                    nac,
                    "Group",
                    false,
                    false,
                    "",
                    slot,
                    0.0,
                    "",
                    "",
                    ""
                );
            }
            
            g_last_tg = tg;
            g_last_src = src;
        }
        
        // Check for site detail changes
        unsigned long long wacn = g_state->p2_wacn;
        unsigned long long siteid = g_state->p2_siteid;
        unsigned long long rfssid = g_state->p2_rfssid;
        
        if (wacn != g_last_wacn || siteid != g_last_siteid || 
            rfssid != g_last_rfssid || nac != g_last_nac) {
            
            if (wacn != 0 || siteid != 0 || rfssid != 0) {
                LOGI("Site details: WACN=0x%llX Site=0x%llX RFSS=0x%llX NAC=0x%X",
                     wacn, siteid, rfssid, nac);
                
                send_site_event_to_flutter(
                    wacn,
                    siteid,
                    rfssid,
                    0,  // systemId (can add if needed)
                    nac
                );
            }
            
            g_last_wacn = wacn;
            g_last_siteid = siteid;
            g_last_rfssid = rfssid;
            g_last_nac = nac;
        }
        
        // Check for signal quality changes (using state fields instead of parsing logs)
        unsigned int tsbk_ok = g_state->p25_p1_fec_ok;
        unsigned int tsbk_err = g_state->p25_p1_fec_err;
        int synctype = g_state->synctype;
        int carrier = g_state->carrier;
        
        // Send signal updates if metrics changed
        if (tsbk_ok != g_last_tsbk_ok || tsbk_err != g_last_tsbk_err || 
            synctype != g_last_synctype || carrier != g_last_carrier) {
            
            // Check if we have P25 sync (synctype 0 or 1 for P25 P1, 35/36 for P25 P2)
            bool hasSync = (synctype == 0 || synctype == 1 || synctype == 35 || synctype == 36);
            bool hasCarrier = (carrier != 0);
            
            send_signal_event_to_flutter(
                tsbk_ok,
                tsbk_err,
                synctype,
                hasCarrier,
                hasSync
            );
            
            g_last_tsbk_ok = tsbk_ok;
            g_last_tsbk_err = tsbk_err;
            g_last_synctype = synctype;
            g_last_carrier = carrier;
        }
        
        // Check for neighbor site changes
        int nb_count = g_state->p25_nb_count;
        
        if (nb_count != g_last_nb_count) {
            send_neighbor_event_to_flutter(
                nb_count,
                g_state->p25_nb_freq,
                g_state->p25_nb_last_seen
            );
            
            g_last_nb_count = nb_count;
        }
        
        // Check for patch changes
        int patch_count = g_state->p25_patch_count;
        
        if (patch_count != g_last_patch_count) {
            send_patch_event_to_flutter(
                patch_count,
                g_state->p25_patch_sgid,
                g_state->p25_patch_is_patch,
                g_state->p25_patch_active,
                g_state->p25_patch_last_update,
                g_state->p25_patch_wgid_count,
                g_state->p25_patch_wgid,
                g_state->p25_patch_wuid_count,
                g_state->p25_patch_wuid,
                g_state->p25_patch_key,
                g_state->p25_patch_alg,
                g_state->p25_patch_key_valid
            );
            
            g_last_patch_count = patch_count;
        }
        
        // Check for group attachment changes
        int ga_count = g_state->p25_ga_count;
        
        if (ga_count != g_last_ga_count) {
            send_ga_event_to_flutter(
                ga_count,
                g_state->p25_ga_rid,
                g_state->p25_ga_tg,
                g_state->p25_ga_last_seen
            );
            
            g_last_ga_count = ga_count;
        }
        
        // Check for affiliation changes
        int aff_count = g_state->p25_aff_count;
        
        if (aff_count != g_last_aff_count) {
            send_aff_event_to_flutter(
                aff_count,
                g_state->p25_aff_rid,
                g_state->p25_aff_last_seen
            );
            
            g_last_aff_count = aff_count;
        }
        
        // Poll every 100ms
        usleep(100000);
    }
    
    LOGI("Poll thread finished");
    return nullptr;
}

// Thread to redirect stderr to logcat AND Flutter
static void* stderr_thread_func(void* arg) {
    char buf[512];
    ssize_t n;
    
    while ((n = read(g_stderr_pipe[0], buf, sizeof(buf) - 1)) > 0) {
        buf[n] = '\0';
        // Remove trailing newline if present
        if (n > 0 && buf[n-1] == '\n') {
            buf[n-1] = '\0';
        }
        if (buf[0] != '\0') {
            __android_log_print(ANDROID_LOG_INFO, LOG_TAG_OUTPUT, "%s", buf);
            // Also send to Flutter UI
            send_to_flutter(buf);
        }
    }
    return nullptr;
}

// Start stderr redirection
static void start_stderr_redirect() {
    if (pipe(g_stderr_pipe) == -1) {
        LOGE("Failed to create stderr pipe");
        return;
    }
    
    // Redirect stderr to our pipe
    dup2(g_stderr_pipe[1], STDERR_FILENO);
    
    // Start reader thread
    pthread_create(&g_stderr_thread, nullptr, stderr_thread_func, nullptr);
    LOGI("stderr redirect started");
}

// Engine thread function
static void* engine_thread_func(void* arg) {
    LOGI("Engine thread started");
    
    if (g_opts && g_state) {
        int rc = dsd_engine_run(g_opts, g_state);
        LOGI("Engine exited with code %d", rc);
    }
    
    g_engine_running = false;
    LOGI("Engine thread finished");
    return nullptr;
}

extern "C" JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    g_jvm = vm;
    LOGI("DSD-Flutter JNI loaded");
    
    // Cache the plugin class and methods for callbacks
    JNIEnv* env = nullptr;
    if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
        jclass localClass = env->FindClass("com/example/dsd_flutter/DsdFlutterPlugin");
        if (localClass) {
            g_plugin_class = (jclass)env->NewGlobalRef(localClass);
            g_send_output_method = env->GetStaticMethodID(g_plugin_class, "sendOutput", "(Ljava/lang/String;)V");
            g_send_call_event_method = env->GetStaticMethodID(g_plugin_class, "sendCallEvent",
                "(IIIILjava/lang/String;ZZLjava/lang/String;IDLjava/lang/String;Ljava/lang/String;Ljava/lang/String;)V");
            g_send_site_event_method = env->GetStaticMethodID(g_plugin_class, "sendSiteEvent",
                "(JJJJI)V");
            g_send_signal_event_method = env->GetStaticMethodID(g_plugin_class, "sendSignalEvent",
                "(IIIZZ)V");
            g_send_network_event_method = env->GetStaticMethodID(g_plugin_class, "sendNetworkEvent",
                "(I[J[J)V");
            g_send_patch_event_method = env->GetStaticMethodID(g_plugin_class, "sendPatchEvent",
                "(I[I[Z[Z[J[I[I[I[I[I[I[Z)V");
            g_send_ga_event_method = env->GetStaticMethodID(g_plugin_class, "sendGroupAttachmentEvent",
                "(I[J[I[J)V");
            g_send_aff_event_method = env->GetStaticMethodID(g_plugin_class, "sendAffiliationEvent",
                "(I[J[J)V");
            env->DeleteLocalRef(localClass);
            LOGI("Flutter callbacks initialized");
        } else {
            LOGE("Failed to find DsdFlutterPlugin class");
        }
    }
    
    // Start stderr redirection to logcat
    start_stderr_redirect();
    
    return JNI_VERSION_1_6;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeInit(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Initializing DSD library");
    
    if (g_opts) {
        LOGI("Already initialized, cleaning up first");
        if (g_engine_running) {
            exitflag = 1;
            pthread_join(g_engine_thread, nullptr);
            pthread_join(g_poll_thread, nullptr);
        }
        if (g_state) {
            freeState(g_state);
            free(g_state);
        }
        free(g_opts);
    }
    
    g_opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
    g_state = (dsd_state*)calloc(1, sizeof(dsd_state));
    
    if (!g_opts || !g_state) {
        LOGE("Failed to allocate memory");
        return;
    }
    
    initOpts(g_opts);
    initState(g_state);
    
    // Apply custom DSD args if set
    {
        std::lock_guard<std::mutex> lock(g_custom_args_mutex);
        if (!g_custom_args.empty()) {
            LOGI("Applying custom DSD args: %s", g_custom_args.c_str());
            
            // Parse decoder flags
            if (g_custom_args.find("-fp") != std::string::npos) {
                g_opts->frame_p25p1 = 1;
                LOGI("Enabled P25 Phase 1");
            }
            if (g_custom_args.find("-fx") != std::string::npos) {
                g_opts->frame_p25p2 = 1;
                LOGI("Enabled P25 Phase 2");
            }
            if (g_custom_args.find("-f1") != std::string::npos) {
                g_opts->frame_p25p1 = 1;
                g_opts->frame_p25p2 = 0;
                LOGI("Enabled P25 Phase 1 only");
            }
            if (g_custom_args.find("-f2") != std::string::npos) {
                g_opts->frame_p25p2 = 1;
                g_opts->frame_p25p1 = 0;
                LOGI("Enabled P25 Phase 2 only");
            }
            if (g_custom_args.find("-fd") != std::string::npos) {
                g_opts->frame_dmr = 1;
                LOGI("Enabled DMR");
            }
            if (g_custom_args.find("-fs") != std::string::npos) {
                g_opts->frame_dmr = 1;
                LOGI("Enabled DMR Simplex");
            }
            if (g_custom_args.find("-fn") != std::string::npos || 
                g_custom_args.find("-fi") != std::string::npos) {
                g_opts->frame_nxdn48 = 1;
                g_opts->frame_nxdn96 = 1;
                LOGI("Enabled NXDN");
            }
            if (g_custom_args.find("-fa") != std::string::npos) {
                g_opts->frame_p25p1 = 1;
                g_opts->frame_p25p2 = 1;
                g_opts->frame_dmr = 1;
                g_opts->frame_nxdn48 = 1;
                g_opts->frame_nxdn96 = 1;
                g_opts->frame_provoice = 1;
                LOGI("Enabled Auto Detection");
            }
            if (g_custom_args.find("-fh") != std::string::npos) {
                g_opts->frame_provoice = 1;
                LOGI("Enabled EDACS/ProVoice");
            }
            
            // Parse modulation flags
            if (g_custom_args.find("-ma") != std::string::npos) {
                g_opts->mod_c4fm = 1;
                g_opts->mod_qpsk = 1;
                g_opts->mod_gfsk = 1;
                LOGI("Enabled auto modulation");
            }
            if (g_custom_args.find("-mc") != std::string::npos) {
                g_opts->mod_c4fm = 1;
                LOGI("Enabled C4FM only");
            }
            if (g_custom_args.find("-mg") != std::string::npos) {
                g_opts->mod_gfsk = 1;
                LOGI("Enabled GFSK only");
            }
            if (g_custom_args.find("-mq") != std::string::npos) {
                g_opts->mod_qpsk = 1;
                LOGI("Enabled QPSK only");
            }
            
            // Parse audio gain
            if (g_custom_args.find("-g 0") != std::string::npos || 
                g_custom_args.find("-g 0.0") != std::string::npos) {
                g_opts->audio_out = 0;
                LOGI("Disabled audio output");
            }
            
            // Parse -i input device (simplified - just detect rtl vs rtltcp)
            size_t i_pos = g_custom_args.find("-i ");
            if (i_pos != std::string::npos) {
                std::string input_str = g_custom_args.substr(i_pos + 3);
                // Extract until next space or end
                size_t space_pos = input_str.find(' ');
                if (space_pos != std::string::npos) {
                    input_str = input_str.substr(0, space_pos);
                }
                
                // Copy to audio_in_dev
                strncpy(g_opts->audio_in_dev, input_str.c_str(), sizeof(g_opts->audio_in_dev) - 1);
                g_opts->audio_in_dev[sizeof(g_opts->audio_in_dev) - 1] = '\0';
                LOGI("Set input device from -i: %s", g_opts->audio_in_dev);
            }
            
            // Parse encryption keys
            size_t h_pos = g_custom_args.find("-H ");
            if (h_pos != std::string::npos) {
                LOGI("Found AES/Hytera key in command string");
                // Key parsing would go here
            }
            
            // Parse other options
            if (g_custom_args.find("-4") != std::string::npos) {
                LOGI("Force privacy key enabled");
            }
            if (g_custom_args.find("-Z") != std::string::npos) {
                LOGI("MBE/PDU logging enabled");
            }
        }
    }
    
    // Initialize Android native USB fields
    g_opts->rtl_android_usb_fd = -1;
    g_opts->rtl_android_usb_path[0] = '\0';
    
    // Reset call tracking
    g_last_tg = 0;
    g_last_src = 0;
    
    // Reset site tracking
    g_last_wacn = 0;
    g_last_siteid = 0;
    g_last_rfssid = 0;
    g_last_nac = 0;
    
    LOGI("DSD initialized successfully");
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeConnect(
    JNIEnv* env,
    jobject thiz,
    jstring host,
    jint port,
    jlong freq_hz,
    jint gain,
    jint ppm,
    jint bias_tee) {
    
    const char* host_str = env->GetStringUTFChars(host, nullptr);
    
    LOGI("Configuring rtl_tcp at %s:%d freq=%lld Hz gain=%d ppm=%d bias_tee=%d", 
         host_str, port, (long long)freq_hz, gain, ppm, bias_tee);
    
    if (g_opts) {
        // Set up rtl_tcp input string: rtltcp:host:port:freq:gain:ppm:bw:sql:vol:bias=1
        // Format: rtltcp:hostname:port:freq:gain:ppm:bw:sql:vol:b=0/1
        // Squelch 0 = disabled (wide open for digital)
        snprintf(g_opts->audio_in_dev, sizeof(g_opts->audio_in_dev),
                 "rtltcp:%s:%d:%lld:%d:%d:48:0:2:b=%d", 
                 host_str, port, (long long)freq_hz, gain, ppm, bias_tee);
        
        // Also set individual options
        snprintf(g_opts->rtltcp_hostname, sizeof(g_opts->rtltcp_hostname), "%s", host_str);
        g_opts->rtltcp_portno = port;
        g_opts->rtltcp_enabled = 1;
        g_opts->rtlsdr_center_freq = (uint32_t)freq_hz;
        g_opts->rtl_gain_value = gain;
        g_opts->rtlsdr_ppm_error = ppm;
        g_opts->rtl_bias_tee = bias_tee;
        g_opts->rtl_dsp_bw_khz = 48;  // Full bandwidth
        g_opts->rtl_squelch_level = 0;  // Disabled - wide open
        g_opts->rtl_volume_multiplier = 2;
        g_opts->audio_in_type = AUDIO_IN_RTL;
        
        // Enable audio output using platform abstraction layer
        snprintf(g_opts->audio_out_dev, sizeof(g_opts->audio_out_dev), "android");
        g_opts->audio_out_type = 0;  // Use platform audio (dsd_audio_*)
        g_opts->audio_out = 1;       // Enable audio output
        
        // Audio output parameters - 8kHz stereo for P25 Phase 2 TDMA support
        // P25 Phase 2 uses two time slots that are mixed to stereo output
        g_opts->pulse_digi_rate_out = 8000;
        g_opts->pulse_digi_out_channels = 2;  // Stereo for P25 Phase 2 dual-slot support
        
        // Disable slot 2 to avoid Reed-Solomon errors causing choppy audio
        // Slot 1 will be duplicated to both channels for smooth playback
        g_opts->slot1_on = 1;
        g_opts->slot2_on = 0;
        g_opts->slot_preference = 0;  // Prefer slot 1
        
        // Disable P25 trunk following by default (enable it when needed via setTrunkFollowing)
        g_opts->p25_trunk = 0;
        
        LOGI("Configured for rtl_tcp input: %s", g_opts->audio_in_dev);
        LOGI("Bias-tee setting: %d", g_opts->rtl_bias_tee);
        LOGI("Audio output enabled: %s type=%d slot1=%d slot2=%d", 
             g_opts->audio_out_dev, g_opts->audio_out_type, 
             g_opts->slot1_on, g_opts->slot2_on);
    }
    
    env->ReleaseStringUTFChars(host, host_str);
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeStart(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Starting DSD engine");
    
    if (g_engine_running) {
        LOGI("Engine already running");
        return;
    }
    
    // Re-apply custom args if set (in case nativeConnect overwrote them)
    {
        std::lock_guard<std::mutex> lock(g_custom_args_mutex);
        if (!g_custom_args.empty() && g_opts) {
            LOGI("Re-applying custom DSD args before start: %s", g_custom_args.c_str());
            
            // Re-parse -i input device
            size_t i_pos = g_custom_args.find("-i ");
            if (i_pos != std::string::npos) {
                std::string input_str = g_custom_args.substr(i_pos + 3);
                // Extract until next space or end
                size_t space_pos = input_str.find(' ');
                if (space_pos != std::string::npos) {
                    input_str = input_str.substr(0, space_pos);
                }
                
                // Copy to audio_in_dev
                strncpy(g_opts->audio_in_dev, input_str.c_str(), sizeof(g_opts->audio_in_dev) - 1);
                g_opts->audio_in_dev[sizeof(g_opts->audio_in_dev) - 1] = '\0';
                LOGI("Re-applied input device from -i: %s", g_opts->audio_in_dev);
            }
        }
    }
    
    if (g_opts && g_state) {
        // Log config before starting
        LOGI("Config: audio_in_dev=%s", g_opts->audio_in_dev);
        LOGI("Config: audio_in_type=%d (RTL=%d)", g_opts->audio_in_type, AUDIO_IN_RTL);
        LOGI("Config: audio_in_fd=%d", g_opts->audio_in_fd);
        LOGI("Config: wav_sample_rate=%d", g_opts->wav_sample_rate);
        LOGI("Config: rtltcp_enabled=%d", g_opts->rtltcp_enabled);
        LOGI("Config: rtltcp_hostname=%s", g_opts->rtltcp_hostname);
        LOGI("Config: rtltcp_portno=%d", g_opts->rtltcp_portno);
        LOGI("Config: rtlsdr_center_freq=%u", g_opts->rtlsdr_center_freq);
        LOGI("Config: audio_out_type=%d", g_opts->audio_out_type);
        LOGI("Config: p25_trunk=%d", g_opts->p25_trunk);
        LOGI("Config: rtl_android_usb_fd=%d", g_opts->rtl_android_usb_fd);
        LOGI("Config: rtl_android_usb_path=%s", g_opts->rtl_android_usb_path);
        
        // Reset call tracking
        g_last_tg = 0;
        g_last_src = 0;
        
        exitflag = 0;
        g_engine_running = true;
        
        int rc = pthread_create(&g_engine_thread, nullptr, engine_thread_func, nullptr);
        if (rc != 0) {
            LOGE("Failed to create engine thread: %d", rc);
            g_engine_running = false;
        } else {
            LOGI("Engine thread created");
            
            // Start poll thread for call events
            rc = pthread_create(&g_poll_thread, nullptr, poll_thread_func, nullptr);
            if (rc != 0) {
                LOGE("Failed to create poll thread: %d", rc);
            } else {
                LOGI("Poll thread created");
            }
        }
    } else {
        LOGE("DSD not initialized");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeStop(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Stopping DSD engine");
    
    if (g_engine_running) {
        exitflag = 1;
        g_engine_running = false;  // Signal poll thread to stop
        pthread_join(g_engine_thread, nullptr);
        pthread_join(g_poll_thread, nullptr);
        LOGI("Engine threads stopped");
        
        // Reset P25 state to prevent retune to old system
        if (g_state) {
            LOGI("Clearing P25 frequency identifier tables");
            p25_reset_iden_tables(g_state);
        }
        if (g_opts && g_state) {
            LOGI("Reinitializing P25 trunking state machine");
            p25_sm_init(g_opts, g_state);
        }
        
        LOGI("Engine stopped");
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeCleanup(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Cleaning up DSD library");
    
    if (g_engine_running) {
        exitflag = 1;
        g_engine_running = false;
        pthread_join(g_engine_thread, nullptr);
        pthread_join(g_poll_thread, nullptr);
    }
    
    if (g_state) {
        freeState(g_state);
        free(g_state);
        g_state = nullptr;
    }
    
    if (g_opts) {
        free(g_opts);
        g_opts = nullptr;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetAudioEnabled(
    JNIEnv* env,
    jobject thiz,
    jboolean enabled) {
    
    LOGI("Setting audio enabled: %d", enabled);
    
    g_audio_enabled_by_user = enabled;
    
    if (g_opts) {
        // Only enable if user wants it AND not muted by filter
        if (enabled && !g_audio_muted_by_filter) {
            g_opts->audio_out = 1;
        } else if (!enabled) {
            g_opts->audio_out = 0;
        }
        LOGI("Audio output %s (user=%d, filter_muted=%d)", 
             g_opts->audio_out ? "enabled" : "disabled",
             g_audio_enabled_by_user, g_audio_muted_by_filter);
    }
}

// ============================================================================
// Talkgroup Filter JNI Functions
// ============================================================================

/**
 * Set the filter mode
 * @param mode 0=disabled, 1=whitelist, 2=blacklist
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetFilterMode(
    JNIEnv* env,
    jobject thiz,
    jint mode) {
    
    int current_tg = 0;
    
    {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        g_filter_mode = static_cast<FilterMode>(mode);
        LOGI("Filter mode set to: %d", mode);
        current_tg = g_last_tg;
    } // Mutex released here
    
    // Apply filter change immediately if there's an active call
    if (current_tg != 0) {
        update_audio_for_talkgroup(current_tg);
    }
}

/**
 * Set the list of talkgroups for filtering
 * @param talkgroups Array of talkgroup IDs
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetFilterTalkgroups(
    JNIEnv* env,
    jobject thiz,
    jintArray talkgroups) {
    
    int current_tg = 0;
    
    {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        g_filter_talkgroups.clear();
        
        if (talkgroups != nullptr) {
            jsize len = env->GetArrayLength(talkgroups);
            jint* tgs = env->GetIntArrayElements(talkgroups, nullptr);
            
            for (jsize i = 0; i < len; i++) {
                g_filter_talkgroups.insert(tgs[i]);
            }
            
            env->ReleaseIntArrayElements(talkgroups, tgs, 0);
            LOGI("Filter talkgroups updated: %zu entries", g_filter_talkgroups.size());
        } else {
            LOGI("Filter talkgroups cleared");
        }
        
        current_tg = g_last_tg;
    } // Mutex released here
    
    // Apply filter change immediately if there's an active call
    if (current_tg != 0) {
        update_audio_for_talkgroup(current_tg);
    }
}

/**
 * Add a single talkgroup to the filter list
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeAddFilterTalkgroup(
    JNIEnv* env,
    jobject thiz,
    jint talkgroup) {
    
    int current_tg = 0;
    
    {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        g_filter_talkgroups.insert(talkgroup);
        LOGI("Added TG %d to filter list (now %zu entries)", talkgroup, g_filter_talkgroups.size());
        current_tg = g_last_tg;
    } // Mutex released here
    
    // Apply filter change immediately if it affects current call
    if (current_tg == talkgroup) {
        update_audio_for_talkgroup(current_tg);
    }
}

/**
 * Remove a single talkgroup from the filter list
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeRemoveFilterTalkgroup(
    JNIEnv* env,
    jobject thiz,
    jint talkgroup) {
    
    int current_tg = 0;
    
    {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        g_filter_talkgroups.erase(talkgroup);
        LOGI("Removed TG %d from filter list (now %zu entries)", talkgroup, g_filter_talkgroups.size());
        current_tg = g_last_tg;
    } // Mutex released here
    
    // Apply filter change immediately if it affects current call
    if (current_tg == talkgroup) {
        update_audio_for_talkgroup(current_tg);
    }
}

/**
 * Clear all talkgroups from the filter list
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeClearFilterTalkgroups(
    JNIEnv* env,
    jobject thiz) {
    
    int current_tg = 0;
    
    {
        std::lock_guard<std::mutex> lock(g_filter_mutex);
        g_filter_talkgroups.clear();
        LOGI("Filter talkgroups cleared");
        current_tg = g_last_tg;
    } // Mutex released here
    
    // Apply filter change immediately if there's an active call
    if (current_tg != 0) {
        update_audio_for_talkgroup(current_tg);
    }
}

/**
 * Get the current filter mode
 */
extern "C" JNIEXPORT jint JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeGetFilterMode(
    JNIEnv* env,
    jobject thiz) {
    
    std::lock_guard<std::mutex> lock(g_filter_mutex);
    return static_cast<jint>(g_filter_mode);
}

// ============================================================================
// Custom DSD Command Arguments
// ============================================================================

/**
 * Set custom DSD command line arguments
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetCustomArgs(
    JNIEnv* env,
    jobject thiz,
    jstring args) {
    
    std::lock_guard<std::mutex> lock(g_custom_args_mutex);
    
    if (args == nullptr) {
        g_custom_args.clear();
        LOGI("Cleared custom DSD args");
        return;
    }
    
    const char* args_cstr = env->GetStringUTFChars(args, nullptr);
    g_custom_args = args_cstr;
    env->ReleaseStringUTFChars(args, args_cstr);
    
    LOGI("Set custom DSD args: %s", g_custom_args.c_str());
}

// ============================================================================
// Native USB RTL-SDR Support
// ============================================================================

#ifdef NATIVE_RTLSDR_ENABLED

/**
 * Check if native RTL-SDR USB support is available
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeIsRtlSdrSupported(
    JNIEnv* env,
    jobject thiz) {
    return JNI_TRUE;
}

/**
 * Open RTL-SDR device using Android USB file descriptor
 * 
 * @param fd USB file descriptor from UsbDeviceConnection.getFileDescriptor()
 * @param devicePath USB device path from UsbDevice.getDeviceName()
 * @param frequency Initial center frequency in Hz
 * @param sampleRate Sample rate in Hz
 * @param gain Gain in tenths of dB (e.g., 480 = 48.0 dB), or 0 for auto
 * @param ppm Frequency correction in PPM
 * @return true on success, false on failure
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeOpenRtlSdrUsb(
    JNIEnv* env,
    jobject thiz,
    jint fd,
    jstring devicePath,
    jlong frequency,
    jint sampleRate,
    jint gain,
    jint ppm,
    jint bias_tee) {
    
    LOGI("Configuring native RTL-SDR USB: fd=%d, freq=%lld, rate=%d, gain=%d, ppm=%d, bias_tee=%d",
         fd, (long long)frequency, sampleRate, gain, ppm, bias_tee);
    
    if (!g_opts) {
        LOGE("DSD not initialized - call nativeInit first");
        return JNI_FALSE;
    }
    
    const char* path = env->GetStringUTFChars(devicePath, nullptr);
    if (!path) {
        LOGE("Failed to get device path string");
        return JNI_FALSE;
    }
    
    // Configure opts for Android native USB mode
    g_opts->rtl_android_usb_fd = fd;
    strncpy(g_opts->rtl_android_usb_path, path, sizeof(g_opts->rtl_android_usb_path) - 1);
    g_opts->rtl_android_usb_path[sizeof(g_opts->rtl_android_usb_path) - 1] = '\0';
    
    // Set RTL input parameters
    g_opts->rtlsdr_center_freq = (uint32_t)frequency;
    g_opts->rtl_gain_value = gain;
    g_opts->rtlsdr_ppm_error = ppm;
    g_opts->rtl_bias_tee = bias_tee;
    g_opts->rtltcp_enabled = 0;  // Not using rtl_tcp
    g_opts->audio_in_type = AUDIO_IN_RTL;
    
    // DSP parameters (same as rtl_tcp mode)
    g_opts->rtl_dsp_bw_khz = 48;  // Full bandwidth
    g_opts->rtl_squelch_level = 0;  // Disabled - wide open for digital
    g_opts->rtl_volume_multiplier = 2;
    
    // Set up audio_in_dev string for RTL mode (not rtltcp)
    snprintf(g_opts->audio_in_dev, sizeof(g_opts->audio_in_dev), "rtl");
    
    // Audio output configuration - stereo for P25 Phase 2 TDMA support
    snprintf(g_opts->audio_out_dev, sizeof(g_opts->audio_out_dev), "android");
    g_opts->audio_out_type = 0;
    g_opts->audio_out = 1;
    g_opts->pulse_digi_rate_out = 8000;
    g_opts->pulse_digi_out_channels = 2;  // Stereo for P25 Phase 2 dual-slot support
    
    // Disable slot 2 to avoid Reed-Solomon errors causing choppy audio
    g_opts->slot1_on = 1;
    g_opts->slot2_on = 0;
    g_opts->slot_preference = 0;  // Prefer slot 1
    
    // Disable P25 trunk following by default (enable it when needed via setTrunkFollowing)
    g_opts->p25_trunk = 0;
    
    env->ReleaseStringUTFChars(devicePath, path);
    
    LOGI("Native USB RTL-SDR configured: path=%s, fd=%d", 
         g_opts->rtl_android_usb_path, g_opts->rtl_android_usb_fd);
    
    return JNI_TRUE;
}

/**
 * Close native RTL-SDR USB device
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeCloseRtlSdrUsb(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Clearing native RTL-SDR USB configuration");
    
    if (g_opts) {
        g_opts->rtl_android_usb_fd = -1;
        g_opts->rtl_android_usb_path[0] = '\0';
    }
}

/**
 * Set frequency on native RTL-SDR device (updates opts for next engine run)
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetRtlSdrFrequency(
    JNIEnv* env,
    jobject thiz,
    jlong frequency) {
    
    if (!g_opts) {
        LOGE("DSD not initialized");
        return JNI_FALSE;
    }
    
    g_opts->rtlsdr_center_freq = (uint32_t)frequency;
    LOGI("Set frequency to %lld Hz in opts", (long long)frequency);
    return JNI_TRUE;
}

/**
 * Set gain on native RTL-SDR device (updates opts for next engine run)
 */
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetRtlSdrGain(
    JNIEnv* env,
    jobject thiz,
    jint gain) {
    
    if (!g_opts) {
        LOGE("DSD not initialized");
        return JNI_FALSE;
    }
    
    g_opts->rtl_gain_value = gain;
    LOGI("Set gain to %d tenths dB in opts", gain);
    return JNI_TRUE;
}

#else // !NATIVE_RTLSDR_ENABLED

// Stub implementations when native RTL-SDR is not enabled
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeIsRtlSdrSupported(
    JNIEnv* env,
    jobject thiz) {
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeOpenRtlSdrUsb(
    JNIEnv* env,
    jobject thiz,
    jint fd,
    jstring devicePath,
    jlong frequency,
    jint sampleRate,
    jint gain,
    jint ppm,
    jint bias_tee) {
    LOGE("Native RTL-SDR support not compiled");
    return JNI_FALSE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeCloseRtlSdrUsb(
    JNIEnv* env,
    jobject thiz) {
    LOGE("Native RTL-SDR support not compiled");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetRtlSdrFrequency(
    JNIEnv* env,
    jobject thiz,
    jlong frequency) {
    LOGE("Native RTL-SDR support not compiled");
    return JNI_FALSE;
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetRtlSdrGain(
    JNIEnv* env,
    jobject thiz,
    jint gain) {
    LOGE("Native RTL-SDR support not compiled");
    return JNI_FALSE;
}

#endif // NATIVE_RTLSDR_ENABLED

// ============================================================================
// HackRF Sample Feeding Support (rtl_tcp emulation)
// ============================================================================

// TCP server thread that emulates rtl_tcp protocol for HackRF
static void* hackrf_tcp_server_thread(void* arg) {
    LOGI("HackRF TCP server thread started, waiting for connections...");
    
    while (g_hackrf_tcp_server_running) {
        // Accept incoming connection
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        LOGI("Waiting for DSD to connect on 127.0.0.1:1235...");
        int client_sock = accept(g_hackrf_tcp_server_sock, 
                                 (struct sockaddr*)&client_addr, 
                                 &client_len);
        
        if (client_sock < 0) {
            if (g_hackrf_tcp_server_running) {
                LOGE("Accept failed: %s (errno=%d)", strerror(errno), errno);
                sleep(1);
            }
            continue;
        }
        
        LOGI("DSD connected! Client socket=%d", client_sock);
        g_hackrf_tcp_client_sock = client_sock;
        
        // Send rtl_tcp header: "RTL0" + tuner_type(4) + ngains(4)
        uint8_t header[12];
        header[0] = 'R';
        header[1] = 'T';
        header[2] = 'L';
        header[3] = '0';
        // Tuner type: 0 (unknown) - big endian
        header[4] = 0;
        header[5] = 0;
        header[6] = 0;
        header[7] = 0;
        // Number of gains: 0 - big endian
        header[8] = 0;
        header[9] = 0;
        header[10] = 0;
        header[11] = 0;
        
        ssize_t sent = send(client_sock, header, sizeof(header), 0);
        if (sent != sizeof(header)) {
            LOGE("Failed to send rtl_tcp header: sent=%zd, expected=%zu, errno=%d (%s)", 
                 sent, sizeof(header), errno, strerror(errno));
            close(client_sock);
            g_hackrf_tcp_client_sock = -1;
            continue;
        }
        
        LOGI("Sent rtl_tcp header (%zu bytes), ready for sample streaming", sizeof(header));
        
        // Keep connection alive - samples are fed via nativeFeedHackRfSamples()
        while (g_hackrf_tcp_server_running && g_hackrf_tcp_client_sock == client_sock) {
            // Check if client is still connected
            uint8_t dummy;
            ssize_t n = recv(client_sock, &dummy, 1, MSG_DONTWAIT);
            if (n == 0) {
                LOGI("Client disconnected cleanly");
                break;
            } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                LOGI("Client disconnected with error: %s (errno=%d)", strerror(errno), errno);
                break;
            }
            usleep(100000); // 100ms
        }
        
        if (g_hackrf_tcp_client_sock == client_sock) {
            close(client_sock);
            g_hackrf_tcp_client_sock = -1;
        }
        LOGI("Client connection closed");
    }
    
    LOGI("HackRF TCP server thread exiting");
    return nullptr;
}

// Start HackRF mode - creates a TCP server for rtl_tcp emulation
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeStartHackRfMode(
    JNIEnv* env,
    jobject thiz,
    jlong frequency,
    jint sampleRate) {
    
    LOGI("Starting HackRF mode: freq=%lld Hz, sampleRate=%d Hz", (long long)frequency, sampleRate);
    
    // Create TCP server socket
    g_hackrf_tcp_server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (g_hackrf_tcp_server_sock < 0) {
        LOGE("Failed to create TCP socket: %s", strerror(errno));
        return JNI_FALSE;
    }
    
    // Set socket options
    int opt = 1;
    if (setsockopt(g_hackrf_tcp_server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        LOGE("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
    }
    
    // Bind to localhost:1235
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    server_addr.sin_port = htons(1235);
    
    if (bind(g_hackrf_tcp_server_sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        LOGE("Failed to bind TCP socket: %s", strerror(errno));
        close(g_hackrf_tcp_server_sock);
        g_hackrf_tcp_server_sock = -1;
        return JNI_FALSE;
    }
    
    // Listen for connections
    if (listen(g_hackrf_tcp_server_sock, 1) < 0) {
        LOGE("Failed to listen on TCP socket: %s", strerror(errno));
        close(g_hackrf_tcp_server_sock);
        g_hackrf_tcp_server_sock = -1;
        return JNI_FALSE;
    }
    
    LOGI("HackRF TCP server listening on 127.0.0.1:1235");
    
    // Start TCP server thread
    g_hackrf_tcp_server_running = true;
    if (pthread_create(&g_hackrf_tcp_server_thread, nullptr, hackrf_tcp_server_thread, nullptr) != 0) {
        LOGE("Failed to create HackRF TCP server thread");
        close(g_hackrf_tcp_server_sock);
        g_hackrf_tcp_server_sock = -1;
        g_hackrf_tcp_server_running = false;
        return JNI_FALSE;
    }
    
    // Give server thread time to start accepting connections
    usleep(100000); // 100ms
    LOGI("HackRF TCP server thread started and ready");
    
    // Initialize DSD options if not already done
    if (!g_opts) {
        g_opts = (dsd_opts*)calloc(1, sizeof(dsd_opts));
        if (!g_opts) {
            LOGE("Failed to allocate opts");
            g_hackrf_tcp_server_running = false;
            pthread_join(g_hackrf_tcp_server_thread, nullptr);
            close(g_hackrf_tcp_server_sock);
            g_hackrf_tcp_server_sock = -1;
            return JNI_FALSE;
        }
        initOpts(g_opts);
    }
    
    if (!g_state) {
        g_state = (dsd_state*)calloc(1, sizeof(dsd_state));
        if (!g_state) {
            LOGE("Failed to allocate state");
            g_hackrf_tcp_server_running = false;
            pthread_join(g_hackrf_tcp_server_thread, nullptr);
            close(g_hackrf_tcp_server_sock);
            g_hackrf_tcp_server_sock = -1;
            return JNI_FALSE;
        }
        initState(g_state);
    }
    
    // Configure for HackRF input via rtl_tcp emulation
    g_opts->audio_in_type = AUDIO_IN_RTL;
    snprintf(g_opts->audio_in_dev, sizeof(g_opts->audio_in_dev), "rtltcp");
    
    // Set RTL-TCP mode to read HackRF samples from localhost
    g_opts->rtltcp_enabled = 1;
    strcpy(g_opts->rtltcp_hostname, "127.0.0.1");
    g_opts->rtltcp_portno = 1235; // Use non-standard port to avoid conflicts
    
    // Set RTL parameters for HackRF - HackRF sends raw IQ that needs FM demod
    g_opts->rtlsdr_center_freq = (uint32_t)frequency;
    g_opts->rtl_gain_value = 0; // Will be set via separate calls
    g_opts->rtlsdr_ppm_error = 0;
    
    // DSP parameters for FM demodulation
    g_opts->rtl_dsp_bw_khz = 48;  // Full bandwidth
    g_opts->rtl_squelch_level = 0;  // Disabled - wide open for digital
    g_opts->rtl_volume_multiplier = 2;
    
    LOGI("HackRF configured: rtl_tcp mode on %s:%d", g_opts->rtltcp_hostname, g_opts->rtltcp_portno);
    
    // Audio output configuration - stereo for P25 Phase 2 TDMA support
    snprintf(g_opts->audio_out_dev, sizeof(g_opts->audio_out_dev), "android");
    g_opts->audio_out_type = 0;
    g_opts->audio_out = 1;
    g_opts->pulse_digi_rate_out = 8000;
    g_opts->pulse_digi_out_channels = 2;  // Stereo for P25 Phase 2 dual-slot support
    
    // DSP settings - enable all digital modes
    g_opts->mod_c4fm = 1;
    g_opts->mod_qpsk = 1;
    g_opts->mod_gfsk = 1;
    g_opts->frame_p25p1 = 1;
    g_opts->frame_p25p2 = 1;
    g_opts->frame_dmr = 1;
    g_opts->frame_nxdn48 = 1;
    g_opts->frame_nxdn96 = 1;
    g_opts->frame_dstar = 1;
    
    // Disable slot 2 to avoid Reed-Solomon errors
    g_opts->slot1_on = 1;
    g_opts->slot2_on = 0;
    g_opts->slot_preference = 0;
    
    // Disable P25 trunk following by default (enable it when needed via setTrunkFollowing)
    g_opts->p25_trunk = 0;
    g_opts->p25_is_tuned = 1;
    
    g_hackrf_mode = true;
    
    LOGI("HackRF mode configured successfully");
    return JNI_TRUE;
}

// Get the HackRF TCP server status
extern "C" JNIEXPORT jint JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeGetHackRfPipeFd(
    JNIEnv* env,
    jobject thiz) {
    
    // Return the server socket FD (for compatibility, though not used for writing)
    return g_hackrf_tcp_server_sock;
}

// Feed samples from HackRF into DSD via TCP
extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeFeedHackRfSamples(
    JNIEnv* env,
    jobject thiz,
    jbyteArray samples) {
    
    if (!g_hackrf_mode || g_hackrf_tcp_client_sock < 0) {
        // No client connected yet, just drop samples
        return JNI_TRUE;
    }
    
    jsize len = env->GetArrayLength(samples);
    if (len == 0) {
        return JNI_TRUE;
    }
    
    jbyte* buffer = env->GetByteArrayElements(samples, nullptr);
    if (!buffer) {
        LOGE("Failed to get sample buffer");
        return JNI_FALSE;
    }
    
    // HackRF sends signed 8-bit samples (-128 to +127)
    // rtl_tcp expects unsigned 8-bit samples (0 to 255)
    // Convert: unsigned = signed + 128
    uint8_t* converted = (uint8_t*)malloc(len);
    if (!converted) {
        env->ReleaseByteArrayElements(samples, buffer, JNI_ABORT);
        return JNI_FALSE;
    }
    
    for (jsize i = 0; i < len; i++) {
        converted[i] = (uint8_t)(buffer[i] + 128);
    }
    
    // Write converted samples to TCP client
    ssize_t written = send(g_hackrf_tcp_client_sock, converted, len, MSG_NOSIGNAL);
    
    free(converted);
    env->ReleaseByteArrayElements(samples, buffer, JNI_ABORT);
    
    if (written < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Would block - client reading too slow
            return JNI_TRUE;
        } else if (errno == EPIPE || errno == ECONNRESET) {
            // Client disconnected
            LOGI("TCP client disconnected (errno=%d)", errno);
            close(g_hackrf_tcp_client_sock);
            g_hackrf_tcp_client_sock = -1;
            return JNI_TRUE;
        } else {
            LOGE("TCP send error: %s", strerror(errno));
            return JNI_FALSE;
        }
    }
    
    return JNI_TRUE;
}

// Stop HackRF mode
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeStopHackRfMode(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Stopping HackRF mode");
    
    g_hackrf_mode = false;
    g_hackrf_tcp_server_running = false;
    
    // Close client connection
    if (g_hackrf_tcp_client_sock >= 0) {
        close(g_hackrf_tcp_client_sock);
        g_hackrf_tcp_client_sock = -1;
    }
    
    // Close server socket
    if (g_hackrf_tcp_server_sock >= 0) {
        shutdown(g_hackrf_tcp_server_sock, SHUT_RDWR);
        close(g_hackrf_tcp_server_sock);
        g_hackrf_tcp_server_sock = -1;
    }
    
    // Wait for server thread to exit
    pthread_join(g_hackrf_tcp_server_thread, nullptr);
    
    LOGI("HackRF mode stopped");
}

// Export retune freeze flag for rtl_sdr_fm to check
extern "C" bool dsd_flutter_retune_frozen(void) {
    return g_retune_freeze.load();
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetRetuneFrozen(
    JNIEnv* env,
    jobject thiz,
    jboolean frozen) {
    
    g_retune_freeze.store(frozen);
    LOGI("Retune freeze set to: %s", frozen ? "true" : "false");
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeRetune(
    JNIEnv* env,
    jobject thiz,
    jint freqHz) {
    
    if (!g_opts || !g_engine_running) {
        LOGE("Cannot retune: DSD engine not running");
        return JNI_FALSE;
    }
    
    LOGI("Retuning to %d Hz (explicit, bypassing freeze)", freqHz);
    
    // Temporarily unfreeze for this explicit retune
    bool was_frozen = g_retune_freeze.load();
    if (was_frozen) {
        g_retune_freeze.store(false);
    }
    
    // Call DSD's rtl_stream_tune function
    int result = dsd_rtl_stream_tune(g_opts, (long int)freqHz);
    
    // Restore freeze state
    if (was_frozen) {
        g_retune_freeze.store(true);
    }
    
    if (result == 0) {
        LOGI("Retune successful");
        return JNI_TRUE;
    } else {
        LOGE("Retune failed with code: %d", result);
        return JNI_FALSE;
    }
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeResetP25State(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Resetting P25 state (frequency tables and state machine)");
    
    // Reset P25 frequency identifier tables
    if (g_state) {
        LOGI("Clearing P25 frequency identifier tables");
        p25_reset_iden_tables(g_state);
    }
    
    // Reinitialize P25 trunking state machine
    if (g_opts && g_state) {
        LOGI("Reinitializing P25 trunking state machine");
        p25_sm_init(g_opts, g_state);
    }
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetBiasTee(
    JNIEnv* env,
    jobject thiz,
    jboolean enabled) {
    
    int on = enabled ? 1 : 0;
    LOGI("Setting bias-tee: %s", on ? "enabled" : "disabled");
    
    // Update opts for future connections
    if (g_opts) {
        g_opts->rtl_bias_tee = on;
    }
    
    // Apply immediately if engine is running
    if (g_engine_running) {
        int result = rtl_stream_set_bias_tee(on);
        if (result == 0) {
            LOGI("Bias-tee %s successfully", on ? "enabled" : "disabled");
            return JNI_TRUE;
        } else {
            LOGE("Failed to set bias-tee: %d", result);
            return JNI_FALSE;
        }
    }
    
    return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeSetTrunkFollowing(
    JNIEnv* env,
    jobject thiz,
    jboolean enabled) {
    
    int on = enabled ? 1 : 0;
    LOGI("Setting trunk following: %s", on ? "enabled" : "disabled");
    
    if (g_opts) {
        g_opts->p25_trunk = on;
        g_opts->trunk_enable = on;  // Keep both in sync
        LOGI("Trunk following %s", on ? "enabled" : "disabled");
    } else {
        LOGE("Cannot set trunk following: g_opts is null");
    }
}


