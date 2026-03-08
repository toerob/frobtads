#ifndef PRINTFILES_H
#define PRINTFILES_H

#include "textutils.h"
#include <string>
#include <vector>

#include "textutils.h"
#include <algorithm>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "breakpoint_handler.h"
#include "curses.h"
#include "frobdebughelper.h"
#include "output_port.h"
#include "source_port.h"

using std::pair;
using std::set;
using std::string;
using std::vector;

// Helper struct for breakpoint identification (adjust as needed)
struct Breakpoint {
  std::string filename;
  unsigned long line;
  bool operator<(const Breakpoint &other) const {
    return std::tie(filename, line) < std::tie(other.filename, other.line);
  }
};

enum UserIntervention {
  NEXT,     // next batch of lines
  PREVIOUS, // previous batch of lines
  SET_NEW_LIMIT,
  SET_BREAKPOINT,
  RESTART_VIEW,
  QUIT
};

struct UserInterventionResult {
  UserIntervention action;
  int result;
  int new_limit;       // Used if action is SET_NEW_LIMIT
  int breakpoint_line; // Used if action is SET_BREAKPOINT
};

class PrintFileHandler {
  SourcePort *source_port;
  OutputPort *output_port;
  std::unique_ptr<BreakpointHandler> &bp_handler;
  int limit = 20;

public:
  PrintFileHandler(SourcePort *source_port, OutputPort *output_port,
                   std::unique_ptr<BreakpointHandler> &bp_handler)
      : source_port(source_port), output_port(output_port),
        bp_handler(bp_handler) {}

  /**
   * Accept user input for controlling the file printout view. This will be
   * called after printing a batch of lines, to ask the user if they want to see
   * more, set a breakpoint, or quit. It will return a UserInterventionResult
   * indicating the user's choice and any relevant data (e.g., new limit or
   * breakpoint line).
   */
  UserInterventionResult parse_user_command(const char *cmd_line) {

    /* Read the user command
     * - if they just press ENTER, show the next batch of lines
     * - if they enter a number, se  t a new limit
     * - if they enter 'b <line>', set a breakpoint on the specified line
     * - if they enter 'p', show the previous batch of lines
     * - if they enter 'n', show the next batch of lines (same as pressing
     * ENTER)
     * - if they enter 'q', exit back to the debugger prompt
     * */
    if (cmd_line[0] != '\0') {
      char cmd[64] = {0};   // Command part (e.g., "q" or "100")
      char args[256] = {0}; // Arguments part

      if (parse_command_line(cmd_line, cmd, args) != 0) {
        return {QUIT, -1, 0, 0};
      } else if (strcmp(cmd, "q") == 0 || strcmp(cmd, "Q") == 0) {
        return {QUIT, 0, 0, 0};
      } else if (strcmp(cmd, "n") == 0 || strcmp(cmd, "N") == 0 ||
                 strcmp(cmd, "") == 0) {
        return {NEXT, 0, 0, 0};
      } else if (strcmp(cmd, "p") == 0 || strcmp(cmd, "P") == 0) {
        return {PREVIOUS, 0, 0, 0};
      } else if (strcmp(cmd, "break") == 0 || strcmp(cmd, "b") == 0) {
        // We expect the user to enter a line number as argument
        char args_copy[256];
        strncpy(args_copy, args, sizeof(args_copy) - 1);
        args_copy[sizeof(args_copy) - 1] = '\0';
        char *line_str = trim_lead(args_copy);

        // Convert and validate the line number
        int line = atoi(line_str);
        if (line <= 0) {
          output_port->print("Invalid line number\n");
          return {SET_BREAKPOINT, -1, 0, 0};
        }
        return {SET_BREAKPOINT, 0, 0, line};

        // If just typing a number, we treat it as setting a new limit for how
        // many lines to show in the next batch
      } else {

        // Convert and validate the line number
        int line = atoi(cmd);
        if (line <= 0) {
          return {SET_NEW_LIMIT, -1, 20, 0};
        }
        return {SET_NEW_LIMIT, 0, line, 0};
      }
    }

    // Empty line means "next".
    return {NEXT, 0, 0, 0};
  }

