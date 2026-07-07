# hwmon

A command-line tool for real-time monitoring of CPU, GPU and RAM usage,
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
