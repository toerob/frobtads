#ifndef FROBDEBUGHELPER_H
#define FROBDEBUGHELPER_H

// Test-only lightweight implementation.
// The production CFrobDebugHelper is VM-heavy; for dapdebugui unit tests we
// only need a small subset.

#include <string>
#include <vector>

#include "mock_vm.h"

/* Debug context structure (subset) */
struct dbgcxdef {
  vm_globals *vmg;   /* VM global variables (unused in mocks) */
  void *ui_ctx;
  int in_debugger;
  int stepping_mode;
};

/* Stepping modes */
#ifndef STEP_NONE
#define STEP_NONE 0
#endif
#ifndef STEP_INTO
#define STEP_INTO 1
#endif
#ifndef STEP_OVER
#define STEP_OVER 2
#endif
#ifndef STEP_OUT
#define STEP_OUT 3
#endif

class CFrobDebugHelper {
public:
  CFrobDebugHelper() = default;
  ~CFrobDebugHelper() = default;

  void init(dbgcxdef *ctx) { ctx_ = ctx; }
  void terminate() { ctx_ = nullptr; }

  void enum_locals(int /*level*/, void (*callback)(void *ctx, const char *name,
                                                  const char *value),
                   void *cb_ctx) {
    if (callback == nullptr)
      return;
    callback(cb_ctx, "localVar", "123");
    callback(cb_ctx, "otherVar", "hello");
  }

  void upsert_breakpoint(int bpnum, const uchar *code_addr, const char *fname,
                         int linenum, int disabled) {
    for (auto &bp : breakpoints_) {
      if (bp.bpnum == bpnum) {
        bp.code_addr = code_addr;
        bp.fname = (fname ? fname : "");
        bp.linenum = linenum;
        bp.disabled = disabled;
        return;
      }
    }
    BreakpointInfo bp;
    bp.bpnum = bpnum;
    bp.code_addr = code_addr;
    bp.fname = (fname ? fname : "");
    bp.linenum = linenum;
    bp.disabled = disabled;
    breakpoints_.push_back(std::move(bp));
  }

  void remove_breakpoint(int bpnum) {
    for (size_t i = 0; i < breakpoints_.size(); ++i) {
      if (breakpoints_[i].bpnum == bpnum) {
        breakpoints_.erase(breakpoints_.begin() + (long)i);
        return;
      }
    }
  }

private:
  struct BreakpointInfo {
    int bpnum = 0;
    const uchar *code_addr = nullptr;
    std::string fname;
    int linenum = 0;
    int disabled = 0;
  };

  dbgcxdef *ctx_ = nullptr;
  std::vector<BreakpointInfo> breakpoints_;
};

#endif /* FROBDEBUGHELPER_H */
