/* 
 * frobdebughelper.cc - FrobTADS debugger helper class implementation
 */

#include "frobdebughelper.h"
#include "frobtadsapp.h"
#include "vmdbg.h"
#include "vmrun.h"
#include "vmpool.h"
#include "vmmeta.h"
#include "vmstack.h"
#include "vmsrcf.h"
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <string>

/* External reference to the global app instance */
extern FrobTadsApplication *globalApp;

/*
 * Constructor
 */
CFrobDebugHelper::CFrobDebugHelper()
    : ctx_(0)
{
}

/*
 * Destructor
 */
CFrobDebugHelper::~CFrobDebugHelper()
{
}

/*
 * Initialize with debug context
 */
void CFrobDebugHelper::init(dbgcxdef *ctx)
{
    ctx_ = ctx;
}

/*
 * Clean up
 */
void CFrobDebugHelper::terminate()
{
    ctx_ = 0;
}

/*
 * Print a string to debug output
 */
void CFrobDebugHelper::print(const char *str)
{
    if (globalApp) {
        globalApp->debugPrint(str);
    } else {
        os_printz(str);
    }
}

/*
 * Flush debug output
 */
void CFrobDebugHelper::flush()
{
    if (globalApp) {
        globalApp->debugFlush();
    } else {
        os_flush();
    }
}

/*
 * Print formatted output
 */
void CFrobDebugHelper::printf(const char *fmt, ...)
{
    char buf[1024];
    va_list args;
    
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    print(buf);
}

/*
 * Print source location at given stack level
 */
void CFrobDebugHelper::print_source_location(int level)
{
    const char *fname;
    unsigned long linenum;
    vm_globals *vmg = ctx_->vmg;
    
    if (G_debugger->get_source_info(vmg_ &fname, &linenum, level) == 0) {
        printf("%s:%lu", fname, linenum);
    } else {
        print("(unknown location)");
    }
}

/*
 * Print full stack trace
 */
void CFrobDebugHelper::print_stack_trace()
{
    vm_globals *vmg = ctx_->vmg;
    
    /* Use lambda callback to capture output */
    auto cb = [](void *ctx0, const char *str, int len) {
        CFrobDebugHelper *helper = (CFrobDebugHelper *)ctx0;
        std::string s(str, str + len);
        helper->print(s.c_str());
    };
    
    /* Create C-style callback wrapper */
    struct thunk_ctx {
        CFrobDebugHelper *helper;
        decltype(cb) *fn;
    } tctx;
    
    tctx.helper = this;
    tctx.fn = &cb;
    
    auto c_cb = [](void *cctx, const char *str, int len) {
        thunk_ctx *tc = (thunk_ctx *)cctx;
        (*(tc->fn))(tc->helper, str, len);
    };
    
    G_debugger->build_stack_listing(vmg_ (void (*)(void *, const char *, int))c_cb,
                                    &tctx, TRUE);
    print("\n");
}

/*
 * Build stack listing as a string
 */
std::string CFrobDebugHelper::build_stack_listing()
{
    vm_globals *vmg = ctx_->vmg;
    int level = 0;
    const char *fname;
    unsigned long linenum;
    char buf[512];
    std::string out;
    
    out += "Stack trace:\n";
    out += "-----------\n";
    
    while (G_debugger->get_source_info(vmg_ &fname, &linenum, level) == 0) {
        snprintf(buf, sizeof(buf), "#%d  at %s:%lu\n", level, fname, linenum);
        out += buf;
        level++;
    }
    
    if (G_debugger->get_source_info(vmg_ &fname, &linenum, level) == 0) {
        out += "... (more frames)\n";
    }
    
    out += "\n";
    return out;
}

/*
 * Evaluate an expression
 */
int CFrobDebugHelper::eval_expr(int level, const char *expr,
                                 char *result, size_t result_size)
{
    vm_globals *vmg = ctx_->vmg;
    return G_debugger->eval_expr(vmg_ result, result_size, expr, level,
                                 0, 0, 0, 0, FALSE);
}

/*
 * Evaluate an assignment expression
 */
int CFrobDebugHelper::eval_assign(int level, const char *lvalue,
                                   const char *rvalue)
{
    char asi_expr[2048];
    char result[32];
    
    snprintf(asi_expr, sizeof(asi_expr), "(%s)=%s", lvalue, rvalue);
    return eval_expr(level, asi_expr, result, sizeof(result));
}

