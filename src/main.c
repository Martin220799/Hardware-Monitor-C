/* Required because -std=c11 (unlike -std=gnu11) hides POSIX extensions
 * such as nanosleep by default. Must appear before all includes. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <time.h>
#include <string.h>

#include "cpu_stat.h"
#include "mem_stat.h"
#include "gpu_stat.h"
#include "thermal.h"
#include "display.h"

/* Refresh interval in milliseconds */
#define REFRESH_INTERVAL_MS 500

/* Reference temperature for bar scaling (°C at 100% fill).
 * 90°C is a conservative approximation of typical Tjmax values of modern
 * x86 CPUs; for more accurate values one would read the actual critical
 * temperature per chip model from hwmon (temp*_crit) - deliberately
 * simplified here to keep the bar scaling easy to follow. */
#define THERMAL_SCALE_MAX_C 90.0

static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int signum) {
    (void)signum;
    g_running = 0;
}

static void sleep_ms(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

int main(void) {
    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    cpu_stat_snapshot_t prev_cpu, curr_cpu;
    cpu_stat_status_t st = cpu_stat_read(&prev_cpu);
    if (st != CPU_STAT_OK) {
        fprintf(stderr, "Error: could not read /proc/stat (code %d)\n", st);
        return EXIT_FAILURE;
    }

    display_enter();

    /* The first measurement needs a time window for a meaningful delta computation */
    sleep_ms(REFRESH_INTERVAL_MS);

    while (g_running) {
        st = cpu_stat_read(&curr_cpu);
        if (st != CPU_STAT_OK) {
            display_leave();
            fprintf(stderr, "Error: could not read /proc/stat (code %d)\n", st);
            return EXIT_FAILURE;
        }

        mem_stat_info_t mem_info;
        mem_stat_status_t mem_st = mem_stat_read(&mem_info);

        thermal_snapshot_t thermal_snap;
        thermal_status_t thermal_st = thermal_read(&thermal_snap);

        display_begin_frame();

        printf("=== hwmon - Hardware Monitor ===  (Ctrl+C to quit)" DISPLAY_CLEAR_EOL "\n" DISPLAY_CLEAR_EOL "\n");

        /* Aggregate CPU usage */
        double total_percent;
        if (cpu_stat_usage_percent(&prev_cpu, &curr_cpu, -1, &total_percent) == CPU_STAT_OK) {
            display_render_bar("CPU total", total_percent, "%", total_percent);
        }

        /* Per-core usage */
        size_t core_count = curr_cpu.core_count;
        for (size_t i = 0; i < core_count; i++) {
            double core_percent;
            if (cpu_stat_usage_percent(&prev_cpu, &curr_cpu, (int)i, &core_percent) == CPU_STAT_OK) {
                char label[32];
                snprintf(label, sizeof(label), "  CPU core %zu", i);
                display_render_bar(label, core_percent, "%", core_percent);
            }
        }

        printf(DISPLAY_CLEAR_EOL "\n");

        /* GPU utilization (shown between CPU and RAM). Omitted entirely when
         * no GPU busy counter is available (e.g. NVIDIA proprietary driver). */
        gpu_stat_snapshot_t gpu_snap;
        if (gpu_stat_read(&gpu_snap) == GPU_STAT_OK) {
            for (size_t i = 0; i < gpu_snap.count; i++) {
                display_render_bar(gpu_snap.gpus[i].label,
                                   gpu_snap.gpus[i].busy_percent, "%",
                                   gpu_snap.gpus[i].busy_percent);
            }
            printf(DISPLAY_CLEAR_EOL "\n");
        }

        /* RAM */
        if (mem_st == MEM_STAT_OK) {
            display_render_bar("RAM", mem_info.used_percent, "%", mem_info.used_percent);
        } else {
            printf("%-28s [ error reading /proc/meminfo ]" DISPLAY_CLEAR_EOL "\n", "RAM");
        }

        printf(DISPLAY_CLEAR_EOL "\n");

        /* Temperature sensors */
        if (thermal_st == THERMAL_OK) {
            for (size_t i = 0; i < thermal_snap.count; i++) {
                double temp = thermal_snap.sensors[i].celsius;
                double scaled_percent = (temp / THERMAL_SCALE_MAX_C) * 100.0;
                display_render_bar(thermal_snap.sensors[i].label, scaled_percent, "°C", temp);
            }
        } else {
            printf("Temperature sensors: none found (e.g. virtualized environment without hwmon/thermal_zone)" DISPLAY_CLEAR_EOL "\n");
        }

        display_end_frame();

        prev_cpu = curr_cpu;
        sleep_ms(REFRESH_INTERVAL_MS);
    }

    display_leave();
    printf("Stopped.\n");
    return EXIT_SUCCESS;
}
