#include "mem_stat.h"

#include <assert.h>
#include <stdio.h>
#include <math.h>

static int double_eq(double a, double b, double eps) {
    return fabs(a - b) < eps;
}

/* Integration test: reads the test system's real /proc/meminfo. */
static void test_read_live_meminfo(void) {
    mem_stat_info_t info;
    mem_stat_status_t st = mem_stat_read(&info);

    assert(st == MEM_STAT_OK);
    assert(info.total_kb > 0);
    assert(info.available_kb <= info.total_kb);
    assert(info.used_kb == info.total_kb - info.available_kb);
    assert(info.used_percent >= 0.0 && info.used_percent <= 100.0);

    /* Sanity check of the percentage computation */
    double expected_percent = ((double)info.used_kb / (double)info.total_kb) * 100.0;
    assert(double_eq(info.used_percent, expected_percent, 0.01));

    printf("test_read_live_meminfo: OK (%.1f%% of %llu kB used)\n",
           info.used_percent, info.total_kb);
}

int main(void) {
    test_read_live_meminfo();

    printf("\nAll mem_stat tests passed.\n");
    return 0;
}
