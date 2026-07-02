/* Color definitions for FrobTADS.
 */
#ifndef COLORS_H
#define COLORS_H

#include "common.h"

/* Takes two FROB_* colors (foreground, background), and returns a
 * color-pair.  The result is always between 0 and 63 and suitable as an
 * argument to the curses COLOR_PAIR() macro.  Example:
 *
 *   COLOR_PAIR(makeColorPair(FROB_GREEN, FROB_BLACK));
 */
inline int
makeColorPair(int fg, int bg)
{
    int res = fg + (bg << 3);
    // Colorpair 0 is wired to white on black and cannot be changed.
    // Therefore, we cannot use 0 for black on black; we'll exchange
    // it with pair 7, which is white on black.
    if (res == 0) return 7;
    if (res == 7) return 0;
    return res;
}

#define FROB_BLACK   0
#define FROB_RED     1
#define FROB_GREEN   2
#define FROB_YELLOW  3
#define FROB_BLUE    4
#define FROB_MAGENTA 5
#define FROB_CYAN    6
#define FROB_WHITE   7

/* Bit layout for the portable (non-curses) color+attribute encoding.
 * ossgetcolor() returns this instead of a curses COLOR_PAIR()-based
 * encoding when running in a linear (os_f_plain) interface, such as
 * plain or ansi mode, where there's no curses color-pair table to look
 * up.  A foreground and background color (0-7, same FROB_* values as
 * above) plus three attribute flags; kept in low bits that can never
 * collide with curses' own (much higher) attribute bits, so the two
 * encodings can coexist in the same function.
 */
#define FROB_PORTABLE_FG(c)        ((c) & 0x7)
#define FROB_PORTABLE_BG(c)        (((c) >> 3) & 0x7)
#define FROB_PORTABLE_HAVE_COLOR   (1 << 6)
#define FROB_PORTABLE_BOLD         (1 << 7)
#define FROB_PORTABLE_REVERSE      (1 << 8)
#define FROB_PORTABLE_INVIS        (1 << 9)

#endif // COLORS_H
