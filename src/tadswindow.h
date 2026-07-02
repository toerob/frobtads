/* The FrobTadsWindow class provides an easy to use interface to the
 * curses WINDOW* structure.  It's basically a wrapper around the curses
 * routines, tailored somewhat to our own needs.  The methods are short
 * one-liners and therefore good candidates for inlining, so using this
 * class implies no overhead.
 */
#ifndef TADSWINDOW_H
#define TADSWINDOW_H

#include "common.h"

#include <memory>
#include "frobcurses.h"

class FrobTadsWindow {
  private:
    // The curses window we maintain.
    const std::unique_ptr<WINDOW, decltype(&delwin)> fWin;

  public:
    /* Creates a new top-level window with 'lines' height, 'cols'
     * width, and coordinates 'yPos' and 'xPos'.
     */
    FrobTadsWindow( int lines, int cols, int yPos, int xPos )
        : fWin(newwin(lines, cols, yPos, xPos), &delwin)
    { }

    /* Returns the height (lines) of the window.
     */
    int
    height() const
    { int y, x; getmaxyx(this->fWin.get(), y, x); return y; }

    /* Returns the width (columns) of the window.
     */
    int
    width() const
    { int y, x; getmaxyx(this->fWin.get(), y, x); return x; }

    /* Moves the cursor to line 'y', column 'x'.
     */
    int
    moveCursor( int y, int x ) { return wmove(this->fWin.get(), y, x); }

    /* Gets a keystroke from the window.  A timeout can be set with
     * setTimeout() before calling this method.
     *
     * The returned value is the same as the curses getch() routine.
     */
    int
    getChar() { return wgetch(this->fWin.get()); }

    /* Sets a timeout for subsequent input operations.  If 'timeout'
     * milliseconds pass and there's no input, ERR is returned.  If
     * 'timeout' is negative, the input methods will not use a timeout
     * but wait indefinitely for input.  A 0 'timeout' will only check
     * whether there's input already available in the input buffer and
     * fetch it, but will otherwise not block.
     */
    void
    setTimeout( int timeout ) { wtimeout(this->fWin.get(), timeout); }

    /* Writes the (UTF-8 encoded) string 'str' to the window at the
     * specified position, using 'attrs' for every character in the
     * string.  We rely on the wide-character curses API to decode
     * 'str' according to the current locale, so a multi-byte
     * character occupies a single screen cell, instead of one cell
     * per byte.
     */
    int
    printStr( int y, int x, int attrs, const char* str )
    {
        wattrset(this->fWin.get(), attrs);
        return mvwaddstr(this->fWin.get(), y, x, str);
    }

    /* Writes the complex character 'ch' (a full glyph plus its
     * attributes) to the window at the specified coordinates.
     */
    int
    printChar( int y, int x, const cchar_t& ch ) { return mvwadd_wch(this->fWin.get(), y, x, &ch); }

    /* Returns the complex character (glyph plus attributes) at
     * position (x,y).
     */
    cchar_t
    charAt( int y, int x )
    {
        cchar_t ch;
        mvwin_wch(this->fWin.get(), y, x, &ch);
        return ch;
    }

    /* Builds a single blank (space) character cell using 'attrs' as
     * its display attributes.  Used to erase/fill areas of the
     * window.  'attrs' is a traditional curses attribute value with
     * the color pair already folded in via COLOR_PAIR(), just like
     * the values ossgetcolor() returns.
     */
    static cchar_t
    blankChar( int attrs )
    {
        cchar_t ch;
        wchar_t wch[2] = { L' ', L'\0' };
        setcchar(&ch, wch, attrs & ~A_COLOR, PAIR_NUMBER(attrs), 0);
        return ch;
    }

    /* Blanks the window (erases its contents).
     */
    int
    blank() { return werase(this->fWin.get()); }

    /* Enables/disables scrolling.
     */
    int
    enableScrolling( bool bf ) { return scrollok(this->fWin.get(), bf); }

    /* Flushes the internal buffers so any pending output-operations
     * will be processed.  This is just a curses wrefresh().
     */
    int
    flush() { return wrefresh(this->fWin.get()); }

    /* Mark the entire window as "touched"; throw away all
     * optimization information about which parts of the window have
     * changed, forcing curses to redraw all characters.
     */
    int
    touch() { return touchwin(this->fWin.get()); }

    /* Enables/disables "keypad mode".  In this mode, input methods
     * (getChar(), etc.) will recognize special keys like function
     * keys, arrow keys, insert, delete, etc.  These are returned as
     * KEY_* values (as defined in <curses.h>).
     */
    int
    keypadMode( bool bf ) { return keypad(this->fWin.get(), bf); }

    /* Enables/disables 8-bit input.  Normally, input is 7-bit,
     * which means that things like German unlauts und everything
     * else above the 7-bit ASCII range won't work.
     */
    int
    input8bit( bool bf ) { return meta(this->fWin.get(), bf); }
};

#endif // TADSWINDOW_H
