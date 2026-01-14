#include <jni.h>
#include <android/log.h>
#include <string>
#include <pthread.h>
#include <cstdio>
#include <unistd.h>
#include <cstring>

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
}

// Native RTL-SDR USB support (when enabled)
#ifdef NATIVE_RTLSDR_ENABLED
#include <rtl-sdr.h>
#include <rtl-sdr-android.h>
#include <dsd-neo/io/rtl_device.h>
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

// Last known site state for change detection
static unsigned long long g_last_wacn = 0;
static unsigned long long g_last_siteid = 0;
static unsigned long long g_last_rfssid = 0;
static int g_last_nac = 0;

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
    
    jstring jtext = env->NewStringUTF(text);
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
    jlong freq_hz) {
    
    const char* host_str = env->GetStringUTFChars(host, nullptr);
    
    LOGI("Configuring rtl_tcp at %s:%d freq=%lld Hz", host_str, port, (long long)freq_hz);
    
    if (g_opts) {
        // Set up rtl_tcp input string: rtltcp:host:port:freq:gain:ppm:bw:sql:vol
        // Format: rtltcp:hostname:port:freq:gain:ppm:bw:sql:vol
        // Squelch 0 = disabled (wide open for digital)
        snprintf(g_opts->audio_in_dev, sizeof(g_opts->audio_in_dev),
                 "rtltcp:%s:%d:%lld:48:0:48:0:2", 
                 host_str, port, (long long)freq_hz);
        
        // Also set individual options
        snprintf(g_opts->rtltcp_hostname, sizeof(g_opts->rtltcp_hostname), "%s", host_str);
        g_opts->rtltcp_portno = port;
        g_opts->rtltcp_enabled = 1;
        g_opts->rtlsdr_center_freq = (uint32_t)freq_hz;
        g_opts->rtl_gain_value = 48;
        g_opts->rtlsdr_ppm_error = 0;
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
        
        // Enable P25 trunk following for control channel
        g_opts->p25_trunk = 1;
        
        LOGI("Configured for rtl_tcp input: %s", g_opts->audio_in_dev);
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
    
    if (g_opts && g_state) {
        // Log config before starting
        LOGI("Config: audio_in_dev=%s", g_opts->audio_in_dev);
        LOGI("Config: audio_in_type=%d (RTL=%d)", g_opts->audio_in_type, AUDIO_IN_RTL);
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
    
    std::lock_guard<std::mutex> lock(g_filter_mutex);
    g_filter_mode = static_cast<FilterMode>(mode);
    LOGI("Filter mode set to: %d", mode);
    
    // Re-evaluate current call if active
    if (g_last_tg != 0) {
        update_audio_for_talkgroup(g_last_tg);
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
    
    // Re-evaluate current call if active
    if (g_last_tg != 0) {
        update_audio_for_talkgroup(g_last_tg);
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
    
    std::lock_guard<std::mutex> lock(g_filter_mutex);
    g_filter_talkgroups.insert(talkgroup);
    LOGI("Added TG %d to filter list (now %zu entries)", talkgroup, g_filter_talkgroups.size());
    
    // Re-evaluate current call if it matches
    if (g_last_tg == talkgroup) {
        update_audio_for_talkgroup(g_last_tg);
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
    
    std::lock_guard<std::mutex> lock(g_filter_mutex);
    g_filter_talkgroups.erase(talkgroup);
    LOGI("Removed TG %d from filter list (now %zu entries)", talkgroup, g_filter_talkgroups.size());
    
    // Re-evaluate current call if it matches
    if (g_last_tg == talkgroup) {
        update_audio_for_talkgroup(g_last_tg);
    }
}

/**
 * Clear all talkgroups from the filter list
 */
extern "C" JNIEXPORT void JNICALL
Java_com_example_dsd_1flutter_DsdFlutterPlugin_nativeClearFilterTalkgroups(
    JNIEnv* env,
    jobject thiz) {
    
    std::lock_guard<std::mutex> lock(g_filter_mutex);
    g_filter_talkgroups.clear();
    LOGI("Filter talkgroups cleared");
    
    // Re-evaluate current call if active
    if (g_last_tg != 0) {
        update_audio_for_talkgroup(g_last_tg);
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
    jint ppm) {
    
    LOGI("Configuring native RTL-SDR USB: fd=%d, freq=%lld, rate=%d, gain=%d, ppm=%d",
         fd, (long long)frequency, sampleRate, gain, ppm);
    
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
    
    // Enable P25 trunk following
    g_opts->p25_trunk = 1;
    
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
    jint ppm) {
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
