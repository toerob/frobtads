/* 
 * dapdebugui.h - Debug Adapter Protocol debugger
 *
 * This implements the DebuggerUI interface to provide DAP support for IDE
 * integration.
 */

#ifndef DAPDEBUGUI_H
#define DAPDEBUGUI_H

#include "debugui.h"
#include "frobdebughelper.h"
#include "json.hpp"
#include <string>
#include <map>
#include <memory>

using json = nlohmann::json;

/*
 * DAP Debugger Implementation
 *
 * Communicates with an IDE or editor via Debug Adapter Protocol messages
 * (JSON-RPC-style envelopes over stdio/socket/TCP).
 */
class CDapDebugUI : public DebuggerUI
{
    friend void dap_signal_handler(int);
    
    // Allow test fixture to access private members
    friend class DapDebugUITest;
    
public:
    CDapDebugUI();
    CDapDebugUI(const std::string& socket_path); // Constructor for socket communication
    CDapDebugUI(int port); // Constructor for TCP communication
    virtual ~CDapDebugUI();
    
    /* ============================================================
     * DebuggerUI Interface Implementation
     * ============================================================ */
    
    virtual void init(VMG_ const char *image_filename) override;
    virtual void init_after_load(VMG0_) override;
    virtual void terminate(VMG0_) override;
    virtual void cmd_loop(VMG_ int bp_number, int error_code, const uchar **pc) override;
    
    /* Optional notifications - used to send DAP events */
    virtual void on_breakpoint_hit(VMG_ int bp_num, const char *file, unsigned long line) override;
    virtual void on_step_complete(VMG_ const char *file, unsigned long line) override;
    virtual void on_error(VMG_ int error_code, const char *message) override;
    virtual void on_execution_resumed(VMG0_) override;
    virtual void on_execution_paused(VMG0_) override;
    
    /* Send a DAP event */
    void send_event(const std::string& event_type, const json& body);
    
protected:
    // Protected for testing - allows test fixtures to access these methods
    /* ============================================================
     * DAP Protocol Implementation
     * ============================================================ */
    
    /* Read a JSON-RPC message from stdin */
    bool read_message(json& msg);
    
    /* Send a JSON-RPC message to stdout */
    void send_message(const json& msg);
    
    /* Send a DAP response */
    void send_response(int request_seq, const std::string& command, 
                      bool success, const json& body = json::object());
    
    /* Process a DAP request */
    void process_request(const json& request);
    
    /* ============================================================
     * DAP Request Handlers
     * ============================================================ */
    
    void handle_initialize(const json& request);
    void handle_launch(const json& request);
    void handle_attach(const json& request);
    void handle_disconnect(const json& request);
    void handle_set_breakpoints(const json& request);
    void handle_configuration_done(const json& request);
    void handle_continue(const json& request);
    void handle_next(const json& request);
    void handle_step_in(const json& request);
    void handle_step_out(const json& request);
    void handle_pause(const json& request);
    void handle_stack_trace(const json& request);
    void handle_scopes(const json& request);
    void handle_variables(const json& request);
    void handle_evaluate(const json& request);
    void handle_threads(const json& request);
    void handle_list_linked_files(const json &request);
    /* ============================================================
     * I/O Management
     * ============================================================ */
    
    void setup_stdio_io();  // Use stdin/stdout (default)
    void setup_socket_io(const std::string& path);  // Use Unix domain socket
    void setup_tcp_io(int port);  // Use TCP socket
    void close_io();
    
    ssize_t read_bytes(void* buf, size_t count);  // Read from current I/O channel
    ssize_t write_bytes(const void* buf, size_t count);  // Write to current I/O channel
    
private:
    /* ============================================================
     * State Management
     * ============================================================ */
    
    struct DebugState {
        bool initialized = false;
        bool launched = false;  // Set when launch request is received
        bool running = false;
        bool should_exit = false;
        bool pause_requested = false;  // Set when IDE requests pause
        // Off by default because enumerating globals can be very expensive.
        // Enable via a custom launch/attach argument: enableGlobalsScope.
        bool enable_globals_scope = false;
        bool sent_breakpoint_invalidation = false;  // Track if we've invalidated breakpoints after VM load
        int next_seq = 1;
        std::string image_file;
        std::map<std::string, std::vector<int>> breakpoints; // file -> line numbers
    };
    
    enum class IOMode {
        StdIO,  // stdin/stdout
        Socket, // Unix domain socket
        TCP     // TCP socket
    };
    
    DebugState state_;
    dbgcxdef* ctx_;
    std::unique_ptr<CFrobDebugHelper> helper_;
    
    IOMode io_mode_ = IOMode::StdIO;
    int io_fd_ = -1;  // File descriptor for socket/tcp communication
    std::string socket_path_;  // Path to Unix domain socket
    int listen_fd_ = -1;  // Listening socket for TCP
};

#endif /* DAPDEBUGUI_H */
