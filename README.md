# hwmon

A command-line tool for real-time monitoring of CPU usage, RAM usage,
and hardware temperatures on Linux, with color-coded ANSI bars right in
the terminal.

## Features

- **No external dependencies** – pure C11, uses only the standard library
  and Linux kernel interfaces (`/proc`, `/sys`)
- Total CPU usage as well as per-core usage
- RAM usage based on `MemAvailable` (not `MemFree` – see rationale below)
- Automatic sensor detection via `hwmon`, with a fallback to `thermal_zone`
- Color coding: green (< 60%), yellow (60–85%), red (> 85%)
- Refreshes every 1 second, low-flicker redraw

## Build

Requirements (Fedora): `sudo dnf install gcc make`, optionally
`sudo dnf install valgrind` for the memory-leak test.

```bash
make          # compiles the "hwmon" binary
make test     # runs the unit tests
make valgrind # runs hwmon under valgrind (stop with Ctrl+C)
make clean    # removes build artifacts
```

## Usage

```bash
./hwmon
```

Quit with `Ctrl+C`. The cursor is hidden on startup and automatically
restored on exit (including on SIGINT/SIGTERM).

## Architecture

```
src/
├── main.c        Event loop: snapshot -> delta computation -> render -> sleep
├── cpu_stat.c/h  Parsing of /proc/stat, delta-based usage computation
├── mem_stat.c/h  Parsing of /proc/meminfo
├── thermal.c/h   Sensor detection via hwmon/thermal_zone (sysfs)
└── display.c/h   ANSI rendering (bars, colors, cursor control)
```

All modules follow a uniform pattern: a function takes an out-parameter
(pointer to a result struct) and returns a status code, instead of
returning the struct directly. This avoids copying larger structs on
every call and makes error cases explicitly checkable.

### Why `MemAvailable` instead of `MemFree`?

`MemFree` only counts completely unused memory. Linux actively uses free
RAM for file caches (`Cached`, `Buffers`), which can be released
immediately when needed. `MemAvailable` (since kernel 3.14) estimates how
much memory would actually be available for new applications without
swapping. Without this distinction, the usage display would falsely
appear near 100% on any long-running Linux system, even when sufficient
memory is in fact available.

### CPU usage computation

`/proc/stat` provides cumulative tick counters since system startup, not
instantaneous values. Usage is derived from two measurements taken a
short time apart:

```
usage% = (1 - Δidle / Δtotal) × 100
```

where `Δidle` is the difference of `idle + iowait` and `Δtotal` is the
difference of all time fields between the two snapshots.

### Temperature scaling

Since there is no uniform "100%" reference point for temperatures, the
bar is scaled to a configurable maximum value (`THERMAL_SCALE_MAX_C`,
default 90°C). This is a deliberate simplification – for more accurate
values one would have to read each sensor chip's individual critical
temperature (`temp*_crit` in hwmon).

### Sensor detection: hwmon vs. thermal_zone

Linux exposes temperature sensors via two parallel interfaces:

- **`/sys/class/hwmon/hwmon*/temp*_input`** – granular, often multiple
  channels per chip (e.g. `coretemp` with one value per CPU core)
- **`/sys/class/thermal/thermal_zone*/temp`** – usually only a single
  coarse ACPI zone, but on some systems (ARM boards, some VMs) the only
  available source

`hwmon` is searched first; only if nothing is found there does the
fallback to `thermal_zone` happen. This prevents the same physical
sensors from being displayed twice under two names.

In virtualized environments without physical hardware access (e.g.
cloud VMs, containers) there may be no sensors present – the tool
displays this explicitly instead of crashing or emitting phantom values.

## Verification

- `gcc -std=c11 -Wall -Wextra -Wpedantic`: no warnings
- `make test`: all unit tests green (pure computation logic +
  integration tests against the live `/proc` filesystem)
- `valgrind --leak-check=full`: no leaks (`0 errors from 0 contexts`)

## Known limitations

- The temperature scale is a fixed approximation, not a chip-specific
  `temp*_crit` evaluation
- No configuration file (interval and scaling maximum are compile-time
  constants in `main.c`)
- Sensor-label truncation possible with very long chip/channel names
  (> 48 characters per field); affects display only
