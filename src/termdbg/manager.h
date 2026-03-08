#ifndef DEBUGGER_COMMAND_MANAGER_H
#define DEBUGGER_COMMAND_MANAGER_H

#include "textutils.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "breakpoint_handler.h"
#include "eval_handler.h"
#include "facade.h"
#include "print_file_handler.h"
#include "stack_handler.h"

using std::function;
using std::make_unique;
using std::string;
using std::unique_ptr;
using std::unordered_map;
using std::vector;

struct DebugCommand {
  const char *name;
  const char *help;
  const char *const *aliases;
  function<void(const char *args)> handler;
};

struct DebugSubcommand {
  const char *name;
  const char *help;
  const char *const *aliases;
};

class DebuggerCommandManager {
public:
  CVmDebug *debugger;
  CVmRun *interpreter;
  CFrobDebugHelper *helper;
  CVmSrcfTable *srcf_table;

  // Reference to the global debug context, which contains VM globals and other
  // state needed by command handlers
  dbgcxdef &g_dbg_ctx;

  // Number of context lines to show around current line
  int context_lines = 5;

  unique_ptr<DebuggerFacade> facade;

  unique_ptr<BreakpointHandler> breakpoint_handler;
  unique_ptr<EvaluateHandler> evaluate_handler;
  unique_ptr<PrintFileHandler> print_handler;
  unique_ptr<StackHandler> stack_handler;

  vector<DebugCommand> cmds;

  DebuggerCommandManager(CVmDebug *debugger, CVmRun *interpreter,
                         CVmSrcfTable *srcf_table, CFrobDebugHelper *helper,
                         dbgcxdef &g_dbg_ctx)
      : debugger(debugger), interpreter(interpreter), srcf_table(srcf_table),
        helper(helper), g_dbg_ctx(g_dbg_ctx),
        facade(make_unique<DebuggerFacade>(debugger, interpreter, helper,
                                           srcf_table, g_dbg_ctx)) {

    breakpoint_handler = make_unique<BreakpointHandler>(
        static_cast<SourcePort *>(facade.get()),
        static_cast<BreakpointPort *>(facade.get()));
    evaluate_handler =
        make_unique<EvaluateHandler>(static_cast<EvalPort *>(facade.get()));
    stack_handler =
        make_unique<StackHandler>(static_cast<SourcePort *>(facade.get()));

    print_handler = make_unique<PrintFileHandler>(
        static_cast<SourcePort *>(facade.get()),
        static_cast<OutputPort *>(facade.get()), breakpoint_handler);

    static const char *step_aliases[] = {"s", nullptr};
    static const char *next_aliases[] = {"n", nullptr};
    static const char *out_aliases[] = {"o", nullptr};
    static const char *cont_aliases[] = {"c", nullptr};
    static const char *brk_aliases[] = {"b", nullptr};
    static const char *info_aliases[] = {"i", nullptr};
    static const char *list_bp_aliases[] = {"l", nullptr};
    static const char *where_aliases[] = {"w", nullptr};
    static const char *stack_aliases[] = {"stk", nullptr};
    static const char *eval_aliases[] = {"e", nullptr};
    static const char *print_aliases[] = {"p", nullptr};
    static const char *files_aliases[] = {"f", nullptr};
    static const char *delete_aliases[] = {"del", "rb", nullptr};
    static const char *quit_aliases[] = {"q", nullptr};
    static const char *help_aliases[] = {"h", "?", nullptr};
    static const char *up_aliases[] = {"u", nullptr};
    static const char *down_aliases[] = {"d", nullptr};
    static const char *watch_aliases[] = {"wt", nullptr};

    // Helper macro to simplify command registration
#define CMD(name, help, aliases, handler)                                      \
  cmds.push_back({name, help, aliases,                                         \
                  [this](const char *args) { this->handler(args); }})

    CMD("step", "Step into next instruction", step_aliases, handle_step);
    CMD("next", "Step over next instruction", next_aliases, handle_next);
    CMD("out", "Step out of current function", out_aliases, handle_out);
    CMD("continue", "Resume execution", cont_aliases, handle_continue);
    CMD("break", "Set breakpoint at line (file or file-num optional)",
        brk_aliases, handle_break);

    CMD("list", "List breakpoints", list_bp_aliases, handle_list_breakpoints);

    CMD("delete", "Delete breakpoint by number", delete_aliases, handle_delete);
    CMD("where", "Show current location", where_aliases, handle_where);
    CMD("rows", "Set source context rows (default: 5)", nullptr, handle_rows);
    CMD("stack", "Show call stack", stack_aliases, show_stack);
    CMD("info", "Show debug information", info_aliases, handle_info);

    CMD("files", "List source files", files_aliases, handle_files);
    CMD("print", "Display source file content using entry", print_aliases,
        handle_print);
    CMD("eval", "Evaluate expression at current frame", eval_aliases,
        handle_eval);
    // CMD("set", "Assign expression: set var=expr", nullptr, handle_set);
    CMD("up", "Move up the stack", up_aliases, handle_up);
    CMD("down", "Move down the stack", down_aliases, handle_down);
    CMD("quit", "Halt execution", quit_aliases, handle_quit);
    CMD("help", "Show this help", help_aliases, handle_help);
    CMD("watch", "Add watch expression: watch <expr>", watch_aliases,
        handle_watch);
    CMD("unwatch", "Unwatch an expression by index: unwatch <index>", nullptr,
        handle_unwatch);
#undef CMD
  }

