#include "textutils.h"
#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <tuple>
#include <vector>

#include <termdbg/breakpoint_handler.h>

struct EnumBreakpointRecord {
  int bpnum;
  const uchar *code_addr;
  int disabled;
  std::string fname;
  unsigned long linenum;
};

struct ToggleResult {
  int rc;
  int bpnum;
  int did_set;
  std::string err;
};

class TestFacade : public SourcePort, public BreakpointPort {
public:
  int get_source_info_rc = 1;
  const char *current_fname = nullptr;
  unsigned long current_line = 0;

  int locate_srcf_entry_by_name_rc = 1;
  size_t located_idx = 0;

  std::vector<std::string> file_paths;
  std::map<size_t, std::string> source_idx_to_name;

  int get_offset_source_info_rc = 1;
  unsigned long offset_source_info_line = 0;
  size_t offset_source_info_entry_idx = 0;
  ulong offset_source_info_ofs = 0;
  const char *offset_source_info_fname = nullptr;
  std::string get_offset_error =
      "No executable code found at or after that line\n";

  const char *ptr_base = "CODE_BASE";

  std::vector<ToggleResult> toggle_results;
  std::vector<const uchar *> toggle_addrs;

  std::vector<int> upsert_bpnums;
  std::vector<int> removed_bpnums;
  std::vector<int> deleted_bpnums;

  std::vector<EnumBreakpointRecord> enum_breakpoints_records;

  virtual int get_source_info(VMG_ const char **fname, unsigned long *linenum,
                              int level) override {
    (void)level;
    if (get_source_info_rc == 0) {
      *fname = current_fname;
      *linenum = current_line;
    }
    return get_source_info_rc;
  }

  virtual int locate_srcf_entry_by_name(const char *name,
                                        size_t &idx) override {
    (void)name;
    if (locate_srcf_entry_by_name_rc == 0) {
      idx = located_idx;
    }
    return locate_srcf_entry_by_name_rc;
  }

  virtual std::vector<std::string>
  get_all_abs_filepaths(bool master_records_only = false) override {
    (void)master_records_only;
    return file_paths;
  }

  virtual int get_source_idx_fname(VMG_ size_t idx,
                                   std::string &fname) override {
    auto it = source_idx_to_name.find(idx);
    if (it == source_idx_to_name.end()) {
      return 1;
    }
    fname = it->second;
    return 0;
  }

  virtual int get_offset_source_info(VMG_ unsigned long &wantline,
                                     size_t entry_idx, ulong &ofs,
                                     const char **fname,
                                     std::string &response) override {
    offset_source_info_line = wantline;
    offset_source_info_entry_idx = entry_idx;
    if (get_offset_source_info_rc != 0) {
      response = get_offset_error;
      return get_offset_source_info_rc;
    }
    ofs = offset_source_info_ofs;
    *fname = offset_source_info_fname;
    return 0;
  }

  virtual const char *get_ptr(pool_ofs_t ofs) override {
    (void)ofs;
    return ptr_base;
  }

  virtual int toggle_breakpoint(VMG_ const uchar *code_addr, const char *cond,
                                int change, int *bpnum, int *did_set,
                                char *errbuf, size_t errbuflen) override {
    (void)cond;
    (void)change;
    toggle_addrs.push_back(code_addr);

    if (toggle_results.empty()) {
      std::snprintf(errbuf, errbuflen, "No toggle result configured");
      *bpnum = 0;
      *did_set = 0;
      return 1;
    }

    auto next = toggle_results.front();
    toggle_results.erase(toggle_results.begin());
    *bpnum = next.bpnum;
    *did_set = next.did_set;
    std::snprintf(errbuf, errbuflen, "%s", next.err.c_str());
    return next.rc;
  }

  virtual void upsert_breakpoint_info(VMG_ int bpnum, const uchar *code_addr,
                                      const char *fname, unsigned long linenum,
                                      int disabled) override {
    (void)code_addr;
    (void)fname;
    (void)linenum;
    (void)disabled;
    upsert_bpnums.push_back(bpnum);
  }

  virtual void remove_breakpoint_info(VMG_ int bpnum) override {
    removed_bpnums.push_back(bpnum);
  }

  virtual void delete_breakpoint(VMG_ int bpnum) override {
    deleted_bpnums.push_back(bpnum);
  }

  virtual void enum_breakpoints(VMG_ EnumBreakpointsCallback cbfunc,
                                void *cbctx) override {
    for (const auto &bp : enum_breakpoints_records) {
      cbfunc(cbctx, bp.bpnum, bp.code_addr, bp.disabled, bp.fname.c_str(),
             bp.linenum);
    }
  }
};

TEST(BreakpointHandlerTest, set_breakpoint_empty_args_returns_usage) {
  auto *facade = new TestFacade();
  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "", response), -1);
  EXPECT_STREQ(response.c_str(),
               "Usage: break <line> | break <file>:<line> | break "
               "<file-num>:<line>\n");
}

