/*
 * Debug UI Factory - Non-Debug Build Version
 * This file is only linked into frob (non-debug executable)
 */

#include "frobtadsapp.h"

/*
 * Factory function for non-debug build
 * Always returns nullptr since debugger is not available
 */
DebuggerUI* createDebuggerUI(const FrobTadsApplication::FrobOptions& opts)
{
    (void)opts; // Suppress unused parameter warning
    return nullptr;
}

/*
 * No-op stub for non-debug build
 * In the debug build, this is implemented in debugui.cc
 */
void installDebuggerUI(DebuggerUI *ui)
{
    (void)ui; // No debugger in non-debug build
}
