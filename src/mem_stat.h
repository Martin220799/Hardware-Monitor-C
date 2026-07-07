#ifndef MEM_STAT_H
#define MEM_STAT_H

typedef enum {
    MEM_STAT_OK = 0,
    MEM_STAT_ERR_OPEN = -1,
    MEM_STAT_ERR_PARSE = -2
} mem_stat_status_t;

typedef struct {
    unsigned long long total_kb;
    unsigned long long available_kb; /* Accounts for cache/buffers that can be reclaimed */
    unsigned long long used_kb;      /* total - available */
    double used_percent;
} mem_stat_info_t;

/*
 * Reads /proc/meminfo and computes memory usage.
 * Out-parameter pattern: result via pointer, status as return value.
 */
mem_stat_status_t mem_stat_read(mem_stat_info_t *out_info);

#endif /* MEM_STAT_H */
