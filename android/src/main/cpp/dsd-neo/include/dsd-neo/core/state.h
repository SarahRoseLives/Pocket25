// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief Core decoder state structure (`dsd_state`) and helper types.
 *
 * Hosts the full `dsd_state` definition so modules needing state fields
 * can include it directly.
 */

#pragma once

#include <dsd-neo/core/state_ext.h>
#include <dsd-neo/core/state_fwd.h>

#include <stdbool.h>
#include <stdint.h>
#include <time.h>

#include <dsd-neo/fec/rs_12_9.h>

#include <dsd-neo/dsp/p25p1_heuristics.h>

/* Forward declaration for mbelib decoder state (opaque in public API). */
struct mbe_parameters;
typedef struct mbe_parameters mbe_parms;

/* Forward declaration for RTL-SDR stream context (opaque, always present in ABI) */
struct RtlSdrContext;

/* Forward declaration for Codec2 context (opaque, always present in ABI) */
struct CODEC2;

//event history (each item)
// NOLINTBEGIN(clang-analyzer-optin.performance.Padding)
// The dsd_state structure is intentionally organized by functional groups for clarity
// and cross-module stability rather than by strict padding minimization. Reordering
// hundreds of fields as suggested by the analyzer would be high-risk and harm
// readability/maintainability without measurable benefit. Suppress the padding
// warning for this aggregate while keeping all other clang-tidy checks active.
typedef struct {
    uint8_t write;      //if this event needs to be written to a log file
    uint8_t color_pair; //this value corresponds to which color pair the line should be in ncurses
    int8_t systype;     //indentifier of which decoded system type this is from (P25, DMR, etc)
    int8_t subtype;     //subtype of systpe (VLC, TLC, PDU data, System Event, etc)
    uint32_t sys_id1;   //sys_id1 through 5 will be a hierarchy of system identifiers
    uint32_t sys_id2;   //for example, trunked P25 has WACN:SYS:CC:SITE_ID:RFSS_ID
    uint32_t sys_id3;   //conventional may only use NAC, RAN, or Color Codes
    uint32_t sys_id4;   //
    uint32_t sys_id5;   //
    int8_t gi;          //group or individual
    uint8_t enc;        //clear or encrypted
    uint8_t enc_alg;    //alg if encrypted
    uint16_t enc_key;   //enc key id value, if encrypted (not key value or key variable)
    uint64_t mi;        //mi, or iv base value from OTA if provided
    uint16_t svc;       //other relevant svc opts if applicable
    uint32_t source_id; //source radio id or other source value
    uint32_t target_id; //group or individual target, or destination value
    char src_str[200];  //source, expressed as a string for M17, YSF, DSTAR, dPMR
    char tgt_str[200];  //target, expressed as a string for M17, YSF, DSTAR, dPMR
    char t_name[200];   //this is the string present from any csv groupName import
    char s_name[200];   //same as above, but if loaded from a src value and not tg value
    char t_mode[200];   //mode, or A,B,D,DE from csv group import file
    char s_mode[200];   //mode, or A,B,D,DE from csv group import file
    uint32_t channel;   //if this occurs on a trunking channel, which channel
    time_t event_time;  //time event occurred

    uint8_t pdu[128 * 24];   //relevant link control, or full PDU if data call (in bytes)
    char sysid_string[200];  //string comprised of system unique identifiers
    char alias[2000];        //if this event has a source radio talker alias or similar
    char gps_s[2000];        //gps, if returned, expressed as a string
    char text_message[2000]; //if this event is a decoded text message, then it goes here
    char event_string[2000]; //user legible and printable string for the event that happened
    char internal_str[2000]; //string that relates to a DSD-neo generated event (ENC LO, error notices, etc)
} Event_History;

//event history for number of each items above
typedef struct Event_History_I {
    Event_History Event_History_Items[255];
} Event_History_I;

//new audio filter stuff from: https://github.com/NedSimao/FilteringLibrary
typedef struct {
    float coef[2];
    float v_out[2];
} LPFilter;

typedef struct {
    float coef;
    float v_out[2];
    float v_in[2];

} HPFilter;

typedef struct {
    LPFilter lpf;
    HPFilter hpf;
    float out_in;
} PBFilter;

typedef struct {
    float alpha;
    float beta;

    float vin[3];
    float vout[3];

} NOTCHFilter;

//end new filters

//group csv import struct
typedef struct {
    unsigned long int groupNumber;
    char groupMode[8]; //char *?
    char groupName[50];
} groupinfo;

typedef struct {
    uint8_t F1;
    uint8_t F2;
    uint8_t MessageType;

    /****************************/
    /***** VCALL parameters *****/
    /****************************/
    uint8_t CCOption;
    uint8_t CallType;
    uint8_t VoiceCallOption;
    uint16_t SourceUnitID;
    uint16_t DestinationID; /* May be a Group ID or a Unit ID */
    uint8_t CipherType;
    uint8_t KeyID;
    uint8_t VCallCrcIsGood;

    /*******************************/
    /***** VCALL_IV parameters *****/
    /*******************************/
    uint8_t IV[8];
    uint8_t VCallIvCrcIsGood;

    /*****************************/
    /***** Custom parameters *****/
    /*****************************/

    /* Specifies if the "CipherType" and the "KeyID" parameter are valid
   * 1 = Valid ; 0 = CRC error */
    uint8_t CipherParameterValidity;

    /* Used on DES and AES encrypted frames */
    uint8_t PartOfCurrentEncryptedFrame; /* Could be 1 or 2 */
    uint8_t PartOfNextEncryptedFrame;    /* Could be 1 or 2 */
    uint8_t CurrentIVComputed[8];
    uint8_t NextIVComputed[8];
} NxdnElementsContent_t;

