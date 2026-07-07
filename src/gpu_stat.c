#include "gpu_stat.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Reads gpu_busy_percent (0..100) from an hwmon chip's backing device.
 * amdgpu and some Intel GPUs expose this file; it lives on the same device
 * as the GPU's temperature sensors, which keeps the "GPU N" numbering here
 * consistent with the thermal labels. Returns 1 on a valid read. */
static int read_busy_percent(const char *hwmon_dirname, int *out_percent) {
    char path[600];
    snprintf(path, sizeof(path),
             "/sys/class/hwmon/%s/device/gpu_busy_percent", hwmon_dirname);
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    int value = -1;
    int matched = fscanf(fp, "%d", &value);
    fclose(fp);
    if (matched != 1 || value < 0 || value > 100) {
        return 0;
    }
    *out_percent = value;
    return 1;
}

/* Raw record used to sort GPUs by hwmon index before assigning "GPU N"
 * labels, so the numbering matches thermal.c (which also orders GPU
 * instances by hwmon index). */
typedef struct {
    int hwmon_index;
    int busy_percent;
} gpu_raw_t;

static int compare_by_index(const void *a, const void *b) {
    const gpu_raw_t *ra = a;
    const gpu_raw_t *rb = b;
    return ra->hwmon_index - rb->hwmon_index;
}

gpu_stat_status_t gpu_stat_read(gpu_stat_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return GPU_STAT_ERR_NONE_FOUND;
    }
    memset(out_snapshot, 0, sizeof(*out_snapshot));

    DIR *dir = opendir("/sys/class/hwmon");
    if (dir == NULL) {
        return GPU_STAT_ERR_NONE_FOUND;
    }

    gpu_raw_t raw[GPU_STAT_MAX];
    size_t count = 0;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL && count < GPU_STAT_MAX) {
        if (entry->d_name[0] == '.') {
            continue;
        }
        int busy;
        if (!read_busy_percent(entry->d_name, &busy)) {
            continue;
        }
        int index = -1;
        sscanf(entry->d_name, "hwmon%d", &index);
        raw[count].hwmon_index = index;
        raw[count].busy_percent = busy;
        count++;
    }
    closedir(dir);

    if (count == 0) {
        return GPU_STAT_ERR_NONE_FOUND;
    }

    qsort(raw, count, sizeof(raw[0]), compare_by_index);

    for (size_t i = 0; i < count; i++) {
        gpu_reading_t *gpu = &out_snapshot->gpus[i];
        if (count == 1) {
            snprintf(gpu->label, GPU_STAT_LABEL_LEN, "GPU");
        } else {
            snprintf(gpu->label, GPU_STAT_LABEL_LEN, "GPU %zu", i + 1);
        }
        gpu->busy_percent = (double)raw[i].busy_percent;
    }
    out_snapshot->count = count;

    return GPU_STAT_OK;
}
