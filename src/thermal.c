#include "thermal.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

/* Reads the entire contents of a small sysfs file into buf.
 * sysfs files do not support seek/fstat for size, hence a fixed
 * small buffer instead of dynamic allocation. */
static int read_sysfs_string(const char *path, char *buf, size_t buf_size) {
    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return 0;
    }
    char *res = fgets(buf, (int)buf_size, fp);
    fclose(fp);
    if (res == NULL) {
        return 0;
    }
    /* Strip trailing newline */
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    return 1;
}

static int read_sysfs_long(const char *path, long *out_value) {
    char buf[64];
    if (!read_sysfs_string(path, buf, sizeof(buf))) {
        return 0;
    }
    char *endptr;
    long val = strtol(buf, &endptr, 10);
    if (endptr == buf) {
        return 0; /* Not a valid number format */
    }
    *out_value = val;
    return 1;
}

/* Plausible range for a real hardware temperature reading in °C.
 * Some drivers report sentinel values for disabled or not-present channels
 * (e.g. -273.1°C = absolute-zero sentinel, or ~65261°C from a 16-bit
 * "not present" code). Readings outside this range are dropped so they do
 * not show up as bogus, pegged bars. */
#define THERMAL_MIN_PLAUSIBLE_C (-40.0)
#define THERMAL_MAX_PLAUSIBLE_C (150.0)

static int is_plausible_celsius(double celsius) {
    return celsius >= THERMAL_MIN_PLAUSIBLE_C && celsius <= THERMAL_MAX_PLAUSIBLE_C;
}

/* Maps a raw hwmon/thermal driver name to a friendly hardware category
 * (e.g. "amdgpu" -> "GPU", "k10temp" -> "CPU"). Matching is by prefix, so
 * decorated names like "r8169_0_a00:00" still resolve. Returns NULL when
 * the driver is unknown, so the caller can fall back to the raw name. */
static const char *friendly_category(const char *driver_name) {
    static const struct { const char *prefix; const char *label; } map[] = {
        { "k10temp",     "CPU" },      /* AMD CPU */
        { "zenpower",    "CPU" },      /* AMD CPU (third-party driver) */
        { "coretemp",    "CPU" },      /* Intel CPU */
        { "cpu_thermal", "CPU" },      /* ARM SoC CPU */
        { "amdgpu",      "GPU" },
        { "radeon",      "GPU" },
        { "nouveau",     "GPU" },
        { "nvidia",      "GPU" },
        { "i915",        "GPU" },      /* Intel GPU */
        { "xe",          "GPU" },      /* Intel GPU (newer driver) */
        { "nvme",        "SSD" },      /* NVMe SSD */
        { "drivetemp",   "Disk" },     /* SATA HDD/SSD */
        { "spd5118",     "RAM" },      /* DDR5 DIMM temperature sensor */
        { "jc42",        "RAM" },      /* DDR4/earlier DIMM temperature sensor */
        { "r8169",       "Ethernet" }, /* Realtek Ethernet NIC (on-chip sensor) */
        { "iwlwifi",     "Wi-Fi" },
        { "mt7921",      "Wi-Fi" },
        { "x86_pkg_temp","CPU" },      /* thermal_zone type on some systems */
        { "acpitz",      "Mainboard" },
    };
    for (size_t i = 0; i < sizeof(map) / sizeof(map[0]); i++) {
        if (strncmp(driver_name, map[i].prefix, strlen(map[i].prefix)) == 0) {
            return map[i].label;
        }
    }
    return NULL;
}

/* Returns the friendly group key for an hwmon chip, falling back to its raw
 * name when the driver is unknown. Writes into out (size out_size). */
static void chip_group(const char *chip_name, char *out, size_t out_size) {
    const char *category = friendly_category(chip_name);
    snprintf(out, out_size, "%s", category ? category : chip_name);
}

/* For a given hwmon chip, determines how many chips share its group and this
 * chip's 1-based position among them (ordered by hwmon index). Lets multiple
 * GPUs / SSDs / DIMMs be numbered "GPU 1", "GPU 2", ... Chips whose group is
 * unique get total == 1 (no numbering needed). */
static void group_instance(const char *this_dirname, const char *group,
                           int *out_total, int *out_rank) {
    int this_index = -1;
    sscanf(this_dirname, "hwmon%d", &this_index);

    int total = 0;
    int rank = 1;
    DIR *dir = opendir("/sys/class/hwmon");
    if (dir != NULL) {
        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            if (entry->d_name[0] == '.') {
                continue;
            }
            char name_path[560];
            snprintf(name_path, sizeof(name_path), "/sys/class/hwmon/%s/name", entry->d_name);
            char chip_name[THERMAL_NAME_LEN];
            if (!read_sysfs_string(name_path, chip_name, sizeof(chip_name))) {
                continue;
            }
            char other_group[THERMAL_NAME_LEN];
            chip_group(chip_name, other_group, sizeof(other_group));
            if (strcmp(other_group, group) != 0) {
                continue;
            }
            total++;
            int idx = -1;
            sscanf(entry->d_name, "hwmon%d", &idx);
            if (idx < this_index) {
                rank++; /* another same-group chip sorts before this one */
            }
        }
        closedir(dir);
    }

    *out_total = (total > 0) ? total : 1;
    *out_rank = rank;
}

