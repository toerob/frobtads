/*
 * dapdebugui.cc - Debug Adapter Protocol debugger
 */
#include "common.h"
#include "dap/dapdebugui.h"
#include "dap/dap_framing.h"
#include "vmdbg.h"
#include "vmfunc.h"
#include "vmpool.h"
#include "vmrun.h"
#include "vmsrcf.h"
// For enumerating the global symbol table (GSYM) when building the "Globals"
// scope.
#include "tct3base.h"
#include <arpa/inet.h>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <netinet/in.h>
#include <poll.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

/* Signal flag for interrupt handling */
volatile sig_atomic_t g_dap_interrupted = 0;

/* Set to 1 while blocked in a DAP message-reading loop (init or cmd_loop).
 * When 0 the VM is running game code and signals should terminate normally. */
volatile sig_atomic_t g_dap_in_message_loop = 0;

/* Global pointer to current DAP debug instance for signal handler */
CDapDebugUI *g_dap_instance = nullptr;

/* Forward declare to avoid static/non-static mismatch */
void dap_signal_handler(int signum);

/* Signal handler for SIGINT and SIGTERM */
void dap_signal_handler(int signum) {
  /* Always set the flag so any DAP loop can observe it. */
  g_dap_interrupted = 1;

  if (g_dap_in_message_loop) {
    /* We're blocked in poll()/read() inside a DAP message loop.
     * The flag is enough — the interrupted syscall (EINTR) plus the flag
     * check will break the loop cleanly. */
    return;
  }

  /* Not in a message loop (VM is running, or no DAP instance).
   * Restore the default handler and re-raise so the process terminates. */
  std::signal(signum, SIG_DFL);
  raise(signum);
}

/*
 * Constructor - default (stdio)
 */
CDapDebugUI::CDapDebugUI()
    : ctx_(nullptr), io_mode_(IOMode::StdIO), io_fd_(-1), listen_fd_(-1) {
  setup_stdio_io();
}

/*
 * Constructor - Unix domain socket
 */
CDapDebugUI::CDapDebugUI(const std::string &socket_path)
    : ctx_(nullptr), io_mode_(IOMode::Socket), io_fd_(-1), listen_fd_(-1),
      socket_path_(socket_path) {
  setup_socket_io(socket_path);
}

/*
 * Constructor - TCP socket
 */
CDapDebugUI::CDapDebugUI(int port)
    : ctx_(nullptr), io_mode_(IOMode::TCP), io_fd_(-1), listen_fd_(-1) {
  setup_tcp_io(port);
}

/*
 * Destructor
 */
CDapDebugUI::~CDapDebugUI() {
  close_io();

  /* Clear global instance pointer */
  if (g_dap_instance == this) {
    g_dap_instance = nullptr;
  }
}

/* ============================================================
 * DebuggerUI Interface Implementation
 * ============================================================ */

/*
 * Initialize the DAP debugger
 */
void CDapDebugUI::init(VMG_ const char *image_filename) {
  ctx_ = new dbgcxdef();   // Create debugger context
  ctx_->vmg = VMGLOB_ADDR; // Ensure we have the latest globals pointer
  ctx_->in_debugger = 0; // Mark we're not in the debugger until cmd_loop starts
  ctx_->stepping_mode = STEP_NONE; // Default stepping mode

  helper_ = std::make_unique<CFrobDebugHelper>();
  helper_->init(ctx_);

  state_.image_file = image_filename ? image_filename : "";

  std::cerr << "[DAP Debug] Initialized with image: " << state_.image_file
            << std::endl;
  std::cerr << "[DAP Debug] state_.launched = " << state_.launched << std::endl;
  std::cerr << "[DAP Debug] state_.should_exit = " << state_.should_exit
            << std::endl;
  std::cerr << "[DAP Debug] About to enter message waiting loop..."
            << std::endl;

  // Wait for launch request before proceeding - this allows IDE to set
  // breakpoints before execution starts The client will have to save up
  // breakpoints in a queue and send them after the launch request, so we just
  // need to wait for that initial launch request before we start executing
  g_dap_in_message_loop = 1;
  while (!state_.launched && !state_.should_exit && !g_dap_interrupted) {
    std::cerr << "[DAP Debug] Loop iteration - trying to read message..."
              << std::endl;
    json request;
    if (read_message(request)) {
      std::cerr << "[DAP Debug] Message read successfully" << std::endl;
      process_request(request);
    } else {
      std::cerr << "[DAP Debug] read_message() returned false" << std::endl;
      // Connection lost or interrupted - stop waiting.
      // read_message() already sets should_exit on EOF/disconnect;
      // also break on interrupt to avoid a tight retry loop.
      if (g_dap_interrupted || state_.should_exit) {
        std::cerr << "[DAP Debug] Startup aborted" << std::endl;
        state_.should_exit = true;
        break;
      }
    }
  }

  g_dap_in_message_loop = 0;

  std::cerr << "[DAP Debug] Exited while loop - state_.launched="
            << state_.launched << std::endl;

  if (state_.launched) {
    std::cerr << "[DAP Debug] Launch complete, starting VM execution"
              << std::endl;
  }
}

/*
 * Post-load initialization
 */
void CDapDebugUI::init_after_load(VMG0_) {
  // Debug info is now available
  std::cerr << "[DAP Debug] Debug info loaded" << std::endl;
}

/*
 * Terminate the DAP debugger
 */
