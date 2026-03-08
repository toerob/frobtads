#include "textutils.h"
#include <gtest/gtest.h>

#include <fstream>
#include <set>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

#include <frobdebughelper.h>
#include <termdbg/print_file_handler.h>

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

extern "C" {
void os_stubs_reset_inputs(void);
int os_stubs_add_input(const char *line);
}

class TestFacade : public SourcePort, public BreakpointPort, public OutputPort {
public:
  std::string output_buffer = "";
  std::vector<std::pair<size_t, std::string>> indexed_files;
  std::vector<std::string> abs_files;
  std::vector<EnumBreakpointRecord> enum_breakpoints_records;
  int get_source_info_rc = 1;
  const char *source_info_fname = nullptr;
  unsigned long source_info_line = 0;
  int locate_srcf_entry_by_name_rc = 1;
  size_t located_source_idx = 0;
  int get_offset_source_info_rc = 1;
  ulong offset_source_info_ofs = 0;
  const char *offset_source_info_fname = nullptr;
  std::string get_offset_error =
      "No executable code found at or after that line\n";
  const char *ptr_base = "CODE_BASE";
  std::vector<ToggleResult> toggle_results;
  SourceContentsResult entry_source_contents =
      SourceContentsResult{-1, "Not implemented\n", "", {}};

  virtual void print(const char *text) override {
    output_buffer +=
        text; // For testing, we just append the text to the output buffer
  }

  virtual void printf(const char *fmt, ...) override {
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    output_buffer += buf;
  }

  virtual std::vector<std::pair<size_t, std::string>>
  get_all_abs_idx_and_filepaths(bool master_records_only = false) override {
    (void)master_records_only;
    if (!indexed_files.empty()) {
      return indexed_files;
    }
    return {{0, "main.t"}, {1, "testfile.t"}};
  }

  virtual int get_source_idx_fname(VMG_ size_t idx,
                                   std::string &fname) override {
    if (idx == 0) {
      fname = "main.t";
      return 0;
    } else if (idx == 1) {
      fname = "testfile.t";
      return 0;
    }
    return 1; // Not found
  }

  virtual std::vector<std::string>
  get_all_abs_filepaths(bool master_records_only = false) override {
    (void)master_records_only;
    return abs_files;
  }

  virtual SourceContentsResult
  get_entry_source_contents(VMG_ size_t filenumber) override {
    (void)filenumber;
    return entry_source_contents;
  }

  virtual int get_source_info(VMG_ const char **fname, unsigned long *linenum,
                              int level) override {
    (void)level;
    if (get_source_info_rc != 0) {
      return get_source_info_rc;
    }
    *fname = source_info_fname;
    *linenum = source_info_line;
    return 0;
  }

  virtual void enum_breakpoints(VMG_ EnumBreakpointsCallback cbfunc,
                                void *cbctx) override {
    for (const auto &bp : enum_breakpoints_records) {
      cbfunc(cbctx, bp.bpnum, bp.code_addr, bp.disabled, bp.fname.c_str(),
             bp.linenum);
    }
  }

  virtual int locate_srcf_entry_by_name(const char *name,
                                        size_t &idx) override {
    (void)name;
    if (locate_srcf_entry_by_name_rc != 0) {
      return locate_srcf_entry_by_name_rc;
    }
    idx = located_source_idx;
    return 0;
  }

  virtual int get_offset_source_info(VMG_ unsigned long &wantline,
                                     size_t entry_idx, ulong &ofs,
                                     const char **fname,
                                     std::string &response) override {
    (void)wantline;
    (void)entry_idx;
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
    (void)code_addr;
    (void)cond;
    (void)change;
    if (toggle_results.empty()) {
      snprintf(errbuf, errbuflen, "No toggle result configured");
      *bpnum = 0;
      *did_set = 0;
      return 1;
    }

    auto next = toggle_results.front();
    toggle_results.erase(toggle_results.begin());
    *bpnum = next.bpnum;
    *did_set = next.did_set;
    snprintf(errbuf, errbuflen, "%s", next.err.c_str());
    return next.rc;
  }
};

TEST(PrintFileHandler, show_files) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(facade, facade);
  auto print_file_handler =
      new PrintFileHandler(facade, facade, breakpoint_handler);

  try {
    std::string response;
    int result = print_file_handler->show_files(response);

    EXPECT_EQ(result, 0);
    EXPECT_EQ(response,
              "Source files with master records:\n0: main.t\n1: testfile.t\n");
  } catch (const std::exception &e) {
    FAIL() << "Exception thrown: " << e.what();
  }
}

TEST(PrintFileHandler, print_file_invalid_arg) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->print_file(vmg_ "\0", 0, response);

  EXPECT_EQ(result, -1);
  EXPECT_EQ(response, "Usage: print <num|filename> to select source to print. "
                      "See numbering of files with command: files\n");
}

TEST(PrintFileHandler, show_files_custom_source_list) {
  auto *facade = new TestFacade();
  facade->indexed_files = {{5, "custom.t"}, {8, "nested/other.t"}};
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->show_files(response);

  EXPECT_EQ(result, 0);
  EXPECT_EQ(response, "Source files with master records:\n"
                      "5: custom.t\n"
                      "8: nested/other.t\n");
}

