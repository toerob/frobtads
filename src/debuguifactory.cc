/*
 * Debug UI Factory - Debug Build Version
 * This file is only linked into frobd (debug executable)
 */

#include "frobtadsapp.h"
#include "debugui.h"
#include "terminaldebugui.h"
#include "dap/dapdebugui.h"

/*
 * Factory function to create the appropriate debugger UI based on options
 * This version is compiled with VM_DEBUGGER and linked into frobd
 */
DebuggerUI* createDebuggerUI(const FrobTadsApplication::FrobOptions& opts)
{
    // If no debug protocol is specified, return nullptr to indicate no debugger UI should be used
    if (opts.debugProtocol == FrobTadsApplication::DebugProtocol::None) {
        return nullptr;
    }

    switch (opts.debugProtocol) {
        case FrobTadsApplication::DebugProtocol::DAP:
            // Create DAP debug UI with appropriate communication channel
            if (!opts.dapSocket.empty()) {
                // Use Unix domain socket
                return new CDapDebugUI(opts.dapSocket);
            } else if (opts.dapPort > 0) {
                // Use TCP socket
                return new CDapDebugUI(opts.dapPort);
            } else {
                // Use stdin/stdout (default)
                return new CDapDebugUI();
            }
        case FrobTadsApplication::DebugProtocol::Terminal:
        default:
            return new CTerminalDebugUI();
    }
}