void CDapDebugUI::terminate(VMG0_) {
  std::cerr << "[DAP Debug] Terminating" << std::endl;
  if (helper_) {
    helper_->terminate();
    helper_.reset();
  }
  if (ctx_) {
    delete ctx_;
    ctx_ = nullptr;
  }
}

/*
 * Main command loop - process DAP messages
 */
void CDapDebugUI::cmd_loop(VMG_ int bp_number, int error_code,
                           const uchar **pc) {

  ctx_->vmg = VMGLOB_ADDR; // Ensure we have the latest globals pointer
  ctx_->in_debugger = 1;   // Mark that we're now in the debugger

  // On first entry to cmd_loop after VM loads, send invalidated event
  // This tells VS Code to resend breakpoints now that debug info is available
  if (!state_.sent_breakpoint_invalidation && G_srcf_table != nullptr) {
    state_.sent_breakpoint_invalidation = true; // Ensure we only send this once

    json invalidated_body;
    invalidated_body["areas"] = json::array({"breakpoints"});
    send_event("invalidated", invalidated_body);

    std::cerr << "[DAP Debug] Sent invalidated event for breakpoints"
              << std::endl;
  }

  // Send stopped event to IDE based on why we entered the debugger
  if (state_.pause_requested) {
    // Paused by user request
    state_.pause_requested = false;
    json body;
    body["reason"] = "pause";
    body["threadId"] = 1;
    body["allThreadsStopped"] = true;
    send_event("stopped", body);

  } else if (bp_number != 0) {
    // We entered due to a breakpoint
    on_breakpoint_hit(vmg_ bp_number, nullptr, 0);
  } else if (error_code != 0) {
    // We entered due to an error
    on_error(vmg_ error_code, "Runtime error");
  } else if (ctx_->stepping_mode != STEP_NONE) {
    // We entered due to a step completing
    on_step_complete(vmg_ nullptr, 0);
  } else {
    // Fallback for other reasons (like initial stopOnEntry)
    json body;
    body["reason"] = "pause";
    body["threadId"] = 1;
    body["allThreadsStopped"] = true;
    send_event("stopped", body);
  }

  // Process messages until execution should resume
  g_dap_in_message_loop = 1;
  while (ctx_->in_debugger && !state_.should_exit && !g_dap_interrupted) {
    json request;
    if (read_message(request)) {
      process_request(request);
    } else {
      // Failed to read message - exit loop
      break;
    }
  }
  g_dap_in_message_loop = 0;

  // If interrupted, mark for exit
  if (g_dap_interrupted) {
    state_.should_exit = true; // Ensure we exit cmd_loop
    ctx_->in_debugger = 0;     // Ensure we exit cmd_loop
  }
}

/* ============================================================
 * Optional Event Notifications
 * ============================================================ */

void CDapDebugUI::on_breakpoint_hit(VMG_ int bp_num, const char *file,
                                    unsigned long line) {
  json body;
  body["reason"] = "breakpoint";
  body["threadId"] = 1;
  body["allThreadsStopped"] = true;
  send_event("stopped", body);
}

void CDapDebugUI::on_step_complete(VMG_ const char *file, unsigned long line) {
  json body;
  body["reason"] = "step";
  body["threadId"] = 1;
  body["allThreadsStopped"] = true;
  send_event("stopped", body);
}

void CDapDebugUI::on_error(VMG_ int error_code, const char *message) {
  json body;
  body["reason"] = "exception";
  body["threadId"] = 1;
  body["text"] = message ? message : "Unknown error";
  body["allThreadsStopped"] = true;
  send_event("stopped", body);
}

void CDapDebugUI::on_execution_resumed(VMG0_) {
  json body;
  body["threadId"] = 1;
  body["allThreadsContinued"] = true;
  send_event("continued", body);
}

void CDapDebugUI::on_execution_paused(VMG0_) {
  // Paused event is sent via cmd_loop
}

/* ============================================================
 * DAP Protocol Implementation
 * ============================================================ */

/*
 * Read a JSON-RPC message from stdin
 * Messages are in the format:
 *   Content-Length: <length>\r\n
 *   \r\n
 *   {json content}
 */
