# DSD-Neo Backend Analysis: ScanGrid Implementation Requirements

## Executive Summary

This document analyzes the DSD-Neo backend to determine what changes are required to implement OP25MCH-style scangrid functionality for ignoring/blocking specific calls on the fly in the Pocket25 application.

**Key Finding**: The DSD-Neo backend **already has all the necessary infrastructure** to support on-the-fly call ignoring. The current implementation in Pocket25 demonstrates this capability working effectively. However, there are some architectural improvements that could be made within DSD-Neo itself to make this feature more robust and flexible.

---

## Current Implementation Analysis

### 1. Existing Filtering Architecture in Pocket25

The Pocket25 application currently implements a **complete filtering system** that operates at the Flutter-to-Native bridge level:

#### Flutter UI Layer (`example/lib/main.dart`)
- Tracks muted talkgroups in a `Set<int> _mutedTalkgroups`
- Supports whitelist and blacklist modes
- Provides UI for long-press to mute/unmute talkgroups
- Syncs filter changes to native layer via method channel

#### Native Bridge Layer (`android/src/main/cpp/dsd_flutter_jni.cpp`)
- Maintains `g_filter_talkgroups` set (C++ std::set)
- Implements filter mode logic (whitelist vs blacklist)
- Provides `update_audio_for_talkgroup()` function that dynamically mutes/unmutes audio
- Thread-safe with mutex protection (`g_filter_mutex`)

#### Integration with DSD-Neo
- Directly manipulates `dsd_opts->audio_out` flag to mute/unmute
- Monitors `dsd_state->lasttg` for talkgroup changes
- Applies filtering in real-time as calls are received

**Current Flow:**
```
Call Detected (DSD-Neo) → 
JNI Bridge Monitors State → 
Check Filter List → 
Set audio_out Flag → 
Audio Muted/Unmuted
```

### 2. DSD-Neo's Native Group Filtering

DSD-Neo has its **own** built-in group filtering system that is more deeply integrated:

#### Group Array System (`include/dsd-neo/core/state.h`)
```c
typedef struct {
    unsigned long int groupNumber;
    char groupMode[8];   // "A", "B", "DE", "D"
    char groupName[50];
} groupinfo;
```

#### Group Modes
- **"A"** - Allow (explicitly whitelisted)
- **"B"** - Block (blacklisted)
- **"DE"** - Digital Encrypted (auto-locked out)
- **"D"** - Digital (informational only)

#### CSV Import System
DSD-Neo can load group lists from CSV files:
```csv
DEC,Mode(A- Allow; B - Block; DE - Digital Enc),Name of Group,Tag
100,B,Example Name,Tag
1449,A,Fire Dispatch,Fire
929,A,Fire Tac,Fire
22033,DE,Law Dispatch,Law
```

#### P25 Trunk State Machine Integration (`src/protocol/p25/p25_trunk_sm.c`)

The P25 trunk state machine has sophisticated filtering logic:

```c
// Check if TG is blocked in group array (mode "DE" or "B")
static int tg_is_blocked(const dsd_state* state, int tg) {
    if (!state || tg <= 0) return 0;
    
    for (unsigned int i = 0; i < state->group_tally; i++) {
        if (state->group_array[i].groupNumber == (unsigned long)tg) {
            const char* m = state->group_array[i].groupMode;
            return (m[0] == 'D' && m[1] == 'E') || (m[0] == 'B' && m[1] == '\0');
        }
    }
    return 0;
}

static int grant_allowed(dsd_opts* opts, dsd_state* state, const p25_sm_event_t* ev) {
    // ... other checks ...
    
    // Group list mode check (string compare - rare path)
    if (tg_is_blocked(state, tg)) {
        sm_log(opts, state, "grant-blocked-mode");
        return 0;  // REJECT THE GRANT - DON'T TUNE TO THIS CALL
    }
    
    return 1;
}
```

**Key Insight**: When a talkgroup is marked as "B" (blocked) or "DE" in the group array, DSD-Neo's P25 trunk state machine **refuses to tune to voice channel grants** for that talkgroup. The radio stays on the control channel and ignores the call entirely.

#### UI Integration (`src/ui/terminal/`)

The ncurses terminal UI has keyboard shortcuts to dynamically add talkgroups to the block list:
- Users can interact with active calls via keyboard commands
- The `ui_cmd_queue.c` system allows adding TGs to the group array with mode "B"
- This happens **on the fly** during operation

