#ifndef BREAKPOINT_PORT_H
#define BREAKPOINT_PORT_H

#include "common.h"

#include "t3std.h"
#include "vmglob.h"
#include "vmpool.h"
#include "vmtype.h"

using EnumBreakpointsCallback = void (*)(void *, int, const uchar *, int,
                                         const char *, unsigned long);

class BreakpointPort {
public:
  virtual void enum_breakpoints(VMG_ EnumBreakpointsCallback cbfunc,
                                void *cbctx) {
    (void)cbfunc;
    (void)cbctx;
  }

  virtual void upsert_breakpoint_info(VMG_ int bpnum, const uchar *code_addr,
                                      const char *fname, unsigned long linenum,
                                      int disabled) {
    (void)bpnum;
    (void)code_addr;
    (void)fname;
    (void)linenum;
    (void)disabled;
  }

  virtual void remove_breakpoint_info(VMG_ int bpnum) { (void)bpnum; }

  virtual int toggle_breakpoint(VMG_ const uchar *code_addr, const char *cond,
                                int change, int *bpnum, int *did_set,
                                char *errbuf, size_t errbuflen) {
    (void)code_addr;
    (void)cond;
    (void)change;
    (void)bpnum;
    (void)did_set;
    (void)errbuf;
    (void)errbuflen;
    return 1;
  }

  virtual void delete_breakpoint(VMG_ int bpnum) { (void)bpnum; }

  virtual const char *get_ptr(pool_ofs_t ofs) {
    (void)ofs;
    return nullptr;
  }

  virtual ~BreakpointPort() = default;
};

#endif /* BREAKPOINT_PORT_H */