  void cmd_loop(VMG_ const char *cmd_line, dbgcxdef &dbg_ctx, int bp_number,
                int error_code, const uchar **pc) {
    (void)cmd_line;
    (void)pc;

    char buf[512];
    char cmd_buf[256];

    std::string message;
    /* Print break information */

    if (bp_number != 0) {
      snprintf(buf, sizeof(buf), "\nBreakpoint %d hit at ", bp_number);
      helper->print(buf);
      helper->print_source_location(vmg_ 0);
      helper->print("\n");
      print_handler->print_source_context(vmg_ 0, context_lines, message);
    } else if (error_code != 0) {
      snprintf(buf, sizeof(buf), "\nError %d at ", error_code);
      helper->print(buf);
      helper->print_source_location(vmg_ 0);
      helper->print("\n");
      helper->print("Execution halted\n");
      print_handler->print_source_context(vmg_ 0, context_lines, message);
    } else if (dbg_ctx.stepping_mode != STEP_NONE) {
      helper->print_source_location(vmg_ 0);
      helper->print("\n");
      print_handler->print_source_context(vmg_ 0, context_lines, message);
    }

    helper->print(message.c_str());
    helper->print("\n");
    helper->flush();

    // Command loop - process commands until user resumes execution
    while (dbg_ctx.in_debugger) {

      auto list = evaluate_handler->evaluate_watch_list(VMG_);
      helper->print(list.c_str());

      // Display the prompt and read a command from the user.
      os_printz("\033[0;36mtdb >");
      os_flush();
      if (os_gets((unsigned char *)cmd_buf, sizeof(cmd_buf)) != nullptr) {
        // printf("> %s\n", cmd_buf);
        // fflush(stdout);

        if (cmd_buf[0] != '\0') {
          process_debugger_command(vmg_ cmd_buf);
        } else {
          // Empty command means "next"
          handle_next(vmg_ "");
        }
      }
    }
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
    while (debugger->get_source_info(vmg_ & fname, &linenum, level) == 0) {
      level++;
    }
    return level;
  }

  /*
   * Process a debugger command
   */
  void process_debugger_command(VMG_ const char *cmd_line) {
    char cmd[64] = {0};
    char args[256] = {0};

    if (parse_command_line(cmd_line, cmd, args) != 0) {
      return;
    }

    const auto &handlers = get_command_handlers();

    auto it = handlers.find(cmd);
    if (it != handlers.end()) {
      it->second(vmg_ args);
    } else {
      char buf[256];
      snprintf(buf, sizeof(buf),
               "Unknown command: %s (type 'help' for commands)\n", cmd);
      helper->print(buf);
    }
    helper->flush();
  }

  unordered_map<string, function<void(const char *)>> get_command_handlers() {
    unordered_map<string, function<void(const char *)>> map;
    for (const auto &cmd : cmds) {
      map.emplace(cmd.name, cmd.handler);
      if (cmd.aliases) {
        for (const char *const *alias = cmd.aliases; *alias != nullptr;
             ++alias) {
          map.emplace(*alias, cmd.handler);
        }
      }
    }
    return map;
  }

