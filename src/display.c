#include "display.h"

#include <stdio.h>

/* ANSI color codes (standard 8/16-color mode for maximum terminal compatibility;
 * deliberately not 256-color/RGB mode, since some minimal TTYs
 * (e.g. the Linux console without X) only support the basic color set) */
#define ANSI_RESET   "\033[0m"
#define ANSI_GREEN   "\033[32m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_RED     "\033[31m"
#define ANSI_BOLD    "\033[1m"

static const char *color_for_percent(double percent) {
    if (percent < 60.0) {
        return ANSI_GREEN;
    } else if (percent < 85.0) {
        return ANSI_YELLOW;
    }
    return ANSI_RED;
}

void display_render_bar(const char *label, double percent, const char *unit, double value) {
    if (percent < 0.0) percent = 0.0;
    if (percent > 100.0) percent = 100.0;

    int filled = (int)((percent / 100.0) * DISPLAY_BAR_WIDTH + 0.5);
    if (filled > DISPLAY_BAR_WIDTH) filled = DISPLAY_BAR_WIDTH;

    const char *color = color_for_percent(percent);

    /* Label left-aligned to a fixed width so bars line up underneath each other */
    printf("%-28s %s[", label, color);

    for (int i = 0; i < DISPLAY_BAR_WIDTH; i++) {
        if (i < filled) {
            fputs("█", stdout);
        } else {
            fputs("░", stdout);
        }
    }

    /* Erase to end of line (DISPLAY_CLEAR_EOL) so a shorter value than the
     * previous frame does not leave stale trailing characters behind. */
    printf("]%s %s%6.1f%s%s" DISPLAY_CLEAR_EOL "\n", ANSI_RESET, ANSI_BOLD, value, unit, ANSI_RESET);
}

void display_enter(void) {
    /* \033[?1049h -> switch to the alternate screen buffer (like htop/less);
     *                the user's shell contents are saved and restored on exit.
     * \033[?25l   -> hide the cursor to avoid it blinking during redraws.
     * \033[H\033[J -> clear the fresh alternate buffer once at startup. */
    fputs("\033[?1049h\033[?25l\033[H\033[J", stdout);
    fflush(stdout);
}

void display_leave(void) {
    /* \033[?25h   -> show the cursor again.
     * \033[?1049l -> leave the alternate screen buffer, restoring the shell. */
    fputs("\033[?25h\033[?1049l", stdout);
    fflush(stdout);
}

void display_begin_frame(void) {
    /* \033[H -> cursor to (1,1). No screen clear: the frame is painted over
     * the previous one line by line, which is what eliminates the flicker. */
    fputs("\033[H", stdout);
}

void display_end_frame(void) {
    /* \033[J -> erase from the cursor to the end of the screen, removing any
     * rows a previous, taller frame may have left behind. Then flush so the
     * whole frame becomes visible at once rather than line by line. */
    fputs("\033[J", stdout);
    fflush(stdout);
}