/* Counts how many valid tempN_input channels a chip directory exposes, so a
 * single-channel chip can be labeled without a redundant "temp1" suffix. */
static int count_input_channels(const char *chip_path) {
    DIR *dir = opendir(chip_path);
    if (dir == NULL) {
        return 0;
    }
    int count = 0;
    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        int channel = -1;
        if (sscanf(entry->d_name, "temp%d_input", &channel) != 1) {
            continue;
        }
        char expected_name[32];
        snprintf(expected_name, sizeof(expected_name), "temp%d_input", channel);
        if (strcmp(entry->d_name, expected_name) == 0) {
            count++;
        }
    }
    closedir(dir);
    return count;
}

/* Scans /sys/class/hwmon/hwmon* for temp*_input files.
 * Each hwmon chip may have multiple temp channels (temp1, temp2, ...).
 * The chip name (name file) plus an optional tempN_label form the
 * label, e.g. "coretemp: Package id 0". */
static void scan_hwmon(thermal_snapshot_t *snap) {
    DIR *hwmon_dir = opendir("/sys/class/hwmon");
    if (hwmon_dir == NULL) {
        return;
    }

    struct dirent *chip_entry;
    while ((chip_entry = readdir(hwmon_dir)) != NULL) {
        if (chip_entry->d_name[0] == '.') {
            continue;
        }

        char chip_path[512];
        snprintf(chip_path, sizeof(chip_path), "/sys/class/hwmon/%s", chip_entry->d_name);

        char name_path[560];
        snprintf(name_path, sizeof(name_path), "%s/name", chip_path);
        char chip_name[THERMAL_NAME_LEN];
        if (!read_sysfs_string(name_path, chip_name, sizeof(chip_name))) {
            strncpy(chip_name, chip_entry->d_name, sizeof(chip_name) - 1);
            chip_name[sizeof(chip_name) - 1] = '\0';
        }

        /* Friendly hardware label, e.g. "CPU", "GPU", "RAM". When several
         * chips share a group (2 GPUs, many DIMMs, ...) number them "GPU 1",
         * "GPU 2". The +20 headroom holds " " plus a decimal int without
         * truncation. */
        char group[THERMAL_NAME_LEN];
        chip_group(chip_name, group, sizeof(group));
        int group_total, group_rank;
        group_instance(chip_entry->d_name, group, &group_total, &group_rank);

        char chip_label[THERMAL_NAME_LEN + 20];
        if (group_total > 1) {
            snprintf(chip_label, sizeof(chip_label), "%s %d", group, group_rank);
        } else {
            snprintf(chip_label, sizeof(chip_label), "%s", group);
        }

        DIR *sub_dir = opendir(chip_path);
        if (sub_dir == NULL) {
            continue;
        }

        /* A single-channel chip needs no per-channel suffix (e.g. just "RAM 1"
         * instead of "RAM 1 temp1"). */
        int channel_count = count_input_channels(chip_path);

        struct dirent *file_entry;
        while ((file_entry = readdir(sub_dir)) != NULL) {
            /* Look for files of the form "tempN_input" */
            int channel = -1;
            if (sscanf(file_entry->d_name, "temp%d_input", &channel) != 1) {
                continue;
            }
            /* sscanf returns 1 as soon as %d is assigned, even if the trailing
             * "_input" literal did NOT match - so "temp1_max", "temp1_crit",
             * etc. also pass the check above. Verify the exact filename here,
             * otherwise threshold/alarm files get read as live temperatures. */
            char expected_name[32];
            snprintf(expected_name, sizeof(expected_name), "temp%d_input", channel);
            if (strcmp(file_entry->d_name, expected_name) != 0) {
                continue;
            }

            char input_path[512 + 256 + 2];
            snprintf(input_path, sizeof(input_path), "%s/%s", chip_path, file_entry->d_name);

            long millidegrees;
            if (!read_sysfs_long(input_path, &millidegrees)) {
                continue;
            }

            /* Drop sentinel/garbage readings before consuming a sensor slot */
            double celsius = (double)millidegrees / 1000.0;
            if (!is_plausible_celsius(celsius)) {
                continue;
            }

            if (snap->count >= THERMAL_MAX_SENSORS) {
                closedir(sub_dir);
                closedir(hwmon_dir);
                return;
            }

            /* Read optional label: tempN_label */
            char label_path[600];
            snprintf(label_path, sizeof(label_path), "%s/temp%d_label", chip_path, channel);
            char channel_label[THERMAL_NAME_LEN];
            int has_label = read_sysfs_string(label_path, channel_label, sizeof(channel_label));

            thermal_reading_t *slot = &snap->sensors[snap->count];
            if (has_label) {
                /* Driver gave a meaningful channel name (e.g. "junction", "Tctl") */
                snprintf(slot->label, THERMAL_LABEL_LEN, "%s %s", chip_label, channel_label);
            } else if (channel_count > 1) {
                /* Multiple unlabeled channels: keep them apart with the index */
                snprintf(slot->label, THERMAL_LABEL_LEN, "%s temp%d", chip_label, channel);
            } else {
                /* Single unlabeled channel: the friendly label alone is enough */
                snprintf(slot->label, THERMAL_LABEL_LEN, "%s", chip_label);
            }
            slot->celsius = celsius;
            snap->count++;
        }
        closedir(sub_dir);
    }
    closedir(hwmon_dir);
}