---

## What DSD-Neo Already Provides

### ✅ Core Capabilities Present

1. **Group Array System**: In-memory storage for talkgroup filter states
2. **CSV Import**: Load initial filter lists from files
3. **Runtime Filtering**: Block/allow logic integrated into trunk state machine
4. **Encryption Lockout**: Auto-detection and blocking of encrypted calls
5. **UI Command Queue**: Asynchronous command system for runtime changes
6. **Multi-Protocol Support**: Works with P25, DMR, NXDN (protocol-specific implementations)

### ✅ P25 Trunking Integration

The P25 trunk state machine (`p25_trunk_sm.c`) has **complete** integration:
- Checks group mode before tuning to voice grants
- Respects both user-defined blocks ("B") and auto-detected encryption ("DE")
- Logs all filtering decisions for debugging
- Properly handles TDMA and FDMA channels

### ⚠️ Current Limitations

1. **No Public API for Runtime Group Management**
   - Group array is in `dsd_state` but no documented API to modify it
   - UI command system exists but is terminal-specific
   - No clear way for external applications to add/remove filters at runtime

2. **No Event System for Call Detection**
   - DSD-Neo doesn't expose a callback/event system for "call detected"
   - External integrations must poll state variables
   - No notification when a call is blocked

3. **CSV-Only Persistence**
   - Group lists are loaded from CSV at startup
   - No built-in way to save runtime changes back to CSV
   - Changes are lost on restart unless externally persisted

4. **Protocol-Specific Implementations**
   - P25 has full trunk integration
   - DMR has some filtering in `dmr_flco.c`
   - Other protocols have varying levels of support

---

## OP25MCH ScanGrid Context

Based on web research, OP25MCH's scangrid provides:
- **Visual grid of talkgroups** showing activity status
- **Tap-to-lock** functionality to focus on specific talkgroups
- **Tap-to-ignore** functionality to skip unwanted talkgroups
- **Real-time updates** as calls come and go
- **Scanner-like experience** familiar to users of commercial scanners

The key feature relevant to this analysis is **"ignore specific calls on the fly"** - the ability to hear a call, decide you don't want to hear that talkgroup anymore, and mute it with a single tap.

---

## Required Changes to DSD-Neo

### Priority 1: Public API for Runtime Group Management

**Current State**: The group array exists but can only be modified by:
1. CSV import at startup
2. Internal protocol handlers auto-adding "DE" entries
3. Terminal UI keyboard commands (not reusable)

**Required Change**: Add a public API in `dsd-neo/core/` for external applications:

```c
// Proposed API in include/dsd-neo/core/group_management.h

/**
 * Add or update a talkgroup in the filter list
 * @param state DSD state structure
 * @param tg Talkgroup number
 * @param mode Filter mode: "A" (allow), "B" (block), "DE" (encrypted)
 * @param name Optional display name (can be NULL)
 * @return 0 on success, -1 on error
 */
int dsd_group_set(dsd_state* state, unsigned long tg, const char* mode, const char* name);

/**
 * Remove a talkgroup from the filter list
 * @param state DSD state structure
 * @param tg Talkgroup number
 * @return 0 on success, -1 if not found
 */
int dsd_group_remove(dsd_state* state, unsigned long tg);

/**
 * Get the current mode for a talkgroup
 * @param state DSD state structure
 * @param tg Talkgroup number
 * @return Mode string ("A", "B", "DE", "") or NULL if not found
 */
const char* dsd_group_get_mode(dsd_state* state, unsigned long tg);

/**
 * Clear all user-defined group filters (preserves auto-detected "DE" entries)
 * @param state DSD state structure
 * @param preserve_encrypted If true, keep "DE" entries
 */
void dsd_group_clear(dsd_state* state, int preserve_encrypted);

/**
 * Export current group list to CSV file
 * @param state DSD state structure
 * @param filepath Path to output CSV file
 * @return 0 on success, -1 on error
 */
int dsd_group_export_csv(dsd_state* state, const char* filepath);
```

**Implementation Notes**:
- Add to `src/core/group_management.c` (new file)
- Use existing `state->group_array` and `state->group_tally`
- Thread-safe with mutex protection
- Validate inputs (mode must be "A", "B", "DE", or "D")
- Handle array capacity limits gracefully

**Rationale**: This API would allow Pocket25 (and other integrations) to directly manipulate DSD-Neo's native filtering system instead of working around it at the JNI layer.

