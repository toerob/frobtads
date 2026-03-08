#include "textutils.h"
#include <gtest/gtest.h>
#include <string>

#include <string>
#include <vector>

#include <frobdebughelper.h>
#include <termdbg/stack_handler.h>

#include <stdio.h>
#include <stdlib.h>

class TestFacade : public SourcePort {
public:
  virtual int get_source_info(VMG_ const char **fname, unsigned long *linenum,
                              int level) {
    // Only show main.t at the top (level 0)
    if (level == 0) {
      *fname = "main.t";
      *linenum = 0;
      return 0;
    }
    // Show testfile.t for levels 1-3
    if (level >= 1 && level <= 3) {
      *fname = "testfile.t";
      *linenum = 42 + (unsigned long)level;
      return 0;
    }
    // Stop at level 4 (no more frames)
    return 1; // 1 - signals no more frames
  }
};

// This is more of an integration test since it relies on the get_source_info
// method of the Facade to simulate a call stack, but it allows us to verify the
// overall behavior of the StackHandler in a realistic way.
TEST(StackHandlerTest, print_watch_list_multiple_frames) {
  auto *facade = new TestFacade();

  auto stack_handler = new StackHandler(facade);

  std::string response;
  auto level = stack_handler->get_current_call_stack(vmg_ "", response);
  // printf("***Response:\n%s", response.c_str());
  EXPECT_EQ(response, "> #0  at main.t:0 <\n"
                      "  #1  at testfile.t:43\n"
                      "  #2  at testfile.t:44\n"
                      "  #3  at testfile.t:45\n");
  EXPECT_EQ(level, 0);
}

TEST(StackHandlerTest,
     print_watch_list_while_traversing_multiple_frames_with_up_and_down) {
  auto *facade = new TestFacade();

  auto stack_handler = new StackHandler(facade);

  std::string response;
  auto level = stack_handler->get_current_call_stack(vmg_ "", response);

  EXPECT_EQ(response, "> #0  at main.t:0 <\n"
                      "  #1  at testfile.t:43\n"
                      "  #2  at testfile.t:44\n"
                      "  #3  at testfile.t:45\n");
  EXPECT_EQ(level, 0);

  std::string up_result;
  EXPECT_EQ(stack_handler->handle_up(vmg_ "", up_result), 0);
  EXPECT_STREQ(up_result.c_str(), "Stack level: 1\n");

  response.clear();
  EXPECT_EQ(stack_handler->get_current_call_stack(vmg_ "", response), 0);
  EXPECT_EQ(response, "  #0  at main.t:0\n"
                      "> #1  at testfile.t:43 <\n"
                      "  #2  at testfile.t:44\n"
                      "  #3  at testfile.t:45\n");

  up_result.clear();
  EXPECT_EQ(stack_handler->handle_up(vmg_ "", up_result), 0);
  EXPECT_STREQ(up_result.c_str(), "Stack level: 2\n");

  response.clear();
  EXPECT_EQ(stack_handler->get_current_call_stack(vmg_ "", response), 0);
  EXPECT_EQ(response, "  #0  at main.t:0\n"
                      "  #1  at testfile.t:43\n"
                      "> #2  at testfile.t:44 <\n"
                      "  #3  at testfile.t:45\n");

  up_result.clear();
  EXPECT_EQ(stack_handler->handle_up(vmg_ "", up_result), 0);
  EXPECT_STREQ(up_result.c_str(), "Stack level: 3\n");

  response.clear();
  EXPECT_EQ(stack_handler->get_current_call_stack(vmg_ "", response), 0);
  EXPECT_EQ(response, "  #0  at main.t:0\n"
                      "  #1  at testfile.t:43\n"
                      "  #2  at testfile.t:44\n"
                      "> #3  at testfile.t:45 <\n");

  up_result.clear();
  EXPECT_EQ(stack_handler->handle_up(vmg_ "", up_result), -1);
  EXPECT_STREQ(up_result.c_str(), "Already at top of stack\n");

  response.clear();
  EXPECT_EQ(stack_handler->get_current_call_stack(vmg_ "", response), 0);
  EXPECT_EQ(response, "  #0  at main.t:0\n"
                      "  #1  at testfile.t:43\n"
                      "  #2  at testfile.t:44\n"
                      "> #3  at testfile.t:45 <\n");

  std::string down_result;
  EXPECT_EQ(stack_handler->handle_down(vmg_ "", down_result), 0);
  EXPECT_STREQ(down_result.c_str(), "Stack level: 2\n");

  response.clear();
  EXPECT_EQ(stack_handler->get_current_call_stack(vmg_ "", response), 0);
  EXPECT_EQ(response, "  #0  at main.t:0\n"
                      "  #1  at testfile.t:43\n"
                      "> #2  at testfile.t:44 <\n"
                      "  #3  at testfile.t:45\n");

  down_result.clear();
  EXPECT_EQ(stack_handler->handle_down(vmg_ "", down_result), 0);
  EXPECT_STREQ(down_result.c_str(), "Stack level: 1\n");

  down_result.clear();
  EXPECT_EQ(stack_handler->handle_down(vmg_ "", down_result), 0);
  EXPECT_STREQ(down_result.c_str(), "Stack level: 0\n");

  down_result.clear();
  EXPECT_EQ(stack_handler->handle_down(vmg_ "", down_result), -1);
  EXPECT_STREQ(down_result.c_str(), "Already at bottom of stack\n");
}
