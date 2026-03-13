/*
 * Debug UI Factory - Debug Build Version
 * This file is only linked into frobd (debug executable)
 */

#include "frobtadsapp.h"
#include "debugui.h"
#include "dap/dapdebugui.h"

/*
 * Factory function to create the appropriate debugger UI based on options
 * This version is compiled with VM_DEBUGGER and linked into frobd
 */
DebuggerUI* createDebuggerUI(const FrobTadsApplication::FrobOptions& opts)
{
    if (opts.debugProtocol != FrobTadsApplication::DebugProtocol::DAP)
        return nullptr;

    if (!opts.dapSocket.empty())
        return new CDapDebugUI(opts.dapSocket);
    if (opts.dapPort > 0)
        return new CDapDebugUI(opts.dapPort);
    return new CDapDebugUI();
}

