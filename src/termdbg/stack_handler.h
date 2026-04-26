#ifndef STACK_H

#define STACK_H

#include "frobdebughelper.h"
#include "textutils.h"
#include "vmdbg.h"
#include <string>
#include <vector>

#include "termdbg/source_port.h"

//using std::unique_ptr;
//using std::make_unique;
//using std::vector;

class StackHandler {
  SourcePort *source_port;

  int stack_level = 0;

public:
  StackHandler(SourcePort *source_port) : source_port(source_port) {}

  int get_stack_level() const { return stack_level; }

  /*
   * Retrieve the current call stack by iterating through stack frames until we
   * run out of frames. For each frame, we get the source information (file name
   * and line number) and format it into a readable stack trace. We also
   * highlight the current frame (indicated by stack_level) to show the user
   * where they are in the call stack.
   */
  int get_current_call_stack(VMG_ const char *args, std::string &result) {
    (void)args;

    // We can get the stack depth and then iterate through the frames to print
    int depth = get_stack_depth(vmg0_);

    for (int level = 0; level < depth; ++level) {
      const char *fname = nullptr;
      unsigned long linenum = 0;

      // Get the source information for the current stack frame. This will give
      // us the file name and line number where the frame is executing,
      // which we can use to display a meaningful stack trace to the user.
      int rc = source_port->get_source_info(vmg_ & fname, &linenum, level);

      if (rc == 0 && fname) {
        std::string resolved = source_port->resolve_source_path(fname);
        char buf[512];
        if (level == stack_level) {
          snprintf(buf, sizeof(buf), "> #%d  at %s:%lu <\n", level,
                   resolved.c_str(), linenum);
        } else {
          snprintf(buf, sizeof(buf), "  #%d  at %s:%lu\n", level,
                   resolved.c_str(), linenum);
        }
        // Accumulate the stack trace into the result string
        result += buf;
      } else {
        // If we couldn't get source info, we can still print the frame number
        // but with unknown location. This ensures we show all frames even if
        // some of them don't have any source info.
        char buf[512];
        if (level == stack_level) {
          snprintf(buf, sizeof(buf), "> #%d  at <unknown>:0 <\n", level);
        } else {
          snprintf(buf, sizeof(buf), "  #%d  at <unknown>:0\n", level);
        }
        // Accumulate the stack trace into the result string
        result += buf;
      }
    }
    return 0;
  }

  /**
   * Handle the 'up' command to move up the call stack (to the caller frame) and
   * display the new context. This allows the user to inspect the caller's
   * context, variables, and source location.
   */
  int handle_up(VMG_ const char *args, std::string &result) {
    (void)args; // void cast to silence unused parameter warning

    int depth = get_stack_depth(vmg0_);
    if (depth <= 0) {
      result = "No stack frames available\n";
      return -1;
    }

    // If we are already at the top of the stack (stack_level + 1 >= depth),
    // we cannot move up any further, so we inform the user and return.
    if (stack_level + 1 >= depth) {
      result = "Already at top of stack\n";
      return -1;
    }

    // Display the new stack level we are moving to.
    stack_level++;
    char buf[128];
    snprintf(buf, sizeof(buf), "Stack level: %d\n", stack_level);
    result = buf;

    return 0;
  }

  /**
   * Handle the 'down' command to move down the call stack (to the callee frame)
   * and display the new context. This allows the user to inspect the callee's
   * context, variables, and source location.
   */

  int handle_down(VMG_ const char *args, std::string &result) {
    (void)args; // void cast to silence unused parameter warning
    if (stack_level <= 0) {
      // helper->print("Already at bottom of stack\n");
      result = "Already at bottom of stack\n";
      return -1;
    }
    stack_level--;
    char buf[128];
    snprintf(buf, sizeof(buf), "Stack level: %d\n", stack_level);
    // helper->print(buf);
    result = buf;
    return 0;
  }

  /**
   * Get the current stack depth by iterating through stack frames until we run
   * out of frames. This is useful for commands like 'up' and 'down' to know how
   * far we can navigate the stack.
   */
  int get_stack_depth(VMG_) {
    int level = 0;
    const char *fname = nullptr;
    unsigned long linenum = 0;
    while (source_port->get_source_info(vmg_ & fname, &linenum, level) == 0) {
      level++;
    }
    return level;
  }
};

#endif /* STACK_H */
