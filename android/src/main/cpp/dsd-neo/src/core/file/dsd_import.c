// SPDX-License-Identifier: ISC
#include <dsd-neo/core/constants.h>
#include <dsd-neo/core/opts.h>
#include <dsd-neo/core/state.h>
#include <dsd-neo/protocol/dmr/dmr_utils_api.h>
#include <dsd-neo/runtime/log.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BSIZE 999

int
csvGroupImport(dsd_opts* opts, dsd_state* state) {
    char filename[1024] = "filename.csv";
    snprintf(filename, sizeof filename, "%s", opts->group_in_file);
    //filename[1023] = '\0'; //necessary?
    char buffer[BSIZE];
    FILE* fp;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG_ERROR("Unable to open group file '%s'\n", filename);
        return -1;
    }
    int row_count = 0;
    int field_count = 0;
    long int group_number = 0; //local group number for array index value
    UNUSED(group_number);
    int i = 0;
    while (fgets(buffer, BSIZE, fp)) {
        field_count = 0;
        row_count++;
        if (row_count == 1) {
            continue; //don't want labels
        }
        char* field = strtok(buffer, ","); //seperate by comma
        while (field) {

            if (field_count == 0) {
                //group_number = atol(field);
                state->group_array[i].groupNumber = atol(field);
                LOG_INFO("%ld, ", state->group_array[i].groupNumber);
            }
            if (field_count == 1) {
                strncpy(state->group_array[i].groupMode, field, sizeof(state->group_array[i].groupMode) - 1);
                state->group_array[i].groupMode[sizeof(state->group_array[i].groupMode) - 1] = '\0';
                LOG_INFO("%s, ", state->group_array[i].groupMode);
            }
            if (field_count == 2) {
                strncpy(state->group_array[i].groupName, field, sizeof(state->group_array[i].groupName) - 1);
                state->group_array[i].groupName[sizeof(state->group_array[i].groupName) - 1] = '\0';
                LOG_INFO("%s ", state->group_array[i].groupName);
            }

            field = strtok(NULL, ",");
            field_count++;
        }
        LOG_INFO("\n");
        i++;
        state->group_tally++;
    }
    fclose(fp);
    return 0;
}

//LCN import for EDACS, migrated to channel map (channel map does both)
int
csvLCNImport(dsd_opts* opts, dsd_state* state) {
    char filename[1024] = "filename.csv";
    snprintf(filename, sizeof filename, "%s", opts->lcn_in_file);
    //filename[1023] = '\0'; //necessary?
    char buffer[BSIZE];
    FILE* fp;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG_ERROR("Unable to open lcn file '%s'\n", filename);
        return -1;
    }
    int row_count = 0;
    int field_count = 0;

    while (fgets(buffer, BSIZE, fp)) {
        field_count = 0;
        row_count++;
        if (row_count == 1) {
            continue; //don't want labels
        }
        char* field = strtok(buffer, ","); //seperate by comma
        while (field) {

            state->trunk_lcn_freq[field_count] = atol(field);
            state->lcn_freq_count++; //keep tally of number of Frequencies imported
            LOG_INFO("LCN [%d] [%ld]", field_count + 1, state->trunk_lcn_freq[field_count]);
            LOG_INFO("\n");

            field = strtok(NULL, ",");
            field_count++;
        }
        LOG_INFO("LCN Count %d\n", state->lcn_freq_count);
    }
    fclose(fp);
    return 0;
}

int
csvChanImport(dsd_opts* opts, dsd_state* state) //channel map import
{
    char filename[1024] = "filename.csv";
    snprintf(filename, sizeof filename, "%s", opts->chan_in_file);

    char buffer[BSIZE];
    FILE* fp;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG_ERROR("Unable to open channel map file '%s'\n", filename);
        return -1;
    }
    int row_count = 0;
    int field_count = 0;

    long int chan_number = 0;

    while (fgets(buffer, BSIZE, fp)) {
        field_count = 0;
        row_count++;
        if (row_count == 1) {
            continue; //don't want labels
        }
        char* field = strtok(buffer, ","); //seperate by comma
        while (field) {

            if (field_count == 0) {
                sscanf(field, "%ld", &chan_number);
            }

            if (field_count == 1) {
                if (chan_number >= 0 && chan_number < 0xFFFF) {
                    sscanf(field, "%ld", &state->trunk_chan_map[chan_number]);
                }
                // adding this should be compatible with EDACS, test and obsolete the LCN Import function if desired
                if (state->lcn_freq_count >= 0
                    && state->lcn_freq_count
                           < (int)(sizeof(state->trunk_lcn_freq) / sizeof(state->trunk_lcn_freq[0]))) {
                    sscanf(field, "%ld", &state->trunk_lcn_freq[state->lcn_freq_count]);
                    state->lcn_freq_count++; // keep tally of number of Frequencies imported
                }
            }

            field = strtok(NULL, ",");
            field_count++;
        }
        if (field_count >= 2 && chan_number >= 0 && chan_number < 0xFFFF) {
            LOG_INFO("Channel [%05ld] [%09ld]", chan_number, state->trunk_chan_map[chan_number]);
        }
        LOG_INFO("\n");
    }
    fclose(fp);
    return 0;
}