  void handle_help(VMG_ const char *args) {
    (void)args;
    helper->print("\nTADS 3 Debugger Commands:\n");
    helper->print("------------------------\n");
    for (const auto cmd : cmds) {
      string names = cmd.name;
      if (cmd.aliases) {
        for (const char *const *alias = cmd.aliases; *alias != nullptr;
             ++alias) {
          names += ", ";
          names += *alias;
        }
      }
      char buf[256];
      snprintf(buf, sizeof(buf), "%-20s - %s\n", names.c_str(), cmd.help);
      helper->print(buf);
    }

    helper->print("info [args]           - Show debug information\n");
    for (const auto &sub : get_info_subcommands()) {
      string names = sub.name;
      if (sub.aliases) {
        for (const char *const *alias = sub.aliases; *alias != nullptr;
             ++alias) {
          names += ", ";
          names += *alias;
        }
      }
      char buf[256];
      snprintf(buf, sizeof(buf), "  info %-12s - %s\n", names.c_str(),
               sub.help);
      helper->print(buf);
    }
    helper->print("\n");
  }

  static const vector<DebugSubcommand> &get_info_subcommands() {
    static const char *locals_aliases[] = {"l", nullptr};

    static const vector<DebugSubcommand> subcommands = {
        {"locals", "Local variables", locals_aliases},
    };

    return subcommands;
  }

  /**
   * Helper function to check if an 'info' subcommand matches the input.
   */
  static bool is_info_subcommand(const char *args, const char *name) {

    for (const auto &sub : get_info_subcommands()) {
      // First check if the subcommand name matches the input
      if (strcmp(sub.name, name) != 0) {
        continue;
      }
      // If the name matches, check if the input matches the subcommand name or
      // any of its aliases
      if (strcmp(args, sub.name) == 0) {
        return true;
      }
      // Check aliases if they exist
      if (sub.aliases) {
        for (const char *const *alias = sub.aliases; *alias != nullptr;
             ++alias) {
          if (strcmp(args, *alias) == 0) {
            return true;
          }
        }
      }
      // If we found a matching subcommand name but the input doesn't match
      // the name or aliases, it's not a valid subcommand invocation
      return false;
    }
    return false;
  }

  /**
   * Handle the 'next' command to step over the next instruction.
   * This will cause the debugger to execute the next instruction,
   * stepping over function calls. The user can use 'step' to step
   * into function calls instead. After executing the step,
   * the debugger will break again and return control to the user.
   */
  void handle_next(VMG_ const char *args) {
    (void)args; // void cast to silence unused parameter warning
    // Inform the user
    helper->print("Stepping over...\n");

    // Set up VM context pointer
    VMGLOB_PTR(ctx_->vmg);

    // Set stepping mode to STEP_OVER (step over next instruction)
    g_dbg_ctx.stepping_mode = STEP_OVER;
    // Tell the VM debugger to step over
    debugger->set_step_over(vmg0_);

    // Resume execution so the step can take effect
    g_dbg_ctx.in_debugger = 0;
  }

  void handle_step(VMG_ const char *args) {
    (void)args;
    // Inform the user
    helper->print("Stepping into...\n");

    // Set up VM context pointer.
    // This macro ensures that the VM global context (vmg) is correctly
    // initialized for the debugger operations that follow. Many VM and debugger
    // functions require a valid vmg pointer to access global state and perform
    // actions such as stepping, breakpoints, and variable inspection.
    VMGLOB_PTR(ctx_->vmg);

    // Set stepping mode to STEP_INTO (step into next instruction)
    g_dbg_ctx.stepping_mode = STEP_INTO;

    // Tell the VM debugger to step in
    debugger->set_step_in(vmg0_);

    // Resume execution so the step can take effect
    g_dbg_ctx.in_debugger = 0;
  }

