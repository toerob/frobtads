#ifndef TEXTUTILS_H
#define TEXTUTILS_H

#include <functional>
#include <string>
#include <vector>

/**
 * Trim leading and trailing whitespace from a string.
 * Modifies the string in place and
 * returns a pointer to the trimmed string.
 */
char *trim(char *s);

/**
 * Trim leading whitespace from a string and return the pointer to the first
 */
char *trim_lead(char *s);

/**
 * Helper function to parse an integer argument,
 * returns true if successful
 */
bool parse_int_arg(const char *args, int &value);

/**
 * Decoupled version: Accepts a vector of file names and returns matching
 * indices. This makes the function independent of G_srcf_table and
 * CVmSrcfEntry, so it can be tested with any string list.
 */
std::vector<int>
find_source_file_matches(const std::vector<std::string> &filenames,
                         const char *filepart);

/*
 * Parse a command line into command and arguments
 */
int parse_command_line(const char *input, char *cmd, char *args);

/**
 * Handle the 'break' command to set a breakpoint. Supported forms:
 *   - break <line>                 -> line in current source file
 *   - break <file>:<line>          -> try to resolve file and set breakpoint
 * there
 *   - break <index> <line>         -> disambiguate from a previous
 * multi-match list
 */
bool parse_file_line_args(const char *args, std::string &filepart, int &line);

std::vector<std::string> read_file_lines(const std::string &filename);


#endif /* TEXTUTILS_H */