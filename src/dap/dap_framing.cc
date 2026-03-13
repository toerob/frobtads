#include "dap/dap_framing.h"

#include "common.h"
#include <cerrno>
#include <cstdlib>

namespace dap {

std::string make_content_length_header(std::size_t content_length) {
  return "Content-Length: " + std::to_string(content_length) + "\r\n\r\n";
}

std::string frame_json_message(const json &message) {
  const std::string payload = message.dump();
  return make_content_length_header(payload.size()) + payload;
}

static bool try_parse_size(const std::string &text, std::size_t &value) {
  errno = 0;
  char *end = nullptr;
  const unsigned long long parsed = std::strtoull(text.c_str(), &end, 10);
  if (errno != 0 || end == text.c_str() || end == nullptr) {
    return false;
  }
  // Reject trailing garbage.
  while (*end != '\0') {
    if (*end != ' ' && *end != '\t') {
      return false;
    }
    ++end;
  }
  value = static_cast<std::size_t>(parsed);
  return true;
}

bool try_parse_content_length_line(const std::string &line,
                                  std::size_t &content_length) {
  constexpr const char *kPrefix = "Content-Length:";
  if (line.rfind(kPrefix, 0) != 0) {
    return false;
  }

  std::string rest = line.substr(std::char_traits<char>::length(kPrefix));
  // Optional whitespace after ':'
  while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t')) {
    rest.erase(rest.begin());
  }

  std::size_t parsed = 0;
  if (!try_parse_size(rest, parsed)) {
    return false;
  }

  content_length = parsed;
  return true;
}

bool parse_framed_json_message(const std::string &framed, json &out) {
  // Must start with the Content-Length header.
  std::size_t header_end = framed.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }

  // Only handle a single Content-Length header line here.
  // If there are multiple headers, the production reader handles them; for unit
  // tests we keep this strict.
  const std::string header_line = framed.substr(0, framed.find("\r\n"));

  std::size_t content_length = 0;
  if (!try_parse_content_length_line(header_line, content_length)) {
    return false;
  }

  const std::size_t body_start = header_end + 4;
  if (framed.size() < body_start + content_length) {
    return false;
  }

  const std::string body = framed.substr(body_start, content_length);

  try {
    out = json::parse(body);
    return true;
  } catch (...) {
    return false;
  }
}

} // namespace dap
