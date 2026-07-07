#include "cpu_stat.h"

#include <stdio.h>
#include <string.h>

/* Parses a single /proc/stat line of the form:
 * "cpu0 18 0 670 602 13 0 2 0 0 0"
 * The last two fields (guest, guest_nice) are ignored, since they are
 * already included in user/nice (see proc(5)).
 */
static int parse_stat_line(const char *line, cpu_stat_raw_t *out) {
    unsigned long long user, nice, system, idle, iowait, irq, softirq, steal;
    /* %*s skips the label token (e.g. "cpu0") */
    int matched = sscanf(line,
                          "%*s %llu %llu %llu %llu %llu %llu %llu %llu",
                          &user, &nice, &system, &idle,
                          &iowait, &irq, &softirq, &steal);
    if (matched < 8) {
        return CPU_STAT_ERR_PARSE;
    }
    out->user = user;
    out->nice = nice;
    out->system = system;
    out->idle = idle;
    out->iowait = iowait;
    out->irq = irq;
    out->softirq = softirq;
    out->steal = steal;
    return CPU_STAT_OK;
}

cpu_stat_status_t cpu_stat_read(cpu_stat_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return CPU_STAT_ERR_PARSE;
    }

    FILE *fp = fopen("/proc/stat", "r");
    if (fp == NULL) {
        return CPU_STAT_ERR_OPEN;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));

    char line[512];
    int aggregate_read = 0;

    while (fgets(line, sizeof(line), fp) != NULL) {
        /* Only lines starting with "cpu" are relevant; after them come
         * other statistics (intr, ctxt, btime, ...) which we ignore. */
        if (strncmp(line, "cpu", 3) != 0) {
            break;
        }

        if (line[3] == ' ') {
            /* Aggregate line "cpu " */
            if (parse_stat_line(line, &out_snapshot->aggregate) != CPU_STAT_OK) {
                fclose(fp);
                return CPU_STAT_ERR_PARSE;
            }
            aggregate_read = 1;
        } else {
            /* Single-core line "cpuN " */
            if (out_snapshot->core_count >= CPU_STAT_MAX_CORES) {
                fclose(fp);
                return CPU_STAT_ERR_OVERFLOW;
            }
            cpu_stat_raw_t *slot = &out_snapshot->cores[out_snapshot->core_count];
            if (parse_stat_line(line, slot) != CPU_STAT_OK) {
                fclose(fp);
                return CPU_STAT_ERR_PARSE;
            }
            out_snapshot->core_count++;
        }
    }

    fclose(fp);

    if (!aggregate_read) {
        return CPU_STAT_ERR_PARSE;
    }

    return CPU_STAT_OK;
}

/* Computes usage from two raw snapshots by taking differences.
 * idle_time = idle + iowait (both "not working")
 * total_time = sum of all fields
 * usage% = (1 - delta_idle / delta_total) * 100
 */
static cpu_stat_status_t compute_percent_from_raw(const cpu_stat_raw_t *prev,
                                                    const cpu_stat_raw_t *curr,
                                                    double *out_percent) {
    unsigned long long prev_idle = prev->idle + prev->iowait;
    unsigned long long curr_idle = curr->idle + curr->iowait;

    unsigned long long prev_total = prev->user + prev->nice + prev->system +
                                     prev->idle + prev->iowait + prev->irq +
                                     prev->softirq + prev->steal;
    unsigned long long curr_total = curr->user + curr->nice + curr->system +
                                     curr->idle + curr->iowait + curr->irq +
                                     curr->softirq + curr->steal;

    if (curr_total < prev_total) {
        /* Counter wraparound or inconsistent snapshots */
        return CPU_STAT_ERR_PARSE;
    }

    unsigned long long delta_total = curr_total - prev_total;
    unsigned long long delta_idle = curr_idle - prev_idle;

    if (delta_total == 0) {
        /* No time progressed between the measurements */
        *out_percent = 0.0;
        return CPU_STAT_OK;
    }

    double idle_ratio = (double)delta_idle / (double)delta_total;
    *out_percent = (1.0 - idle_ratio) * 100.0;

    /* Numerical safeguard against rounding artifacts */
    if (*out_percent < 0.0) *out_percent = 0.0;
    if (*out_percent > 100.0) *out_percent = 100.0;

    return CPU_STAT_OK;
}

cpu_stat_status_t cpu_stat_usage_percent(const cpu_stat_snapshot_t *prev,
                                          const cpu_stat_snapshot_t *curr,
                                          int core_index,
                                          double *out_percent) {
    if (prev == NULL || curr == NULL || out_percent == NULL) {
        return CPU_STAT_ERR_PARSE;
    }

    if (core_index < 0) {
        return compute_percent_from_raw(&prev->aggregate, &curr->aggregate, out_percent);
    }

    size_t idx = (size_t)core_index;
    if (idx >= prev->core_count || idx >= curr->core_count) {
        return CPU_STAT_ERR_PARSE;
    }

    return compute_percent_from_raw(&prev->cores[idx], &curr->cores[idx], out_percent);
}
