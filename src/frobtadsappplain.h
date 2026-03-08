#ifndef FROBTADSAPPPLAIN_H
#define FROBTADSAPPPLAIN_H

#include "common.h"

#include <thread>
#include <chrono>
#include "frobtadsapp.h"


class FrobTadsApplicationPlain: public FrobTadsApplication {
  private:
    bool fUseColors;
    
    // ANSI color codes
    const char* COLOR_RESET = "\033[0m";
    const char* COLOR_STACK = "\033[0;36m";         // Cyan
    
  public:
    FrobTadsApplicationPlain( const FrobOptions& opts )
        : FrobTadsApplication(opts), fUseColors(true)
    {
        // Just tell the osgen layer to use plain mode.
        os_plain();
        
        // Check if colors are supported (simple check for terminal)
        const char* term = getenv("TERM");
        if (!term || strstr(term, "dumb")) {
            fUseColors = false;
        }
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
    print( int, int, int, const char* str )
    { printf("%s", str); }

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
    
    // Debug support methods
    virtual void
    debugPrint(const char* str)
    {
        if (str) {
            // Use cyan color for debug text to distinguish from game output
            if (fUseColors) {
                printf("%s%s%s", COLOR_STACK, str, COLOR_RESET);
            } else {
                printf("%s", str);
            }
            fflush(stdout);
        }
    }
};

#endif // FROBTADSAPPPLAIN_H