---

### Priority 2: Event/Callback System for Call State Changes

**Current State**: External applications must poll `dsd_state` variables to detect:
- New calls starting (`lasttg`, `lastsrc` changes)
- Calls ending (no explicit notification)
- Calls being blocked (no notification)

**Required Change**: Add an event callback system:

```c
// Proposed API in include/dsd-neo/core/events_api.h

typedef enum {
    DSD_EVENT_CALL_START,      // New call detected
    DSD_EVENT_CALL_UPDATE,     // Call parameters changed
    DSD_EVENT_CALL_END,        // Call ended
    DSD_EVENT_CALL_BLOCKED,    // Call was blocked by filter
    DSD_EVENT_GRANT_RECEIVED,  // Trunk grant received (before filter)
} dsd_event_type_t;

typedef struct {
    dsd_event_type_t type;
    int protocol;              // P25_P1, P25_P2, DMR, etc.
    unsigned long tg;          // Talkgroup number
    unsigned long src;         // Source radio ID
    int slot;                  // Slot number (for TDMA)
    int nac;                   // NAC/Color Code/RAN
    int encrypted;             // 0=clear, 1=encrypted
    int emergency;             // 0=normal, 1=emergency
    int block_reason;          // For CALL_BLOCKED events
    char system_info[256];     // WACN:SYS:SITE or similar
} dsd_call_event_t;

typedef void (*dsd_event_callback_t)(const dsd_call_event_t* event, void* user_data);

/**
 * Register a callback for call events
 * @param opts DSD options structure
 * @param callback Function to call on events
 * @param user_data Opaque pointer passed to callback
 */
void dsd_register_event_callback(dsd_opts* opts, dsd_event_callback_t callback, void* user_data);
```

**Implementation Notes**:
- Store callback pointer in `dsd_opts`
- Call from P25 trunk state machine when grants are received/blocked
- Call from protocol handlers when calls start/end
- Call from audio state machine when call ends due to hangtime
- Thread-safety: callbacks executed on decoder thread (document this clearly)

**Rationale**: This would eliminate the need for Pocket25's JNI layer to continuously poll state variables. Events would be pushed immediately, reducing latency and CPU usage.

---

### Priority 3: Improved Encryption Lockout Control

**Current State**: Encryption lockout is controlled by:
1. `opts->trunk_tune_enc_calls` flag (0=lock out, 1=allow)
2. Auto-detection adds "DE" mode to group array
3. No per-TG override capability

**Required Change**: Add per-talkgroup encryption policy:

```c
// In dsd_opts structure, add:
int trunk_enc_policy;  // 0=block_all, 1=block_unknown, 2=allow_all

// In group_management API:
/**
 * Set encryption policy for a specific talkgroup
 * @param state DSD state structure
 * @param tg Talkgroup number
 * @param allow_encrypted 1=allow encrypted calls, 0=block
 */
int dsd_group_set_enc_policy(dsd_state* state, unsigned long tg, int allow_encrypted);
```

**Implementation Notes**:
- Modify `grant_allowed()` in `p25_trunk_sm.c` to check per-TG policy
- Allow overriding global `trunk_tune_enc_calls` on per-TG basis
- Store policy in group array (extend structure if needed)

**Rationale**: Users may want to hear some encrypted talkgroups (e.g., if they have keys) while blocking others. Current implementation is all-or-nothing.

---

### Priority 4: Multi-Protocol Consistency

**Current State**: Filtering is well-implemented for P25 trunking, but:
- DMR has partial support in `dmr_flco.c`
- NXDN has partial support in `nxdn_element.c`
- Conventional modes may not respect group filters

**Required Change**: Ensure consistent filtering across all protocols:

1. **Audit all protocol handlers** to ensure they call `tg_is_blocked()` or equivalent
2. **Add filtering to DMR trunk state machine** (if one exists or is planned)
3. **Document protocol-specific behavior** (e.g., conventional vs. trunked)

**Implementation Notes**:
- May require refactoring to create a common filtering layer
- Consider moving `tg_is_blocked()` from `p25_trunk_sm.c` to `core/`
- Add filtering hooks to `src/protocol/dmr/`, `src/protocol/nxdn/`, etc.

**Rationale**: Users expect consistent behavior regardless of protocol. A TG blocked in P25 should also be blocked in DMR.

---

### Priority 5: Runtime Persistence

