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

// Last known call state for change detection
static int g_last_tg = 0;
static int g_last_src = 0;

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

// Poll thread - checks state for call changes
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
                
                LOGI("Call event: type=%d tg=%d src=%d nac=0x%X slot=%d", 
                     eventType, tg, src, nac, slot);
                
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
    
    // Reset call tracking
    g_last_tg = 0;
    g_last_src = 0;
    
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
        
        // Audio output parameters - 8kHz mono is typical for decoded voice
        g_opts->pulse_digi_rate_out = 8000;
        g_opts->pulse_digi_out_channels = 1;  // Mono output
        
        // Enable P25 trunk following for control channel
        g_opts->p25_trunk = 1;
        
        LOGI("Configured for rtl_tcp input: %s", g_opts->audio_in_dev);
        LOGI("Audio output enabled: %s type=%d", g_opts->audio_out_dev, g_opts->audio_out_type);
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
    
    if (g_opts) {
        g_opts->audio_out = enabled ? 1 : 0;
        LOGI("Audio output %s", enabled ? "enabled" : "disabled");
    }
}
