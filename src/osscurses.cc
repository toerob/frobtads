/* This file implements the functions described in tads2/osgen.h.
 */
#include "common.h"

#include <sys/time.h>
#include <unistd.h>
#include <string.h> // FD_SET is a macro for memset()
#include <string>

#include "frobtadsapp.h"

#include "os.h"
extern "C" {
#include "osgen.h"
}

#include "colors.h"
#include "frobcurses.h"

/* Defined in tads2/osgen3.c; true whenever the active interface is a
 * linear (non-cursor-addressed) one, i.e. plain or ansi mode.
 */
extern "C" int os_f_plain;


/* Determine whether the active local character set is UTF-8.  See the
 * declaration in osgen.h for why the generic input line editor needs this.
 */
int
oss_utf8_mode( void )
{
    char mapname[32];
    os_get_charmap(mapname, 0);
    return not stricmp(mapname, "utf-8") or not stricmp(mapname, "utf8");
}


/* Translate a portable color specification to an oss-style color code.
 *
 * We treat colors as another form of attribute, just like curses does.
 * Therefore, the return value is an OR'ed together combination of
 * curses attributes that include information for both color as well as
 * hilite.
 *
 * Note that we cannot support all the 16 ANSI colors, since curses has
 * only 8 colors.  The other 8 are normally obtained by using the A_BOLD
 * attribute.  Since A_BOLD is reserved for the TADS OS_ATTR_BOLD
 * attribute, we can only use the 8 low intensity ANSI colors.  Using
 * A_BOLD to support the other 8 would leave us no way to display the
 * TADS OS_ATTR_BOLD attribute.  A solution to this would be to use
 * A_UNDERLINE instead of A_BOLD for bold text.  But a) not all
 * terminals support this and b) it looks ugly.  Using A_HILITE is no
 * solution either, as most terminals treat it the same as A_REVERSE.
 * If you ask me, 8 colors should be more than enough for everyone [sic].
 * Back in my days we had only 2 of them, you know.
 */
int
ossgetcolor( int fg, int bg, int attrs, int screen_color )
{
    int f, b, ret;

    // If we don't have color-support enabled, just check if the
    // caller wants to know the statusline colors, in which case we
    // should use the A_REVERSE attribute, wants invisible text, in
    // which case we use the A_INVIS attribute, and if bold is to be
    // used.
    if (not globalApp->colorsEnabled()) {
        f = fg;
        b = (bg == OSGEN_COLOR_TRANSPARENT) ? screen_color : bg;

        // Linear (os_f_plain) interfaces such as plain and ansi mode
        // don't have a curses attr_t to encode into; use our own
        // portable bit layout instead (see colors.h).
        if (os_f_plain) {
            ret = 0;
            if (b == OSGEN_COLOR_STATUSBG)
                ret |= FROB_PORTABLE_REVERSE;
            else if (f == b)
                ret |= FROB_PORTABLE_INVIS;
            if (attrs & OS_ATTR_HILITE) ret |= FROB_PORTABLE_BOLD;
            return ret;
        }

        if (b == OSGEN_COLOR_STATUSBG)
            // It's the statusline; reverse it.
            ret = A_REVERSE;
        else if (f == b)
            // The caller wants invisible text.
            ret = A_INVIS;
        else
            // Don't know, don't care.
            ret = A_NORMAL;
        // Check for bold.
        if (attrs & OS_ATTR_HILITE) ret |= A_BOLD;
        return ret;
    }

    switch (fg) {
      case OSGEN_COLOR_BLACK:
      case OSGEN_COLOR_GRAY:       f = FROB_BLACK; break;

      case OSGEN_COLOR_MAROON:
      case OSGEN_COLOR_RED:        f = FROB_RED; break;

      case OSGEN_COLOR_GREEN:
      case OSGEN_COLOR_LIME:       f = FROB_GREEN; break;

      case OSGEN_COLOR_NAVY:
      case OSGEN_COLOR_BLUE:       f = FROB_BLUE; break;

      case OSGEN_COLOR_TEAL:
      case OSGEN_COLOR_CYAN:       f = FROB_CYAN; break;

      case OSGEN_COLOR_PURPLE:
      case OSGEN_COLOR_MAGENTA:    f = FROB_MAGENTA; break;

      case OSGEN_COLOR_OLIVE:
      case OSGEN_COLOR_YELLOW:     f = FROB_YELLOW; break;

      case OSGEN_COLOR_SILVER:
      case OSGEN_COLOR_WHITE:      f = FROB_WHITE; break;

      case OSGEN_COLOR_TEXTBG:     f = globalApp->options.bgColor; break;

      case OSGEN_COLOR_INPUT:      f = globalApp->options.textColor; break;

      case OSGEN_COLOR_STATUSLINE: f = globalApp->options.statTextColor; break;

      case OSGEN_COLOR_STATUSBG:   f = globalApp->options.statBgColor; break;

      default:                     f = globalApp->options.textColor;
    }

    switch ((bg != OSGEN_COLOR_TRANSPARENT) ? bg : screen_color) {
      case OSGEN_COLOR_BLACK:
      case OSGEN_COLOR_GRAY:       b = FROB_BLACK; break;

      case OSGEN_COLOR_MAROON:
      case OSGEN_COLOR_RED:        b = FROB_RED; break;

      case OSGEN_COLOR_GREEN:
      case OSGEN_COLOR_LIME:       b = FROB_GREEN; break;

      case OSGEN_COLOR_NAVY:
      case OSGEN_COLOR_BLUE:       b = FROB_BLUE; break;

      case OSGEN_COLOR_TEAL:
      case OSGEN_COLOR_CYAN:       b = FROB_CYAN; break;

      case OSGEN_COLOR_PURPLE:
      case OSGEN_COLOR_MAGENTA:    b = FROB_MAGENTA; break;

      case OSGEN_COLOR_OLIVE:
      case OSGEN_COLOR_YELLOW:     b = FROB_YELLOW; break;

      case OSGEN_COLOR_SILVER:
      case OSGEN_COLOR_WHITE:      b = FROB_WHITE; break;

      case OSGEN_COLOR_TEXT:
      case OSGEN_COLOR_INPUT:      b = globalApp->options.textColor; break;

      case OSGEN_COLOR_STATUSBG:   b = globalApp->options.statBgColor; break;

      case OSGEN_COLOR_STATUSLINE: b = globalApp->options.statTextColor; break;

      default:                     b = globalApp->options.bgColor;
    }

    // Construct the color encoding: our own portable bit layout for
    // linear (os_f_plain) interfaces, or a curses color pair otherwise.
    if (os_f_plain) {
        ret = FROB_PORTABLE_FG(f) | (FROB_PORTABLE_FG(b) << 3) | FROB_PORTABLE_HAVE_COLOR;
        if (attrs & OS_ATTR_HILITE) ret |= FROB_PORTABLE_BOLD;
        return ret;
    }

    ret = COLOR_PAIR(makeColorPair(f,b));

    // Make Tads happy if it wants bold.
    if (attrs & OS_ATTR_HILITE) ret |= A_BOLD;
    return ret;
}


