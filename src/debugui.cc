#include "common.h"
#include "frobdebughelper.h"
#include "frobtadsapp.h"
#include "debugui.h"
#include "os.h"
#include "t3std.h"
#include "tcerr.h"
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

/* Global active debugger UI instance - set before vm_run_image is called */
static DebuggerUI *s_active_debug_ui = 0;

/* Global debugger context */
static dbgcxdef g_dbg_ctx = {0};

/* Global debugger helper */
static CFrobDebugHelper *g_helper = 0;

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