bool CDapDebugUI::read_message(json &msg) {
  std::cerr << "[DAP Debug] read_message() called" << std::endl;

  // Check for interrupt signal
  if (g_dap_interrupted || state_.should_exit) {
    std::cerr << "[DAP Debug] Aborting due to interrupt/exit" << std::endl;
    return false;
  }

  // Read headers
  std::string line;
  std::size_t content_length = 0;

  std::cerr << "[DAP Debug] Starting header read loop..." << std::endl;

  // Wait for data with periodic timeout so we can check for interrupts
  // and detect dead connections.
  struct pollfd pfd;
  pfd.fd = io_fd_;
  pfd.events = POLLIN;

  std::cerr << "[DAP Debug] Waiting for data with poll()..." << std::endl;
  while (true) {
    if (g_dap_interrupted || state_.should_exit) {
      std::cerr << "[DAP Debug] Aborting poll loop (interrupt/exit)" << std::endl;
      return false;
    }

    int poll_result = poll(&pfd, 1, 5000); // 5-second timeout
    if (poll_result < 0) {
      if (errno == EINTR) {
        // Interrupted by signal - recheck flags and retry
        continue;
      }
      std::cerr << "[DAP Debug] Poll error: " << strerror(errno) << std::endl;
      state_.should_exit = true;
      return false;
    }

    if (poll_result == 0) {
      // Timeout - loop back and recheck interrupt/exit flags
      continue;
    }

    // Check for hangup or error (client disconnected)
    if (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) {
      std::cerr << "[DAP Debug] Client disconnected (poll revents=0x"
                << std::hex << pfd.revents << std::dec << ")" << std::endl;
      state_.should_exit = true;
      return false;
    }

    if (pfd.revents & POLLIN) {
      break; // Data available
    }
  }

  std::cerr << "[DAP Debug] Data available, reading headers..." << std::endl;

  // Check for Ctrl-D (ASCII 4) in curses mode
  unsigned char first_byte;
  ssize_t peek_result = read_bytes(&first_byte, 1);
  if (peek_result == 1 && first_byte == 4) {
    std::cerr << "[DAP Debug] Ctrl-D detected, exiting..." << std::endl;
    state_.should_exit = true;
    return false;
  }
  if (peek_result <= 0) {
    std::cerr << "[DAP Debug] EOF or error on first byte" << std::endl;
    return false;
  }

  // Start reading header line with the first byte we already read
  line += (char)first_byte;

  // Read lines byte by byte until we get all headers
  while (true) {
    // Check for interrupt or exit
    if (g_dap_interrupted || state_.should_exit) {
      std::cerr << "[DAP Debug] Aborting due to interrupt/exit" << std::endl;
      return false;
    }

    // Read rest of current line
    char c;
    while (true) {
      ssize_t n = read_bytes(&c, 1);
      if (n == 1) {
        if (c == '\n') {
          break; // End of line
        }
        line += c;
      } else if (n == 0) {
        // EOF - client disconnected
        std::cerr << "[DAP Debug] EOF while reading header" << std::endl;
        state_.should_exit = true;
        return false;
      } else {
        // Error (read_bytes handles EINTR internally)
        std::cerr << "[DAP Debug] Read error in header: " << strerror(errno)
                  << std::endl;
        state_.should_exit = true;
        return false;
      }
    }

    std::cerr << "[DAP Debug] Read header line (length=" << line.length()
              << "): " << line << std::endl;

    // Remove \r if present
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
      std::cerr << "[DAP Debug] Removed \\r, line is now: " << line
                << std::endl;
    }

    // Empty line marks end of headers
    if (line.empty()) {
      std::cerr << "[DAP Debug] Found empty line, end of headers" << std::endl;
      break;
    }

    // Parse Content-Length header
    std::size_t parsed_length = 0;
    if (dap::try_parse_content_length_line(line, parsed_length)) {
      content_length = parsed_length;
      std::cerr << "[DAP Debug] Parsed Content-Length: " << content_length
                << std::endl;
    }

    // Start next line
    line.clear();
  }

  std::cerr << "[DAP Debug] Exited header loop, content_length="
            << content_length << std::endl;

  if (content_length == 0) {
    std::cerr << "[DAP Debug] Invalid content_length, returning false"
              << std::endl;
    return false;
  }

  std::cerr << "[DAP Debug] About to read " << content_length
            << " bytes of content" << std::endl;

  // Read content
  std::string content;
  content.resize(content_length);
  size_t total_read = 0;
  while (total_read < content_length) {
    if (g_dap_interrupted || state_.should_exit) {
      std::cerr << "[DAP Debug] Aborting content read (interrupt/exit)"
                << std::endl;
      return false;
    }
    ssize_t n = read_bytes(&content[total_read], content_length - total_read);
    if (n == 0) {
      std::cerr << "[DAP Debug] EOF reading content: got " << total_read
                << " of " << content_length << " bytes" << std::endl;
      state_.should_exit = true;
      return false;
    }
    if (n < 0) {
      std::cerr << "[DAP Debug] Read error in content: " << strerror(errno)
                << std::endl;
      state_.should_exit = true;
      return false;
    }
    total_read += n;
  }

  std::cerr << "[DAP Debug] Successfully read " << total_read
            << " bytes of content" << std::endl;

  // Parse JSON
  try {
    msg = json::parse(content);
    return true;
  } catch (const json::exception &e) {
    std::cerr << "[DAP Debug] JSON parse error: " << e.what() << std::endl;
    return false;
  }
}

/*
 * Send a JSON-RPC message to stdout
 */
void CDapDebugUI::send_message(const json &msg) {
  const std::string content = msg.dump();
  const std::string header_str = dap::make_content_length_header(content.size());

  // Write header
  write_bytes(header_str.c_str(), header_str.length());
  // Write content
  write_bytes(content.c_str(), content.length());
}

/*
 * Send a DAP event
 */
void CDapDebugUI::send_event(const std::string &event_type, const json &body) {
  json event;
  event["type"] = "event";
  event["seq"] = state_.next_seq++;
  event["event"] = event_type;
  event["body"] = body;
  send_message(event);
}

/*
 * Send a DAP response
 */
void CDapDebugUI::send_response(int request_seq, const std::string &command,
                                bool success, const json &body) {
  json response;
  response["type"] = "response";
  response["seq"] = state_.next_seq++;
  response["request_seq"] = request_seq;
  response["command"] = command;
  response["success"] = success;
  if (!body.is_null()) {
    response["body"] = body;
  }
  send_message(response);
}

/* ============================================================
 * DAP Request Processing
 * ============================================================ */

