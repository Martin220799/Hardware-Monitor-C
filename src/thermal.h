#ifndef THERMAL_H
#define THERMAL_H

#include <stddef.h>

#define THERMAL_MAX_SENSORS 64
/* Final label field size (e.g. "coretemp: Package id 0") */
#define THERMAL_LABEL_LEN 128
/* Size for raw chip-name/channel-label values before concatenation -
 * deliberately chosen smaller than THERMAL_LABEL_LEN so that "<a>: <b>"
 * is guaranteed to fit into THERMAL_LABEL_LEN without truncation
 * (compiler-verifiable). */
#define THERMAL_NAME_LEN 48

typedef enum {
    THERMAL_OK = 0,
    THERMAL_ERR_NONE_FOUND = -1 /* No sensor found (e.g. in a VM/container) */
} thermal_status_t;

typedef struct {
    char label[THERMAL_LABEL_LEN]; /* e.g. "coretemp Package id 0" or "thermal_zone0" */
    double celsius;
} thermal_reading_t;

typedef struct {
    thermal_reading_t sensors[THERMAL_MAX_SENSORS];
    size_t count;
} thermal_snapshot_t;

/*
 * Scans sysfs directories (hwmon and thermal) for available temperature
 * sensors and reads their current values.
 * Out-parameter pattern: result via pointer, status as return value.
 *
 * Returns THERMAL_ERR_NONE_FOUND when no sensors were found (e.g. in
 * virtualized environments) - this is not a hard error; the caller should
 * handle it gracefully (e.g. display "n/a").
 */
thermal_status_t thermal_read(thermal_snapshot_t *out_snapshot);

#endif /* THERMAL_H */
