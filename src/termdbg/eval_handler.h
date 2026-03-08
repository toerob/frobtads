#ifndef WATCHEXPR_H
#define WATCHEXPR_H

#include "frobdebughelper.h"
#include "textutils.h"
#include "vmdbg.h"
#include <string>
#include <vector>

#include "termdbg/eval_port.h"

class EvaluateHandler {
  EvalPort *eval_port;
  std::vector<std::string> watch_expressions;

public:
  EvaluateHandler(EvalPort *eval_port) : eval_port(eval_port) {}

  std::string evaluate_watch_list(VMG_) {
    std::string buffer;
    if (watch_expressions.size() > 0) {
      buffer += "\nWatch:\n";
      for (size_t i = 0; i < watch_expressions.size(); ++i) {
        const std::string &expr = watch_expressions[i];
        char res[1024];
        int is_lval = 0, is_openable = 0;

        int rc = eval_port->eval_expr(vmg_ res, sizeof(res), expr.c_str(), 0,
                    &is_lval, &is_openable, 0, 0, FALSE);
        if (rc == 0) {
          buffer +=
              "  " + std::to_string(i + 1) + ": " + expr + " = " + res + "\n";
        } else {
          buffer += "  " + std::to_string(i + 1) + ": " + expr + " = <error>\n";
        }
      }
    }
    return buffer;
  }
  

  int handle_watch(VMG_ const char *args, std::string &response) {
    if (args[0] == '\0') {
      response = "Usage: watch <expr> to add a watch expression\n";
      return -1;
    }

    char copy[512];
    strncpy(copy, args, sizeof(copy) - 1);
    copy[sizeof(copy) - 1] = '\0';

    char *trimmed = trim(copy);
    if (trimmed[0] == '\0') {
      response = "Usage: watch <expr> to add a watch expression\n";
      return -1;
    }
    watch_expressions.push_back(std::string(trimmed));
    return 0;
  }

  int handle_unwatch(VMG_ const char *args, std::string &response) {
    if (args[0] == '\0') {

      response = "Usage: unwatch <num> to select watch expression to unwatch\n";
      return -1;
    }
    int watch_nr = 0;
    if (parse_int_arg(args, watch_nr)) {
      if (watch_nr >= 1 && (size_t)watch_nr <= watch_expressions.size()) {
        watch_expressions.erase(watch_expressions.begin() + (watch_nr - 1));
        response = "Watch entry removed\n";
        return 0;
      }
    }
    response = "Invalid watch number\n";
    return -1;
  }

  std::string evaluate_locals(VMG_ int stack_level) {
    // enum_ctx is a context struct used when enumerating local variables.
    // It can hold any state needed during the enumeration process.
    // In this case, it just has a level field to track the current nesting
    // level.
    struct enum_ctx {
      int level;
    } ectx = {0};

    // We'll capture the local variables output into a string buffer
    // that we can return at the end.

    std::string buffer;

    // locals_cb is a callback function that will be called by the VM debugger
    // for each local variable in the current frame.
    // It receives a context pointer (cbctx), the variable name and its
    // length.
    auto locals_cb = [this, &buffer, stack_level](void *cbctx, const char *name,
                                                  size_t namelen) {
      // Cast the context pointer to our enum_ctx struct to access any
      // state we need during enumeration.
      VMGLOB_PTR(((dbgcxdef *)0)->vmg);

      // Make a local copy of the variable name, ensuring it's null-terminated
      // and does not exceed our buffer size. This is important because the
      // name provided by the debugger may not be null-terminated and we need
      // to ensure we don't read past the provided length.
      char nm[256];
      size_t copylen = namelen < sizeof(nm) - 1 ? namelen : sizeof(nm) - 1;
      memcpy(nm, name, copylen);
      nm[copylen] = '\0';

      // Evaluate the variable's value using the VM debugger's eval_expr
      // function. This function will attempt to evaluate the expression for
      // the variable name in the context of the current stack frame. It will
      // return the value as a string in the val buffer, and also indicate
      // whether it's an lvalue or if it can be opened (e.g., for complex
      // types).

      // If it is an lvalue, we could potentially allow the user to modify it.
      // If it's openable, we could allow the user to inspect its fields.
      // For now, we just display the value as a string.

      char val[1024];
      int is_lval = 0;
      int is_openable = 0;

      int rc = eval_port->eval_expr(vmg_ val, sizeof(val), nm, stack_level,
                &is_lval, &is_openable, 0, 0, FALSE);
      if (rc == 0) {
        buffer += std::string(nm) + " = " + val + "\n";
      } else {
        buffer += std::string(nm) + " = <error evaluating>\n";
      }
    };

    // A helper struct used to pass C++ lambdas or function pointers
    // as context to C-style callback APIs that expect a void* context
    // argument. Allows a bridge between C++ closures and C callbacks.
    struct thunk {
      decltype(locals_cb) *fn;
    } tctx;

    // Set the function pointer in the thunk to our locals_cb that
    // contains the logic for handling each local variable.
    tctx.fn = &locals_cb;

    // c_thunk is a C-style callback function that matches the signature
    // expected by the VM debugger's enum_locals function.
    auto c_thunk = [](void *cctx, const char *name, size_t namelen) {
      thunk *tc = (thunk *)cctx;
      (*(tc->fn))(cctx, name, namelen);
    };

    // Call the VM debugger's enum_locals function, passing our c_thunk
    // callback and the context.
    // debugger
    eval_port->enum_locals(
      vmg_(void (*)(void *, const char *, size_t)) c_thunk, &tctx,
      stack_level);

    return buffer;
  }

};

#endif /* WATCHEXPR_H */