void CDapDebugUI::process_request(const json &request) {
  std::string command = request.value("command", "");

  if (command == "initialize") {
    handle_initialize(request);
  } else if (command == "launch") {
    handle_launch(request);
  } else if (command == "attach") {
    handle_attach(request);
  } else if (command == "disconnect") {
    handle_disconnect(request);
  } else if (command == "setBreakpoints") {
    handle_set_breakpoints(request);
  } else if(command == "configurationDone") {
    handle_configuration_done(request);
  } else if (command == "continue") {
    handle_continue(request);
  } else if (command == "next") {
    handle_next(request);
  } else if (command == "stepIn") {
    handle_step_in(request);
  } else if (command == "stepOut") {
    handle_step_out(request);
  } else if (command == "pause") {
    handle_pause(request);
  } else if (command == "stackTrace") {
    handle_stack_trace(request);
  } else if (command == "scopes") {
    handle_scopes(request);
  } else if (command == "variables") {
    handle_variables(request);
  } else if (command == "evaluate") {
    handle_evaluate(request);
  } else if (command == "threads") {
    handle_threads(request);
  } else if (command == "frobd.listLinkedFiles") {
    handle_list_linked_files(request);
  } else {
    // Unknown command
    send_response(request["seq"], command, false, json::object());
  }
}

/* ============================================================
 * DAP Request Handlers (Stubs for now)
 * ============================================================ */

/**
 * Handle the 'initialize' request
 * - respond with capabilities and send initialized event
 */
void CDapDebugUI::handle_initialize(const json &request) {
  json body;
  body["supportsConfigurationDoneRequest"] = true;
  body["supportsEvaluateForHovers"] = true;
  body["supportsStepInTargetsRequest"] = false;
  body["supportsFrobdLinkedFilesRequest"] = true;

  send_response(request["seq"], "initialize", true, body);

  // Send initialized event
  send_event("initialized", json::object());
  state_.initialized = true;
}

/**
 * Handle the 'launch' request.
 * This will typically be the first request received from the IDE after
 * initialization.
 *
 * We use this as the signal to start the VM execution, so we
 * mark state_.launched as true here.
 *
 * The IDE will then be able to send a continue request to start
 * execution after the initial break.
 */
void CDapDebugUI::handle_launch(const json &request) {

  // Optional custom config: exposing the "Globals" scope can be very expensive
  // for large games, so keep it opt-in.
  state_.enable_globals_scope = false;
  if (request.contains("arguments") &&
      request["arguments"].contains("enableGlobalsScope") &&
      request["arguments"]["enableGlobalsScope"].is_boolean()) {
    state_.enable_globals_scope =
        request["arguments"]["enableGlobalsScope"].get<bool>();
  }

  // Check for stopOnEntry option
  bool stop_on_entry = false;

  if (request.contains("arguments") &&
      request["arguments"].contains("stopOnEntry")) {
    stop_on_entry = request["arguments"]["stopOnEntry"].get<bool>();
  }

  // Launch implementation - mark as launched so init() completes and VM starts
  state_.launched = true;

  // Check if the IDE wants to break at the first instruction
  if (stop_on_entry) {
    // pause_requested will cause the cmd_loop to send a "stopped" event
    // immediately after starting the VM, which will pause execution at the
    // first instruction.
    state_.pause_requested = true;

    std::cerr << "[DAP Debug] stopOnEntry=true, will break at first instruction"
              << std::endl;
  }

  // Respond to launch request - we can respond before the VM actually starts
  // since the IDE just needs to know we've accepted the launch and will start
  // soon.
  send_response(request["seq"], "launch", true, json::object());
  std::cerr << "[DAP Debug] Launch request received, VM will now start"
            << std::endl;
}

/**
 * Handle the 'attach' request.
 * For our purposes, attach and launch can be treated the same since the IDE
 * just needs to know we're ready to start the VM. The difference is mostly in
 * how the IDE presents this to the user (attach is usually for already-running
 * processes, while launch is for starting a new process), but since we don't
 * have a real attach scenario, we can just treat attach as another way to start
 * the VM.
 */
void CDapDebugUI::handle_attach(const json &request) {
  // Same custom config as launch.
  state_.enable_globals_scope = false;
  if (request.contains("arguments") &&
      request["arguments"].contains("enableGlobalsScope") &&
      request["arguments"]["enableGlobalsScope"].is_boolean()) {
    state_.enable_globals_scope =
        request["arguments"]["enableGlobalsScope"].get<bool>();
  }
  send_response(request["seq"], "attach", true, json::object());
}

/**
 * Handle the 'disconnect' request - this should cleanly exit the debugger
 */
void CDapDebugUI::handle_disconnect(const json &request) {
  send_response(request["seq"], "disconnect", true, json::object());
  // Exit any DAP/UI loops.
  state_.should_exit = true;

  // Some unit tests call this without having called init().
  if (ctx_ != nullptr) {
    // Ensure we have the latest globals pointer so G_interpreter is valid.
    VMGLOB_PTR(ctx_->vmg);

    // Mark that we're leaving the debugger command loop.
    ctx_->in_debugger = 0;

    // Important: simply setting in_debugger=0 just resumes VM execution.
    // For a DAP "disconnect" (VS Code Stop), we want the debuggee to end so
    // the VM can shut down and call CVmDebugUI::terminate(), which frees this UI.
    if (G_interpreter != nullptr) {
      G_interpreter->set_halt_vm(TRUE);
    }
  }

  // If we're using sockets, close the transport to unblock the client.
  close_io();
}

/**
 * Handle the 'setBreakpoints' request - this will set breakpoints for a given
 * source file and lines. The request will contain the source file path and an
 * array of line numbers. We will try to set breakpoints at those lines and
 * respond with which breakpoints were successfully set.
 */
