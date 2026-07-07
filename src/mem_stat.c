#include "mem_stat.h"

#include <stdio.h>
#include <string.h>

/* Searches /proc/meminfo for a line "Label:    <value> kB" and reads the value.
 * Returns: 1 if found, 0 if not found. */
static int find_field(FILE *fp, const char *label, unsigned long long *out_value) {
    rewind(fp);
    char line[256];
    size_t label_len = strlen(label);

    while (fgets(line, sizeof(line), fp) != NULL) {
        if (strncmp(line, label, label_len) == 0) {
            /* Field found, parse the value after the label */
            if (sscanf(line + label_len, "%llu", out_value) == 1) {
                return 1;
            }
            return 0;
        }
    }
    return 0;
}

mem_stat_status_t mem_stat_read(mem_stat_info_t *out_info) {
    if (out_info == NULL) {
        return MEM_STAT_ERR_PARSE;
    }

    FILE *fp = fopen("/proc/meminfo", "r");
    if (fp == NULL) {
        return MEM_STAT_ERR_OPEN;
    }

    unsigned long long total = 0, available = 0;
    int has_total = find_field(fp, "MemTotal:", &total);
    int has_available = find_field(fp, "MemAvailable:", &available);

    fclose(fp);

    if (!has_total || !has_available) {
        /* MemAvailable is missing on very old kernels (< 3.14); deliberately
         * treated as an error here instead of falling back to MemFree, since
         * MemFree significantly overestimates the actual usage. */
        return MEM_STAT_ERR_PARSE;
    }

    if (available > total) {
        return MEM_STAT_ERR_PARSE;
    }

    out_info->total_kb = total;
    out_info->available_kb = available;
    out_info->used_kb = total - available;
    out_info->used_percent = (total > 0)
        ? ((double)out_info->used_kb / (double)total) * 100.0
        : 0.0;

    return MEM_STAT_OK;
}