TEST(BreakpointHandlerTest, set_breakpoint_line_only_uses_current_source) {
  auto *facade = new TestFacade();
  facade->get_source_info_rc = 0;
  facade->current_fname = "/tmp/main.t";
  facade->locate_srcf_entry_by_name_rc = 0;
  facade->located_idx = 3;
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 42;
  facade->offset_source_info_fname = "/tmp/main.t";
  facade->toggle_results = {{0, 7, 1, ""}};

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "12", response), 0);
  EXPECT_STREQ(response.c_str(), "Breakpoint 7 set at /tmp/main.t:12\n");
  EXPECT_EQ(facade->offset_source_info_entry_idx, 3u);
  EXPECT_EQ(facade->offset_source_info_line, 12u);
  ASSERT_EQ(facade->upsert_bpnums.size(), 1u);
  EXPECT_EQ(facade->upsert_bpnums[0], 7);
}

TEST(BreakpointHandlerTest, set_breakpoint_line_only_no_current_source_info) {
  auto *facade = new TestFacade();
  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "10", response), -1);
  EXPECT_STREQ(response.c_str(), "No current source information available\n");
}

TEST(BreakpointHandlerTest,
     set_breakpoint_line_only_current_file_not_found_in_source_table) {
  auto *facade = new TestFacade();
  facade->get_source_info_rc = 0;
  facade->current_fname = "/tmp/main.t";

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "10", response), -1);
  EXPECT_STREQ(response.c_str(),
               "Could not find source file entry for current file\n");
}

TEST(BreakpointHandlerTest, set_breakpoint_file_colon_single_match) {
  auto *facade = new TestFacade();
  facade->file_paths = {"/a/main.t", "/b/other.t"};
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 100;
  facade->offset_source_info_fname = "/a/main.t";
  facade->toggle_results = {{0, 12, 1, ""}};

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "main.t:21", response), 0);
  EXPECT_STREQ(response.c_str(), "Breakpoint 12 set at /a/main.t:21\n");
  EXPECT_EQ(facade->offset_source_info_entry_idx, 0u);
  EXPECT_EQ(facade->offset_source_info_line, 21u);
}

TEST(BreakpointHandlerTest, set_breakpoint_file_space_line_single_match) {
  auto *facade = new TestFacade();
  facade->file_paths = {"/a/main.t", "/b/other.t"};
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 100;
  facade->offset_source_info_fname = "/a/main.t";
  facade->toggle_results = {{0, 13, 1, ""}};

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "main.t 22", response), 0);
  EXPECT_STREQ(response.c_str(), "Breakpoint 13 set at /a/main.t:22\n");
}

TEST(BreakpointHandlerTest, set_breakpoint_invalid_location_no_matches) {
  auto *facade = new TestFacade();
  facade->file_paths = {"/a/main.t", "/b/other.t"};

  auto breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(
      breakpoint_handler->set_breakpoint(vmg_ "nonexistent.t:10", response),
      -1);
  EXPECT_STREQ(response.c_str(), "No matching source files found\n");
}

TEST(BreakpointHandlerTest, set_breakpoint_missing_line_returns_usage) {
  auto *facade = new TestFacade();

  auto breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "main.t", response), -1);
  EXPECT_STREQ(response.c_str(),
               "Usage: break <line> | break <file> <line> | break "
               "<file>:<line> | break <file-num>:<line>\n");
}

TEST(BreakpointHandlerTest,
     set_breakpoint_multiple_matches_returns_disambiguation_list) {
  auto *facade = new TestFacade();
  facade->file_paths = {"/a/main.t", "/b/main.t", "/x/other.t"};
  facade->source_idx_to_name = {
      {0, "/a/main.t"},
      {1, "/b/main.t"},
  };

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->set_breakpoint(vmg_ "main.t:8", response), -1);
  EXPECT_EQ(response, "Multiple source files match:\n"
                      "1: /a/main.t\n"
                      "2: /b/main.t\n"
                      "\nSupply a longer path to disambiguate\n\n");
}

TEST(BreakpointHandlerTest,
     set_bp_on_entry_offset_lookup_failure_bubbles_error) {
  auto *facade = new TestFacade();
  facade->get_offset_source_info_rc = 1;
  facade->get_offset_error = "No executable code found at or after that line\n";

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  unsigned long wantline = 55;
  EXPECT_EQ(breakpoint_handler->set_bp_on_entry(0, wantline, response), -1);
  EXPECT_STREQ(response.c_str(),
               "No executable code found at or after that line\n");
}

