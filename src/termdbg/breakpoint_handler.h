#ifndef BREAKPOINTS_H
#define BREAKPOINTS_H

#include "textutils.h"
#include <set>
#include <string>
#include <vector>

#include "breakpoint_port.h"
#include "frobdebughelper.h"
#include "source_port.h"
#include "textutils.h"

#include "t3std.h"
#include "vmdbg.h"
#include "vmglob.h"
#include "vmpool.h"
#include "vmtype.h"

using std::make_unique;
using std::pair;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

class BreakpointHandler {
public:
  SourcePort *source_port;
  BreakpointPort *breakpoint_port;

private:
  /* Local registry of active breakpoints */
  struct BpInfo {
    int bpnum;
    const uchar *code_addr;
    std::string fname;
    unsigned long linenum;
    bool disabled;
  };

  /*
   * Maintain a local registry of breakpoints to avoid needing to
   * query the VM for breakpoint information every time we print a
   * file. We will sync this registry with the VM breakpoints when we
   * fetch file contents for printing, and also update it when
   * breakpoints are added/removed.
   */
  std::vector<BpInfo> bp_registry_;

  // Helper method to sync the local breakpoint registry with the VM breakpoints
  // - this because the VM is the source of truth for breakpoints, but we want
  // to avoid querying it repeatedly when printing files
  void sync_from_vm(VMG0_) {
    std::vector<BpInfo> synced_registry;

    struct EnumBpCtx {
      std::vector<BpInfo> *registry;
    } ectx;

    ectx.registry = &synced_registry;

    auto bp_cb = [](void *cbctx, int bpnum, const uchar *code_addr,
                    int disabled, const char *fname, unsigned long linenum) {
      EnumBpCtx *ctx = (EnumBpCtx *)cbctx;
      BpInfo info;
      info.bpnum = bpnum;
      info.code_addr = code_addr;
      info.fname = fname ? fname : "";
      info.linenum = linenum;
      info.disabled = (disabled != 0);
      ctx->registry->push_back(info);
    };

    breakpoint_port->enum_breakpoints(vmg_(EnumBreakpointsCallback) bp_cb,
                      &ectx);

    if (!synced_registry.empty() || bp_registry_.empty()) {
      bp_registry_.swap(synced_registry);
    }
  }

public:
  BreakpointHandler(SourcePort *source_port, BreakpointPort *breakpoint_port)
      : source_port(source_port), breakpoint_port(breakpoint_port) {}

  /* Remove a breakpoint from the local registry by breakpoint number */
  void remove_from_registry(int bpnum) {
    for (auto it = bp_registry_.begin(); it != bp_registry_.end(); ++it) {
      if (it->bpnum == bpnum) {
        bp_registry_.erase(it);
        return;
      }
    }
  }

  /**
   * Extracts the source file index and line number for a breakpoint.
   * Returns true if valid, false otherwise. Handles file:line, <line>, and
   * disambiguation forms.
   */
  int resolve_breakpoint_location(const char *args, int &source_idx,
                                  unsigned long &line, string &response) {
    std::string filepart;
    int user_specificed_line = 0; // Initialize line number to zero
    if (!parse_file_line_args(args, filepart, user_specificed_line)) {
      response = "Usage: break <line> | break <file> <line> | break "
                 "<file>:<line> | break "
                 "<file-num>:<line>\n";
      return -1;
    }

    // If no file was specified, use the current source file
    if (filepart.empty()) {

      const char *curfname = NULL;
      unsigned long curline = 0;
        if (source_port->get_source_info(vmg_ & curfname, &curline, 0) != 0 ||
          curfname == NULL) {
        response = "No current source information available\n";
        return -1;
      }

      // Try to find the source file entry for the current file name
      size_t idx;
      if (source_port->locate_srcf_entry_by_name(curfname, idx) == 0) {
        source_idx = (int)idx;
        line = user_specificed_line;
        return 0;
      }

      response = "Could not find source file entry for current file\n";
      return -1;
    }

    // We have a file part, so we now need to resolve it to a source file index.
    // This can be an exact match, suffix match, base name match, or substring
    // match. If there are multiple matches, we will print them and tell the
    // user to disambiguate by providing a longer path.  If there are multiple
    // matches, we will print them and tell the user to disambiguate by
    // providing a longer path
    int file_index = -1;
    auto filepart_string = filepart.c_str();

    // Get the list of all source file paths (with absolute paths) from the
    // global source file table and find matches for the file part provided by
    // the user.
    auto file_paths = source_port->get_all_abs_filepaths();
    auto matches = find_source_file_matches(file_paths, filepart_string);
    if (matches.empty()) {
      response = "No matching source files found\n";
      return -1;
    }

    // If we have exactly one match, we can use it directly.
    if (matches.size() == 1) {
      source_idx = matches[0];
      line = user_specificed_line;
      return 0;
    }

    // If we have multiple matches, we need to ask the user to disambiguate.
    response = get_matching_source_files(matches);
    return -1;
  }

