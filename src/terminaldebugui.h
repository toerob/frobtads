/*
 * terminaldebugui.h - Terminal-based debugger UI
 *
 * GDB-style Terminal Debugger UI for TADS 3
 * Implements the DebuggerUI interface for interactive terminal debugging
 */

#ifndef TERMINALDEBUGUI_H
#define TERMINALDEBUGUI_H

#include "debugui.h"

/*
 * Terminal debugger implementation
 * This is the traditional interactive command-line debugger
 */
class CTerminalDebugUI : public DebuggerUI
{
public:
    CTerminalDebugUI();
    virtual ~CTerminalDebugUI();
    
    /* DebuggerUI interface implementation */
    virtual void init(VMG_ const char *image_filename) override;
    virtual void init_after_load(VMG0_) override;
    virtual void terminate(VMG0_) override;
    virtual void cmd_loop(VMG_ int bp_number, int error_code, const uchar **pc) override;
};

#endif /* TERMINALDEBUGUI_H */