//Decimal Variant of Key Import
int
csvKeyImportDec(dsd_opts* opts, dsd_state* state) //multi-key support
{
    char filename[1024] = "filename.csv";
    snprintf(filename, sizeof filename, "%s", opts->key_in_file);

    char buffer[BSIZE];
    FILE* fp;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG_ERROR("Unable to open file '%s'\n", filename);
        return -1;
    }
    int row_count = 0;
    int field_count = 0;

    unsigned long long int keynumber = 0;
    unsigned long long int keyvalue = 0;

    uint16_t hash = 0;
    uint8_t hash_bits[24];
    memset(hash_bits, 0, sizeof(hash_bits));

    while (fgets(buffer, BSIZE, fp)) {
        field_count = 0;
        row_count++;
        if (row_count == 1) {
            continue; //don't want labels
        }
        char* field = strtok(buffer, ","); //seperate by comma
        while (field) {

            if (field_count == 0) {
                sscanf(field, "%llu", &keynumber);
                if (keynumber > 0xFFFF) //if larger than 16-bits, get its hash instead
                {
                    keynumber = keynumber & 0xFFFFFF; //truncate to 24-bits (max allowed)
                    for (int i = 0; i < 24; i++) {
                        hash_bits[i] = ((keynumber << i) & 0x800000) >> 23; //load into array for CRC16
                    }
                    hash = ComputeCrcCCITT16d(hash_bits, 24);
                    keynumber = hash & 0xFFFF; //make sure its no larger than 16-bits
                    LOG_INFO("Hashed ");
                }
            }

            if (field_count == 1) {
                sscanf(field, "%llu", &keyvalue);
                if (keynumber < 0x1FFFFULL) {
                    state->rkey_array[keynumber] = keyvalue & 0xFFFFFFFFFF; // doesn't exceed 40-bit value
                }
            }

            field = strtok(NULL, ",");
            field_count++;
        }
        LOG_INFO("Key [%03lld] [%05lld]", keynumber, state->rkey_array[keynumber]);
        LOG_INFO("\n");
    }
    fclose(fp);
    return 0;
}

//Hex Variant of Key Import
int
csvKeyImportHex(dsd_opts* opts, dsd_state* state) //key import for hex keys
{
    char filename[1024] = "filename.csv";
    snprintf(filename, sizeof filename, "%s", opts->key_in_file);
    char buffer[BSIZE];
    FILE* fp;
    fp = fopen(filename, "r");
    if (fp == NULL) {
        LOG_ERROR("Unable to open file '%s'\n", filename);
        return -1;
    }
    int row_count = 0;
    int field_count = 0;
    unsigned long long int keynumber = 0;

    while (fgets(buffer, BSIZE, fp)) {
        field_count = 0;
        row_count++;
        if (row_count == 1) {
            continue; //don't want labels
        }
        char* field = strtok(buffer, ","); //seperate by comma
        while (field) {

            if (field_count == 0) {
                sscanf(field, "%llX", &keynumber);
            }

            if (field_count == 1) {
                if (keynumber < 0x1FFFFULL) {
                    sscanf(field, "%llX", &state->rkey_array[keynumber]);
                }
            }

            //this could also theoretically nuke other keys that are at the same offset
            if (field_count == 2) {
                if (keynumber + 0x101 < 0x1FFFF) {
                    sscanf(field, "%llX", &state->rkey_array[keynumber + 0x101]);
                }
            }

            if (field_count == 3) {
                if (keynumber + 0x201 < 0x1FFFF) {
                    sscanf(field, "%llX", &state->rkey_array[keynumber + 0x201]);
                }
            }

            if (field_count == 4) {
                if (keynumber + 0x301 < 0x1FFFF) {
                    sscanf(field, "%llX", &state->rkey_array[keynumber + 0x301]);
                }
            }

            field = strtok(NULL, ",");
            field_count++;
        }

        if (keynumber < 0x1FFFFULL) {
            LOG_INFO("Key [%04llX] [%016llX]", keynumber, state->rkey_array[keynumber]);
        } else {
            LOG_INFO("Key [%04llX] [out-of-range]", keynumber);
        }

        // If longer key is loaded (or clash with the 0x101, 0x201, 0x301 offset), then print the full key listing.
        if (keynumber < 0x1FFFFULL) {
            unsigned long long out1 = 0, out2 = 0, out3 = 0;
            unsigned long long idx1 = keynumber + 0x101ULL;
            unsigned long long idx2 = keynumber + 0x201ULL;
            unsigned long long idx3 = keynumber + 0x301ULL;
            if (idx1 < 0x1FFFFULL) {
                out1 = state->rkey_array[idx1];
            }
            if (idx2 < 0x1FFFFULL) {
                out2 = state->rkey_array[idx2];
            }
            if (idx3 < 0x1FFFFULL) {
                out3 = state->rkey_array[idx3];
            }
            if (out1 != 0 || out2 != 0 || out3 != 0) {
                LOG_INFO(" [%016llX] [%016llX] [%016llX]", out1, out2, out3);
            }
        }
        LOG_INFO("\n");
    }
    fclose(fp);
    return 0;
}