  /**
   * Handle the 'break' command to set a breakpoint
   * Supported forms:
   *   - break <line>                 -> line in current source file
   *   - break <file>:<line>          -> resolve a file and set breakpoint there
   *   - break <index> <line>         -> disambiguate from a previous
   * multi-match list
   */
  int set_breakpoint(VMG_ const char *args, std::string &response) {
    /**
     * Extract file name and line number from an argument string.
     * Returns true if parsing succeeded, false otherwise.
     * If filepart is nullptr, only line is parsed.
     */
    if (args[0] == '\0') {
      response = "Usage: break <line> | break <file>:<line> | break "
                 "<file-num>:<line>\n";
      return -1;
    }

    // Try to resolve the breakpoint location from the user input.
    // This will handle parsing the file and line information, as well as
    // resolving the file part to a specific source file index.
    int source_idx = -1;
    unsigned long line = 0;
    if (resolve_breakpoint_location(args, source_idx, line, response) != 0) {
      return -1;
    }

    // We have a valid source index and line number,
    // so we try to set breakpoint given that information.
    return set_bp_on_entry(source_idx, line, response);
  }

  std::string get_matching_source_files(const vector<int> &matches) {
    std::string response = "Multiple source files match:\n";

    // Collect the source file entry (name) for each match
    for (size_t i = 0; i < matches.size(); ++i) {
      std::string fname;
      long idx;
        if (source_port->get_source_idx_fname(vmg_(size_t) matches[i], fname) ==
          0) {
        response += std::to_string(i + 1) + ": " + fname + "\n";
      } else {
        response += std::to_string(i + 1) + ": (unknown)\n";
      }
    }
    response += "\nSupply a longer path to disambiguate\n\n";
    return response;
  }

