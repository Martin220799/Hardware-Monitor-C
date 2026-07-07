#include "cpu_stat.h"

#include <assert.h>
#include <stdio.h>
#include <math.h>

static int double_eq(double a, double b, double eps) {
    return fabs(a - b) < eps;
}

/* Tests the pure computation logic with synthetic snapshots,
 * independent of the test system's actual /proc/stat. */
static void test_usage_percent_basic(void) {
    cpu_stat_snapshot_t prev = {0};
    cpu_stat_snapshot_t curr = {0};

    /* Simulates: 100 ticks elapsed, 30 of them idle -> 70% usage */
    prev.aggregate.user = 0;
    prev.aggregate.idle = 0;
    curr.aggregate.user = 70;
    curr.aggregate.idle = 30;

    double percent;
    cpu_stat_status_t st = cpu_stat_usage_percent(&prev, &curr, -1, &percent);

    assert(st == CPU_STAT_OK);
    assert(double_eq(percent, 70.0, 0.01));
    printf("test_usage_percent_basic: OK\n");
}

static void test_usage_percent_no_time_elapsed(void) {
    cpu_stat_snapshot_t prev = {0};
    cpu_stat_snapshot_t curr = {0};

    prev.aggregate.user = 50;
    curr.aggregate.user = 50; /* Identical values -> no delta */

    double percent;
    cpu_stat_status_t st = cpu_stat_usage_percent(&prev, &curr, -1, &percent);

    assert(st == CPU_STAT_OK);
    assert(double_eq(percent, 0.0, 0.01));
    printf("test_usage_percent_no_time_elapsed: OK\n");
}

static void test_usage_percent_fully_idle(void) {
    cpu_stat_snapshot_t prev = {0};
    cpu_stat_snapshot_t curr = {0};

    curr.aggregate.idle = 100; /* Everything idle -> 0% usage */

    double percent;
    cpu_stat_status_t st = cpu_stat_usage_percent(&prev, &curr, -1, &percent);

    assert(st == CPU_STAT_OK);
    assert(double_eq(percent, 0.0, 0.01));
    printf("test_usage_percent_fully_idle: OK\n");
}

static void test_usage_percent_invalid_core_index(void) {
    cpu_stat_snapshot_t prev = {0};
    cpu_stat_snapshot_t curr = {0};
    prev.core_count = 2;
    curr.core_count = 2;

    double percent;
    /* Core index 5 does not exist with only 2 measured cores */
    cpu_stat_status_t st = cpu_stat_usage_percent(&prev, &curr, 5, &percent);

    assert(st != CPU_STAT_OK);
    printf("test_usage_percent_invalid_core_index: OK\n");
}

/* Integration test: reads the test system's real /proc/stat.
 * Only checks structure (success, aggregate line plausible), since the
 * exact value is platform-dependent. */
static void test_read_live_proc_stat(void) {
    cpu_stat_snapshot_t snap;
    cpu_stat_status_t st = cpu_stat_read(&snap);

    assert(st == CPU_STAT_OK);
    assert(snap.core_count > 0);
    /* idle should be > 0 on any real system after boot */
    assert(snap.aggregate.idle > 0 || snap.aggregate.user > 0);
    printf("test_read_live_proc_stat: OK (%zu cores detected)\n", snap.core_count);
}

int main(void) {
    test_usage_percent_basic();
    test_usage_percent_no_time_elapsed();
    test_usage_percent_fully_idle();
    test_usage_percent_invalid_core_index();
    test_read_live_proc_stat();

    printf("\nAll cpu_stat tests passed.\n");
    return 0;
}
