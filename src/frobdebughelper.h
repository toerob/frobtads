/* 
 * frobdebughelper.h - FrobTADS debugger helper class
 * 
 * This class provides common utilities and helper methods for the
 * FrobTADS debugger UI implementation, similar to CHtmlDebugHelper
 * in the HTML TADS debugger.
 */

#ifndef FROBDEBUGHELPER_H
#define FROBDEBUGHELPER_H

#include "vmdbg.h"
#include "vmglob.h"
#include <string>
#include <vector>


/* Debug context structure */
struct dbgcxdef {
  vm_globals *vmg;   /* VM global variables */
  void *ui_ctx;      /* UI context pointer */
  int in_debugger;   /* Are we currently in the debugger? */
  int stepping_mode; /* Current stepping mode: 0=none, 1=step, 2=next, 3=finish */
};

/* Stepping mode constants */
#define STEP_NONE 0
#define STEP_INTO 1
#define STEP_OVER 2
#define STEP_OUT 3

/*
 * FrobTADS Debugger Helper Class
 * 
 * This class encapsulates common debugger functionality and provides
 * a cleaner interface for debugger operations. It manages:
 * - Output formatting and display
 * - Stack trace generation
 * - Expression evaluation
 * - Breakpoint enumeration
 * - Local variable inspection
 */
class CFrobDebugHelper
{
public:
  struct BreakpointInfo {
    int bpnum;
    const uchar *code_addr;
    std::string fname;
    int linenum;
    int disabled;
  };

    /* Constructor */
    CFrobDebugHelper();
    
    /* Destructor */
    ~CFrobDebugHelper();
    
    /* Initialize the helper with debug context */
    void init(dbgcxdef *ctx);
    
    /* Clean up resources */
    void terminate();
    
    /*
     * Output functions
     */
    
    /* Print a string to the debug output */
    virtual void print(const char *str);
    
    /* Flush any buffered output */
    void flush();
    
    /* Print formatted output */
    virtual void printf(const char *fmt, ...);
    
    /*
     * Stack trace operations
     */
    
    /* Print the current source location at a given stack level */
    void print_source_location(int level);
    
    /* Print a full stack trace */
    void print_stack_trace();
    
    /* Build a stack listing as a string (for display) */
    std::string build_stack_listing();
    
    /*
     * Expression evaluation
     */
    
    /* Evaluate an expression and return the result as a string */
    int eval_expr(int level, const char *expr, char *result, size_t result_size);
    
    /* Assign a value to an lvalue expression */
    int eval_assign(int level, const char *lvalue, const char *rvalue);
    
    /*
     * Local variable inspection
     */
    
    /* Enumerate local variables at the given stack level */
    void enum_locals(int level, void (*callback)(void *ctx, const char *name,
                     const char *value), void *ctx);
    
    /*
     * Breakpoint operations
     */
    
    /* Enumerate all breakpoints */
    void enum_breakpoints(void (*callback)(void *ctx, int bpnum,
                          const char *fname, int linenum, int disabled), void *ctx);

    /* Record or update a breakpoint in the helper registry */
    void upsert_breakpoint(int bpnum, const uchar *code_addr,
                 const char *fname, int linenum, int disabled);

    /* Remove a breakpoint from the helper registry */
    void remove_breakpoint(int bpnum);
    
    /* Get breakpoint description string */
    bool get_bp_desc(int bpnum, char *buf, size_t buflen);
    
    /*
     * Source file operations
     */
    
    /* Enumerate all source files in the debug information */
    void enum_source_files(void (*callback)(void *ctx, const char *fname), void *ctx);
    
    /*
     * Utility functions
     */
    
    /* Get the base name from a full file path */
    static const char* get_base_name(const char *path);
    
    /* Find source info from a code address */
    bool find_source_from_addr(const uchar *code_addr, const char **fname,
                                unsigned long *linenum);
    
private:
    /* Debug context pointer */
    dbgcxdef *ctx_;

  /* Helper-side breakpoint registry used by enum_breakpoints */
  std::vector<BreakpointInfo> breakpoints_;
    
    /* Disable copy constructor and assignment */
    CFrobDebugHelper(const CFrobDebugHelper&);
    CFrobDebugHelper& operator=(const CFrobDebugHelper&);
};

#endif /* FROBDEBUGHELPER_H */