TEST(PrintFileHandler, print_source_matches) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  // Which is: "Multiple source files match:\n  1: \n  2: \n"
  // "Multiple source files match:\n 1: main.t\n 2: testfile.t\n"

  std::vector<int> matches = {0, 1};
  print_file_handler->print_source_matches(matches);
  printf("Output buffer: %s\n", facade->output_buffer.c_str());
  //"Multiple source files match:\n%2zu: %s\n%2zu: %s\n"
  EXPECT_EQ(facade->output_buffer, "Multiple source files match:\n"
                                   "  1: main.t\n"
                                   "  2: testfile.t\n");
}

TEST(PrintFileHandler, parse_user_command_empty_defaults_to_next) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  auto result = print_file_handler->parse_user_command("");
  EXPECT_EQ(result.action, NEXT);
  EXPECT_EQ(result.result, 0);
}

TEST(PrintFileHandler, parse_user_command_quit_variants) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  auto lower = print_file_handler->parse_user_command("q");
  EXPECT_EQ(lower.action, QUIT);
  EXPECT_EQ(lower.result, 0);

  auto upper = print_file_handler->parse_user_command("Q");
  EXPECT_EQ(upper.action, QUIT);
  EXPECT_EQ(upper.result, 0);
}

TEST(PrintFileHandler, parse_user_command_next_and_previous) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  auto next = print_file_handler->parse_user_command("n");
  EXPECT_EQ(next.action, NEXT);
  EXPECT_EQ(next.result, 0);

  auto prev = print_file_handler->parse_user_command("P");
  EXPECT_EQ(prev.action, PREVIOUS);
  EXPECT_EQ(prev.result, 0);
}

TEST(PrintFileHandler, parse_user_command_set_new_limit_valid_and_invalid) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  auto valid = print_file_handler->parse_user_command("42");
  EXPECT_EQ(valid.action, SET_NEW_LIMIT);
  EXPECT_EQ(valid.result, 0);
  EXPECT_EQ(valid.new_limit, 42);

  auto invalid = print_file_handler->parse_user_command("abc");
  EXPECT_EQ(invalid.action, SET_NEW_LIMIT);
  EXPECT_EQ(invalid.result, -1);
  EXPECT_EQ(invalid.new_limit, 20);
}

TEST(PrintFileHandler, parse_user_command_breakpoint_valid_and_invalid) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  auto valid = print_file_handler->parse_user_command("b 17");
  EXPECT_EQ(valid.action, SET_BREAKPOINT);
  EXPECT_EQ(valid.result, 0);
  EXPECT_EQ(valid.breakpoint_line, 17);

  auto invalid = print_file_handler->parse_user_command("break 0");
  EXPECT_EQ(invalid.action, SET_BREAKPOINT);
  EXPECT_EQ(invalid.result, -1);
  EXPECT_EQ(invalid.breakpoint_line, 0);
  EXPECT_NE(facade->output_buffer.find("Invalid line number\n"),
            std::string::npos);
}

TEST(PrintFileHandler, print_file_numeric_source_lookup_error_returns_message) {
  auto *facade = new TestFacade();
  facade->entry_source_contents =
      SourceContentsResult{-1, "Invalid file number\n", "", {}};
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->print_file(vmg_ "999", 0, response);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(response, "Invalid file number\n");
}

TEST(PrintFileHandler, print_file_filename_no_matches) {
  auto *facade = new TestFacade();
  facade->abs_files = {"/games/main.t", "/games/other.t"};
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result =
      print_file_handler->print_file(vmg_ "does-not-exist.t", 0, response);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(response, "No matching source files found\n");
}

TEST(PrintFileHandler, print_file_filename_multiple_matches_prints_choices) {
  auto *facade = new TestFacade();
  facade->abs_files = {"/games/main.t", "/mods/main.t", "/games/other.t"};
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->print_file(vmg_ "main.t", 0, response);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(response, "\n(Type a longer path to select the correct file)\n");
  EXPECT_EQ(facade->output_buffer, "Multiple source files match:\n"
                                   "  1: main.t\n"
                                   "  2: testfile.t\n");
}

TEST(PrintFileHandler, print_source_context_no_source_info) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->print_source_context(vmg_ 0, 2, response);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(response, "(source not available)\n");
}

TEST(PrintFileHandler, print_source_context_file_not_found) {
  auto *facade = new TestFacade();
  facade->get_source_info_rc = 0;
  facade->source_info_fname = "/tmp/this-file-should-not-exist-123456.t";
  facade->source_info_line = 3;

  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->print_source_context(vmg_ 0, 2, response);
  EXPECT_EQ(result, -1);
  EXPECT_EQ(response, "(source not available)\n");
}

