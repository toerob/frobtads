#ifndef SOURCE_PORT_H
#define SOURCE_PORT_H

#include "common.h"

#include "t3std.h"
#include "vmglob.h"
#include "vmpool.h"
#include "vmtype.h"

#include <string>
#include <utility>
#include <vector>

struct SourceContentsResult {
  int status;                     // 0 = success, nonzero = error
  std::string message;            // error or info
  std::string fname;              // filename of the source file
  std::vector<std::string> lines; // lines of the source file (empty if error)
};

class SourcePort {
public:
  virtual int get_source_info(VMG_ const char **fname, unsigned long *linenum,
                              int level) {
    (void)fname;
    (void)linenum;
    (void)level;
    return 1;
  }

  virtual int get_source_idx_fname(VMG_ size_t idx, std::string &fname) {
    (void)idx;
    (void)fname;
    return 1;
  }

  virtual SourceContentsResult get_entry_source_contents(VMG_ size_t filenumber) {
    (void)filenumber;
    return SourceContentsResult{-1, "Not implemented\n", "", {}};
  }

  virtual int get_offset_source_info(VMG_ unsigned long &wantline,
                                     size_t entry_idx, ulong &ofs,
                                     const char **fname,
                                     std::string &response) {
    (void)wantline;
    (void)entry_idx;
    (void)ofs;
    (void)fname;
    (void)response;
    return 1;
  }

  virtual std::vector<std::string>
  get_all_abs_filepaths(bool master_records_only = false) {
    (void)master_records_only;
    return {};
  }

  virtual std::vector<std::pair<size_t, std::string>>
  get_all_abs_idx_and_filepaths(bool master_records_only = false) {
    (void)master_records_only;
    return {};
  }

  virtual int locate_srcf_entry_by_name(const char *name, size_t &idx) {
    (void)name;
    (void)idx;
    return 1;
  }

  virtual int locate_srcf_entry_by_idx(size_t idx, size_t &idx_result) {
    (void)idx;
    (void)idx_result;
    return 1;
  }

  // Resolve a raw source path from debug info to an absolute, normalized path.
  // Returns the input unchanged if resolution is not possible.
  virtual std::string resolve_source_path(const char *fname) {
    return fname ? fname : "";
  }

  virtual ~SourcePort() = default;
};

#endif /* SOURCE_PORT_H */