  /**
   * Handle the 'out' command to step out of the current function. This will
   * cause the debugger to run until the current function returns, and then
   * break again.
   */
  void handle_out(VMG_ const char *args) {
    (void)args; // void cast to silence unused parameter warning

    // Inform the user
    helper->print("Stepping out...\n");

    // Set up VM context pointer
    VMGLOB_PTR(ctx_->vmg);

    // Set stepping mode to STEP_OUT (step out of current frame)
    g_dbg_ctx.stepping_mode = STEP_OUT;

    // Tell the VM debugger to step out
    debugger->set_step_out(vmg0_);

    // Resume execution so the step can take effect
    g_dbg_ctx.in_debugger = 0;
  }

  /**
   * Handle the 'continue' command to resume execution. This will cause the
   * debugger to clear any stepping mode and continue running until the next
   * breakpoint or error occurs.
   */
  void handle_continue(VMG_ const char *args) {
    (void)args; // void cast to silence unused parameter warning
    helper->print("Resuming execution...\n");

    // Set up VM context pointer
    VMGLOB_PTR(ctx_->vmg);

    // Clear stepping mode
    g_dbg_ctx.stepping_mode = STEP_NONE;

    // Tell VM debugger to clear single-step mode
    debugger->set_go(vmg0_);

    g_dbg_ctx.in_debugger = 0;
  }

  /**
   * Handle the 'break' command to set a breakpoint
   * Supported forms:
   *   - break <line>                 -> line in current source file
   *   - break <file>:<line>          -> resolve a file and set breakpoint there
   *   - break <index> <line>         -> disambiguate from a previous
   * multi-match list
   */
  void handle_break(VMG_ const char *args) {
    std::string response = "";
    breakpoint_handler->set_breakpoint(vmg_ args, response);
    helper->print(response.c_str()); // Show the message regardless outcome
  }

  /**
   * Handle the 'info' command to display debug information
   * Supported subcommands:
   *   - info locals         -> show local variables in the current frame
   *   - info                -> show general debug info (current location, etc.)
   */
  // TODO: remove the need for subcommands
  void handle_info(VMG_ const char *args) {
    auto stack_level = stack_handler->get_stack_level();
    if (strlen(args) == 0 || is_info_subcommand(args, "locals")) {
      helper->print("\nLocal variables:\n");

      auto local_info = evaluate_handler->evaluate_locals(vmg_ stack_level);
      helper->print(local_info.c_str());
      return;
    }

    helper->print("\nDebug Information:\n");
    helper->print("------------------\n");
    helper->print("Current location: ");
    helper->print_source_location(vmg_ stack_level);
    helper->print("\n");
    helper->flush();
  }

  /**
   * Handle the 'info breakpoints' command to list all current breakpoints. This
   * will display the breakpoint number, location (file and line)
   */
  void handle_list_breakpoints(VMG_ const char *args) {
    (void)args;
    std::string response = "";
    breakpoint_handler->list_breakpoints(vmg_ response);
    helper->print(response.c_str());
  }

  /**
   * Handle the 'where' command to display the current source location and
   * context
   */
  void handle_where(VMG_ const char *args) {
    (void)args;
    helper->print_source_location(vmg_ stack_handler->get_stack_level());
    helper->print("\n");

    std::string message;
    print_handler->print_source_context(vmg_ stack_handler->get_stack_level(),
                                        context_lines, message);
    helper->print(message.c_str());
    helper->print("\n");
  }

  /**
   * Handle the 'eval' command to evaluate an expression in the context of the
   * current stack frame and display the result. This allows the user to inspect
   * variables, call functions, or perform calculations based on the current
   * program state while debugging. The expression is evaluated using the VM
   * debugger's eval_expr function, which takes into account the current stack
   * frame and context to
   */
  void handle_eval(VMG_ const char *args) {
    if (args[0] == '\0') {
      helper->print("Usage: eval <expression>\n");
      return;
    }

    // Evaluate the expression using the VM debugger's eval_expr function. This
    // function will attempt to evaluate the expression in the context of the
    // current stack frame (stack_level) and return the result as a string in
    // the res buffer. It also indicates whether the expression is an lvalue or
    // if it can be opened (e.g., for complex types) through the is_lval and
    // is_openable flags. For now, we just display the result as a string
    char res[1024];
    int is_lval = 0;
    int is_openable = 0;
    int rc = debugger->eval_expr(vmg_ res, sizeof(res), args,
                                 stack_handler->get_stack_level(), &is_lval,
                                 &is_openable, 0, 0, FALSE);
    if (rc == 0) {
      helper->print(res);
      helper->print("\n");
    } else {
      helper->print("Evaluation failed\n");
    }
  }

