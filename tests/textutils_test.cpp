#include "textutils.h"
#include <gtest/gtest.h>
#include <string>

#include <cstdlib>
#include <cstring>
#include <ctype.h>
#include <string.h>
#include <vector>

// -------------------------------------
// trim tests
// -------------------------------------
TEST(TrimTest, TrimLeadingAndTrailing) {
  char buf1[] = "   hello world   ";
  char *trimmed = trim(buf1);
  EXPECT_STREQ(trimmed, "hello world");
}

TEST(TrimTest, TrimOnlyLeading) {
  char buf2[] = "   leading";
  char *trimmed = trim(buf2);
  EXPECT_STREQ(trimmed, "leading");
}

TEST(TrimTest, TrimOnlyTrailing) {
  char buf3[] = "trailing   ";
  char *trimmed = trim(buf3);
  EXPECT_STREQ(trimmed, "trailing");
}

TEST(TrimTest, TrimNoWhitespace) {
  char buf4[] = "nowhitespace";
  char *trimmed = trim(buf4);
  EXPECT_STREQ(trimmed, "nowhitespace");
}

// -------------------------------------
// trim_lead tests
// -------------------------------------
TEST(TrimLeadTest, TrimLeadingWhitespace) {
  char buf5[] = "   leadonly";
  char *trimmed = trim_lead(buf5);
  EXPECT_STREQ(trimmed, "leadonly");
}

TEST(TrimLeadTest, TrimLeadingWhitespaceSameEnding) {
  char buf5[] = "   leadonly   ";
  char *trimmed = trim_lead(buf5);
  EXPECT_STREQ(trimmed, "leadonly   ");
}

TEST(TrimLeadTest, TrimLeadNoWhitespace) {
  char buf6[] = "none";
  char *trimmed = trim_lead(buf6);
  EXPECT_STREQ(trimmed, "none");
}

// -------------------------------------
// parse_file_line_args tests
// -------------------------------------

TEST(ParseFileLineArgsTest, Colon) {
  std::string filepart;
  int line = 0;
  EXPECT_TRUE(parse_file_line_args("foo.t:42", filepart, line));
  EXPECT_EQ(filepart, "foo.t");
  EXPECT_EQ(line, 42);
}

TEST(ParseFileLineArgsTest, ColonAndSpaces) {
  std::string filepart;
  int line = 0;
  EXPECT_TRUE(parse_file_line_args("foo.t:   42", filepart, line));
  EXPECT_EQ(filepart, "foo.t");
  EXPECT_EQ(line, 42);
}

TEST(ParseFileLineArgsTest, Space) {
  std::string filepart;
  int line = 0;
  EXPECT_TRUE(parse_file_line_args("bar.t 99", filepart, line));
  EXPECT_EQ(filepart, "bar.t");
  EXPECT_EQ(line, 99);
}

TEST(ParseFileLineArgsTest, Spaces) {
  std::string filepart;
  int line = 0;
  EXPECT_TRUE(parse_file_line_args("bar.t        99", filepart, line));
  EXPECT_EQ(filepart, "bar.t");
  EXPECT_EQ(line, 99);
}

TEST(ParseFileLineArgsTest, LineOnly) {
  std::string filepart;
  int line = 0;
  EXPECT_TRUE(parse_file_line_args("123", filepart, line));
  EXPECT_EQ(filepart, "");
  EXPECT_EQ(line, 123);
}

TEST(ParseFileLineArgsTest, Invalid) {
  std::string filepart;
  int line = 0;
  EXPECT_FALSE(parse_file_line_args("foo.t:abc", filepart, line));
  EXPECT_FALSE(parse_file_line_args("foo.t -1", filepart, line));
  EXPECT_FALSE(parse_file_line_args("", filepart, line));
}

// -------------------------------------
// parse_command_line tests
// -------------------------------------
TEST(ParseCommandLineTest, CommandAndArgs) {
  char cmd[64];
  char args[256];
  EXPECT_EQ(0, parse_command_line("break foo.t:42", cmd, args));
  EXPECT_STREQ(cmd, "break");
  EXPECT_STREQ(args, "foo.t:42");
}

TEST(ParseCommandLineTest, CommandOnly) {
  char cmd[64];
  char args[256];
  EXPECT_EQ(0, parse_command_line("continue", cmd, args));
  EXPECT_STREQ(cmd, "continue");
  EXPECT_STREQ(args, "");
}

TEST(ParseCommandLineTest, LeadingWhitespace) {
  char cmd[64];
  char args[256];
  EXPECT_EQ(0, parse_command_line("   step", cmd, args));
  EXPECT_STREQ(cmd, "step");
  EXPECT_STREQ(args, "");
}

TEST(ParseCommandLineTest, CommandAndArgsWithExtraSpaces) {
  char cmd[64];
  char args[256];
  EXPECT_EQ(0, parse_command_line("   print   var1   ", cmd, args));
  EXPECT_STREQ(cmd, "print");
  EXPECT_STREQ(args, "var1   ");
}

TEST(ParseCommandLineTest, EmptyInput) {
  char cmd[64];
  char args[256];
  EXPECT_EQ(1, parse_command_line("", cmd, args));
  EXPECT_STREQ(cmd, "");
  EXPECT_STREQ(args, "");
}

TEST(ParseCommandLineTest, OnlyWhitespace) {
  char cmd[64];
  char args[256];
  EXPECT_EQ(1, parse_command_line("    ", cmd, args));
  EXPECT_STREQ(cmd, "");
  EXPECT_STREQ(args, "");
}

// -------------------------------------
// find_source_file_matches tests
// -------------------------------------

TEST(FindSourceFileMatches2Test, ExactMatch) {
  std::vector<std::string> filenames = {"foo.t", "bar.t", "baz.t"};
  std::vector<int> matches = find_source_file_matches(filenames, "bar.t");
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches[0], 1);
}

TEST(FindSourceFileMatches2Test, SuffixMatch) {
  std::vector<std::string> filenames = {"/path/to/foo.t", "/other/bar.t"};
  std::vector<int> matches = find_source_file_matches(filenames, "bar.t");
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches[0], 1);
}

TEST(FindSourceFileMatches2Test, NoMatch) {
  std::vector<std::string> filenames = {"foo.t", "bar.t"};
  std::vector<int> matches = find_source_file_matches(filenames, "baz.t");
  EXPECT_EQ(matches.size(), 0);
}

TEST(FindSourceFileMatches2Test, SubstringMatch) {
  std::vector<std::string> filenames = {"src/foo.t", "src/bar.t"};
  std::vector<int> matches = find_source_file_matches(filenames, "ar");
  EXPECT_EQ(matches.size(), 1);
  EXPECT_EQ(matches[0], 1);
}

// -------------------------------------
// parse_int_arg tests
// -------------------------------------
TEST(ParseIntArgTest, ValidInteger) {
  int value = 0;
  EXPECT_TRUE(parse_int_arg("42", value));
  EXPECT_EQ(value, 42);
}

TEST(ParseIntArgTest, InvalidInteger) {
  int value = 0;
  EXPECT_FALSE(parse_int_arg("abc", value));
  EXPECT_EQ(value, 0);
}

TEST(ParseIntArgTest, LeadingWhitespace) {
  int value = 0;
  EXPECT_TRUE(parse_int_arg("   123", value));
  EXPECT_EQ(value, 123);
}