TEST(PrintFileHandler, print_source_context_renders_lines_with_current_marker) {
  auto *facade = new TestFacade();
  const std::string path = "/tmp/frobtads_print_context_test.t";
  {
    std::ofstream out(path);
    out << "line 1\n";
    out << "line 2\n";
    out << "line 3\n";
    out << "line 4\n";
  }

  facade->get_source_info_rc = 0;
  facade->source_info_fname = path.c_str();
  facade->source_info_line = 3;
  facade->enum_breakpoints_records = {
      {10, nullptr, 0, path, 2},
  };

  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  std::string response;
  int result = print_file_handler->print_source_context(vmg_ 0, 1, response);
  EXPECT_EQ(result, 0);
  EXPECT_NE(response.find("=>"), std::string::npos);
  EXPECT_NE(response.find("line 2"), std::string::npos);
  EXPECT_NE(response.find("line 3"), std::string::npos);
  EXPECT_NE(response.find("line 4"), std::string::npos);
  EXPECT_NE(response.find("*"), std::string::npos);

  std::remove(path.c_str());
}

TEST(PrintFileHandler, print_lines_interactively_quit_immediately) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  os_stubs_reset_inputs();
  ASSERT_EQ(os_stubs_add_input("q"), 0);

  std::vector<std::string> lines = {"line 1", "line 2"};
  std::set<std::pair<std::string, unsigned long>> bps;

  int result =
      print_file_handler->print_lines_interactively(lines, "main.t", bps);
  EXPECT_EQ(result, 0);
  EXPECT_NE(facade->output_buffer.find("[Exiting file view]"),
            std::string::npos);
}

TEST(PrintFileHandler, print_lines_interactively_invalid_then_quit) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  os_stubs_reset_inputs();
  ASSERT_EQ(os_stubs_add_input("abc"), 0);
  ASSERT_EQ(os_stubs_add_input("q"), 0);

  std::vector<std::string> lines = {"line 1", "line 2", "line 3"};
  std::set<std::pair<std::string, unsigned long>> bps;

  int result =
      print_file_handler->print_lines_interactively(lines, "main.t", bps);
  EXPECT_EQ(result, 0);
  EXPECT_NE(facade->output_buffer.find("[Exiting file view]"),
            std::string::npos);
}

TEST(PrintFileHandler, print_lines_interactively_set_new_limit_then_quit) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  os_stubs_reset_inputs();
  ASSERT_EQ(os_stubs_add_input("2"), 0);
  ASSERT_EQ(os_stubs_add_input("q"), 0);

  std::vector<std::string> lines = {"line 1", "line 2", "line 3"};
  std::set<std::pair<std::string, unsigned long>> bps;

  int result =
      print_file_handler->print_lines_interactively(lines, "main.t", bps);
  EXPECT_EQ(result, 0);
  EXPECT_NE(facade->output_buffer.find("[New limit set to 2 lines]"),
            std::string::npos);
}

TEST(PrintFileHandler, print_lines_interactively_previous_then_quit) {
  auto *facade = new TestFacade();
  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  os_stubs_reset_inputs();
  ASSERT_EQ(os_stubs_add_input("p"), 0);
  ASSERT_EQ(os_stubs_add_input("q"), 0);

  std::vector<std::string> lines = {"line 1", "line 2", "line 3"};
  std::set<std::pair<std::string, unsigned long>> bps;

  int result =
      print_file_handler->print_lines_interactively(lines, "main.t", bps);
  EXPECT_EQ(result, 0);
  auto first_pos = facade->output_buffer.find("line 1");
  ASSERT_NE(first_pos, std::string::npos);
  EXPECT_NE(facade->output_buffer.find("line 1", first_pos + 1),
            std::string::npos);
}

TEST(PrintFileHandler,
     print_lines_interactively_set_breakpoint_success_then_quit) {
  auto *facade = new TestFacade();
  facade->locate_srcf_entry_by_name_rc = 0;
  facade->located_source_idx = 0;
  facade->get_offset_source_info_rc = 0;
  facade->offset_source_info_ofs = 77;
  facade->offset_source_info_fname = "main.t";
  facade->toggle_results = {{0, 5, 1, ""}};

  auto breakpoint_handler = make_unique<BreakpointHandler>(
      static_cast<SourcePort *>(facade), static_cast<BreakpointPort *>(facade));
  auto print_file_handler = new PrintFileHandler(
      static_cast<SourcePort *>(facade), static_cast<OutputPort *>(facade),
      breakpoint_handler);

  os_stubs_reset_inputs();
  ASSERT_EQ(os_stubs_add_input("b 2"), 0);
  ASSERT_EQ(os_stubs_add_input("q"), 0);

  std::vector<std::string> lines = {"line 1", "line 2", "line 3"};
  std::set<std::pair<std::string, unsigned long>> bps;

  int result =
      print_file_handler->print_lines_interactively(lines, "main.t", bps);
  EXPECT_EQ(result, 0);
  EXPECT_NE(facade->output_buffer.find("[Breakpoint set successfully]"),
            std::string::npos);
  EXPECT_NE(facade->output_buffer.find("Breakpoint 5 set at main.t:2"),
            std::string::npos);
  EXPECT_TRUE(bps.count({"main.t", 2}) == 1u);
}