void CDapDebugUI::handle_set_breakpoints(const json &request) {
  json body;
  json breakpoints_array = json::array();

  // Ensure we have the latest globals pointer to access debug info
  VMGLOB_PTR(ctx_->vmg);

  // Validate request structure first
  if (!request.contains("arguments") ||
      !request["arguments"].contains("source")) {
    send_response(request["seq"], "setBreakpoints", false, body);
    return;
  }

  const json &args = request["arguments"];

  // Extract source file path and lines from request
  std::string file_path = args["source"]["path"];

  // Get the list of line numbers and convert to vector
  std::vector<unsigned long> lines;
  if (args.contains("lines") && args["lines"].is_array()) {
    for (const auto &line_json : args["lines"]) {
      lines.push_back(line_json.get<unsigned long>());
    }
  }

  // Find the source file ID in the source file table
  int source_id = -1;

  // The source file table contains entries for all source files
  if (G_srcf_table != nullptr) {
    for (size_t i = 0; i < G_srcf_table->get_count(); ++i) {

      // Get the source file entry
      CVmSrcfEntry *entry = G_srcf_table->get_entry(i);
      if (entry != nullptr && entry->get_name() != nullptr) {

        // Check if source file matches
        const char *entry_name = entry->get_name();

        // Try exact match first
        if (file_path == entry_name) {
          source_id = i;
          break;
        }

        // Try matching just the filename
        if (file_path.find(os_get_root_name((char *)entry_name)) !=
            std::string::npos) {
          source_id = i;
          break;
        }
      }
    }
  }

  // For each line, try to set a breakpoint
  for (unsigned long line : lines) {
    json bp;
    bp["line"] = (int)line;

    if (source_id >= 0 && G_srcf_table != nullptr) {
      // Get the source file entry for this source ID
      CVmSrcfEntry *entry = G_srcf_table->get_entry(source_id);
      if (entry != nullptr) {
        // Find the code address for this line
        unsigned long find_line = line;
        ulong code_ofs = entry->find_src_addr(&find_line, FALSE);

        if (code_ofs != 0) {
          // Get the actual code address
          const uchar *code_addr =
              (const uchar *)G_code_pool->get_ptr(code_ofs);

          // Try to set the breakpoint
          int bpnum = 0;
          int did_set = 0;
          char errbuf[256];

          // Toggle breakpoint at this address (this will set if not already
          // set, or remove if already set - we will check did_set to see which
          // happened)
          int result =
              G_debugger->toggle_breakpoint(vmg_ code_addr, nullptr, 0, &bpnum,
                                            &did_set, errbuf, sizeof(errbuf));

          if (result == 0 && did_set) {
            // Successfully set breakpoint
            bp["verified"] = true;
            bp["line"] = (int)find_line; // Use actual line where BP was set
            bp["id"] = bpnum;
            if (helper_) {
              helper_->upsert_breakpoint(bpnum, code_addr,
                                         entry->get_name(),
                                         (int)find_line, FALSE);
            }
          } else {
            // Failed to set or breakpoint was removed
            bp["verified"] = false;
            if (!did_set && result == 0) {
              // Breakpoint was removed (toggled off)
              bp["message"] = "Breakpoint removed";
              if (helper_) {
                helper_->remove_breakpoint(bpnum);
              }
            } else {
              bp["message"] =
                  std::string("Failed to set breakpoint: ") + errbuf;
            }
          }
        } else {
          // No code at this line
          bp["verified"] = false;
          bp["message"] = "No executable code at this line";
        }
      } else {
        bp["verified"] = false;
        bp["message"] = "Source file entry not found";
      }
    } else {
      bp["verified"] = false;
      bp["message"] = "Source file not found in debug info";
    }

    breakpoints_array.push_back(bp);
  }

  body["breakpoints"] = breakpoints_array;
  send_response(request["seq"], "setBreakpoints", true, body);
}

void CDapDebugUI::handle_configuration_done(const json &request) {
  // For now, we don't have any special configuration to do, so we just respond
  // with success. In a more complete implementation, this is where we would
  // finalize any setup after receiving all initial configuration from the IDE.
  send_response(request["seq"], "configurationDone", true, json::object());
}

/**
 * Handle the 'stepIn' request - this will set the stepping mode to STEP_INTO
 * and tell the VM debugger to step into the next instruction.
 */
void CDapDebugUI::handle_step_in(const json &request) {
  // Ensure we have the latest globals pointer to access debug info
  VMGLOB_PTR(ctx_->vmg);

  ctx_->stepping_mode = STEP_INTO; // Set stepping mode to step into
  G_debugger->set_step_in();       // Step into in current/only thread

  // Mark that we're exiting the debugger so that execution can resume
  ctx_->in_debugger = 0;

  // Respond to IDE immediately so it can update UI - the actual step will
  // happen asynchronously and will trigger a "stopped" event when it completes
  send_response(request["seq"], "stepIn", true, json::object());
}

/**
 * Handle the 'next' request - this will set the stepping mode to STEP_OVER
 * and tell the VM debugger to step over the next instruction.
 */
void CDapDebugUI::handle_next(const json &request) {
  VMGLOB_PTR(ctx_->vmg);

  ctx_->stepping_mode = STEP_OVER;  // Set stepping mode to step over
  G_debugger->set_step_over(vmg0_); // Step over in current thread

  // Mark that we're exiting the debugger so that execution can resume
  ctx_->in_debugger = 0;

  // Respond to IDE immediately so it can update UI - the actual step will
  // happen asynchronously and will trigger a "stopped" event when it completes
  send_response(request["seq"], "next", true, json::object());
}

