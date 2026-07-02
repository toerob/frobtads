#ifndef FROBTADSAPPANSI_H
#define FROBTADSAPPANSI_H

#include "common.h"

#include <cstdio>
#include <thread>
#include <chrono>
#include "frobtadsapp.h"
#include "colors.h"


/* Just like FrobTadsApplicationPlain (linear stdio output, no cursor
 * addressing, no banners/statusline), except that print() translates
 * the color/attribute value ossgetcolor() computes into real ANSI SGR
 * escape codes, instead of ignoring it.
 */
class FrobTadsApplicationAnsi: public FrobTadsApplication {
  public:
    FrobTadsApplicationAnsi( const FrobOptions& opts )
    : FrobTadsApplication(opts)
    {
        // Just like plain mode, tell the osgen layer to use linear,
        // non-cursor-addressed output; unlike plain mode, also track
        // color/attribute state instead of discarding it.
        os_ansi();

        // Unlike plain mode, we can actually display colors, so honor
        // the usual color options (there's no curses has_colors() check
        // to make here, since any terminal that understands ANSI escape
        // codes at all supports at least the 8 basic colors).
        this->fColorsEnabled = opts.useColors or opts.forceColors;
    }


    /* ============================================================
     * Interface implementation.
     * ============================================================
     */
  protected:
    virtual void
    init()
    { }

    virtual void
    resizeEvent()
    { }

  public:
    virtual void
    moveCursor( int, int )
    { }

    virtual void
    print( int, int, int attrs, const char* str )
    {
        bool wroteCode = false;

        if (attrs & FROB_PORTABLE_HAVE_COLOR) {
            printf("\x1b[%d;%dm", 30 + FROB_PORTABLE_FG(attrs), 40 + FROB_PORTABLE_BG(attrs));
            wroteCode = true;
        } else if (attrs & FROB_PORTABLE_REVERSE) {
            printf("\x1b[7m");
            wroteCode = true;
        }
        if (attrs & FROB_PORTABLE_BOLD) {
            printf("\x1b[1m");
            wroteCode = true;
        }

        printf("%s", str);

        // Reset, but only if we actually changed anything, to avoid
        // spamming plain, uncolored text with needless escape codes.
        if (wroteCode) printf("\x1b[0m");
    }

    virtual void
    flush()
    { fflush(stdout); }

    virtual void
    clear( int, int, int, int, int )
    { }

    virtual void
    scrollRegionUp( int, int, int, int, int )
    { }

    virtual void
    scrollRegionDown( int, int, int, int, int )
    { }

    virtual int
    getRawChar( bool, int )
    { return getchar(); }

    virtual void
    sleep( int ms )
    {
        flush();
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    virtual int
    height() const
    { return 25; }

    virtual int
    width() const
    { return 80; }
};

#endif // FROBTADSAPPANSI_H