  void handle_print(VMG_ const char *args) {
    int limit = 20;
    std::string response;
    // print_handler->print_file(vmg_ args, limit, response,
    //   getchar, os_printz, KEY_BACKSPACE);
    print_handler->print_file(vmg_ args, limit, response);

    helper->print(response.c_str());
  }

  void handle_files(VMG_ const char *args) {
    (void)args;
    std::string response = "";
    print_handler->show_files(response);
    helper->print(response.c_str());
  }

  void handle_rows(VMG_ const char *args) {
    if (args[0] == '\0') {
      char buf[128];
      snprintf(buf, sizeof(buf), "Source context lines: %d\n", context_lines);
      helper->print(buf);
      helper->print("Usage: rows <num> to set context lines (default: 5)\n");
      return;
    }

    int lines = atoi(args);
    if (lines < 0 || lines > 20) {
      helper->print("Context lines must be between 0 and 20\n");
      return;
    }

    context_lines = lines;
    char buf[128];
    snprintf(buf, sizeof(buf), "Source context lines set to %d\n",
             context_lines);
    helper->print(buf);
  }

  void handle_delete(VMG_ const char *args) {
    if (args[0] == '\0') {
      helper->print("Usage: delete <breakpoint-number>\n");
      helper->print("Use 'info breakpoints' to see breakpoint numbers\n");
      return;
    }

    int bpnum = atoi(args);
    if (bpnum <= 0) {
      helper->print("Invalid breakpoint number\n");
      return;
    }

    std::string response;
    breakpoint_handler->delete_breakpoint(vmg_ bpnum, response);
    helper->print(response.c_str());
  }

  void handle_quit(VMG_ const char *args) {
    (void)args;
    helper->print("Halting execution\n");
    interpreter->set_halt_vm(TRUE);
    g_dbg_ctx.in_debugger = 0;
  }

  void handle_watch(VMG_ const char *args) {
    std::string response;
    if (evaluate_handler->handle_watch(vmg_ args, response) != 0) {
      helper->print(response.c_str());
    }
  }

  void handle_unwatch(VMG_ const char *args) {
    std::string response;
    if (evaluate_handler->handle_unwatch(vmg_ args, response) != 0) {
      helper->print(response.c_str());
    }
  }

  void show_stack(VMG_ const char *args) {
    (void)args;

    std::string result;

    if (stack_handler->get_current_call_stack(vmg_ "", result) != 0) {
      helper->print("Failed to get call stack\n");
      return;
    }
    helper->print(result.c_str());
  }

  /**
   * Handle the 'up' command to move up the call stack (to the caller frame) and
   * display the new context. This allows the user to inspect the caller's
   * context, variables, and source location.
   */
  void handle_up(VMG_ const char *args) {
    (void)args; // void cast to silence unused parameter warning

    std::string up_result;
    if (stack_handler->handle_up(vmg_ args, up_result) != 0 &&
        !up_result.empty()) {
      helper->print(up_result.c_str());
    }

    show_stack(vmg_ "");
    handle_where(vmg_ "");
  }

  /**
   * Handle the 'down' command to move down the call stack (to the callee frame)
   * and display the new context. This allows the user to inspect the callee's
   * context, variables, and source location.
   */
  void handle_down(VMG_ const char *args) {
    (void)args; // void cast to silence unused parameter warning

    std::string down_result;

    if (stack_handler->handle_down(vmg_ args, down_result) != 0 &&
        !down_result.empty()) {
      helper->print(down_result.c_str());
    }

    show_stack(vmg_ "");
    handle_where(vmg_ "");
  }

  /*
   *   Execute an assignment expression at a given stack level.
   *   Builds an expression of the form "(lvalue)=rvalue" and evaluates it
   *   via the VM debugger. Returns the debugger's return code.
   */
  int vm_eval_asi_expr(dbgcxdef *ctx, int level, const char *lvalue,
                       const char *rvalue) {
    return helper->eval_assign(level, lvalue, rvalue);
  }
};

#endif /* DEBUGGER_COMMAND_MANAGER_H */
