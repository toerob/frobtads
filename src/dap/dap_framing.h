#ifndef DAP_FRAMING_H
#define DAP_FRAMING_H

#include <cstddef>
#include <string>

#include "json.hpp"

namespace dap {

using json = nlohmann::json;

// Formats a DAP/JSON-RPC style header for a given content length.
// Example: "Content-Length: 123\r\n\r\n"
std::string make_content_length_header(std::size_t content_length);

// Frames a JSON message using the DAP transport framing.
// Result: header + JSON payload.
std::string frame_json_message(const json &message);

// Attempts to parse a single framed message string into JSON.
// Returns false on framing or JSON parse errors.
bool parse_framed_json_message(const std::string &framed, json &out);

// Attempts to parse a Content-Length header line.
// Accepts: "Content-Length: <n>" (with optional spaces after ':').
// Returns true only if the line is a valid Content-Length header.
bool try_parse_content_length_line(const std::string &line,
                                  std::size_t &content_length);

} // namespace dap

#endif /* DAP_FRAMING_H */