**Current State**: 
- Group lists loaded from CSV at startup via `-G group.csv`
- Runtime changes (added blocks, encryption lockouts) are not saved
- Changes lost on application restart

**Required Change**: Add auto-save and reload capability:

```c
// Add to dsd_opts:
char group_list_file[512];  // Path to CSV file
int group_autosave;         // 1=save on changes, 0=manual only

// In group_management API:
/**
 * Enable auto-save of group list changes
 * @param state DSD state structure
 * @param filepath Path to CSV file (overwrites on changes)
 * @param enable 1=enable auto-save, 0=disable
 */
int dsd_group_autosave_enable(dsd_state* state, const char* filepath, int enable);

/**
 * Save current group list to file (manual save)
 * @param state DSD state structure
 * @return 0 on success, -1 on error
 */
int dsd_group_save(dsd_state* state);
```

**Implementation Notes**:
- Write CSV atomically (temp file + rename)
- Only save user-defined entries (preserve vs. auto-detected "DE")
- Debounce writes to avoid excessive I/O (e.g., max once per second)
- Handle file errors gracefully (log warning, continue operation)

**Rationale**: Users want their ignore/block lists to persist across restarts. This is essential for a good UX.

---

## Alternative Approach: Keep External Layer

**If DSD-Neo changes are not feasible**, Pocket25's current approach can be enhanced:

### Advantages of Current External Approach
1. ✅ **No DSD-Neo modifications required** - works with vanilla DSD-Neo
2. ✅ **Application-specific logic** - Flutter layer has full control
3. ✅ **Database persistence** - SQLite handles storage elegantly
4. ✅ **Already working** - proven implementation in production

### Enhancements to External Approach

If keeping the external filtering layer, consider:

1. **More granular state monitoring**:
   - Monitor `state->p25_p2_audio_allowed[]` for slot-level control
   - Check `state->DMRvcL` and `state->DMRvcR` for DMR voice state
   - Use `state->currentslot` for TDMA tracking

2. **Proactive grant interception**:
   - Monitor P25 trunk state machine's grant queue
   - Block grants before tune happens (requires DSD-Neo API)
   - Currently can only mute after tune, which wastes a retune cycle

3. **Bi-directional sync**:
   - Sync Pocket25 filter list → DSD-Neo group array
   - This would make both systems consistent
   - Requires implementing Priority 1 API above

---

## Comparison: Current vs. Proposed

| Feature | Current Pocket25 | With DSD-Neo API |
|---------|------------------|------------------|
| Runtime add/remove filters | ✅ JNI layer | ✅ Native API |
| Audio muting | ✅ `audio_out` flag | ✅ Grant rejection |
| Call detection | ⚠️ Polling state | ✅ Event callbacks |
| Persistence | ✅ SQLite database | ✅ CSV auto-save |
| Multi-protocol | ⚠️ P25-focused | ✅ All protocols |
| Encryption per-TG | ❌ Global only | ✅ Per-TG policy |
| Trunk efficiency | ⚠️ Mute after tune | ✅ Reject before tune |
| Reusable by others | ❌ Pocket25-specific | ✅ Any application |

---

## Recommended Implementation Plan

### Phase 1: Enhance Current Pocket25 Implementation (No DSD-Neo changes)
**Timeline**: Immediate  
**Effort**: Low

1. Sync Pocket25 filter list to DSD-Neo's group array at startup
   - Use existing CSV import functionality
   - Generate CSV from SQLite database
   - Load via `-G` flag when starting DSD-Neo

2. Monitor additional state variables for better protocol support
   - Add DMR-specific filtering in JNI layer
   - Handle NXDN calls properly

3. Document current limitations
   - Note that filtering happens after tune (inefficient)
   - Explain why blocked calls briefly tune away from control channel

**Outcome**: Improved current system with no upstream changes required.

---

### Phase 2: Contribute Priority 1 API to DSD-Neo (Upstream contribution)
**Timeline**: 1-2 weeks for design/implementation  
**Effort**: Medium

1. Propose API design to DSD-Neo maintainers
   - Create GitHub issue with API proposal
   - Get feedback on design and naming

2. Implement core API functions
   - Add `src/core/group_management.c`
   - Add `include/dsd-neo/core/group_management.h`
   - Include thread safety (mutex)
   - Write unit tests in `tests/core/`

3. Update P25 trunk state machine
   - Refactor `tg_is_blocked()` to use new API
   - Ensure backward compatibility with existing code