/* Translate a key event from "raw" mode to processed mode.
 */
void
oss_raw_key_to_cmd( os_event_info_t* evt )
{
    if (evt->key[0] == 0) switch (evt->key[1]) {
        // Toggle scrollback-mode with F1.
        case CMD_F1: evt->key[1] = CMD_SCR; break;

        // Delete entire input with F2.
        case CMD_F2: evt->key[1] = CMD_KILL; break;

        // Delete from current position to end of input with F3.
        case CMD_F3: evt->key[1] = CMD_DEOL; break;
    } else switch (evt->key[0]) {
        // Also toggle scrollback mode with the ESC key, since
        // some terminals lack function keys.
        case 27: evt->key[0] = 0; evt->key[1] = CMD_SCR; break;
    }
}


/* Clear an area of the screen.
 */
void
ossclr( int top, int left, int bottom, int right, int color )
{
    globalApp->clear(top, left, bottom, right, color);
}


/* Display text at a particular location in a particular color.
 */
void
ossdsp( int line, int column, int color, const char* msg )
{
    // Suppress output if the text should be invisible.  This is the
    // only way to have "invisible" text without color-support.
    // When colors are enabled, text and background will have the
    // same color, so the text is really invisible in color-mode;
    // A_INVIS/FROB_PORTABLE_INVIS is not needed then.
    bool invisible = os_f_plain
        ? (color & FROB_PORTABLE_INVIS) != 0
        : (color & A_INVIS) != 0;
    if (not invisible) globalApp->print(line, column, color, msg);
}


/* Print 'len' bytes of 'str' with the given oss-level color, for "ansi"
 * mode's linear (non-windowed) output path.  See the declaration in
 * osgen.h for why this exists as its own entry point instead of going
 * through ossdsp().
 */
void
oss_ansi_print( int color, const char* str, size_t len )
{
    if (color & FROB_PORTABLE_INVIS) return;

    // globalApp->print() takes a null-terminated string, but os_print()
    // only guarantees 'len' valid bytes starting at 'str'.
    std::string text(str, len);
    globalApp->print(0, 0, color, text.c_str());
}


/* Move the cursor.
 */
void
ossloc( int line, int column )
{
    globalApp->moveCursor(line, column);
}


/* Scroll a region of the screen UP one line.
 */
void
ossscu( int top_line, int left_column, int bottom_line, int right_column, int blank_color )
{
    globalApp->scrollRegionUp(top_line, left_column, bottom_line, right_column, blank_color);
}


/* Scroll a region of the screen DOWN one line.
 */
void
ossscr( int top_line, int left_column, int bottom_line, int right_column, int blank_color )
{
    globalApp->scrollRegionDown(top_line, left_column, bottom_line, right_column, blank_color);
}


/* Determine if stdin is at end-of-file.  If possible, this should
 * return the stdin status even if we've never attempted to read stdin.
 *
 * On non-Microsoft platforms, we check with select() if stdin can be
 * read from.  Note that feof() won't work, since this function is
 * required to check the status of stdin before even an attempt is made
 * to read from it.
 *
 * On Microsoft Windows (Windows also defines MSDOS), we use an
 * appropriate win32 API call.
 */
int
oss_eof_on_stdin()
{
#ifndef MSDOS
    fd_set rfds;
    timeval tv;

    // Zero time: non-blocking operation.
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    FD_ZERO(&rfds);
    // stdin always has a file descriptor of 0.
    FD_SET(0, &rfds);

    return select(1, &rfds, 0, 0, &tv) == -1;
#else
    return _eof(_fileno(stdin)) > 0;
#endif
}

/* Get system information.
 */
int
oss_get_sysinfo( int code, void*, long* result )
{
    switch(code)
    {
      case SYSINFO_TEXT_COLORS:
        // We support ANSI colors for both foreground and
        // background.
        *result = SYSINFO_TXC_ANSI_FGBG;
        break;

      case SYSINFO_TEXT_HILITE:
        // We do text highlighting.
        *result = 1;
        break;

      default:
        // We didn't recognize the code.
        return false;
    }
    // We recognized the code.
    return true;
}
