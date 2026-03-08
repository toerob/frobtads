#ifndef FACADE_H
#define FACADE_H

#include "common.h"

#include "breakpoint_port.h"
#include "eval_port.h"
#include "frobdebughelper.h"
#include "output_port.h"
#include "source_port.h"
#include "textutils.h"
#include "vmdbg.h"
#include <string>
#include <vector>

#include "t3std.h"
#include "vmglob.h"
#include "vmpool.h"
#include "vmsrcf.h"
#include "vmtype.h"

#include <fstream>
#include <set>
#include <string>
#include <vector>

using std::getline;
using std::set;
using std::string;
using std::vector;

/**
 * Concrete adapter that implements the small capability ports by delegating to
 * the debugger and helper classes.
 */
class DebuggerFacade : public SourcePort,
                       public BreakpointPort,
                       public OutputPort,
                       public EvalPort {
public:
  CVmDebug *debugger;
  CVmRun *interpreter;
  CFrobDebugHelper *helper;
  CVmSrcfTable *srcf_table;
  dbgcxdef &g_dbg_ctx;

  DebuggerFacade(CVmDebug *debugger, CVmRun *interpreter,
                 CFrobDebugHelper *helper, CVmSrcfTable *srcf_table,
                 dbgcxdef &g_dbg_ctx)
      : debugger(debugger), interpreter(interpreter), helper(helper),
        srcf_table(srcf_table), g_dbg_ctx(g_dbg_ctx) {}

  virtual int eval_expr(VMG_ char *res, size_t res_size, const char *expr,
                        int flags, int *is_lval, int *is_openable, int, int,
                        int) override {

    return debugger->eval_expr(vmg_ res, res_size, expr, flags, is_lval,
                               is_openable, 0, 0, FALSE);
  }

  virtual void enum_locals(VMG_ void (*cbfunc)(void *, const char *, size_t),
                           void *cbctx, int level) override {
    debugger->enum_locals(vmg_ cbfunc, cbctx, level);
  }
  virtual int get_source_info(VMG_ const char **fname, unsigned long *linenum,
                              int level) override {
    return debugger->get_source_info(vmg_ fname, linenum, level);
  }

  virtual void enum_breakpoints(VMG_ EnumBreakpointsCallback cbfunc,
                                void *cbctx) override {
    struct EnumBpCtx {
      EnumBreakpointsCallback cbfunc;
      void *cbctx;
    } ectx;

    ectx.cbfunc = cbfunc;
    ectx.cbctx = cbctx;

    auto helper_cb = [](void *ctx, int bpnum, const char *fname, int linenum,
                        int disabled) {
      EnumBpCtx *ec = (EnumBpCtx *)ctx;
      ec->cbfunc(ec->cbctx, bpnum, nullptr, disabled, fname,
                 (unsigned long)linenum);
    };

    helper->enum_breakpoints(helper_cb, &ectx);
  }

  virtual void upsert_breakpoint_info(VMG_ int bpnum, const uchar *code_addr,
                                      const char *fname, unsigned long linenum,
                                      int disabled) override {
    helper->upsert_breakpoint(bpnum, code_addr, fname, (int)linenum, disabled);
  }

  virtual void remove_breakpoint_info(VMG_ int bpnum) override {
    helper->remove_breakpoint(bpnum);
  }

  virtual int get_source_idx_fname(VMG_ size_t idx,
                                   std::string &fname) override {
    CVmSrcfEntry *e = srcf_table->get_entry(idx);
    if (e) {
      fname = e->get_name();
      return 0;
    }
    return 1;
  }

  virtual SourceContentsResult
  get_entry_source_contents(VMG_ size_t filenumber) override {

    auto count = srcf_table->get_count();
    if (filenumber >= count) {
      std::string msg = "File number must be between 0 and " +
                        std::to_string(count - 1) + "\n";
      return SourceContentsResult{-1, msg, "", {}};
    }

    helper->printf("Fetching source contents for file number: %zu\n",
                   filenumber);

    CVmSrcfEntry *entry = srcf_table->get_entry(filenumber);

    helper->printf("Got source file entry for file number %zu: %s\n",
                   filenumber, entry ? entry->get_name() : "null");

    if (entry == nullptr) {
      return SourceContentsResult{-1, "Invalid file number\n", "", {}};
    }
    if (!entry->is_master()) {
      return SourceContentsResult{
          -1, "File number corresponds to a duplicate entry\n", "", {}};
    }

    const char *fname = entry->get_name();
    unsigned long linenum;

    /*
    if (debugger->get_source_info(vmg_ &fname, &linenum, 0) != 0 || !fname) {
      return SourceContentsResult{-1, "Source information not available\n", "",
                                  {}};
    }*/

    vector<string> lines = read_file_lines(fname);
    if (lines.empty()) {
      std::string msg = std::string(fname) + ":" + std::to_string(linenum) +
                        "\nFailed to read source file or file is empty\n";
      return SourceContentsResult{-1, msg.c_str(), fname, {}};
    }

    return SourceContentsResult{0, "", fname, std::move(lines)};
  }

  virtual int get_offset_source_info(VMG_ unsigned long &wantline,
                                     size_t entry_idx, ulong &ofs,
                                     const char **fname,
                                     std::string &response) override {

    CVmSrcfEntry *entry = srcf_table->get_entry((size_t)entry_idx);
    if (!entry) {
      response = "Invalid source entry\n";
      return 1;
    }

    *fname = entry->get_name();
    // Try to find the code address for the requested line,
    // (ofs stands for offset in the code pool)

    // Set the ofs reference to the code address for the requested line,
    // Note: ofs=0 means failure
    ofs = entry->find_src_addr(&wantline, FALSE);
    if (ofs == 0) {
      response = "No executable code found at or after that line\n";
      return 1;
    }

    // If ofs is 0, it means we couldn't find an executable line for the
    // requested line number
    return 0; // SUCCESS
  }

  virtual int toggle_breakpoint(VMG_ const uchar *code_addr, const char *cond,
                                int change, int *bpnum, int *did_set,
                                char *errbuf, size_t errbuflen) override {

    return debugger->toggle_breakpoint(vmg_ code_addr, 0, 0, bpnum, did_set,
                                       errbuf, sizeof(errbuf));
  }

  virtual void delete_breakpoint(VMG_ int bpnum) override {
    debugger->delete_breakpoint(vmg_ bpnum);
  }

  virtual inline const char *get_ptr(pool_ofs_t ofs) override {
    return G_code_pool->get_ptr(ofs);
  }

  /**
   * Function to find source file matches for a given file name (or
   * partial file name). This is used when setting breakpoints by file and line,
   * to find the correct source file in the debug info. We will look for exact
   * matches, suffix matches, base name matches, and substring matches, and
   * return a list of matching source file IDs.
   */
  virtual vector<string>
  get_all_abs_filepaths(bool master_records_only = false) override {
    vector<string> filenames;
    size_t tblc = srcf_table ? srcf_table->get_count() : 0;
    for (size_t i = 0; i < tblc; ++i) {
      CVmSrcfEntry *e = srcf_table->get_entry(i);
      if (master_records_only && !e->is_master()) {
        continue;
      }
      if (e) {
        filenames.push_back(e->get_name());
      }
    }
    return filenames;
  }

  virtual vector<std::pair<size_t, string>>
  get_all_abs_idx_and_filepaths(bool master_records_only = false) override {
    vector<std::pair<size_t, string>> filenames;
    size_t tblc = srcf_table ? srcf_table->get_count() : 0;
    for (size_t i = 0; i < tblc; ++i) {
      CVmSrcfEntry *e = srcf_table->get_entry(i);
      if (master_records_only && !e->is_master()) {
        continue;
      }
      if (e) {
        filenames.push_back({i, e->get_name() ? e->get_name() : "(unnamed)"});
      }
    }
    return filenames;
  }

  virtual int locate_srcf_entry_by_name(const char *fname,
                                        size_t &idx_result) override {
    for (size_t idx = 0; idx < G_srcf_table->get_count(); ++idx) {
      CVmSrcfEntry *e = G_srcf_table->get_entry(idx);
      if (e && strcmp(e->get_name(), fname) == 0) {
        idx_result = idx;
        return 0;
      }
    }
    return 1;
  }

  virtual int locate_srcf_entry_by_idx(size_t idx,
                                       size_t &idx_result) override {
    if (idx < G_srcf_table->get_count()) {
      CVmSrcfEntry *e = G_srcf_table->get_entry(idx);
      if (e) {
        idx_result = idx;
        return 0;
      }
    }
    return -1;
  }

  virtual void print(const char *text) override { helper->print(text); }
  virtual void printf(const char *fmt, ...) override {
    char buf[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    helper->print(buf);
  }
};

#endif /* FACADE_H */
