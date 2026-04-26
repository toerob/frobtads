/*
 * GDB-style Terminal Debugger UI for TADS 3
 *
 * A simple, clean command-line debugger interface with commands similar to gdb:
 *   - step (s): Step into next instruction
 *   - next (n): Step over next instruction
 *   - finish (f): Step out of current frame
 *   - continue (c): Resume execution
 *   - break (b): Set breakpoint at line
 *   - list (l): List breakpoints
 *   - delete (del): Delete breakpoint by number
 *   - where (w): Show current location
 *   - stack (stk): Show stack trace
 *   - eval (e): Evaluate expression
 *   - print (p): Print source file contents
 *   - files (f): List source files
 *   - up (u): Move up the stack
 *   - down (d): Move down the stack
 *   - help (h, ?): Show this help message
 *   - info (i): Display debug information
 *   - quit (q): Exit debugger
 */

#include "common.h"
#include "frobdebughelper.h"
#include "frobtadsapp.h"
#include "debugui.h"
#include "os.h"
#include "t3std.h"
#include "tcerr.h"
#include "terminaldebugui.h"
#include "vmconsol.h"
#include "vmdbg.h"
#include "vmerr.h"
#include "vmerrnum.h"
#include "vmglob.h"
#include "vmrun.h"
#include "vmsrcf.h"
#include "vmtype.h"

#include <algorithm>
#include <cstdio>
#include <ctype.h>
#include <memory>
#include <string.h>
#include <string>
#include <unordered_map>
#include <vector>

#include "termdbg/breakpoint_handler.h"
#include "termdbg/manager.h"
#include "termdbg/print_file_handler.h"
#include "termdbg/eval_handler.h"

#include "textutils.h"

/* Global active debugger UI instance - set before vm_run_image is called */
static DebuggerUI *s_active_debug_ui = 0;

/* Global debugger context */
static dbgcxdef g_dbg_ctx = {0};

/* Global debugger helper */
static CFrobDebugHelper *g_helper = 0;

std::unique_ptr<DebuggerCommandManager> g_command_manager;

/*
 * Install the debugger UI to be used by the CVmDebugUI static methods.
 * Call this before vm_run_image() to register the active debugger UI.
 */
void installDebuggerUI(DebuggerUI *ui)
{
    s_active_debug_ui = ui;
}

/* -------------------------------------------------------------------- */
/*
 * CVmDebugUI static method implementations.
 * These delegate to the registered DebuggerUI instance.
 */

void CVmDebugUI::init(VMG_ const char *image_filename)
{
    if (s_active_debug_ui)
        s_active_debug_ui->init(vmg_ image_filename);
}

void CVmDebugUI::init_after_load(VMG0_)
{
    if (s_active_debug_ui)
        s_active_debug_ui->init_after_load(vmg0_);
}

void CVmDebugUI::terminate(VMG0_)
{
    if (s_active_debug_ui)
    {
        s_active_debug_ui->terminate(vmg0_);
        delete s_active_debug_ui;
        s_active_debug_ui = 0;
    }
}

void CVmDebugUI::cmd_loop(VMG_ int bp_number, int error_code, const uchar **pc)
{
    if (s_active_debug_ui)
        s_active_debug_ui->cmd_loop(vmg_ bp_number, error_code, pc);
}

/* -------------------------------------------------------------------- */
/*
 * CTerminalDebugUI implementation
 */

/*
 * Constructor
 */
CTerminalDebugUI::CTerminalDebugUI() {
}

/*
 * Destructor
 */
CTerminalDebugUI::~CTerminalDebugUI() {}

/*
 * Initialize the debugger
 */
void CTerminalDebugUI::init(VMG_ const char *image_filename) {

  g_dbg_ctx.vmg = VMGLOB_ADDR; // Set VM globals pointer in debug context
  g_dbg_ctx.in_debugger =
      0; // Not in debugger yet - will be set when we enter cmd_loop
  g_dbg_ctx.stepping_mode = STEP_NONE; // No stepping mode active
  g_dbg_ctx.ui_ctx =
      NULL; // No UI context needed for this simple implementation

  /* Create and initialize helper */
  g_helper = new CFrobDebugHelper();
  g_helper->init(&g_dbg_ctx);

  /* Clear the HALT flag since we're starting */
  G_interpreter->set_halt_vm(FALSE);

  // Initialize the command manager with references to the debugger,
  // interpreter, source file table, helper, and debug context
  g_command_manager = std::make_unique<DebuggerCommandManager>(
      G_debugger, G_interpreter, G_srcf_table, g_helper, g_dbg_ctx,
      image_filename);

  g_helper->print("\nTADS 3 Debugger initialized\n");
  g_helper->print("Type 'help' for available commands\n\n");
  g_helper->flush();
}

/*
 * Post-load initialization (after image is loaded)
 */
void CTerminalDebugUI::init_after_load(VMG0_) {
  g_helper->print("Debug info loaded\n");
  g_helper->flush();
}

/*
 * Terminate the debugger
 */
void CTerminalDebugUI::terminate(VMG0_) {
  if (g_helper) {
    g_helper->print("Debugger terminated\n");
    g_helper->flush();
    g_helper->terminate();

    delete g_helper;
    g_helper = 0;
  }
}

/*
 * Main debugger command loop
 * Called whenever execution hits a breakpoint or debugger event
 */
void CTerminalDebugUI::cmd_loop(VMG_ int bp_number, int error_code,
                                const uchar **pc) {
  char cmd_buf[256];

  /* Set up our context */
  g_dbg_ctx.vmg = VMGLOB_ADDR;
  g_dbg_ctx.in_debugger = 1;

  /* Command loop - process commands until user resumes execution */
  g_command_manager->cmd_loop(vmg_ cmd_buf, g_dbg_ctx, bp_number, error_code,
                              pc);
}
