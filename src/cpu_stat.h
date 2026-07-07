#ifndef CPU_STAT_H
#define CPU_STAT_H

#include <stddef.h>

/* Maximum number of supported CPU cores (index 0 = aggregate "cpu" line) */
#define CPU_STAT_MAX_CORES 256

typedef enum {
    CPU_STAT_OK = 0,
    CPU_STAT_ERR_OPEN = -1,
    CPU_STAT_ERR_PARSE = -2,
    CPU_STAT_ERR_OVERFLOW = -3
} cpu_stat_status_t;

/* Raw, cumulative tick values of a single line from /proc/stat */
typedef struct {
    unsigned long long user;
    unsigned long long nice;
    unsigned long long system;
    unsigned long long idle;
    unsigned long long iowait;
    unsigned long long irq;
    unsigned long long softirq;
    unsigned long long steal;
} cpu_stat_raw_t;

/* Snapshot of all cores at a point in time */
typedef struct {
    cpu_stat_raw_t cores[CPU_STAT_MAX_CORES];
    size_t core_count; /* Number of cores actually read (excluding the aggregate "cpu" line) */
    cpu_stat_raw_t aggregate; /* The "cpu" line (sum of all cores) */
} cpu_stat_snapshot_t;

/*
 * Reads the current state from /proc/stat into out_snapshot.
 * Out-parameter pattern: returns a status code, data via pointer.
 * Returns: CPU_STAT_OK on success, a negative error code otherwise.
 */
cpu_stat_status_t cpu_stat_read(cpu_stat_snapshot_t *out_snapshot);

/*
 * Computes the percentage usage (0.0 - 100.0) between two snapshots
 * for a given core index (-1 = aggregate over all cores).
 * Writes the result to out_percent.
 */
cpu_stat_status_t cpu_stat_usage_percent(const cpu_stat_snapshot_t *prev,
                                          const cpu_stat_snapshot_t *curr,
                                          int core_index,
                                          double *out_percent);

#endif /* CPU_STAT_H */
