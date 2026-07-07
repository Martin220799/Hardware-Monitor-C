#ifndef GPU_STAT_H
#define GPU_STAT_H

#include <stddef.h>

/* Maximum number of GPUs reported */
#define GPU_STAT_MAX 8
/* Label field size (e.g. "GPU 1") */
#define GPU_STAT_LABEL_LEN 16

typedef enum {
    GPU_STAT_OK = 0,
    GPU_STAT_ERR_NONE_FOUND = -1 /* No GPU busy-percent counter available */
} gpu_stat_status_t;

typedef struct {
    char label[GPU_STAT_LABEL_LEN]; /* "GPU" for a single GPU, else "GPU 1", "GPU 2", ... */
    double busy_percent;            /* 0.0 - 100.0 */
} gpu_reading_t;

typedef struct {
    gpu_reading_t gpus[GPU_STAT_MAX];
    size_t count;
} gpu_stat_snapshot_t;

/*
 * Reads current GPU utilization from sysfs. amdgpu (and some Intel) GPUs
 * expose an instantaneous busy percentage via gpu_busy_percent; unlike CPU
 * usage this needs no delta between snapshots.
 * Out-parameter pattern: result via pointer, status as return value.
 *
 * GPUs are numbered by hwmon index to match the "GPU N" temperature labels
 * from thermal.c. Returns GPU_STAT_ERR_NONE_FOUND when no busy counter is
 * present (e.g. the NVIDIA proprietary driver, or a VM) - not a hard error;
 * the caller should simply omit the GPU usage section.
 */
gpu_stat_status_t gpu_stat_read(gpu_stat_snapshot_t *out_snapshot);

#endif /* GPU_STAT_H */