//dPMR
/* Could only be 2 or 4 */
#define NB_OF_DPMR_VOICE_FRAME_TO_DECODE 2

typedef struct {
    unsigned char RawVoiceBit[NB_OF_DPMR_VOICE_FRAME_TO_DECODE * 4][72];
    unsigned int errs1[NB_OF_DPMR_VOICE_FRAME_TO_DECODE
                       * 4]; /* 8 x errors #1 computed when demodulate the AMBE voice bit of the frame */
    unsigned int errs2[NB_OF_DPMR_VOICE_FRAME_TO_DECODE
                       * 4]; /* 8 x errors #2 computed when demodulate the AMBE voice bit of the frame */
    unsigned char AmbeBit[NB_OF_DPMR_VOICE_FRAME_TO_DECODE * 4][49]; /* 8 x 49 bit of AMBE voice of the frame */
    unsigned char CCHData[NB_OF_DPMR_VOICE_FRAME_TO_DECODE][48];
    unsigned int CCHDataHammingOk[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned char CCHDataCRC[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int CCHDataCrcOk[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned char CalledID[8];
    unsigned int CalledIDOk;
    unsigned char CallingID[8];
    unsigned int CallingIDOk;
    unsigned int FrameNumbering[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int CommunicationMode[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int Version[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int CommsFormat[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int EmergencyPriority[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int Reserved[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned char SlowData[NB_OF_DPMR_VOICE_FRAME_TO_DECODE];
    unsigned int ColorCode[NB_OF_DPMR_VOICE_FRAME_TO_DECODE / 2];
} dPMRVoiceFS2Frame_t;

struct dsd_state {
    int* dibit_buf;
    int* dibit_buf_p;
    int* dmr_payload_buf;
    int* dmr_payload_p;
    // Per-dibit reliability buffer (0..255). Aligned with dmr_payload_buf.
    uint8_t* dmr_reliab_buf;
    uint8_t* dmr_reliab_p;
    int repeat;
    short* audio_out_buf;
    short* audio_out_buf_p;
    short* audio_out_bufR;
    short* audio_out_buf_pR;
    float* audio_out_float_buf;
    float* audio_out_float_buf_p;
    float* audio_out_float_bufR;
    float* audio_out_float_buf_pR;
    float* aout_max_buf_p;
    float* aout_max_buf_pR;
    mbe_parms* cur_mp;
    mbe_parms* prev_mp;
    mbe_parms* prev_mp_enhanced;
    mbe_parms* cur_mp2;
    mbe_parms* prev_mp2;
    mbe_parms* prev_mp_enhanced2;
    // 64-bit state placed early to reduce padding
    unsigned long long int payload_mi;
    unsigned long long int payload_miR;
    unsigned long long int payload_miN;
    unsigned long long int payload_miP;
    unsigned long long int K;
    unsigned long long int K1;
    unsigned long long int K2;
    unsigned long long int K3;
    unsigned long long int K4;
    unsigned long long int R;
    unsigned long long int RR;
    unsigned long long int H;
    unsigned long long int HYTL;
    unsigned long long int HYTR;
    long int bit_counterL;
    long int bit_counterR;
    unsigned long long int p2_wacn;
    unsigned long long int p2_sysid;
    unsigned long long int p2_cc; //p1 NAC
    unsigned long long int p2_siteid;
    unsigned long long int p2_rfssid;
    long int p25_cc_freq;   //cc freq from net_stat
    long int trunk_cc_freq; //protocol-agnostic alias (kept in sync with p25_cc_freq)
    unsigned long long int edacs_site_id;
    time_t last_cc_sync_time; //use this to start hunting for CC after signal lost
    time_t last_vc_sync_time; //flag for voice activity bursts, tune back on con+ after more than x seconds no voice
    // Timestamp of last tune to a VC (used to provide a short startup grace
    // window so we don't bounce back to CC before MAC_PTT/ACTIVE/audio arrives)
    time_t p25_last_vc_tune_time;
    // Monotonic twins for SM timing (seconds)
    double last_cc_sync_time_m;
    double last_vc_sync_time_m;
    double p25_last_vc_tune_time_m;
    time_t
        last_active_time; //time the a 'call grant' was received, used to clear the active_channel strings after x seconds
    time_t last_t3_tune_time;   // last time a DMR T3 grant was received (wall clock)
    double last_t3_tune_time_m; // same as above, monotonic seconds
    // DMR: rate-limit for single-fragment SLCO logging per slot
    time_t slco_sfrag_last[2];
    unsigned long long int m17_dst;
    unsigned long long int m17_src;
    //event history itemized per slot
    Event_History_I* event_history_s;
    // Codec2 contexts (NULL when codec2 unavailable; unconditional for ABI stability)
    struct CODEC2* codec2_3200; // M17 fullrate
    struct CODEC2* codec2_1600; // M17 halfrate
    void* rc2_context;
    struct RtlSdrContext* rtl_ctx; // RTL-SDR stream context (NULL when unused; unconditional for ABI stability)
    time_t ui_msg_expire;          // epoch seconds when ui_msg should stop displaying
    // AES key segments
    unsigned long long int A1[2];
    unsigned long long int A2[2];
    unsigned long long int A3[2];
    unsigned long long int A4[2];
    // DMR LRRP 64-bit values
    unsigned long long int dmr_lrrp_source[2];
    unsigned long long int dmr_lrrp_target[2];
    // P25 trunking freq storage
    long int p25_vc_freq[2];
    long int trunk_vc_freq[2]; //protocol-agnostic alias (kept in sync with p25_vc_freq)
    // Trunking LCNs and maps
    long int trunk_lcn_freq[26];
    long int trunk_chan_map[0xFFFF];
    // DMR Tier III: simple provenance/trust for learned LCN->freq mappings
    // 0=unset, 1=learned (unconfirmed), 2=trusted (confirmed on-current-site CC)
    uint8_t dmr_lcn_trust[0x1000];
    groupinfo group_array[0x3FF];
    // DMR late entry MI
    uint64_t late_entry_mi_fragment[2][8][3];
    // Multi-key array
    unsigned long long int rkey_array[0x1FFFF];
    // Temporary audio buffers
    float audio_out_temp_buf[160];
    float* audio_out_temp_buf_p;
    float audio_out_temp_bufR[160];
    float* audio_out_temp_buf_pR;
    //analog/raw signal audio buffers (float path for better SNR, convert to int16 at output)
    float analog_out_f[960]; // float buffer for analog monitor path
    short analog_out[960];   // int16 buffer for output and legacy paths
    int analog_sample_counter;
    //new stereo float sample storage
    float f_l[160];     //single sample left
    float f_r[160];     //single sample right
    float f_l4[4][160]; //quad sample for up to a P25p2 4V
    float f_r4[4][160]; //quad sample for up to a P25p2 4V
    //new stereo short sample storage
    short s_l[160];      //single sample left
    short s_r[160];      //single sample right
    short s_l4[18][160]; //quad sample for up to a P25p2 4V
    short s_r4[18][160]; //quad sample for up to a P25p2 4V
    //new stereo short sample storage tapped from 48_k internal upsampling
    short s_lu[160 * 6];     //single sample left
    short s_ru[160 * 6];     //single sample right
    short s_l4u[4][160 * 6]; //quad sample for up to a P25p2 4V
    short s_r4u[4][160 * 6]; //quad sample for up to a P25p2 4V
    int audio_out_idx;
    int audio_out_idx2;
    int audio_out_idxR;
    int audio_out_idx2R;
    float center;
    int jitter;
    int synctype;
    float min;
    float max;
    float lmid;
    float umid;
    float minref;
    float maxref;
    float lastsample;
    float sbuf[128];
    int sidx;
    float maxbuf[1024];
    float minbuf[1024];
    int midx;
    char err_str[64];
    char err_buf[64];
    char err_strR[64];
    char err_bufR[64];
    char fsubtype[16];
    char ftype[16];
    int symbolcnt;
    int symbolc;

    /* C4FM timing assist (clock loop hinting). Lightweight EL/M&M error drives
       occasional ±1 nudges of symbolCenter; disabled by default. */
    int c4fm_clk_mode;     /* 0=off, 1=Early-Late, 2=M&M */
    int c4fm_clk_prev_dec; /* last sliced level for M&M (-3,-1,1,3; 0 if unknown) */
    int c4fm_clk_run_dir;  /* last run direction (-1,0,+1) */
    int c4fm_clk_run_len;  /* consecutive runs in same direction */
    int c4fm_clk_cooldown; /* cooldown countdown to avoid rapid flips */

    int rf_mod;
    /* M17 polarity auto-detection: 0=unknown, 1=normal, 2=inverted.
     * Set when preamble detected; overridden if user specifies -xz. */
    int m17_polarity;
    /* Multi-rate sync hunting: cycle through SPS values when no sync found.
     * sps_hunt_counter: symbols searched without valid sync
     * sps_hunt_idx: current SPS index in cycle (0=10, 1=20, 2=5, 3=8) */
    int sps_hunt_counter;
    int sps_hunt_idx;
    int lastsynctype;
    int lastp25type;
    int offset;
    int carrier;
    char tg[25][16];
    int tgcount;
    int lasttg;
    int lasttgR;
    int lastsrc;
    int lastsrcR;
    int8_t gi[2]; //group, or private call, per slot
    uint8_t eh_index;
    uint8_t eh_slot;
    int nac;
    int errs;
    int errs2;
    int errsR;
    int errs2R;
    int mbe_file_type;
    int optind;
    // CLI argv/argc snapshot for file playback modes (set by frontend)
    int cli_argc_effective;
    char** cli_argv;
    // User config autosave state (set by frontend when config is active)
    int config_autosave_enabled;
    char config_autosave_path[1024];
    int numtdulc;
    int firstframe;
    char slot0light[8];
    float aout_gain;
    float aout_gainR;
    float aout_gainA;
    float aout_max_buf[200];
    float aout_max_bufR[200];
    int aout_max_buf_idx;
    int aout_max_buf_idxR;
    int samplesPerSymbol;
    int symbolCenter;
    char algid[9];
    char keyid[17];
    int currentslot;
    int hardslot;
    int p25kid;
    int payload_algid;
    int payload_algidR;
    int payload_keyid;
    int payload_keyidR;
    int payload_mfid;
    int payload_mfidR;
    int p25vc;
    int M;

    //AES Key Segments
    int aes_key_loaded[2];

    //xl specific, we need to know if the ESS is from HDU, or from LDU2
    int xl_is_hdu;

    unsigned int debug_audio_errors;
    unsigned int debug_audio_errorsR;
    unsigned int debug_header_errors;
    unsigned int debug_header_critical_errors;
    int debug_mode; //debug misc things

    // Last dibit read
    int last_dibit;

    // Heuristics state data for +P25 signals
    P25Heuristics p25_heuristics;

    // Heuristics state data for -P25 signals
    P25Heuristics inv_p25_heuristics;

    //input sample buffer for monitoring Input
    short input_sample_buffer;  //HERE HERE
    short pulse_raw_out_buffer; //HERE HERE

    unsigned int dmr_color_code;
    unsigned int dmr_t3_syscode;
    unsigned int nxdn_last_ran;
    unsigned int nxdn_last_rid;
    unsigned int nxdn_last_tg;
    unsigned int nxdn_cipher_type;
    unsigned int nxdn_key;
    char nxdn_call_type[1024];

    NxdnElementsContent_t NxdnElementsContent;

    char ambe_ciphered[49];
    char ambe_deciphered[49];

    unsigned int color_code;
    unsigned int color_code_ok;
    unsigned int PI;
    unsigned int PI_ok;
    unsigned int LCSS;
    unsigned int LCSS_ok;

    unsigned int dmr_fid;
    unsigned int dmr_so;
    unsigned int dmr_flco;

    unsigned int dmr_fidR;
    unsigned int dmr_soR;
    unsigned int dmr_flcoR;

    char slot1light[8];
    char slot2light[8];
    int directmode;

    int dmr_stereo_payload[144];    //load up 144 dibit buffer for every single DMR TDMA frame
    uint8_t dmr_stereo_reliab[144]; //parallel reliability for stereo cache (0..255)
    int data_header_blocks[2];      //collect number of blocks to follow from data header per slot
    int data_block_counter[2];      //counter for number of data blocks collected
    uint8_t data_header_valid[2];   //flag for verifying the data header if still valid (in case of tact/burst fec errs)
    uint8_t data_header_padding[2]; //collect number of padding octets in last block per slot
    uint8_t data_header_format[2];  //collect format of data header (conf or unconf) per slot
    uint8_t data_header_sap[2];     //collect sap info per slot
    uint8_t data_p_head[2];         //flag for dmr proprietary header to follow

    //new stuff below here
    uint8_t data_conf_data[2];   //flag for confirmed data blocks per slot
    uint8_t data_block_poc[2];   //padding octets in the header (needed for Data PDU Decryption)
    uint16_t data_byte_ctr[2];   //number of bytes acculumated
    uint8_t data_ks_start[2];    //where the start of the keystream should be applied to PDU data
    uint8_t udt_uab_reserved[2]; //UDT: header UAB indicates reserved/unknown count (use CRC-based EOM)
    uint8_t dmr_pdu_sf
        [2][24 * 128]; //unified pdu 'superframe' //[slot][byte] -- increased capacity to 127(+1) full rate blocks
    uint8_t cap_plus_csbk_bits[2][12 * 8 * 8]; //CSBK Cap+ FL initial and appended block bit storage, by slot
    uint8_t cap_plus_block_num[2];             //received block number storage -- per timeslot
    uint8_t data_block_crc_valid[2][127];      //flag each individual block as good crc on confirmed data
    // Confirmed data sequence tracking (DBSN expected per slot)
    uint8_t data_dbsn_expected[2];
    uint8_t data_dbsn_have[2];
    char dmr_embedded_signalling
        [2][7]
        [48]; //embedded signalling 2 slots by 6 vc by 48 bits (replacing TS1SuperFrame.TimeSlotRawVoiceFrame.Sync structure)

    char dmr_cach_fragment[4][17]; //unsure of size, will need to check/verify
    int dmr_cach_counter;          //counter for dmr_cach_fragments 0-3; not sure if needed yet.

    //dmr talker alias new/fixed stuff
    uint8_t dmr_alias_format[2];    //per slot
    uint8_t dmr_alias_block_len[2]; //per slot
    uint8_t dmr_alias_char_size[2]; //per slot
    char dmr_alias_block_segment[2][4][7]
                                [16]; //2 slots, by 4 blocks, by up to 7 alias bytes that are up to 16-bit chars
    char dmr_embedded_gps[2][600];    //2 slots by 99 char string for string embedded gps
    char dmr_lrrp_gps[2][600];        //2 slots by 99 char string for string lrrp gps
    char dmr_site_parms[200];         //string for site/net info depending on type of DMR system (TIII or Con+)
    char call_string[2][200];         //string for call information
    char active_channel[31][200];     //string for storing and displaying active trunking channels

    //Generic Talker Alias String
    char generic_talker_alias[2][500];
    // Source unit ID that last populated generic_talker_alias per slot
    // Used to suppress stale alias across protocol/call transitions
    uint32_t generic_talker_alias_src[2];

    dPMRVoiceFS2Frame_t dPMRVoiceFS2Frame;

    /* Event_History_I* event_history_s; */

    //new audio filter structs
    LPFilter RCFilter;
    HPFilter HRCFilter;
    PBFilter PBF;
    NOTCHFilter NF;
    LPFilter RCFilterL;
    HPFilter HRCFilterL;
    LPFilter RCFilterR;
    HPFilter HRCFilterR;

    char dpmr_caller_id[20];
    char dpmr_target_id[20];

    int dpmr_color_code;

    short int dmr_stereo; //need state variable for upsample function
    short int dmr_ms_mode;
    unsigned int dmrburstL;
    unsigned int dmrburstR;
    int dropL;
    int dropR;
    int DMRvcL;
    int DMRvcR;

    //keystream octet and bit arrays
    uint8_t ks_octetL[129 * 18];         //arbitary size, but large enough for the largest packed PDUs
    uint8_t ks_octetR[129 * 18];         //arbitary size, but large enough for the largest packed PDUs
    uint8_t ks_bitstreamL[128 * 18 * 8]; //arbitary size, but large enough for the largest PDUs
    uint8_t ks_bitstreamR[129 * 18 * 8]; //arbitary size, but large enough for the largest PDUs
    int octet_counter;

    //AES Specific Variables
    uint8_t aes_key[32]; //was 64 for some reason
    uint8_t aes_iv[16];
    uint8_t aes_ivR[16];

    //NXDN DES and AES, signal new VCALL_IV and new IV
    uint8_t nxdn_new_iv; //1 when a new IV comes in, else 0

    short int dmr_encL;
    short int dmr_encR;

    //P2 variables
    int p2_hardset;         //flag for checking whether or not P2 wacn and sysid are hard set by user
    int p2_scramble_offset; //offset counter for scrambling application
    int p2_vch_chan_num;    //vch channel number (0 or 1, not the 0-11 TS)
    int ess_b[2][96];       //external storage for ESS_B fragments
    int fourv_counter[2];   //external reference counter for ESS_B fragment collection
    int voice_counter[2];   //external reference counter for 18V x 2 P25p2 Superframe
    int p2_is_lcch;         //flag to tell us when a frame is lcch and not sacch
    // P25p2 per-slot audio gating (set on MAC_PTT/ACTIVE, cleared on MAC_END/IDLE/SIGNAL)
    int p25_p2_audio_allowed[2];
    // P25p2 small output jitter buffers (per-slot ring of decoded 20 ms frames)
    // Depth 3 per checklist to bound latency (~60 ms max)
    float p25_p2_audio_ring[2][3][160];
    int p25_p2_audio_ring_head[2]; // pop index
    int p25_p2_audio_ring_tail[2]; // push index
    int p25_p2_audio_ring_count[2];
    // P25p2 currently active voice slot (0 or 1), -1 when unknown/idle
    int p25_p2_active_slot;
    // P25p2 recent MAC_ACTIVE/PTT timestamps per slot (guards early bounce)
    time_t p25_p2_last_mac_active[2];
    // Monotonic twins for last MAC_ACTIVE/PTT per slot
    double p25_p2_last_mac_active_m[2];
    // P25p2 recent MAC_END_PTT timestamps per slot (enables early teardown
    // once per-slot jitter/audio has drained)
    time_t p25_p2_last_end_ptt[2];
    // P25p1 recent TDU/TDULC timestamps (enables early teardown on Phase 1)
    time_t p25_p1_last_tdu;   // wall clock (legacy)
    double p25_p1_last_tdu_m; // monotonic seconds (preferred)

    // P25 Phase 2 RS(63,35) metrics (hexbits, t=14)
    unsigned int p25_p2_rs_facch_ok;
    unsigned int p25_p2_rs_facch_err;
    unsigned int p25_p2_rs_facch_corr; // total corrected symbols over accepts
    unsigned int p25_p2_rs_sacch_ok;
    unsigned int p25_p2_rs_sacch_err;
    unsigned int p25_p2_rs_sacch_corr; // total corrected symbols over accepts
    unsigned int p25_p2_rs_ess_ok;
    unsigned int p25_p2_rs_ess_err;
    unsigned int p25_p2_rs_ess_corr;     // total corrected symbols over accepts
    unsigned int p25_p2_soft_erasure_ok; // soft-decision RS successful recoveries
    // P25P1 soft decision counters
    unsigned int p25_p1_soft_golay_ok; // soft Golay corrections (hard would have failed)
    unsigned int p25_p2_soft_ess_ok;   // soft ESS corrections
    // P25p2 early ENC lockout counter (MAC_PTT-driven)
    unsigned int p25_p2_enc_lo_early;
    // P25p2 early ENC lockout hardening: require confirmation across two indications
    uint8_t p25_p2_enc_pending[2];
    uint32_t p25_p2_enc_pending_ttg[2];

    //iden freq storage for frequency calculations
    int p25_chan_tdma[16];              // set from iden_up vs iden_up_tdma (bit0 = TDMA flag)
    uint8_t p25_chan_tdma_explicit[16]; // 0=unknown, 1=explicit FDMA, 2=explicit TDMA
    int p25_chan_iden;
    int p25_chan_type[16];
    int p25_trans_off[16];
    int p25_chan_spac[16];
    long int p25_base_freq[16];
    // Per-IDEN provenance and trust level
    // - wacn/sysid/rfss/site capture the system/site context when the IDEN was learned
    // - trust: 0=unknown, 1=unconfirmed (learned off-CC/adjacent), 2=confirmed on matching CC
    unsigned long long int p25_iden_wacn[16];
    unsigned long long int p25_iden_sysid[16];
    unsigned long long int p25_iden_rfss[16];
    unsigned long long int p25_iden_site[16];
    uint8_t p25_iden_trust[16];

    //p25 frequency storage for trunking and display in ncurses
    int p25_cc_is_tdma;  // control channel modulation: 0=FDMA (C4FM), 1=TDMA (QPSK)
    int p25_sys_is_tdma; // system hint: 1 when P25p2 voice observed (TDMA present)

    // Candidate evaluation tracking (current freq and start time in monotonic seconds)
    long p25_cc_eval_freq;
    double p25_cc_eval_start_m;
    // Persisted CC cache bookkeeping
    uint8_t p25_cc_cache_loaded; // 1 once we attempted to load per-system cache

    // Trunk SM metrics (shared by P25 and DMR trunking)
    unsigned int p25_sm_tune_count;      // number of VC tunes via SM
    unsigned int p25_sm_release_count;   // number of release requests via SM
    unsigned int p25_sm_cc_return_count; // number of actual returns to CC via SM
    // One-shot flag to force immediate return-to-CC on explicit MAC_END/IDLE
    // or policy events; cleared by the SM after handling
    int p25_sm_force_release;
    int trunk_sm_force_release; // protocol-agnostic alias (kept in sync with p25_sm_force_release)
    // Timestamp of last p25_sm_on_release() (0 when none yet)
    time_t p25_sm_last_release_time;
    // Last SM status/reason tag (e.g., "after-tune", "release-deferred-gated") and timestamp
    char p25_sm_last_reason[32];
    time_t p25_sm_last_reason_time;
    // Ring buffer of recent SM tags (for ncurses diagnostics)
    int p25_sm_tag_count;      // number of valid entries (<= 8)
    int p25_sm_tag_head;       // next write index (monotonic)
    char p25_sm_tags[8][32];   // recent tags (text)
    time_t p25_sm_tag_time[8]; // per-tag timestamp
    // Watchdog start time for prolonged post-hang gating on P25p2 VCs
    time_t p25_sm_posthang_start;
    // Monotonic twin for post-hang watchdog (seconds)
    double p25_sm_posthang_start_m;

    // High-level SM mode for UI/telemetry (distinct from minimal P25p2 follower)
    // 0=unknown, 1=on CC, 2=on VC (grant-following or armed), 3=hang, 4=hunting CC
    int p25_sm_mode;

    // Retune backoff bookkeeping
    // Blocks immediate re-tune to same VC/slot after a recent return
    time_t p25_retune_block_until;
    long p25_retune_block_freq;
    int p25_retune_block_slot; // -1 when N/A
    // Cached P25 SM tunables (seconds), resolved once at p25_sm_init()
    double p25_cfg_vc_grace_s;
    double p25_cfg_grant_voice_to_s;
    double p25_cfg_min_follow_dwell_s;
    double p25_cfg_retune_backoff_s;
    double p25_cfg_mac_hold_s;
    double p25_cfg_cc_grace_s;
    double p25_cfg_ring_hold_s;        // seconds to honor audio ring after recent MAC
    double p25_cfg_force_rel_extra_s;  // safety-net extra seconds beyond hang
    double p25_cfg_force_rel_margin_s; // safety-net hard margin seconds beyond extra
    double p25_cfg_tail_ms;            // P2 tail wait in ms before early release
    double p25_cfg_p1_tail_ms;         // P1 tail wait in ms before early release
    double p25_cfg_p1_err_hold_pct;    // P1 elevated-error threshold percentage
    double p25_cfg_p1_err_hold_s;      // P1 elevated-error additional hold seconds

    // P25 Phase 1 control/data channel FEC/CRC telemetry (for BER display)
    // NOTE: This does not reflect IMBE voice quality. Voice frames have their own
    // Golay/Hamming/RS protection and IMBE ECC; see P1 Voice metrics and
    // debug_{audio,header}_* counters.
    unsigned int p25_p1_fec_ok;  // count of TSBK/MDPU header CRC/FEC successes
    unsigned int p25_p1_fec_err; // count of TSBK/MDPU header CRC/FEC failures
    // P25 Phase 1 voice/header FEC telemetry (Reed-Solomon outcome for HDU/LDU/TDULC)
    unsigned int p25_p1_voice_fec_ok;  // count of successful RS decodes (HDU/LDU/TDULC)
    unsigned int p25_p1_voice_fec_err; // count of failed RS decodes (HDU/LDU/TDULC)
    // P25 Phase 1 DUID/frame-type histogram (since last tune/reset)
    unsigned int p25_p1_duid_hdu;
    unsigned int p25_p1_duid_ldu1;
    unsigned int p25_p1_duid_ldu2;
    unsigned int p25_p1_duid_tdu;
    unsigned int p25_p1_duid_tdulc;
    unsigned int p25_p1_duid_tsbk;
    unsigned int p25_p1_duid_mpdu;

    // P25 Phase 1 voice error moving average (last N IMBE frames)
    uint8_t p25_p1_voice_err_hist[64];
    int p25_p1_voice_err_hist_len;          // window length (<=64), default 50
    int p25_p1_voice_err_hist_pos;          // ring head
    unsigned int p25_p1_voice_err_hist_sum; // sum of values in window

    // P25 Phase 2 voice error moving average per slot (errs2 from AMBE decode)
    uint8_t p25_p2_voice_err_hist[2][64];
    int p25_p2_voice_err_hist_len; // window length (<=64), default 50
    int p25_p2_voice_err_hist_pos[2];
    unsigned int p25_p2_voice_err_hist_sum[2];

    // P25 regroup/patch tracking (active super group IDs)
    // Tracks up to 8 concurrent SGIDs; updates come from MFID A4/90 regroup PDUs.
    int p25_patch_count;
    uint16_t p25_patch_sgid[8];
    uint8_t p25_patch_is_patch[8]; // 1=two-way patch, 0=simulselect
    uint8_t p25_patch_active[8];   // 1=active, 0=inactive
    time_t p25_patch_last_update[8];
    // Membership (best-effort): WGIDs and WUIDs per SG
    uint8_t p25_patch_wgid_count[8];
    uint16_t p25_patch_wgid[8][8];
    uint8_t p25_patch_wuid_count[8];
    uint32_t p25_patch_wuid[8][8];
    // Optional crypt/state context from GRG commands
    uint16_t p25_patch_key[8]; // Key ID
    uint8_t p25_patch_alg[8];  // ALG (vendor-specific)
    uint8_t p25_patch_ssn[8];  // SSN
    // Whether p25_patch_key[i] has been explicitly set by a GRG command
    uint8_t p25_patch_key_valid[8];

    // P25 affiliated RIDs tracking (simple fixed-size table)
    // Tracks up to 256 recent unit registrations (RIDs) with last-seen time for aging.
    // Entries are added on Unit Registration Accept and removed on Deregistration Ack
    // or when the last-seen exceeds an aging threshold.
    int p25_aff_count;             // number of active entries
    uint32_t p25_aff_rid[256];     // RID values
    time_t p25_aff_last_seen[256]; // last seen timestamp per RID

    // P25 Group Affiliation tracking: RID↔TG observations with aging
    int p25_ga_count;
    uint32_t p25_ga_rid[512];
    uint16_t p25_ga_tg[512];
    time_t p25_ga_last_seen[512];

    // P25 neighbors seen via Adjacent Status (best-effort)
    // Track a small set of recently announced neighbor/control candidates for UI purposes.
    int p25_nb_count;            // number of active neighbor entries
    long int p25_nb_freq[32];    // neighbor/control frequencies in Hz
    time_t p25_nb_last_seen[32]; // last seen timestamp per entry

    // P25 current-call flags (per logical slot; FDMA uses slot 0)
    uint8_t p25_call_emergency[2]; // 1 if current call is emergency
    uint8_t p25_call_priority[2];  // 0..7 call priority (0 if unknown)
    uint8_t p25_call_is_packet[2]; // 1 if call/service marked as packet (data), else 0

    //experimental symbol file capture read throttle
    int symbol_throttle; //throttle speed
    int use_throttle;    //only use throttle if set to 1

    //dmr trunking stuff
    int dmr_rest_channel;
    int dmr_mfid; //just when 'fid' is used as a manufacturer ID and not a feature set id
    int dmr_vc_lcn;
    int dmr_vc_lsn;
    int dmr_tuned_lcn;
    uint16_t dmr_cc_lpcn; //dmr t3 logical physical channel number
    uint32_t tg_hold;     //single TG to hold on when enabled

    //edacs
    int ea_mode;

    unsigned short esk_mask;
    uint32_t edacs_sys_id;
    uint32_t edacs_area_code;
    int edacs_lcn_count;    //running tally of lcn's observed on edacs system
    int edacs_cc_lcn;       //current lcn for the edacs control channel
    int edacs_vc_lcn;       //current lcn for any active vc (not the one we are tuned/tuning to)
    int edacs_tuned_lcn;    //the vc we are currently tuned to...above variable is for updating all in the matrix
    int edacs_vc_call_type; //the type of call on the given VC - see defines below
    int edacs_a_bits;       //  Agency Significant Bits
    int edacs_f_bits;       //   Fleet Significant Bits
    int edacs_s_bits;       //Subfleet Significant Bits
    int edacs_a_shift;      //Calculated Shift for A Bits
    int edacs_f_shift;      //Calculated Shift for F Bits
    int edacs_a_mask;       //Calculated Mask for A Bits
    int edacs_f_mask;       //Calculated Mask for F Bits
    int edacs_s_mask;       //Calculated Mask for S Bits

    //trunking group and lcn freq list
    unsigned int group_tally; //tally number of groups imported from CSV file for referencing later
    int lcn_freq_count;
    int lcn_freq_roll; //number we have 'rolled' to in search of the CC
    int is_con_plus;   //con_plus flag for knowing its safe to skip payload channel after x seconds of no voice sync

    //new nxdn stuff
    int nxdn_part_of_frame;
    int nxdn_ran;
    int nxdn_sf;
    bool
        nxdn_sacch_non_superframe; //flag to indicate whether or not a sacch is a part of a superframe, or an individual piece
    uint8_t nxdn_sacch_frame_segment[4][18]; //part of frame by 18 bits
    uint8_t nxdn_sacch_frame_segcrc[4];
    uint8_t nxdn_alias_block_number;
    char nxdn_alias_block_segment[4][4][8];

    //site/srv/cch info

    char nxdn_location_category[14];
    uint32_t nxdn_location_sys_code;
    uint16_t nxdn_location_site_code;

    //channel access information
    uint8_t nxdn_rcn;
    uint8_t nxdn_base_freq;
    uint8_t nxdn_step;
    uint8_t nxdn_bw;

    // NXDN trunking: last observed call grant mapping (for UI/logging)
    uint16_t nxdn_grant_chan;
    long int nxdn_grant_freq;

    //multi-key array
    int keyloader; //let us know the keyloader is active

    //dmr late entry mi

    //dmr manufacturer branding and sub_branding (i.e., Motorola and Con+)
    char dmr_branding[20];
    char dmr_branding_sub[80];

    //Remus DMR End Call Alert Beep
    int dmr_end_alert[2]; //dmr TLC end call alert beep has already played once flag

    //Bitmap Filtering Options
    int audio_smoothing;

    //YSF Fusion Call Strings and Info
    uint8_t ysf_dt; //data type -- VD1, VD2, Full Rate, etc.
    uint8_t ysf_fi; //frame information -- HC, CC, TC
    uint8_t ysf_cm; //group or private call
    char ysf_tgt[11];
    char ysf_src[11];
    char ysf_upl[11];
    char ysf_dnl[11];
    char ysf_rm1[6];
    char ysf_rm2[6];
    char ysf_rm3[6];
    char ysf_rm4[6];
    char ysf_txt[21][21]; //text storage blocks

    //DSTAR Call Strings and Info
    char dstar_rpt1[9];
    char dstar_rpt2[9];
    char dstar_dst[9];
    char dstar_src[13];
    char dstar_txt[60];
    char dstar_gps[60];

    //M17 Storage
    uint8_t m17_lsf[360];
    uint8_t m17_pkt[850];

    // Soft symbol buffer for Viterbi decoding (M17, NXDN, etc.)
    // Stores raw float symbol values alongside dibits for soft-decision FEC
    float soft_symbol_buf[512];  // Ring buffer for soft symbols
    int soft_symbol_head;        // Write index (wraps at 512)
    int soft_symbol_frame_start; // Index where current frame started
    uint8_t m17_pbc_ct;          //pbc packet counter
    uint8_t m17_str_dt;          //stream contents

    uint8_t m17_can; //can value that was decoded from signal
    int m17_can_en;  //can value supplied to the encoding side
    int m17_rate;    //sampling rate for audio input
    int m17_vox;     //vox enabled via PWR value

    char m17_dst_csd[20];
    char m17_src_csd[20];

    char m17_src_str[50];
    char m17_dst_str[50];

    uint8_t m17_meta[16]; //packed meta
    uint8_t m17_enc;      //enc type
    uint8_t m17_enc_st;   //scrambler or data subtye
    int m17encoder_tx;    //if TX (encode + decode) M17 Stream is enabled
    int m17encoder_eot;   //signal if we need to send the EOT frame

    //misc str storage
    char str50a[50];
    char str50b[50];
    char str50c[50];
    char m17dat[50];  //user supplied m17 data input string
    char m17sms[800]; //user supplied sms text string

    //tyt_ap=1 active
    int tyt_ap;
    int tyt_bp;
    int tyt_ep;
    // retrevis rc2
    int retevis_ap;

    //kenwood scrambler on DMR with forced application
    int ken_sc;

    //anytone bp
    int any_bp;

    //generic ks
    int straight_ks;
    int straight_mod;

    uint8_t static_ks_bits[2][882];
    int static_ks_counter[2];

    // DMR: consecutive EMB decode failures per slot (hysteresis for robustness)
    uint8_t dmr_emb_err[2];

    /* ─────────────────────────────────────────────────────────────────────────
     * DMR Resample-on-Sync Support
     *
     * Implements SDRTrunk-style threshold calibration and CACH resampling to
     * improve first-frame decode accuracy. See dmr_sync.h for details.
     * ───────────────────────────────────────────────────────────────────────── */

    /** Symbol history circular buffer for retrospective resampling.
     *  Stores symbol-rate floats (one per dibit decision), not raw audio samples. */
    float* dmr_sample_history;
    int dmr_sample_history_size;  /**< Buffer size (DMR_SAMPLE_HISTORY_SIZE) */
    int dmr_sample_history_head;  /**< Write index into circular buffer */
    int dmr_sample_history_count; /**< Symbols written (for underflow check) */

    // Transient UI message (shown briefly in ncurses printer)
    char ui_msg[128];

    // Extension slots for module-owned per-state allocations (see core/state_ext.h).
    void* state_ext[DSD_STATE_EXT_MAX];
    dsd_state_ext_cleanup_fn state_ext_cleanup[DSD_STATE_EXT_MAX];
};

// NOLINTEND(clang-analyzer-optin.performance.Padding)
