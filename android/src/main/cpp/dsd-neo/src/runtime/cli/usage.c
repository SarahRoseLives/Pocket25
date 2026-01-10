// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (C) 2026 by arancormonk <180709949+arancormonk@users.noreply.github.com>
 */

/**
 * @file
 * @brief CLI usage/help text implementation - moved from apps/dsd-cli/main.c
 *        to make runtime CLI self-contained.
 */

#include <dsd-neo/runtime/cli.h>

#include <stdio.h>

void
dsd_cli_usage(void) {
    printf("\n");
    printf("Usage: dsd-neo [options]            Decoder/Trunking Mode\n");
    printf("  or:  dsd-neo [options] -r <files> Read/Play saved mbe data from file(s)\n");
    printf("  or:  dsd-neo -h                   Show help\n");
    printf("\n");
    printf("Config Options:\n");
    printf("      --config [PATH]        Enable INI config loading (default path if omitted)\n");
    printf("      --profile <name>       Load a named profile section (profile.<name>)\n");
    printf("      --list-profiles        List available profiles in the config and exit\n");
    printf("      --interactive-setup    Run interactive bootstrap wizard\n");
    printf("      --print-config         Print the effective config (as INI) and exit\n");
    printf("      --dump-config-template Print a commented config template and exit\n");
    printf("      --validate-config [PATH] Validate a config file and exit\n");
    printf("      --strict-config        Treat --validate-config warnings as errors\n");
    printf("\n");
    printf("Display Options:\n");
    printf("  -N            Use NCurses Terminal\n");
    printf("                 dsd-neo -N 2> console_log.txt \n");
    printf("  -Z            Log MBE/PDU Payloads to console\n");
    printf("  -j            Enable P25 LCW explicit retune (format 0x44)\n");
    printf("  -^            Prefer P25 CC candidates (RFSS/Adjacent/Network) during hunt\n");
    printf("      --p25-vc-grace <s>     P25: Seconds after VC tune before eligible to return to CC\n");
    printf("      --p25-min-follow-dwell <s>  P25: Minimum follow dwell after first voice\n");
    printf("      --p25-grant-voice-timeout <s>  P25: Max seconds from grant to voice before returning\n");
    printf("      --p25-retune-backoff <s>  P25: Block immediate re-tune to same VC for N seconds after return\n");
    printf("      --p25-mac-hold <s>     P25: Seconds to keep MAC activity eligible for audio after last MAC\n");
    printf("      --p25-ring-hold <s>    P25: Ring gate window for slot audio activity\n");
    printf("      --p25-cc-grace <s>     P25: CC hunt grace window before treating CC as lost\n");
    printf("      --p25-force-release-extra <s>  P25: Safety-net extra seconds beyond hangtime\n");
    printf("      --p25-force-release-margin <s> P25: Safety-net hard margin seconds beyond extra\n");
    printf("      --p25-p1-err-hold-pct <pct>   P25p1: IMBE error %% threshold to extend hang\n");
    printf("      --p25-p1-err-hold-sec <s>     P25p1: Additional seconds to hold when threshold exceeded\n");

    printf("\n");
    printf("Device Options:\n");
    printf("  -O            List All Pulse Audio Input Sources and Output Sinks (devices).\n");
    printf("\n");
    printf("Input/Output options:\n");
    printf("  -i <device>   Audio input device (default is pulse)\n");
    printf("                pulse for pulse audio signal input \n");
    printf("                pulse:6 or pulse:virtual_sink2.monitor for pulse audio signal input on virtual_sink2 (see "
           "-O) \n");
    printf("                rtl for rtl dongle (Default Values -- see below)\n");
    printf("                rtl:dev:freq:gain:ppm:bw:sql:vol for rtl dongle (see below)\n");
    printf("                rtltcp for rtl_tcp (default 127.0.0.1:1234)\n");
    printf("                rtltcp:host:port for rtl_tcp server address\n");
    printf("                tcp for TCP raw PCM16LE mono audio input (Port 7355)\n");
    printf("                tcp:192.168.7.5:7355 for custom address and port \n");
    printf("                udp for UDP direct audio input (default host 127.0.0.1; default port 7355)\n");
    printf("                udp:0.0.0.0:7355 to bind all interfaces for UDP input\n");
    printf("                m17udp for M17 UDP/IP socket bind input (default host 127.0.0.1; default port 17000)\n");
    printf("                m17udp:192.168.7.8:17001 for M17 UDP/IP bind input (Binding Address and Port\n");
    printf("                filename.bin for OP25/FME capture bin files\n");
    printf("                filename.wav for 48K/1 wav files (SDR++, GQRX)\n");
    printf("                filename.wav -s 96000 for 96K/1 wav files (DSDPlus)\n");
    printf("                (Use single quotes '/directory/audio file.wav' when directories/spaces are present)\n");
    printf("  -s <rate>     Sample rate (Hz) for WAV/TCP/UDP inputs (e.g., 48000, 96000)\n");
    printf("      --input-volume <N>  Scale non-RTL input samples by N (integer 1..16).\n");
    printf("      --input-level-warn-db <dB>  Warn if input power below dBFS (default -40).\n");
    printf("  -o <device>   Audio output device (default is pulse)\n");
    printf("                pulse for pulse audio decoded voice or analog output\n");
    printf("                pulse:1 or pulse:alsa_output.pci-0000_0d_00.3.analog-stereo for pulse audio decoded voice "
           "or analog output on device (see -O) \n");
    printf("                null for no audio output\n");
    printf("                udp for UDP socket blaster output (default host 127.0.0.1; default port 23456)\n");
    printf("                udp:192.168.7.8:23470 for UDP socket blaster output (Target Address and Port\n");
    printf(
        "                m17udp for M17 UDP/IP socket blaster output (default host 127.0.0.1; default port 17000)\n");
    printf("                m17udp:192.168.7.8:17001 for M17 UDP/IP blaster output (Target Address and Port\n");
    printf("  -d <dir>      Create mbe data files, use this directory (TDMA version is experimental)\n");
    printf("  -r <files>    Read/Play saved mbe data from file(s)\n");
    printf("  -g <float>    Audio Digital Output Gain  (Default: 0 = Auto;        )\n");
    printf("                                           (Manual:  1 = 2%%; 50 = 100%%)\n");
    printf("  -n <float>    Audio Analog  Output Gain  (Default: 0 = Auto; 0-100%%  )\n");
    printf("  -6 <file>     Output raw audio .wav file (48K/1). (WARNING! Large File Sizes 1 Hour ~= 360 MB)\n");
    printf("  -7 <dir>      Create/Use Custom directory for Per Call decoded .wav file saving.\n");
    printf("                 (Use ./folder for Nested Directory!)\n");
    printf("                 (Use /path/to/folder for hard coded directory!)\n");
    printf("                 (Use Before the -P option!)\n");
    printf("  -8            Enable Source Audio Monitor\n");
    printf("                 (Set Squelch in RTL, SDR++ or GQRX, etc, if monitoring mixed analog/digital)\n");
    printf("  -w <file>     Output synthesized speech to a single static .wav file. (Do not use with -P Per Call "
           "Switch)\n");
    printf(
        "  -P            Enable Per Call WAV file saving. (Do not use with -w filename.wav single wav file switch)\n");
    printf("                 (Per Call works with everything now and doesn't require ncurses terminal!)\n");
    printf("  -a            Enable Call Alert Beep\n");
    printf("                 (Warning! Might be annoying.)\n");
    printf("  -J <file>     Specify Filename for Event Log Output.\n");
    printf("  -L <file>     Specify Filename for LRRP Data Output.\n");
    printf("  -Q <file>     Specify Filename for OK-DMRlib Structured File Output. (placed in DSP folder)\n");
    printf("  -Q <file>     Specify Filename for M17 Float Stream Output. (placed in DSP folder)\n");
    printf("  -c <file>     Output symbol capture to .bin file\n");
    printf("  -q            Reverse Mute - Mute Unencrypted Voice and Unmute Encrypted Voice\n");
    printf("  -V <num>      TDMA Voice Synthesis: 0=Off, 1=Slot1, 2=Slot2, 3=Both; Default is 3\n");
    printf("  -y            Enable Experimental Pulse Audio Float Audio Output\n");
    printf("  -v <hex>      Set Filtering Bitmap Options (Advanced Option)\n");
    printf("                1 1 1 1 (0xF): PBF/LPF/HPF/HPFD on\n");
    printf("\n");
    printf("RTL-SDR options:\n");
    printf(" Usage: rtl:dev:freq:gain:ppm:bw:sql:vol[:bias[=on|off]]\n");
    printf("  NOTE: all arguments after rtl are optional now for trunking, but user configuration is recommended\n");
    printf("  dev  <num>    RTL-SDR Device Index Number (default 0)\n");
    printf("  freq <num>    RTL-SDR Frequency (851800000 or 851.8M) \n");
    printf("  gain <num>    RTL-SDR Device Gain (0-49)(default = 0; Hardware AGC recommended)\n");
    printf("  ppm  <num>    RTL-SDR PPM Error (default = 0)\n");
    printf("  bw   <num>    RTL-SDR DSP Bandwidth (kHz) (default 48). Allowed: 4,6,8,12,16,24,48.\n");
    printf("                   Note: This is the DSP baseband used to derive capture rate;\n");
    printf("                         it is NOT the tuner IF filter.\n");
    printf("  sq   <val>    RTL-SDR Squelch Threshold (Optional)\n");
    printf("                 (Negative = dB; Positive/Zero = linear mean power)\n");
    printf("  vol  <num>    RTL-SDR Sample 'Volume' Multiplier (default = 2)(1,2,3)\n");
    printf("  bias [on|off] Enable 5V bias tee on compatible dongles (default off)\n");
    printf(" Example: dsd-neo -fs -i rtl -C cap_plus_channel.csv -T\n");
    printf(" Example: dsd-neo -fp -i rtl:0:851.375M:22:-2:24:0:2\n");
    printf("\n");
    printf("RTL-TCP options:\n");
    printf(" Usage: rtltcp[:host:port[:freq:gain:ppm:bw:sql:vol[:bias[=on|off]]]]\n");
    printf("  host: default 127.0.0.1; port: default 1234\n");
    printf("  Remaining fields mirror rtl: string semantics.\n");
    printf(" Example: dsd-neo -i rtltcp:192.168.1.10:1234:851.375M:22:-2:24:0:2 -N\n");
    printf("\n");
    printf("UDP examples:\n");
    printf(" Example: dsd-neo -i udp -o pulse -N\n");
    printf("   Listen for UDP audio on 127.0.0.1:7355 and play to PulseAudio.\n");
    printf(" Example: dsd-neo -i udp:0.0.0.0:7355 -o pulse -N\n");
    printf("   Bind all interfaces; point GQRX/SDR++ UDP audio to this host:port.\n");
    printf("\n");
    printf("Encoder options:\n");
    printf("  -fZ           M17 Stream Voice Encoder\n");
    printf("\n");
    printf("Other options:\n");

    printf("  --auto-ppm    Enable spectrum-based RTL auto PPM (6 dB gate; 1 ppm step)\n");
    printf("  --auto-ppm-snr <dB>  Set SNR gate for auto PPM (default 6)\n");
    printf("  --rtltcp-autotune    Enable RTL-TCP adaptive networking (buffer/recv tuning)\n");
    printf(" Example: dsd-neo -fZ -M M17:9:DSD-NEO:ARANCORMO -i pulse -6 m17signal.wav -8 -N 2> m17encoderlog.txt\n");
    printf("   Run M17 Encoding, listening to pulse audio server, with internal decode/playback and output to 48k/1 "
           "wav file\n");
    printf("\n");
    printf(" Example: dsd-neo -fZ -M M17:9:DSD-NEO:ARANCORMO -i tcp -o pulse -8 -N 2> m17encoderlog.txt\n");
    printf("   Run M17 Encoding, listening to default tcp input, without internal decode/playback and output to 48k/1 "
           "analog output device\n");
    printf("\n");
    printf("  -fP           M17 Packet Encoder\n");
    printf(" Example: dsd-neo -fP -M M17:9:DSD-NEO:ARANCORMO -6 m17pkt.wav -8 -S 'Hello World'\n");
    printf("\n");
    printf("  -fB           M17 BERT Encoder\n");
    printf(" Example: dsd-neo -fB -M M17:9:DSD-NEO:ARANCORMO -6 m17bert.wav -8\n");
    printf("\n");
    printf("  -M            M17 Encoding User Configuration String: M17:CAN:SRC:DST:INPUT_RATE:VOX (see examples "
           "above).\n");
    printf("                  CAN 0-15 (default 7); SRC and DST have to be no more than 9 UPPER base40 characters.\n");
    printf("                  BASE40: '  ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.'\n");
    printf("                  Input Rate Default is 48000; Use Multiples of 8000 up to 48000.\n");
    printf("                  VOX Enabled on 1; (Default = 0)\n");
    printf("                  Values not entered into the M17: string are set to default values.\n");
    printf("Decoder options:\n");
    printf("  -fa           Auto Detection\n");
    printf("  -fA           Passive Analog Audio Monitor\n");
    printf("  -ft           TDMA Trunking P25p1 Control and Voice, P25p2 Trunked Channels, and DMR\n");
    printf("  -fs           DMR TDMA BS and MS Simplex\n");
    printf("  -f1           Decode only P25 Phase 1\n");
    printf("  -f2           Decode only P25 Phase 2 (6000 sps) **\n");
    printf("  -fd           Decode only DSTAR\n");
    printf("  -fx           Decode only X2-TDMA\n");
    printf("  -fy           Decode only YSF\n");
    printf("  -fz             Decode only M17\n");
    printf("  -fU             Decode only M17 UDP/IP Frame***\n");
    printf("  -fi             Decode only NXDN48 (6.25 kHz) / IDAS\n");
    printf("  -fn             Decode only NXDN96 (12.5 kHz)\n");
    printf("  -fp             Decode only ProVoice\n");
    printf("  -fh             Decode only EDACS Standard/ProVoice\n");
    printf("  -fH             Decode only EDACS Standard/ProVoice with ESK 0xA0\n");
    printf(
        "  -fh344          Decode only EDACS Standard/ProVoice and set AFS to 344 or similar custom 11-bit scheme\n");
    printf("  -fH434          Decode only EDACS Standard/ProVoice and set AFS to custom 11-bit scheme with ESK 0xA0\n");
    printf("  -fe             Decode only EDACS EA/ProVoice\n");
    printf("  -fE             Decode only EDACS EA/ProVoice with ESK 0xA0\n");
    printf("  -fm             Decode only dPMR\n");
    printf("  -l            Disable DMR, dPMR, NXDN, M17 input filtering\n");
    printf("  -u <num>      Unvoiced speech quality (default=3)\n");
    printf("  -xx           Expect non-inverted X2-TDMA signal\n");
    printf("  -xr           Expect inverted DMR signal\n");
    printf("  -xd           Expect inverted ICOM dPMR signal\n");
    printf("  -xz           Force inverted M17 signal (polarity is auto-detected by default)\n");
    printf("\n");
    printf("  ** Phase 2 Single Frequency may require user to manually set WACN/SYSID/CC parameters if MAC_SIGNAL not "
           "present.\n");
    printf("  *** configure UDP Input with -i m17udp:127.0.0.1:17000 \n");
    printf("\n");
    printf("  NOTE: All frame types are now auto-detectable with -fa using multi-rate SPS hunting.\n");
    printf("        M17 polarity is auto-detected from preamble; use -xz only to force inverted.\n");
    printf("\n");
    printf("Advanced Decoder options:\n");
    printf("  -X <hex>      Manually Set P2 Parameters (WACN, SYSID, CC/NAC)\n");
    printf("                 (-X BEE00ABC123)\n");
    printf("  -D <dec>      Manually Set TIII DMR Location Area n bit len (0-10)(10 max)\n");
    printf("                 (Value defaults to max n bit value for site model size)\n");
    printf("                 (Setting 0 will show full Site ID, no area/subarea)\n");
    printf("\n");
    printf("  -ma           Auto-select modulation optimizations\n");
    printf("  -mc           Only C4FM optimizations (locks demod; no auto override)\n");
    printf("  -mg           Only GFSK optimizations (locks demod; no auto override)\n");
    printf("  -mq           Only QPSK optimizations (locks demod; no auto override)\n");
    printf("  -m2           P25p2 6000 sps QPSK (locks demod)\n");
    printf("  -F            Relax P25 Phase 2 MAC_SIGNAL CRC Checksum Pass/Fail\n");
    printf("                 Use this feature to allow MAC_SIGNAL even if CRC errors.\n");
    printf("  -F            Relax DMR RAS/CRC CSBK/DATA Pass/Fail\n");
    printf("                 Enabling on some systems could lead to bad channel assignments/site data decoding if bad "
           "or marginal signal\n");
    printf("  -F            Relax NXDN SACCH/FACCH/CAC/F2U CRC Pass/Fail\n");
    printf("  -F            Relax M17 LSF/PKT CRC Error Checking\n");
    printf("\n");
    printf("  -b <dec>      Manually Enter Basic Privacy Key (Decimal Value of Key Number)\n");
    printf("                 (NOTE: This used to be the 'K' option! \n");
    printf("\n");
    printf("  -H <hex>      Manually Enter Hytera 10/32/64 Char Basic Privacy Hex Key (see example below)\n");
    printf("                 Encapulate in Single Quotation Marks; Space every 16 chars.\n");
    printf("                 -H 0B57935150 \n");
    printf("                 -H '736B9A9C5645288B 243AD5CB8701EF8A' \n");
    printf("                 -H '20029736A5D91042 C923EB0697484433 005EFC58A1905195 E28E9C7836AA2DB8' \n");
    printf("\n");
    printf("  -H <hex>      Manually Enter AES-128 or AES-256 Hex Key (see example below)\n");
    printf("                 Encapulate in Single Quotation Marks; Space every 16 chars.\n");
    printf("                 -H '736B9A9C5645288B 243AD5CB8701EF8A' \n");
    printf("                 -H '20029736A5D91042 C923EB0697484433 005EFC58A1905195 E28E9C7836AA2DB8' \n");
    printf("\n");
    printf("  -R <dec>      Manually Enter dPMR or NXDN EHR Scrambler Key Value (Decimal Value)\n");
    printf("                 \n");
    printf("  -1 <hex>      Manually Enter RC4 or DES Key Value (DMR, P25, NXDN) (Hex Value) \n");
    printf("                 \n");
    printf("  -2 <hex>      Manually Enter and Enforce TYT 16-bit BP Key Value (DMR) (Hex Value) \n");
    printf("                 \n");
    printf("  -! <hex>      Manually Enter and Enforce TYT Advanced Privacy (PC4) AP Hex Key (see example below)\n");
    printf("                 Encapulate in Single Quotation Marks; Space every 16 chars.\n");
    printf("                 -! '736B9A9C5645288B 243AD5CB8701EF8A' \n");
    printf("                 \n");
    printf(
        "  -@ <hex>      Manually Enter and Enforce Retevis Advanced Privacy (RC2) AP Hex Key (see example below)\n");
    printf("                 Encapulate in Single Quotation Marks; Space every 16 chars.\n");
    printf("                 -@ '736B9A9C5645288B 243AD5CB8701EF8A' \n");
    printf("                 \n");
    printf(
        "  -5 <hex>      Manually Enter and Enforce TYT Enhanced Privacy (AES-128) EP Hex Key (see example below)\n");
    printf("                 Encapulate in Single Quotation Marks; Space every 16 chars.\n");
    printf("                 -5 '736B9A9C5645288B 243AD5CB8701EF8A' \n");
    printf("                 \n");
    printf("  -9 <dec>      Manually Enter and Enforce Kenwood 15-bit Scrambler Key Value (DMR) (Dec Value) \n");
    printf("                 \n");
    printf("  -A <hex>      Manually Enter and Enforce Anytone 16-bit BP Key Value (DMR) (Hex Value) \n");
    printf("                 \n");
    printf("  -S <str>      Manually Enter and Enforce Generic Static Keystream -> Length and BYTE PACKED / ALIGNED "
           "String for AMBE (up to 882 bits)\n");
    printf("                  For Example, enter 16-bit Keystream 0909 as:\n");
    printf("                    -S 16:0909\n");
    printf("                  For Example, enter 49-bit Keystream as:\n");
    printf("                    -S 49:123456789ABC80\n");
    printf("                  For Example, enter 49-bit Keystream (MBP 70) as:\n");
    printf("                    -S 49:ED0AED4AED4AED4A\n");
    printf("                 \n");
    printf("  -k <file>     Import Key List from csv file (Decimal Format) -- Lower Case 'k'.\n");
    printf("                  Only supports NXDN, DMR Basic Privacy (decimal value). \n");
    printf("                  (dPMR and Hytera 32/64 char not supported, DMR uses TG value as key id -- "
           "EXPERIMENTAL!!). \n");
    printf("                 \n");
    printf("  -K <file>     Import Key List from csv file (Hexidecimal Format) -- Capital 'K'.\n");
    printf("                  Use for Hex Value Hytera 10-char BP keys, RC4 10-char, DES 16-char Hex Keys, and "
           "AES128/256 32/64-char keys. \n");
    printf("                 \n");
    printf("  -4            Force Privacy Key over Encryption Identifiers (DMR MBP/HBP and NXDN Scrambler) \n");
    printf("                 \n");
    printf("  -0            Force RC4 Key over Missing PI header/LE Encryption Identifiers (DMR) \n");
    printf("                 \n");
    printf("  -3            Disable DMR Late Entry Encryption Identifiers (VC6 Single Burst) \n");
    printf("                  Note: Disable this if false positives on Voice ENC occur. \n");
    printf("\n");
    printf(" Trunking Options:\n");
    printf("  -C <file>     Import Channel to Frequency Map (channum, freq) from csv file. (Capital C)                 "
           "  \n");
    printf("                 (See channel_map.csv for example)\n");
    printf("  -G <file>     Import Group List Allow/Block and Label from csv file.\n");
    printf("                 (See group.csv for example)\n");
    printf("  -T            Enable Trunking Features (NXDN/P25/EDACS/DMR) with RIGCTL/TCP or RTL Input\n");
    printf("  -Y            Enable Scanning Mode with RIGCTL/TCP or RTL Input \n");
    printf(
        "                 Experimental -- Can only scan for sync with enabled decoders, don't mix NXDN and DMR/P25!\n");
    printf("                 This is not a Trunking Feature, just scans through conventional frequencies fast!\n");
    printf("  -W            Use Imported Group List as a Trunking Allow/White List -- Only Tune with Mode A\n");
    printf("  -p            Disable Tune to Private Calls (DMR TIII, P25, NXDN Type-C and Type-D)\n");
    printf("  -E            Disable Tune to Group Calls (DMR TIII, Con+, Cap+, P25, NXDN Type-C, and Type-D)\n");
    printf("  -e            Enable Tune to Data Calls (DMR TIII, Cap+, NXDN Type-C)\n");
    printf("                 (NOTE: No Clear Distinction between Cap+ Private Voice Calls and Data Calls -- Both "
           "enabled with Data Calls \n");
    printf("  --enc-lockout  P25: Do not tune encrypted calls (ENC lockout On)\n");
    printf("  --enc-follow   P25: Allow encrypted calls (ENC lockout Off; default)\n");
    printf("  --no-p25p2-soft    Disable P25P2 soft-decision RS erasure marking\n");
    printf("  -I <dec>      Specify TG to Hold During Trunking (DMR, P25, NXDN Type-C Trunking)\n");
    printf("  -U <port>     Enable RIGCTL/TCP; Set TCP Port for RIGCTL. (4532 on SDR++)\n");
    printf("  -B <Hertz>    Set RIGCTL Setmod Bandwidth in Hertz (0 - default - Off)\n");
    printf("                 P25 - 12000; NXDN48 - 7000; NXDN96: 12000; DMR - 7000-12000; EDACS/PV - 12000-24000;\n");
    printf("                 May vary based on system stregnth, etc.\n");
    printf("  -t <secs>     Set Trunking or Scan Speed VC/sync loss hangtime in seconds. (default = 2 seconds)\n");
    printf("\n");
    printf(" Trunking Example TCP: dsd-neo -fs -i tcp -U 4532 -T -C dmr_t3_chan.csv -G group.csv -N 2> log.txt\n");
    printf(" Trunking Example RTL: dsd-neo -fs -i rtl:0:450M:26:-2:8 -T -C connect_plus_chan.csv -G group.csv -N 2> "
           "log.txt\n");
    printf("\n");

    printf("DMR TIII Tools:\n");
    printf("  --calc-lcn <file>           Calculate LCNs from a CSV of frequencies (Hz or MHz).\n");
    printf("  --calc-cc-freq <freq>       Anchor CC frequency (Hz or MHz).\n");
    printf("  --calc-cc-lcn <int>         Anchor CC LCN number.\n");
    printf("  --calc-step <hz>            Override inferred channel step (Hz, e.g., 12500).\n");
    printf("  --calc-start-lcn <int>      Starting LCN when no anchor (default 1).\n");
    printf(" Example: dsd-neo --calc-lcn freqs.csv --calc-cc-freq 451.2375 --calc-cc-lcn 50\n");
    printf("\n");
}