/* Fallback: /sys/class/thermal/thermal_zone* - usually provides only a
 * single coarse ACPI zone, but on systems without hwmon (some ARM boards,
 * some VMs) this is the only source. */
static void scan_thermal_zones(thermal_snapshot_t *snap) {
    DIR *dir = opendir("/sys/class/thermal");
    if (dir == NULL) {
        return;
    }

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "thermal_zone", 12) != 0) {
            continue;
        }
        if (snap->count >= THERMAL_MAX_SENSORS) {
            break;
        }

        char temp_path[512];
        snprintf(temp_path, sizeof(temp_path), "/sys/class/thermal/%s/temp", entry->d_name);

        long millidegrees;
        if (!read_sysfs_long(temp_path, &millidegrees)) {
            continue;
        }

        double celsius = (double)millidegrees / 1000.0;
        if (!is_plausible_celsius(celsius)) {
            continue;
        }

        char type_path[512];
        snprintf(type_path, sizeof(type_path), "/sys/class/thermal/%s/type", entry->d_name);
        char zone_type[THERMAL_LABEL_LEN];
        if (!read_sysfs_string(type_path, zone_type, sizeof(zone_type))) {
            strncpy(zone_type, entry->d_name, sizeof(zone_type) - 1);
            zone_type[sizeof(zone_type) - 1] = '\0';
        }

        /* Map the zone type to a friendly label when recognized, otherwise
         * keep the raw type (which is already reasonably descriptive). */
        const char *friendly = friendly_category(zone_type);

        thermal_reading_t *slot = &snap->sensors[snap->count];
        snprintf(slot->label, THERMAL_LABEL_LEN, "%s", friendly ? friendly : zone_type);
        slot->celsius = celsius;
        snap->count++;
    }
    closedir(dir);
}

/* Display priority per hardware group, so sensors are shown grouped in a
 * meaningful order (CPU, then GPU, RAM, storage, and network/other last)
 * rather than in sysfs discovery order. Unknown groups sort to the end. */
static int category_priority(const char *label) {
    static const struct { const char *prefix; int rank; } order[] = {
        { "CPU",       0 },
        { "GPU",       1 },
        { "RAM",       2 },
        { "SSD",       3 },
        { "Disk",      4 },
        { "Wi-Fi",     5 },
        { "Ethernet",  6 },
        { "Mainboard", 7 },
    };
    for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); i++) {
        if (strncmp(label, order[i].prefix, strlen(order[i].prefix)) == 0) {
            return order[i].rank;
        }
    }
    return 8;
}

/* Orders readings by group priority, then alphabetically by label so that
 * "GPU 1" precedes "GPU 2" and channels stay grouped under their device. */
static int compare_readings(const void *a, const void *b) {
    const thermal_reading_t *ra = a;
    const thermal_reading_t *rb = b;
    int pa = category_priority(ra->label);
    int pb = category_priority(rb->label);
    if (pa != pb) {
        return pa - pb;
    }
    return strcmp(ra->label, rb->label);
}

thermal_status_t thermal_read(thermal_snapshot_t *out_snapshot) {
    if (out_snapshot == NULL) {
        return THERMAL_ERR_NONE_FOUND;
    }

    memset(out_snapshot, 0, sizeof(*out_snapshot));

    scan_hwmon(out_snapshot);

    /* Only fall back to thermal_zone if hwmon returned nothing - otherwise
     * CPU core temperatures would appear twice (under two names). */
    if (out_snapshot->count == 0) {
        scan_thermal_zones(out_snapshot);
    }

    if (out_snapshot->count == 0) {
        return THERMAL_ERR_NONE_FOUND;
    }

    qsort(out_snapshot->sensors, out_snapshot->count,
          sizeof(out_snapshot->sensors[0]), compare_readings);

    return THERMAL_OK;
}
