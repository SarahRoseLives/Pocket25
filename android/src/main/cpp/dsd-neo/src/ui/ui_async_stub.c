// Stub UI functions for Android build - no ncurses terminal UI

#include <dsd-neo/ui/ui_async.h>

int ui_start(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    return 0; // Success - nothing to start
}

void ui_stop(void) {
    // Nothing to stop
}

int ui_post_cmd(int cmd_id, const void* payload, size_t payload_sz) {
    (void)cmd_id;
    (void)payload;
    (void)payload_sz;
    return 0;
}

int ui_drain_cmds(dsd_opts* opts, dsd_state* state) {
    (void)opts;
    (void)state;
    return 0;
}

int ui_is_thread_context(void) {
    return 0;
}