/**
 * Handle the 'stepOut' request - this will set the stepping mode to STEP_OUT
 * and tell the VM debugger to step out of the current function. We will then
 * exit the debugger (ctx_->in_debugger = 0) so that execution can resume.
 */
void CDapDebugUI::handle_step_out(const json &request) {
  // Ensure we have the latest globals pointer to access debug info
  VMGLOB_PTR(ctx_->vmg);

  ctx_->stepping_mode = STEP_OUT;  // Set stepping mode to step out
  G_debugger->set_step_out(vmg0_); // Step out in current thread

  // Mark that we're exiting the debugger so that execution can resume
  ctx_->in_debugger = 0;

  // Respond to IDE immediately so it can update UI - the actual step will
  // happen asynchronously and will trigger a "stopped" event when it completes
  send_response(request["seq"], "stepOut", true, json::object());
}

// Continue
void CDapDebugUI::handle_continue(const json &request) {
  // Ensure we have the latest globals pointer to access debug info
  VMGLOB_PTR(ctx_->vmg);

  ctx_->stepping_mode = STEP_NONE; // Clear stepping mode

  // Tell VM debugger to clear single-step mode
  G_debugger->set_go();

  // Mark that we're exiting the debugger so that execution can resume
  ctx_->in_debugger = 0;

  // Respond to IDE immediately so it can update UI - the actual continue will
  // happen asynchronously and will trigger a "continued" event followed by a
  // "stopped" event when execution resumes.
  send_response(request["seq"], "continue", true, json::object());
}

/**
 * Handle the 'pause' request - this will set a flag to indicate we want to
 * pause, and then immediately call set_break_stop() to interrupt execution. The
 * cmd_loop will then send a "stopped" event with reason "pause" to the IDE.
 */
void CDapDebugUI::handle_pause(const json &request) {
  // Set pause flag for internal tracking
  state_.pause_requested = true;

  // Immediately call set_break_stop() to interrupt execution
  // (same approach as HTML TADS debugger uses for Break button)
  if (ctx_ != 0) {
    // Ensure we have the latest globals pointer to access debug info
    VMGLOB_PTR(ctx_->vmg);

    // Call set_break_stop to interrupt execution and enter the debugger
    G_debugger->set_break_stop();
  }

  send_response(request["seq"], "pause", true, json::object());

  std::cerr << "[DAP Debug] Pause requested, set_break_stop() called"
            << std::endl;
}

void CDapDebugUI::handle_stack_trace(const json &request) {
  json body;
  json stack_frames = json::array();

  // Get VM globals
  VMGLOB_PTR(ctx_->vmg);

  // Get the start level and levels requested (DAP protocol parameters)
  int start_level = 0;
  int levels = 20; // default
  if (request.contains("arguments")) {
    if (request["arguments"].contains("startFrame")) {
      start_level = request["arguments"]["startFrame"];
    }
    if (request["arguments"].contains("levels")) {
      levels = request["arguments"]["levels"];
    }
  }

  // For each stack level, get source info and build a stack frame object
  for (int level = start_level; level < start_level + levels; ++level) {
    const char *fname = nullptr;
    unsigned long linenum = 0;

    // Get source info for this stack level (this is the public API)
    if (G_debugger->get_source_info(vmg_ & fname, &linenum, level) != 0)
      break; // No more stack frames available

    // Build a simple function name (we'll use the file name for now)
    // A more complete implementation would walk the stack to get actual
    // function names
    std::string func_name =
        fname ? os_get_root_name((char *)fname) : "<unknown>";

    // Build the stack frame
    json frame;
    frame["id"] = level + 1000; // Frame IDs must be unique and positive
    frame["name"] = func_name;
    frame["line"] = (int)linenum;
    frame["column"] = 0;

    // Build source information
    if (fname != nullptr && fname[0] != '\0') {
      json source;

      // Convert to absolute path if needed
      char abs_path[OSFNMAX];
      if (os_is_file_absolute(fname)) {
        // Already absolute path
        strcpy(abs_path, fname);
      } else {
        // Try to make the path absolute relative to the image file directory
        if (!state_.image_file.empty()) {
          // Get the directory containing the image file
          char img_dir[OSFNMAX];
          strcpy(img_dir, state_.image_file.c_str());
          os_get_path_name(img_dir, sizeof(img_dir), img_dir);

          // Build absolute path
          os_build_full_path(abs_path, sizeof(abs_path), img_dir, fname);
        } else {
          // Fall back to relative path as-is
          strcpy(abs_path, fname);
        }
      }

      source["name"] = os_get_root_name(abs_path); // Just the filename
      source["path"] = abs_path;                   // Full absolute path
      frame["source"] = source;
    }

    stack_frames.push_back(frame);
  }

  body["stackFrames"] = stack_frames;
  body["totalFrames"] = (int)stack_frames.size();
  send_response(request["seq"], "stackTrace", true, body);
}

/**
 * Handle the 'scopes' request - this should return the variable scopes for a
 * given stack frame.
 *
 * We currently expose two scopes:
 *  - "Locals": stack locals for the requested frame
 *  - "Globals": global object symbols (from the image's GSYM table)
 *
 * variablesReference convention:
 *  - handle_stack_trace() uses frame ids of (stackLevel + 1000)
 *  - we use that same frame id as the variablesReference for the Locals scope
 *    so that the client can request locals by echoing the frame id back in a
 *    'variables' request.
 *  - globals use a separate range: (2,000,000 + stackLevel)
 */