  /**
   * Set a breakpoint on a specific source file entry and line number
   */ 
  int set_bp_on_entry(int entry_idx, unsigned long &wantline,
                      std::string &response) {

    const char *fname = nullptr;
    ulong ofs = 0;
    if (source_port->get_offset_source_info(wantline, entry_idx, ofs, &fname,
                                            response) != 0) {
      return -1;
    }

    // Note: We have to cast the code address to const uchar* because
    // that's what the debugger expects, even though it's really a code
    // pointer.

    // The debugger will use this address to set breakpoints and will compare
    // it against the current instruction pointer during execution.
    const uchar *code_addr = (const uchar *)breakpoint_port->get_ptr(ofs);

    int bpnum = 0;
    int did_set = 0;
    char errbuf[256] = {0};

    // Toggle the breakpoint at the found code address.
    // The debugger will set a breakpoint if there isn't one already,
    // or clear it if it is already set.
    // We can check the did_set flag to see if we just set it or if it was
    // already there. The errbuf will contain an error message if the
    // operation fails.
    int res =
      breakpoint_port->toggle_breakpoint(vmg_ code_addr, 0, 0, &bpnum,
                         &did_set, errbuf, sizeof(errbuf));

    //  If res is non-zero, there was an error.
    if (res != 0) {
      char buf[512];
      snprintf(buf, sizeof(buf), "Failed to set breakpoint: %s\n", errbuf);
      response = buf;
      return -1;
    }

    char buf[256];

    // if we successfully set a breakpoint, did_set will be true. If there was
    // already a breakpoint at that location, did_set will be false.

    // We can use this to inform the user whether we set a new breakpoint or
    // if there was already one there. The bpnum variable will contain the
    // breakpoint number assigned by the debugger, which can be used for later
    // reference (e.g., for deleting the breakpoint).

    if (did_set) {
      /* Record the breakpoint in our local registry */
      BpInfo info;
      info.bpnum = bpnum;
      info.code_addr = code_addr;
      info.fname = fname ? fname : "";
      info.linenum = wantline;
      info.disabled = false;
      bp_registry_.push_back(info);
      breakpoint_port->upsert_breakpoint_info(vmg_ bpnum, code_addr, fname,
                      wantline, FALSE);

      snprintf(buf, sizeof(buf), "Breakpoint %d set at %s:%lu\n", bpnum, fname,
               wantline);

    } else {
      /* A breakpoint already existed at this location and was removed by the
       * first toggle. Save the original number, then re-add the breakpoint. */
      int old_bpnum = bpnum;
      breakpoint_port->toggle_breakpoint(vmg_ code_addr, 0, 0, &bpnum,
                     &did_set, errbuf, sizeof(errbuf));

      /* Update registry: replace old entry with re-added breakpoint */
      remove_from_registry(old_bpnum);

      breakpoint_port->remove_breakpoint_info(vmg_ old_bpnum);
      BpInfo info;
      info.bpnum = bpnum;
      info.code_addr = code_addr;
      info.fname = fname ? fname : "";
      info.linenum = wantline;
      info.disabled = false;
      bp_registry_.push_back(info);
      breakpoint_port->upsert_breakpoint_info(vmg_ bpnum, code_addr, fname,
                      wantline, FALSE);
      snprintf(buf, sizeof(buf), "Breakpoint %d already set at %s:%lu\n", bpnum,
               fname, wantline);
    }
    response = buf;
    return 0;
  }

  int delete_breakpoint(VMG_ int bpnum, std::string &response) {
    if (bpnum <= 0) {
      response = "Invalid breakpoint number\n";
      return -1;
    }

    breakpoint_port->delete_breakpoint(vmg_ bpnum);
    remove_from_registry(bpnum);
    breakpoint_port->remove_breakpoint_info(vmg_ bpnum);

    char buf[128];
    snprintf(buf, sizeof(buf), "Breakpoint %d deleted\n", bpnum);
    response = buf;
    return 0;
  }

  void list_breakpoints(VMG_ string &response) {
    sync_from_vm(vmg0_);

    response += "Breakpoints:\n";

    if (bp_registry_.empty()) {
      response += "  No breakpoints set.\n";
      return;
    }

    int count = 0;
    for (const auto &bp : bp_registry_) {
      char buf[512];
      count++;

      const char *base = strrchr(bp.fname.c_str(), '/');
      if (base == nullptr)
        base = strrchr(bp.fname.c_str(), '\\');
      if (base != nullptr)
        base++;
      else
        base = bp.fname.c_str();

      if (bp.disabled) {
        snprintf(buf, sizeof(buf), "  %d: %s:%lu (disabled)\n", bp.bpnum, base,
                 bp.linenum);
      } else {
        snprintf(buf, sizeof(buf), "  %d: %s:%lu\n", bp.bpnum, base,
                 bp.linenum);
      }
      response += buf;
    }

    char buf[128];
    snprintf(buf, sizeof(buf), "\nTotal: %d breakpoint(s)\n", count);
    response += buf;
    return;
  }

  /**
   * Fetch the list of breakpoints as a set of (filename, line number) pairs
   * for quick lookup. Uses the local registry maintained by set_bp_on_entry.
   *
   * returns: A set of pairs, where each pair consists of a source file name
   * and a line number, representing the location of each active breakpoint.
   */
  set<pair<string, unsigned long>> fetch_breakpoints(VMG0_) {
    sync_from_vm(vmg0_);

    set<pair<string, unsigned long>> bp_set;

    for (const auto &bp : bp_registry_) {
      if (!bp.fname.empty() && !bp.disabled) {
        bp_set.emplace(bp.fname, bp.linenum);
      }
    }

    return bp_set;
  }
};

#endif /* BREAKPOINTS_H */
