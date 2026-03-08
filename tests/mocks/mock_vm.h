/*
 * mock_vm.h - Mock VM structures and functions for testing
 *
 * Provides stub implementations of TADS VM interfaces needed for testing
 * the DAP debug UI without requiring a full VM instance.
 */

#ifndef MOCK_VM_H
#define MOCK_VM_H

#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Provide a couple of VM-style constants used by dapdebugui.cc
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

// Max path sizes used by some adapter code.
#ifndef OSFNMAX
#define OSFNMAX 1024
#endif

// Minimal VM-ish types
typedef unsigned char uchar;
typedef unsigned long ulong;
typedef unsigned long vm_obj_id_t;

#ifndef VM_INVALID_OBJ
#define VM_INVALID_OBJ ((vm_obj_id_t)0)
#endif

// In normal (VMGLOB_VARS) builds these are empty; we keep them empty here so
// dapdebugui.cc signatures and calls compile without passing vmg around.
#ifndef VMG_
#define VMG_
#endif
#ifndef VMG0_
#define VMG0_
#endif
#ifndef vmg_
#define vmg_
#endif
#ifndef vmg0_
#define vmg0_
#endif

// The adapter uses VMGLOB_* for legacy compatibility.
#ifndef VMGLOB_ADDR
#define VMGLOB_ADDR ((vm_globals *)0)
#endif
#ifndef VMGLOB_PTR
#define VMGLOB_PTR(x) ((void)(x))
#endif

// Dummy vm_globals type used only for ctx_ bookkeeping in tests.
struct vm_globals {
    int unused;
};

// Forward declarations for global pointers used by dapdebugui.cc
class CVmDebugger;
class CVmSrcfTable;
class CVmCodePool;
class CVmInterpreter;
class CTcPrs;

extern CVmDebugger *G_debugger;
extern CVmSrcfTable *G_srcf_table;
extern CVmCodePool *G_code_pool;
extern CVmInterpreter *G_interpreter;
extern CTcPrs *G_prs;

// Mock source file entry
class CVmSrcfEntry {
public:
    CVmSrcfEntry(const char* name = nullptr) : name_(name ? name : "") {}

    const char* get_name() const { return name_.empty() ? nullptr : name_.c_str(); }
    
    ulong find_src_addr(unsigned long* line, int exact) {
        // Mock implementation - just return a fake address
        return 0x1000;
    }

    int is_master() const {
        // For test purposes treat all files as master units.
        return 1;
    }
    
private:
    std::string name_;
};

// Mock source file table
class CVmSrcfTable {
public:
    CVmSrcfTable() : count_(0) {}
    
    size_t get_count() const { return count_; }
    CVmSrcfEntry* get_entry(size_t idx) { 
        return idx < count_ ? &entries_[idx] : nullptr; 
    }
    
    void add_entry(const char* name) {
        if (count_ < 10) {
            entries_[count_] = CVmSrcfEntry(name);
            count_++;
        }
    }
    
private:
    CVmSrcfEntry entries_[10];
    size_t count_;
};

// Mock code pool
class CVmCodePool {
public:
    const void* get_ptr(ulong offset) {
        // Return a dummy pointer
        static uchar dummy_code[256] = {0};
        return dummy_code;
    }
};

// Mock debugger interface
class CVmDebugger {
public:
    int toggle_breakpoint(VMG_ const uchar* code_addr, const char* cond,
                         int change, int* bpnum, int* did_set,
                         char* errbuf, size_t errbuflen) {
        // Mock: successfully set breakpoint
        *bpnum = 1;
        *did_set = 1;
        return 0;
    }
    
    void set_go() {
        // Mock: resume execution
    }
    
    void set_step_over(VMG0_) {
        // Mock: step over
    }
    
    void set_step_in() {
        // Mock: step in
    }
    
    void set_step_out(VMG0_) {
        // Mock: step out
    }
    
    void set_break_stop() {
        // Mock: request break
    }
    
    int get_source_info(VMG_ const char** fname, unsigned long* linenum, int level) {
        // Mock: return fake source info for level 0 only
        if (level == 0) {
            static const char* mock_file = "/mock/test.t";
            *fname = mock_file;
            *linenum = 42;
            return 0;
        }
        return 1; // No more frames
    }
    
    int eval_expr(VMG_ char* result, size_t result_size, const char* expr,
                 int level, int* is_lval, int* is_openable,
                 void* self_obj, int* is_self_obj_valid, int speculative) {
        // Mock: return a simple result
        snprintf(result, result_size, "<mock result for: %s>", expr);
        *is_lval = 0;
        *is_openable = 0;
        return 0; // Success
    }
};

class CVmInterpreter {
public:
    void set_halt_vm(int /*flag*/) {
        // no-op in tests
    }
};

// Backing objects for the global pointers
extern CVmSrcfTable mock_srcf_table;
extern CVmCodePool mock_code_pool;
extern CVmDebugger mock_debugger;
extern CVmInterpreter mock_interpreter;

// Mock OS functions
inline const char* os_get_root_name(char* path) {
    // Return just the filename part
    const char* last_slash = strrchr(path, '/');
    if (last_slash) return last_slash + 1;
    
    last_slash = strrchr(path, '\\');
    if (last_slash) return last_slash + 1;
    
    return path;
}

inline int os_is_file_absolute(const char* path) {
    // Simple check: starts with / or contains :
    return (path[0] == '/' || strchr(path, ':') != nullptr) ? 1 : 0;
}

inline void os_get_path_name(char* buf, size_t buflen, const char* path) {
    // Extract directory part
    const char* last_slash = strrchr(path, '/');
    if (!last_slash) last_slash = strrchr(path, '\\');
    
    if (last_slash) {
        size_t len = last_slash - path;
        if (len >= buflen) len = buflen - 1;
        memcpy(buf, path, len);
        buf[len] = '\0';
    } else {
        buf[0] = '\0';
    }
}

inline void os_build_full_path(char* buf, size_t buflen, const char* dir, const char* file) {
    // Simple path join
    snprintf(buf, buflen, "%s/%s", dir, file);
}

// Setup and teardown functions
void mock_vm_setup();
void mock_vm_teardown();

#endif /* MOCK_VM_H */