TEST(BreakpointHandlerTest,
     set_bp_on_entry_toggle_failure_returns_debug_error) {
  auto *facade = new TestFacade();
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 10;
  facade->offset_source_info_fname = "/tmp/main.t";
  facade->toggle_results = {{1, 0, 0, "boom"}};

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  unsigned long wantline = 7;
  EXPECT_EQ(breakpoint_handler->set_bp_on_entry(2, wantline, response), -1);
  EXPECT_STREQ(response.c_str(), "Failed to set breakpoint: boom\n");
}

TEST(BreakpointHandlerTest,
     set_bp_on_entry_when_already_set_readds_and_reports_existing) {
  auto *facade = new TestFacade();
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 10;
  facade->offset_source_info_fname = "/tmp/main.t";
  facade->toggle_results = {
      {0, 9, 0, ""},
      {0, 11, 1, ""},
  };

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  unsigned long wantline = 9;
  EXPECT_EQ(breakpoint_handler->set_bp_on_entry(1, wantline, response), 0);
  EXPECT_STREQ(response.c_str(),
               "Breakpoint 11 already set at /tmp/main.t:9\n");
  ASSERT_EQ(facade->toggle_addrs.size(), 2u);
  ASSERT_EQ(facade->removed_bpnums.size(), 1u);
  EXPECT_EQ(facade->removed_bpnums[0], 9);
  ASSERT_EQ(facade->upsert_bpnums.size(), 1u);
  EXPECT_EQ(facade->upsert_bpnums[0], 11);
}

TEST(BreakpointHandlerTest, delete_breakpoint_invalid_number) {
  auto *facade = new TestFacade();
  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->delete_breakpoint(vmg_ 0, response), -1);
  EXPECT_STREQ(response.c_str(), "Invalid breakpoint number\n");
}

TEST(BreakpointHandlerTest, delete_breakpoint_valid_forwards_to_facade) {
  auto *facade = new TestFacade();
  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  EXPECT_EQ(breakpoint_handler->delete_breakpoint(vmg_ 17, response), 0);
  EXPECT_STREQ(response.c_str(), "Breakpoint 17 deleted\n");
  ASSERT_EQ(facade->deleted_bpnums.size(), 1u);
  EXPECT_EQ(facade->deleted_bpnums[0], 17);
  ASSERT_EQ(facade->removed_bpnums.size(), 1u);
  EXPECT_EQ(facade->removed_bpnums[0], 17);
}

TEST(BreakpointHandlerTest, list_breakpoints_empty_registry) {
  auto *facade = new TestFacade();
  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  breakpoint_handler->list_breakpoints(vmg_ response);
  EXPECT_EQ(response, "Breakpoints:\n  No breakpoints set.\n");
}

TEST(BreakpointHandlerTest,
     list_breakpoints_formats_basename_and_disabled_status) {
  auto *facade = new TestFacade();
  facade->enum_breakpoints_records = {
      {3, nullptr, 0, "/tmp/game/main.t", 12},
      {4, nullptr, 1, "C:\\projects\\game\\other.t", 21},
  };
  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  breakpoint_handler->list_breakpoints(vmg_ response);

  EXPECT_EQ(response, "Breakpoints:\n"
                      "  3: main.t:12\n"
                      "  4: other.t:21 (disabled)\n"
                      "\nTotal: 2 breakpoint(s)\n");
}

TEST(BreakpointHandlerTest, fetch_breakpoints_returns_only_enabled_entries) {
  auto *facade = new TestFacade();
  facade->enum_breakpoints_records = {
      {1, nullptr, 0, "/tmp/game/main.t", 12},
      {2, nullptr, 1, "/tmp/game/disabled.t", 5},
      {3, nullptr, 0, "", 77},
      {4, nullptr, 0, "/tmp/game/main.t", 12},
  };

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  auto bps = breakpoint_handler->fetch_breakpoints(vmg0_);
  EXPECT_EQ(bps.size(), 1u);
  EXPECT_TRUE(bps.count({"/tmp/game/main.t", 12}) == 1u);
}

TEST(BreakpointHandlerTest,
     list_breakpoints_preserves_local_registry_when_vm_enum_is_empty) {
  auto *facade = new TestFacade();
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 999;
  facade->offset_source_info_fname = "/tmp/keep.t";
  facade->toggle_results = {{0, 99, 1, ""}};

  auto *breakpoint_handler = new BreakpointHandler(
      static_cast<SourcePort *>(facade),
      static_cast<BreakpointPort *>(facade));

  std::string response;
  unsigned long wantline = 40;
  ASSERT_EQ(breakpoint_handler->set_bp_on_entry(0, wantline, response), 0);

  facade->enum_breakpoints_records.clear();

  response.clear();
  breakpoint_handler->list_breakpoints(vmg_ response);
  EXPECT_EQ(response, "Breakpoints:\n"
                      "  99: keep.t:40\n"
                      "\nTotal: 1 breakpoint(s)\n");
}
