#include "textutils.h"
#include <cstdlib>
#include <ctype.h>
#include <fstream>
#include <set>
#include <string.h>
#include <string>
#include <vector>

using std::ifstream;
using std::set;
using std::string;
using std::vector;

/**
 * Trim leading and trailing whitespace from a string.
 * Modifies the string in place and
 * returns a pointer to the trimmed string.
 */
char *trim(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  char *end = s + strlen(s) - 1;
  while (end > s && isspace((unsigned char)*end)) {
    *end = '\0';
    end--;
  }
  return s;
}

/**
 * Trim leading whitespace from a string and return the pointer to the first
 */
char *trim_lead(char *s) {
  while (*s && isspace((unsigned char)*s))
    s++;
  return s;
}

/**
 * Helper function to parse an integer argument,
 * returns true if successful
 */
bool parse_int_arg(const char *args, int &value) {
  // Check if the argument is empty
  if (!args || *args == '\0') {
    return false;
  }

  // Pointer to track where parsing stopped
  char *end = nullptr;

  // Use strtol to parse the integer, and check for errors
  long parsed = strtol(args, &end, 10);

  // Check if parsing succeeded and consumed the entire string
  if (!end || *end != '\0') {
    return false;
  }

  value = (int)parsed;

  return true;
}

/*
 * Parse a command line into command and arguments
 */
int parse_command_line(const char *input, char *cmd, char *args) {
  const char *p = input;
  int cmd_idx = 0;
  int args_idx = 0;

  // Skip leading whitespace
  while (*p && isspace(*p))
    p++;

  /* Extract command */
  while (*p && !isspace(*p) && cmd_idx < 63) {
    cmd[cmd_idx++] = tolower(*p);
    p++;
  }
  cmd[cmd_idx] = '\0';

  /* Skip whitespace between command and arguments */
  while (*p && isspace(*p))
    p++;

  /* Extract arguments */
  while (*p && args_idx < 255) {
    args[args_idx++] = *p;
    p++;
  }

  args[args_idx] = '\0'; // Ensure null-termination

  // Return 0 if we got a command, 1 if the input was empty or just whitespace
  return (cmd_idx > 0) ? 0 : 1;
}

/**
 * Handle the 'break' command to set a breakpoint. Supported forms:
 *   - break <line>                 -> line in current source file
 *   - break <file>:<line>          -> try to resolve file and set breakpoint
 * there
 *   - break <index> <line>         -> disambiguate from a previous
 * multi-match list
 */
bool parse_file_line_args(const char *args, std::string &filepart, int &line) {
  if (!args || args[0] == '\0') {
    return false;
  }

  char args_copy[256];
  strncpy(args_copy, args, sizeof(args_copy) - 1);
  args_copy[sizeof(args_copy) - 1] = '\0';
  char *a = trim_lead(args_copy);

  char *col = strchr(a, ':');
  if (col) {
    *col = '\0';
    char *file = a;
    char *linepart = col + 1;
    while (*linepart && isspace((unsigned char)*linepart)) {
      linepart++;
    }
    int l = atoi(linepart);
    if (l <= 0) {
      return false;
    }
    filepart = file;
    line = l;
    return true;
  } else {
    // Check for space-separated file and line (e.g., 'file line')
    char *tok1 = strtok(a, " \t");
    char *tok2 = strtok(NULL, " \t");
    if (tok1 && tok2) {
      // If tok2 is a valid line number
      int l = atoi(tok2);
      if (l > 0) {
        filepart = tok1;
        line = l;
        return true;
      }
    }
    // Otherwise, treat as just a line number
    int l = atoi(tok1);
    if (l <= 0)
      return false;
    filepart.clear();
    line = l;
    return true;
  }
}

/**
 * Helper function to find source file matches for a given file name (or
 * partial file name). This is used when setting breakpoints by file and line,
 * to find the correct source file in the debug info. We will look for exact
 * matches, suffix matches, base name matches, and substring matches, and
 * return a list of matching source file IDs.
 */
std::vector<int>
find_source_file_matches(const std::vector<std::string> &filenames,
                         const char *filepart) {
  const size_t max_matches = 32;
  size_t tblc = filenames.size();
  size_t qlen = strlen(filepart);
  std::vector<int> matches;
  std::set<std::string> seen;

  auto try_add = [&](size_t idx) {
    const std::string &fname = filenames[idx];
    if (seen.count(fname) == 0 && matches.size() < max_matches) {
      matches.push_back((int)idx);
      seen.insert(fname);
    }
  };

  // First look for exact matches
  for (size_t i = 0; i < tblc && matches.size() < max_matches; i++) {
    const char *nm = filenames[i].c_str();
    if (strcmp(nm, filepart) == 0) {
      try_add(i);
    }
  }

  // Then look for suffix matches
  for (size_t i = 0; i < tblc && matches.size() < max_matches; i++) {
    const char *nm = filenames[i].c_str();
    size_t nlen = strlen(nm);
    if (nlen >= qlen && strcmp(nm + (nlen - qlen), filepart) == 0) {
      try_add(i);
    }
  }

  // Then look for base name matches
  for (size_t i = 0; i < tblc && matches.size() < max_matches; i++) {
    const char *nm = filenames[i].c_str();
    const char *bs = strrchr(nm, '/');
    if (!bs) {
      bs = nm;
    } else {
      bs++;
    }
    if (strcmp(bs, filepart) == 0) {
      try_add(i);
    }
  }

  // Finally look for substring matches
  for (size_t i = 0; i < tblc && matches.size() < max_matches; i++) {
    const char *nm = filenames[i].c_str();
    if (strstr(nm, filepart) != NULL) {
      try_add(i);
    }
  }

  return matches;
}

vector<string> read_file_lines(const string &filename) {
  vector<string> lines;
  std::ifstream file(filename);
  std::string line;
  while (std::getline(file, line)) {
    lines.push_back(line);
  }
  return lines;
}