4. Submit pull request to DSD-Neo
   - Include documentation
   - Include example usage
   - Work with maintainer on code review

**Outcome**: DSD-Neo gains public API for group management. Pocket25 can use this API directly in future versions.

---

### Phase 3: Contribute Priority 2 Event System to DSD-Neo (Upstream contribution)
**Timeline**: 2-3 weeks for design/implementation  
**Effort**: Medium-High

1. Design event system architecture
   - Consider performance impact
   - Ensure thread safety
   - Design for extensibility (future event types)

2. Implement event infrastructure
   - Add callback registration to `dsd_opts`
   - Add event structure definitions
   - Add documentation for callback thread safety

3. Integrate with protocol handlers
   - P25 trunk state machine (grants, blocks)
   - DMR handlers
   - NXDN handlers
   - Conventional mode handlers

4. Submit pull request to DSD-Neo
   - Include performance benchmarks
   - Include example integration
   - Document threading model clearly

**Outcome**: DSD-Neo gains event system. Pocket25 can eliminate polling and get instant notifications.

---

### Phase 4: Integrate DSD-Neo APIs into Pocket25 (Pocket25 enhancement)
**Timeline**: 1-2 weeks  
**Effort**: Medium

1. Update JNI bridge to use new DSD-Neo APIs
   - Call `dsd_group_set()` instead of direct state manipulation
   - Register event callback instead of polling
   - Remove now-redundant code

2. Update Flutter integration
   - Simplify state management (events vs. polling)
   - Improve responsiveness
   - Reduce CPU usage

3. Add CSV export feature to UI
   - "Export Block List" button
   - Saves current filters to DSD-Neo-compatible CSV

**Outcome**: Pocket25 uses DSD-Neo's native filtering system directly. More efficient, cleaner code, better integration.

---

## Conclusion

### Summary of Findings

1. **DSD-Neo already has the core infrastructure** for scangrid-style call filtering
2. **The filtering works at the trunk state machine level** - blocked calls are never tuned to
3. **Pocket25's current implementation is functional** but works around DSD-Neo rather than with it
4. **Modest API additions to DSD-Neo** would benefit all integrations, not just Pocket25

### What Must Be Changed in DSD-Neo

**Minimum viable changes** (to support on-the-fly call ignoring):

1. ✅ **Already works** - group array filtering exists and functions
2. ✅ **Already works** - P25 trunk integration is complete
3. ❌ **Missing** - Public API for runtime group management (Priority 1)
4. ❌ **Missing** - Event system for call detection (Priority 2)

**Additional improvements** (for completeness):

5. ⚠️ **Partial** - Multi-protocol consistency (Priority 4)
6. ❌ **Missing** - Runtime persistence/auto-save (Priority 5)
7. ❌ **Missing** - Per-TG encryption policy (Priority 3)

### Recommended Approach

**For immediate needs**: Enhance Pocket25's existing external filtering (Phase 1)  
**For long-term solution**: Contribute APIs to DSD-Neo (Phases 2-3), then integrate (Phase 4)

### Final Assessment

The question "what must be changed in DSD-Neo itself" has a nuanced answer:

- **Strictly speaking**: Nothing - it already works via external manipulation
- **For proper integration**: Add Priority 1 API (group management)
- **For optimal UX**: Add Priority 2 API (event system)
- **For feature completeness**: Add Priorities 3-5

The current Pocket25 implementation demonstrates that on-the-fly call ignoring is **fully functional** with DSD-Neo as-is. The proposed changes would make it cleaner, more efficient, and reusable by other projects.

---

## References

### DSD-Neo Source Files Analyzed
- `include/dsd-neo/core/state.h` - Group array structure
- `include/dsd-neo/core/opts.h` - Options structure
- `src/protocol/p25/p25_trunk_sm.c` - P25 trunk filtering logic
- `src/ui/terminal/ui_cmd_queue.c` - UI command system
- `include/dsd-neo/ui/ui_cmd.h` - Command ID definitions
- `examples/group.csv` - CSV format reference

### Pocket25 Source Files Analyzed
- `android/src/main/cpp/dsd_flutter_jni.cpp` - JNI bridge filtering
- `example/lib/main.dart` - Flutter UI filtering
- `example/lib/services/database_service.dart` - Filter persistence

### External References
- OP25MCH GitHub: https://github.com/SarahRoseLives/OP25MCH
- DSD-Neo GitHub: https://github.com/arancormonk/dsd-neo
