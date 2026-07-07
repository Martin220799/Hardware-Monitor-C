#ifndef DISPLAY_H
#define DISPLAY_H

/* Width of the bar area in characters (excluding label/percentage) */
#define DISPLAY_BAR_WIDTH 40

/* ANSI "erase from cursor to end of line". Append this before the newline
 * of any manually printed line so that leftover characters from a wider
 * previous frame are cleared during in-place overwrite. */
#define DISPLAY_CLEAR_EOL "\033[K"

/*
 * Renders a single colored bar for a percentage value (0-100).
 * label:   caption to the left of the bar (e.g. "CPU0", "coretemp: Core 0")
 * percent: value 0.0-100.0 that determines both the bar fill AND the color
 * unit:    unit for the value display on the right (e.g. "%", "°C")
 * value:   the raw value to display (may differ from percent, e.g. for °C)
 *
 * Color scheme: green (< 60%), yellow (60-85%), red (> 85%) - thresholds
 * apply relative to the passed percent value.
 * The rendered line clears to end-of-line so it overwrites cleanly.
 */
void display_render_bar(const char *label, double percent, const char *unit, double value);

/*
 * Frame model for flicker-free, in-place updates (like htop/top):
 *
 *   display_enter();                 // once at startup
 *   while (running) {
 *       display_begin_frame();       // cursor home, no screen blanking
 *       ... print lines ...
 *       display_end_frame();         // clear any leftover rows + flush
 *   }
 *   display_leave();                 // once at shutdown
 *
 * Nothing is ever fully blanked between frames: each line is painted over
 * the previous one, which is what removes the visible flicker.
 */

/* Enters the alternate screen buffer and hides the cursor. The previous
 * terminal contents (and shell scrollback) are preserved and restored by
 * display_leave(), so hwmon does not pollute the scrollback. */
void display_enter(void);

/* Restores the cursor and leaves the alternate screen buffer. Safe to call
 * from a signal handler path; always call it before exiting. */
void display_leave(void);

/* Moves the cursor to the top-left without clearing, so the next frame
 * overwrites the previous one in place. */
void display_begin_frame(void);

/* Erases any rows left over below the current frame (e.g. when the sensor
 * count shrank) and flushes stdout so the frame appears at once. */
void display_end_frame(void);

#endif /* DISPLAY_H */