void CDapDebugUI::handle_scopes(const json &request) {
  json body;
  json scopes = json::array();

  if (!request.contains("arguments") || !request["arguments"].contains("frameId") ||
      !request["arguments"]["frameId"].is_number_integer()) {
    body["scopes"] = scopes;
    send_response(request["seq"], "scopes", false, body);
    return;
  }

  const int frame_id = request["arguments"]["frameId"].get<int>();
  int stack_level = frame_id - 1000;
  if (stack_level < 0)
    stack_level = 0;

  // Reserve a separate variablesReference range for globals so we can
  // distinguish it from the locals convention (frame_id).
  constexpr int kGlobalsRefBase = 2000000;

  // Convention used by this adapter:
  // - stack frame ids are (stackLevel + 1000) in handle_stack_trace()
  // - variablesReference for locals is that same frame id
  // This makes the DAP client request variables by echoing back the frame id.
  json locals;
  locals["name"] = "Locals";
  locals["variablesReference"] = frame_id;
  locals["expensive"] = false;
  scopes.push_back(locals);

  json globals;
  if (state_.enable_globals_scope && G_prs != nullptr &&
      G_prs->get_global_symtab() != nullptr) {
    globals["name"] = "Globals";
    globals["variablesReference"] = kGlobalsRefBase + stack_level;
    globals["expensive"] = true;
    scopes.push_back(globals);
  }

  body["scopes"] = scopes;
  send_response(request["seq"], "scopes", true, body);
}

/**
 * Handle the 'variables' request - this should return the variables for a given
 * variablesReference (which would be provided in the scopes response). For
 * the locals scope, we treat variablesReference as a frame id (stackLevel +
 * 1000) and enumerate local variables at that stack level.
 *
 * For the globals scope, variablesReference is (2,000,000 + stackLevel) and we
 * enumerate safe global object symbols from the global symbol table (GSYM).
 *
 * Complex/expandable values are not currently supported (variablesReference is
 * always 0 for returned variables).
 */
void CDapDebugUI::handle_variables(const json &request) {
  json body;
  json variables = json::array();

  // Ensure we have the latest globals pointer to access debug info
  if (ctx_ == nullptr || helper_ == nullptr) {
    body["variables"] = variables;
    send_response(request["seq"], "variables", false, body);
    return;
  }
  VMGLOB_PTR(ctx_->vmg);

  if (!request.contains("arguments") ||
      !request["arguments"].contains("variablesReference")) {
    body["variables"] = variables;
    send_response(request["seq"], "variables", false, body);
    return;
  }

  const json &args = request["arguments"];
  int variables_ref = args.value("variablesReference", 0);

  constexpr int kGlobalsRefBase = 2000000;

  // Apply optional paging requested by the client
  size_t start = 0;
  size_t count = 0;
  if (args.contains("start") && args["start"].is_number_integer()) {
    start = static_cast<size_t>(args["start"].get<int>());
  }
  if (args.contains("count") && args["count"].is_number_integer()) {
    count = static_cast<size_t>(args["count"].get<int>());
  }

  // Globals scope: (kGlobalsRefBase + stackLevel)
  if (variables_ref >= kGlobalsRefBase) {
    // Safety/UX: keep globals opt-in. If the client still asks for globals
    // variablesReference while disabled, return an empty list quickly.
    if (!state_.enable_globals_scope) {
      body["variables"] = variables;
      send_response(request["seq"], "variables", true, body);
      return;
    }

    const int stack_level = variables_ref - kGlobalsRefBase;

    // It's possible for G_prs or its global symtab to be unavailable if debug
    // info wasn't loaded.
    if (G_prs == nullptr || G_prs->get_global_symtab() == nullptr) {
      body["variables"] = variables;
      send_response(request["seq"], "variables", true, body);
      return;
    }

    struct GlobalsCtx {
      CDapDebugUI *ui;
      json *out;
      int stack_level;
      size_t start;
      size_t count;
      size_t seen;
    };

    GlobalsCtx gctx{this, &variables, stack_level, start, count, 0};

    auto enum_cb = [](void *ctx0, CTcSymbol *sym) {
      auto *ctx = static_cast<GlobalsCtx *>(ctx0);
      if (ctx == nullptr || ctx->ui == nullptr || ctx->out == nullptr ||
          sym == nullptr)
        return;

      // Only include global object symbols to avoid evaluating callable
      // entities (properties/functions) with potential side effects.
      if (sym->get_type() != TC_SYM_OBJ)
        return;

      if (ctx->count != 0 && ctx->out->size() >= ctx->count)
        return;

      if (ctx->seen++ < ctx->start)
        return;

      const std::string name(sym->get_sym(), sym->get_sym_len());

      vm_obj_id_t obj_id = sym->get_val_obj();
      if (obj_id == VM_INVALID_OBJ)
        return;

      json var;
      var["name"] = name;
      var["value"] = std::string("obj#") + std::to_string((unsigned long)obj_id);
      var["type"] = "object";
      var["variablesReference"] = 0;
      var["evaluateName"] = name;
      ctx->out->push_back(var);
    };

    G_prs->get_global_symtab()->enum_entries(enum_cb, &gctx);

    body["variables"] = variables;
    send_response(request["seq"], "variables", true, body);
    return;
  }

  // Convention used by this adapter:
  // - stack frame ids are (stackLevel + 1000) in handle_stack_trace()
  // - variablesReference for the "Locals" scope is that same frame id
  // So variablesReference 1000 => frame 0 locals, 1001 => frame 1, etc.
  int stack_level = variables_ref - 1000;
  if (variables_ref == 0 || stack_level < 0) {
    body["variables"] = variables;
    send_response(request["seq"], "variables", true, body);
    return;
  }

  struct VarItem {
    std::string name;
    std::string value;
  };

  struct CollectCtx {
    std::vector<VarItem> *out;
  };

  std::vector<VarItem> collected;
  CollectCtx collect_ctx{&collected};

  auto collect_cb = [](void *ctx0, const char *name, const char *value) {
    auto *cc = static_cast<CollectCtx *>(ctx0);
    if (cc == nullptr || cc->out == nullptr || name == nullptr)
      return;
    VarItem item;
    item.name = name;
    item.value = value ? value : "";
    cc->out->push_back(std::move(item));
  };

  helper_->enum_locals(stack_level, collect_cb, &collect_ctx);

  size_t end = collected.size();
  if (start > collected.size())
    start = collected.size();
  if (count != 0 && start + count < end)
    end = start + count;

  for (size_t i = start; i < end; ++i) {
    const auto &v = collected[i];
    json var;
    var["name"] = v.name;
    var["value"] = v.value;
    var["variablesReference"] = 0;
    var["evaluateName"] = v.name;
    variables.push_back(var);
  }

  body["variables"] = variables;
  send_response(request["seq"], "variables", true, body);
}