  int print_lines_interactively(const std::vector<std::string> &lines,
                                const std::string &fname,
                                set<pair<std::string, unsigned long>> &bps) {

    const char *LINE_COLOR = "\033[0;36m"; // Print with default Cyan color

    // Set some safety rails for line range:
    unsigned long max_index = (unsigned long)lines.size() - 1;
    unsigned long printed_lines = 0; // Lines we've printed in the current batch

    // Iterate through the lines and print them with formatting, while checking
    // for user intervention after each batch
    for (int i = 0; i <= max_index; ++i) {

      unsigned long current_line =
          i + 1; // 1-based line number for display/breakpoints

      const auto &line = lines[i];

      // Mark breakpoint lines with a red '*' marker
      auto is_bp = bps.find({fname, current_line}) != bps.end();
      const char *bp = is_bp ? "\033[0;31m*\033[0m" : " ";

      // Print the current line highlighted with a marker and color
      output_port->printf("%s%s   %4lu  %s\n", LINE_COLOR, bp, current_line,
              line.c_str());

      printed_lines++;

      std::string instruction_text =
          "\n[enter/n for the next " + std::to_string(limit) +
          " lines, p for the previous " + std::to_string(limit) +
          " lines, <nr> to set a new limit, b <line> to set a breakpoint, q to "
          "return] >";

      // Check if we reached the limit for this batch of lines
      if (printed_lines >= limit || i >= max_index) {
        printed_lines = 0; // reset for the next batch

        auto prompt_and_parse = [&](UserInterventionResult &out) -> bool {
          output_port->print(instruction_text.c_str());
          char cmd_line[256];
          if (os_gets((unsigned char *)cmd_line, sizeof(cmd_line)) == nullptr) {
            return false;
          }
          out = parse_user_command(cmd_line);
          return true;
        };

        // After printing a batch of lines, ask the user what to do next
        UserInterventionResult result;
        if (!prompt_and_parse(result)) {
          output_port->print("Error processing user input.\n");
          return -1;
        }

        while (result.result != 0) {
          if (!prompt_and_parse(result)) {
            output_port->print("Error processing user input.\n");
            return -1;
          }
        }

        if (result.action == QUIT) {
          output_port->print("[Exiting file view]\n");
          return 0;

        } else if (result.action == SET_NEW_LIMIT && result.result == 0) {
          limit = result.new_limit;
          output_port->printf("[New limit set to %d lines]\n", limit);
        } else if (result.action == SET_BREAKPOINT && result.result == 0) {
          // We need to set a breakpoint on the specified line
          size_t bp_line = (size_t)result.breakpoint_line;

          // Try to find the source file entry for the current file name
          printf("Trying to set breakpoint on %s:%zu\n", fname.c_str(),
                 bp_line);

          size_t source_idx = 0;
          if (source_port->locate_srcf_entry_by_name(fname.c_str(), source_idx) ==
              0) {
            std::string response;
            // unsigned long actual_line = bp_line;
            unsigned long wantline = (unsigned long)bp_line;
            if (bp_handler->set_bp_on_entry(source_idx, wantline, response) ==
                0) {
              bps.insert({fname, wantline}); // Use wantline, not bp_line
              output_port->print("[Breakpoint set successfully]\n");
              output_port->print(response.c_str());
            } else {
              output_port->print("[Failed to set breakpoint]\n");
              output_port->print(response.c_str());
            }
          }

          // Stay put to give context around the new breakpoint
          i = i - (limit * 1) > 0 ? i - (limit * 1) : 0;

        } else if (result.action == PREVIOUS) {
          i = i - (limit * 2) > 0 ? i - (limit * 2) : -1;
        }
      }
      if (i == max_index) {
        // Reset the line index to show the end of the file with some context
        // lines before it
        i = i - limit > 0 ? i - limit : 0;
      }
    }

    return 0;
  }

