#include "textutils.h"
#include <gtest/gtest.h>
#include <string>

#include <string>
#include <vector>

#include <frobdebughelper.h>
#include <termdbg/eval_handler.h>

#include <stdio.h>
#include <stdlib.h>

// Callback signature for enum_locals - convenience for readability in the
// test facade
using EVAL_LOCALS_CALLBACK = void (*)(void *, const char *, size_t);

class TestFacade : public EvalPort {
public:
  virtual int eval_expr(VMG_ char *res, size_t res_size, const char *expr,
                        int flags, int *is_lval, int *is_openable, int, int,
                        int) {
    // For testing, we can simply return a fixed result
    //  or an error based on the expression
    if (std::string(expr) == "x + y") {
      strncpy(res, "<error>", res_size);
      return 1;
    } else {
      strncpy(res, "42", res_size);
      return 0;
    }
  }

  virtual void enum_locals(VMG_ EVAL_LOCALS_CALLBACK cbfunc, void *cbctx,
                           int level) {
    // For testing, we can just call the callback with some dummy local
    // variables
    if (level == 0) {
      cbfunc(cbctx, "local_var1", strlen("local_var1"));
      cbfunc(cbctx, "local_var2", strlen("local_var2"));
    } else if (level == 1) {
      cbfunc(cbctx, "local_var3", strlen("local_var3"));
    }
  }
};

TEST(EvaluateHandlerTest, print_watch_list) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  std::string response;
  eval_handler->handle_watch(vmg_ "x + y", response);
  auto output = eval_handler->evaluate_watch_list(vmg_);

  EXPECT_EQ(output, "\nWatch:\n  1: x + y = <error>\n");
}

TEST(EvaluateHandlerTest, unwatch_invalid_index) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  std::string response;
  eval_handler->handle_watch(vmg_ "x + y", response);
  int rc = eval_handler->handle_unwatch(vmg_ "2", response);

  EXPECT_EQ(rc, -1);
  EXPECT_STREQ(response.c_str(), "Invalid watch number\n");
}

TEST(EvaluateHandlerTest, unwatch_valid_index) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  std::string response;
  eval_handler->handle_watch(vmg_ "x + y", response);
  eval_handler->handle_watch(vmg_ "a * b", response);

  int rc = eval_handler->handle_unwatch(vmg_ "1", response);

  EXPECT_EQ(rc, 0);
  EXPECT_STREQ(response.c_str(), "Watch entry removed\n");
}

TEST(EvaluateHandlerTest, unwatch_no_index) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  std::string response;
  eval_handler->handle_watch(vmg_ "x + y", response);
  int rc = eval_handler->handle_unwatch(vmg_ "", response);

  EXPECT_EQ(rc, -1);
  EXPECT_STREQ(response.c_str(),
               "Usage: unwatch <num> to select watch expression to unwatch\n");
}

TEST(EvaluateHandlerTest, multiple_watch_expressions) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  std::string response;
  eval_handler->handle_watch(vmg_ "x + y", response);
  eval_handler->handle_watch(vmg_ "a * b", response);
  auto output = eval_handler->evaluate_watch_list(vmg_);
  EXPECT_EQ(output, "\nWatch:\n  1: x + y = <error>\n  2: a * b = 42\n");

  // remove first expression and check output again
  eval_handler->handle_unwatch(vmg_ "1", response);

  output = eval_handler->evaluate_watch_list(vmg_);
  EXPECT_EQ(output, "\nWatch:\n  1: a * b = 42\n");
}

// enum locals

TEST(EvaluateHandlerTest, enum_locals_level_0) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  auto output = eval_handler->evaluate_locals(vmg_ 0);

  EXPECT_EQ(output, "local_var1 = 42\nlocal_var2 = 42\n");
}

TEST(EvaluateHandlerTest, enum_locals_level_1) {
  auto *facade = new TestFacade();
  auto *eval_handler = new EvaluateHandler(facade);

  auto output = eval_handler->evaluate_locals(vmg_ 1);

  EXPECT_EQ(output, "local_var3 = 42\n");
}
