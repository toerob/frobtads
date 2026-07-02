/* This is the curses-specific implementation of FrobTadsApplication.
 */
#ifndef FROBTADSAPPCURSES_H
#define FROBTADSAPPCURSES_H

#include "common.h"

#include <memory>
#include "frobtadsapp.h"
#include "tadswindow.h"


class FrobTadsApplicationCurses: public FrobTadsApplication {
  private:
    // The window we use for I/O.
    std::unique_ptr<FrobTadsWindow> fGameWindow;

  public:
    FrobTadsApplicationCurses( const FrobOptions& opts );
    ~FrobTadsApplicationCurses();


    /* ============================================================
     * Interface implementation.
     * ============================================================
     */
  protected:
    virtual void
    init();

    // We simply handle everything in init().
    virtual void
    resizeEvent()
    { this->init(); }

  public:
    virtual void
    moveCursor( int line, int column )
    { this->fGameWindow->moveCursor(line, column); }

    virtual void
    print( int line, int column, int attrs, const char* str )
    { this->fGameWindow->printStr(line, column, attrs, str); }

    virtual void
    flush()
    { this->fGameWindow->flush(); }

    virtual void
    clear( int top, int left, int bottom, int right, int attrs );

    virtual void
    scrollRegionUp( int top, int left, int bottom, int right, int attrs );

    virtual void
    scrollRegionDown( int top, int left, int bottom, int right, int attrs );

    virtual int
    getRawChar( bool cursorVisible, int timeout );

    virtual void
    sleep( int ms )
    { this->fGameWindow->flush(); napms(ms); }

    virtual int
    height() const
    { return this->fGameWindow->height(); }

    virtual int
    width() const
    { return this->fGameWindow->width(); }
};

#endif // FROBTADSAPPCURSES_H