  int print_file(VMG_ const char *args, int limit, std::string &response) {

    if (args[0] == '\0') {
      response = "Usage: print <num|filename> to select source to print. "
                 "See numbering of files with command: files\n";
      return -1;
    }

    int filenumber = 0;
    if (parse_int_arg(args, filenumber)) {
      // Fetch the source file entry for the given file number
      // and the lines of the source file
      auto result =
          source_port->get_entry_source_contents(vmg_(size_t) filenumber);
      if (result.status != 0) {
        response = result.message;
        return result.status;
      }

      output_port->printf("File nr %d: %s\nContents:\n\n", filenumber,
                          result.fname.c_str());

      set<pair<string, unsigned long>> bps = 
          bp_handler->fetch_breakpoints(vmg0_);
      auto lines_since_pause = 0;

      if (print_lines_interactively(result.lines, result.fname, bps) != 0) {
        response = "Error printing file contents\n";
        return -1;
      }
      response = "";
      return 0;
    }

    // If the argument is not a number, treat it as a file name or part of a
    // file name to search for

    auto all_files = source_port->get_all_abs_filepaths();
    vector<int> matches = find_source_file_matches(all_files, args);

    if (matches.empty()) {
      response = "No matching source files found\n";
      return -1;
    }

    if (matches.size() > 1) {
      print_source_matches(matches);
      response = "\n(Type a longer path to select the correct file)\n";
      return -1;
    }

    // If there's exactly one match, print that file directly
    auto result =
        source_port->get_entry_source_contents(vmg_(size_t) matches[0]);
    if (result.status != 0) {
      response = result.message;
      return result.status;
    }

    output_port->printf("File nr %d: %s\nContents:\n\n", filenumber,
                        result.fname.c_str());

    set<pair<string, unsigned long>> bps = bp_handler->fetch_breakpoints(vmg0_);
    auto lines_since_pause = 0;

    if (print_lines_interactively(result.lines, result.fname, bps) != 0) {
      response = "Error printing file contents\n";
      return -1;
    }
    response = "";
    return 0;
  }

  /*
   * Print source code context (current line with surrounding lines)
   */
  int print_source_context(VMG_ int level, int context_lines,
                           std::string &response) {
    const char *fname;
    unsigned long linenum;

    // Try to find source info for the current code address
    if (source_port->get_source_info(vmg_ & fname, &linenum, level) != 0 ||
        !fname) {
      response = "(source not available)\n";
      return -1;
    }

    /* Try to open and read the source file */
    FILE *fp = fopen(fname, "r");
    if (!fp) {
      response = "(source not available)\n";
      return -1;
    }

    auto bps = bp_handler->fetch_breakpoints(vmg0_);

    // Read lines from the file
    // Calculate the range of lines to display, ensuring we don't go below line
    // 1
    unsigned long start_line =
        (linenum > context_lines) ? linenum - context_lines : 1;
    unsigned long end_line = linenum + context_lines;
    unsigned long current_line = 1;
    char line_buf[512];

    // ANSI color codes
    const char *CUR_LINE = "\033[0;37m";   // (white)
    const char *PREV_LINES = "\033[2m";    // Dimmed (cyan)
    const char *NEXT_LINES = "\033[0;36m"; // Cyan

    // Read through the file and print lines in the range,
    // marking the current line and breakpoints
    std::string output = "";
    while (fgets(line_buf, sizeof(line_buf), fp) && current_line <= end_line) {

      // Mark breakpoint lines with a red '*' marker
      auto is_bp = bps.find({fname, current_line}) != bps.end();
      const char *bp = is_bp ? "\033[0;31m*\033[0m" : " ";

      if (current_line >= start_line) {

        // Highlight the current line, dim previous lines, and normal for next
        // lines. Also, show breakpoint marker if there's a breakpoint on the
        // line.
        char buf[512];
        if (current_line == linenum) {
          snprintf(buf, sizeof(buf), "%s=> %s%4lu  %s", CUR_LINE, bp,
                   current_line, line_buf);
          output += buf;

        } else if (current_line < linenum) {
          snprintf(buf, sizeof(buf), "%s   %s%4lu  %s", PREV_LINES, bp,
                   current_line, line_buf);
          output += buf;

        } else {
          snprintf(buf, sizeof(buf), "%s   %s%4lu  %s", NEXT_LINES, bp,
                   current_line, line_buf);
          output += buf;
        }

        // Add newline if the line doesn't end with one
        size_t len = strlen(line_buf);
        if (len > 0 && line_buf[len - 1] != '\n') {
          output += "\n";
        }
      }
      current_line++;
    }
    fclose(fp);
    response = output;
    return 0;
  }

  /**
   * Helper function to print a list of source file matches
   */
  void print_source_matches(const std::vector<int> &matches) {
    output_port->print("Multiple source files match:\n");

    for (size_t i = 0; i < matches.size(); ++i) {
      std::string fname;
      source_port->get_source_idx_fname(vmg_(size_t) matches[i], fname);
      std::string msg = "  " + std::to_string(i + 1) + ": " + fname + "\n";
      output_port->print(msg.c_str());
    }
  }

  int show_files(std::string &response) {
    auto entries = source_port->get_all_abs_idx_and_filepaths(true);
    response = "Source files with master records:\n";
    for (size_t i = 0; i < entries.size(); ++i) {
      response +=
          std::to_string(entries[i].first) + ": " + entries[i].second + "\n";
    }
    return 0;
  }
};

#endif /* PRINT_FILE_HANDLER_H */