/*
 * Enumerate local variables
 */
void CFrobDebugHelper::enum_locals(int level,
                                    void (*callback)(void *ctx, const char *name,
                                                    const char *value),
                                    void *cb_ctx)
{
    vm_globals *vmg = ctx_->vmg;
    
    /* Context for enumeration callback */
    struct enum_ctx {
        CFrobDebugHelper *helper;
        void (*user_cb)(void *, const char *, const char *);
        void *user_ctx;
        int level;
    };
    
    enum_ctx ectx;
    ectx.helper = this;
    ectx.user_cb = callback;
    ectx.user_ctx = cb_ctx;
    ectx.level = level;
    
    /* Callback for enumeration */
    auto enum_cb = [](void *ctx0, const char *sym, size_t len) {
        enum_ctx *ec = (enum_ctx *)ctx0;
        std::string sym_str(sym, len);
        char val_buf[256];
        
        /* Evaluate the symbol to get its value */
        int err = ec->helper->eval_expr(ec->level, sym_str.c_str(),
                                        val_buf, sizeof(val_buf));
        if (err == 0) {
            ec->user_cb(ec->user_ctx, sym_str.c_str(), val_buf);
        }
    };
    
    /* Enumerate locals at the given stack level */
    G_debugger->enum_locals(vmg_ (void (*)(void *, const char *, size_t))enum_cb,
                           &ectx, level);
}

void CFrobDebugHelper::enum_breakpoints(
    void (*callback)(void *ctx, int bpnum, const char *fname,
                     int linenum, int disabled),
    void *cb_ctx)
{
    for (const auto &bp : breakpoints_) {
        const char *fname = bp.fname.empty() ? nullptr : bp.fname.c_str();
        callback(cb_ctx, bp.bpnum, fname, bp.linenum, bp.disabled);
    }
}

void CFrobDebugHelper::upsert_breakpoint(int bpnum, const uchar *code_addr,
                                         const char *fname, int linenum,
                                         int disabled)
{
    for (auto &bp : breakpoints_) {
        if (bp.bpnum == bpnum) {
            bp.code_addr = code_addr;
            bp.fname = fname ? fname : "";
            bp.linenum = linenum;
            bp.disabled = disabled;
            return;
        }
    }

    BreakpointInfo info;
    info.bpnum = bpnum;
    info.code_addr = code_addr;
    info.fname = fname ? fname : "";
    info.linenum = linenum;
    info.disabled = disabled;
    breakpoints_.push_back(info);
}

void CFrobDebugHelper::remove_breakpoint(int bpnum)
{
    for (auto it = breakpoints_.begin(); it != breakpoints_.end(); ++it) {
        if (it->bpnum == bpnum) {
            breakpoints_.erase(it);
            return;
        }
    }
}

/*
 * Get breakpoint description
 */
bool CFrobDebugHelper::get_bp_desc(int bpnum, char *buf, size_t buflen)
{
    /* Not implemented yet - would need access to internal BP structures */
    snprintf(buf, buflen, "Breakpoint #%d", bpnum);
    return true;
}

/*
 * Enumerate source files
 */
void CFrobDebugHelper::enum_source_files(
    void (*callback)(void *ctx, const char *fname),
    void *cb_ctx)
{
    vm_globals *vmg = ctx_->vmg;
    
    /* Use the source table directly */
    if (!G_srcf_table)
        return;
        
    for (size_t i = 0; i < G_srcf_table->get_count(); ++i) {
        CVmSrcfEntry *entry = G_srcf_table->get_entry(i);
        if (entry) {
            const char *fname = entry->get_name();
            const char *base_fname = get_base_name(fname);
            callback(cb_ctx, base_fname);
        }
    }
}

/*
 * Get base name from full path
 */
const char* CFrobDebugHelper::get_base_name(const char *path)
{
    if (!path) return "";
    
    const char *base = strrchr(path, '/');
    if (!base) base = strrchr(path, '\\');
    
    return base ? base + 1 : path;
}

/*
 * Find source info from code address
 * NOTE: Not implemented - returns false since get_source_info_from_addr
 * is no longer available in the tads3 CVmDebug class.
 */
bool CFrobDebugHelper::find_source_from_addr(const uchar *code_addr,
                                              const char **fname,
                                              unsigned long *linenum)
{
    (void)code_addr;
    (void)fname;
    (void)linenum;
    return false;
}