/**
 * Handle the 'evaluate' request - this should evaluate an expression in the
 * context of a given stack frame and return the result.
 */
void CDapDebugUI::handle_evaluate(const json &request) {
  json body;

  // Ensure we have the latest globals pointer to access debug info
  VMGLOB_PTR(ctx_->vmg);

  // Validate request structure first
  if (!request.contains("arguments") ||
      !request["arguments"].contains("expression")) {
    body["result"] = "Error: No expression provided";
    body["variablesReference"] = 0;
    send_response(request["seq"], "evaluate", false, body);
    return;
  }

  // Extract expression and context from request
  const json &args = request["arguments"];
  std::string expr = args["expression"];

  // Convert frame ID back to stack level so we can evaluate in the correct
  // context

  // Frame IDs are level + 1000, so level = frameId - 1000.
  // This is because frame IDs must be positive and unique,
  // we can't just use the level directly as the ID since level 0 would be ID 0
  // which is reserved, so we add an offset of 1000 to ensure all frame IDs are
  // positive and don't conflict with other IDs we might want to use in the
  // future.

  int frame_id = args.value("frameId", 1000);
  int stack_level = frame_id - 1000;

  // Ensure stack level is not negative (in case of invalid frameId)
  if (stack_level < 0)
    stack_level = 0;

  std::cerr << "[DAP Debug] Evaluating expression: " << expr << " at frame "
            << frame_id << " (level " << stack_level << ")" << std::endl;

  // Use the VM's eval_expr to evaluate the expression
  char result[1024];

  // is_lval indicates whether the expression is an l-value (something that can
  // be assigned to)
  int is_lval = 0;

  // This indicates whether the result is a complex object that can be expanded
  int is_openable = 0;

  // Evaluate the expression in the context of the specified stack level. The
  // result will be stored in the 'result' buffer. The expression is
  // evaluated in the context of the specified stack level, which allows the
  // user to inspect variables and state at that point in the call stack.
  // For now we won't support complex objects or l-values, but we will set these
  // flags so that the IDE knows the capabilities of our evaluate
  // implementation.
  int eval_result = G_debugger->eval_expr(
      vmg_ result, sizeof(result), expr.c_str(), stack_level, &is_lval,
      &is_openable, nullptr, nullptr, FALSE);

  std::cerr << "[DAP Debug] eval_expr returned: " << eval_result << std::endl;

  if (eval_result == 0) {
    // Success
    std::cerr << "[DAP Debug] Result: " << result << std::endl;
    body["result"] = result;
    body["variablesReference"] = 0; // TODO: Support complex objects
    send_response(request["seq"], "evaluate", true, body);
  } else {
    // Evaluation failed
    std::cerr << "[DAP Debug] Evaluation failed with code: " << eval_result
              << std::endl;
    body["result"] = "Error: Failed to evaluate expression";
    body["variablesReference"] = 0;
    send_response(request["seq"], "evaluate", false, body);
  }
}

/**
 * Handle the 'threads' request - this should return the list of threads in the
 * debugged process. For our purposes, we will just return a single thread
 * representing the TADS VM, since the VM is single-threaded.
 */
void CDapDebugUI::handle_threads(const json &request) {
  json body;
  json thread;
  thread["id"] = 1;
  thread["name"] = "TADS VM";
  body["threads"] = json::array({thread});
  send_response(request["seq"], "threads", true, body);
}

// Handler for the custom frobd.listLinkedFiles request
void CDapDebugUI::handle_list_linked_files(const json &request) {
  json body;
  body["files"] = json::array();

  std::string response;
  size_t count = G_srcf_table->get_count();
  if (count == 0) {
    body["result"] = "No source files loaded.\n";
    send_response(request["seq"], "frobd.listLinkedFiles", false, body);
    return;
  }

  // Iterate through the source file table and collect info on master files
  std::map<size_t, std::string> file_list;
  for (size_t i = 0; i < count; ++i) {
    CVmSrcfEntry *entry = G_srcf_table->get_entry(i);
    if (entry && entry->is_master()) {
      char buf[1024];
      snprintf(buf, sizeof(buf), "%s", entry->get_name());
      json file_info;
      file_info["bytecodeUnitId"] = static_cast<int>(i);
      file_info["path"] = buf;
      body["files"].push_back(file_info);
    }
  }
  send_response(request["seq"], "frobd.listLinkedFiles", true, body);
}