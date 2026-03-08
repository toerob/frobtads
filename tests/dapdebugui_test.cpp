/*
 * dapdebugui_test.cpp - Unit tests for DAP Debug UI
 * 
 * Tests the core functionality of the DAP debugger interface.
 */

#include <gtest/gtest.h>
#include <sstream>
#include <string>
#include <vector>

// Include the testable wrapper which exposes private members
#include "dapdebugui_testable.h"

// VM stubs/mocks
#include "mocks/mock_vm.h"

#include "dap/dap_framing.h"

using json = nlohmann::json;

// ============================================================
// Test Fixtures
// ============================================================

class DapDebugUITest : public ::testing::Test {
protected:
    void SetUp() override {
        mock_vm_setup();
    }

    void TearDown() override {
        mock_vm_teardown();
    }

    // Helper to create a valid DAP message
    std::string create_dap_message(const json& content) {
        std::string body = content.dump();
        std::stringstream ss;
        ss << "Content-Length: " << body.length() << "\r\n";
        ss << "\r\n";
        ss << body;
        return ss.str();
    }

    // Parse one or more DAP protocol messages from a raw output stream.
    //
    // Our unit tests often capture stdout from the adapter after invoking a
    // handler; a single handler can emit multiple framed DAP messages
    // (e.g. initialize sends a response + an "initialized" event).
    // This helper walks the stream, reads each "Content-Length" header,
    // slices out the JSON payload, and returns the decoded messages in order.
    static std::vector<json> parse_dap_stream(const std::string &out) {
        std::vector<json> msgs;
        size_t pos = 0;

        while (pos < out.size()) {
            // Skip any leading whitespace/noise.
            while (pos < out.size() && (out[pos] == '\r' || out[pos] == '\n'))
                ++pos;
            if (pos >= out.size())
                break;

            // Find end of header block.
            size_t header_end = out.find("\r\n\r\n", pos);
            if (header_end == std::string::npos)
                break;

            // Parse Content-Length.
            size_t content_length = 0;
            {
                // Header block is usually single line: Content-Length: N
                std::string header_block = out.substr(pos, header_end - pos);
                std::stringstream hs(header_block);
                std::string header_line;
                while (std::getline(hs, header_line)) {
                    if (!header_line.empty() && header_line.back() == '\r')
                        header_line.pop_back();
                    size_t parsed = 0;
                    if (dap::try_parse_content_length_line(header_line, parsed)) {
                        content_length = parsed;
                        break;
                    }
                }
            }
            if (content_length == 0)
                break;

            const size_t body_start = header_end + 4;
            if (body_start + content_length > out.size())
                break;

            const std::string json_str = out.substr(body_start, content_length);
            msgs.push_back(json::parse(json_str));
            pos = body_start + content_length;
        }

        return msgs;
    }

    // Friend-only access helpers. CDapDebugUI declares `friend class DapDebugUITest;`
    // but GoogleTest generates derived test classes, which are *not* friends.
    // Keeping these in the fixture lets derived tests use them safely.
    static void set_ctx(CDapDebugUI &ui, dbgcxdef *ctx) { ui.ctx_ = ctx; }
    static dbgcxdef *get_ctx(CDapDebugUI &ui) { return ui.ctx_; }

    static void set_helper(CDapDebugUI &ui, std::unique_ptr<CFrobDebugHelper> h) {
        ui.helper_ = std::move(h);
    }
    static CFrobDebugHelper *get_helper(CDapDebugUI &ui) { return ui.helper_.get(); }

    static bool pause_requested(const CDapDebugUI &ui) { return ui.state_.pause_requested; }
    static void set_image_file(CDapDebugUI &ui, const std::string &path) { ui.state_.image_file = path; }
};

// ============================================================
// Message Protocol Tests
// ============================================================

TEST_F(DapDebugUITest, SendMessageFormatsCorrectly) {
    // Test that send_message creates properly formatted DAP messages
    json test_msg;
    test_msg["type"] = "response";
    test_msg["seq"] = 1;
    test_msg["command"] = "initialize";
    test_msg["success"] = true;

    // Capture stdout and stderr (constructor prints to stderr)
    testing::internal::CaptureStdout();
    testing::internal::CaptureStderr();
    
    CDapDebugUITestable debugUI;
    debugUI.send_message(test_msg);
    
    std::string output = testing::internal::GetCapturedStdout();
    std::string err_output = testing::internal::GetCapturedStderr();
    (void)err_output; // Suppress unused warning
    
    // Verify format: Content-Length header, blank line, then JSON
    EXPECT_TRUE(output.find("Content-Length: ") == 0);
    EXPECT_TRUE(output.find("\r\n\r\n") != std::string::npos);
    EXPECT_TRUE(output.find("\"type\":\"response\"") != std::string::npos);
}

TEST_F(DapDebugUITest, SendEventCreatesValidEvent) {
    // Capture stdout and stderr
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr(); // Discard constructor output
    
    testing::internal::CaptureStdout();
    
    json body;
    body["reason"] = "breakpoint";
    body["threadId"] = 1;
    
    debugUI.send_event("stopped", body);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Parse the output to verify structure
    size_t body_start = output.find("\r\n\r\n") + 4;
    std::string json_str = output.substr(body_start);
    json parsed = json::parse(json_str);
    
    EXPECT_EQ(parsed["type"], "event");
    EXPECT_EQ(parsed["event"], "stopped");
    EXPECT_EQ(parsed["body"]["reason"], "breakpoint");
    EXPECT_EQ(parsed["body"]["threadId"], 1);
}

TEST_F(DapDebugUITest, SendResponseCreatesValidResponse) {
    // Capture stderr from constructor
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr(); // Discard
    
    testing::internal::CaptureStdout();
    
    json body;
    body["supportsConfigurationDoneRequest"] = true;
    
    debugUI.send_response(5, "initialize", true, body);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Parse the output to verify structure
    size_t body_start = output.find("\r\n\r\n") + 4;
    std::string json_str = output.substr(body_start);
    json parsed = json::parse(json_str);
    
    EXPECT_EQ(parsed["type"], "response");
    EXPECT_EQ(parsed["command"], "initialize");
    EXPECT_EQ(parsed["request_seq"], 5);
    EXPECT_TRUE(parsed["success"].get<bool>());
    EXPECT_TRUE(parsed["body"]["supportsConfigurationDoneRequest"].get<bool>());
}

// ============================================================
// Request Handler Tests
// ============================================================

TEST_F(DapDebugUITest, HandleInitializeRequest) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    json request;
    request["seq"] = 1;
    request["type"] = "request";
    request["command"] = "initialize";
    request["arguments"] = json::object();
    
    // Capture stdout to check response
    testing::internal::CaptureStdout();
    
    debugUI.handle_initialize(request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Verify we got a response back
    EXPECT_TRUE(output.find("Content-Length: ") == 0);

    // initialize sends a response + an initialized event
    auto msgs = parse_dap_stream(output);
    ASSERT_GE(msgs.size(), 2u);

    json response = msgs[0];
    EXPECT_EQ(response["type"], "response");
    EXPECT_EQ(response["command"], "initialize");
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_TRUE(response["body"].contains("supportsConfigurationDoneRequest"));

    json evt = msgs[1];
    EXPECT_EQ(evt["type"], "event");
    EXPECT_EQ(evt["event"], "initialized");
}

TEST_F(DapDebugUITest, HandleLaunchRequest) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    json request;
    request["seq"] = 2;
    request["type"] = "request";
    request["command"] = "launch";
    request["arguments"]["program"] = "test.t3";
    request["arguments"]["stopOnEntry"] = false;
    
    // Capture stdout
    testing::internal::CaptureStdout();
    
    debugUI.handle_launch(request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Verify response
    EXPECT_TRUE(output.find("\"command\":\"launch\"") != std::string::npos);
    EXPECT_TRUE(output.find("\"success\":true") != std::string::npos);
}

TEST_F(DapDebugUITest, HandleThreadsRequest) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    json request;
    request["seq"] = 3;
    request["type"] = "request";
    request["command"] = "threads";
    
    // Capture stdout
    testing::internal::CaptureStdout();
    
    debugUI.handle_threads(request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    auto msgs = parse_dap_stream(output);
    ASSERT_EQ(msgs.size(), 1u);
    json response = msgs[0];
    
    EXPECT_EQ(response["command"], "threads");
    EXPECT_TRUE(response["success"].get<bool>());
    EXPECT_TRUE(response["body"].contains("threads"));
    EXPECT_TRUE(response["body"]["threads"].is_array());
    EXPECT_EQ(response["body"]["threads"].size(), 1);
    EXPECT_EQ(response["body"]["threads"][0]["id"], 1);
    EXPECT_EQ(response["body"]["threads"][0]["name"], "TADS VM");
}

TEST_F(DapDebugUITest, HandleDisconnectRequest) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    json request;
    request["seq"] = 4;
    request["type"] = "request";
    request["command"] = "disconnect";
    
    // Capture stdout
    testing::internal::CaptureStdout();
    
    debugUI.handle_disconnect(request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    EXPECT_TRUE(output.find("\"command\":\"disconnect\"") != std::string::npos);
    EXPECT_TRUE(output.find("\"success\":true") != std::string::npos);
}

// ============================================================
// State Management Tests
// ============================================================

TEST_F(DapDebugUITest, InitialStateCorrect) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    // The debugUI should not be paused initially
    EXPECT_FALSE(debugUI.is_pause_requested());
}

TEST_F(DapDebugUITest, PauseRequestSetsFlag) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    json request;
    request["seq"] = 5;
    request["type"] = "request";
    request["command"] = "pause";
    
    // Capture stdout (response)
    testing::internal::CaptureStdout();
    
    debugUI.handle_pause(request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Verify pause was acknowledged
    EXPECT_TRUE(output.find("\"command\":\"pause\"") != std::string::npos);

    // Also verify internal state is updated (friend access)
    EXPECT_TRUE(pause_requested(debugUI));
}

TEST_F(DapDebugUITest, HandleSetBreakpointsUsesMockDebugInfo) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    // Provide a minimal ctx_ so handlers can use it.
    auto *ctx = new dbgcxdef();
    ctx->vmg = VMGLOB_ADDR;
    ctx->in_debugger = 1;
    ctx->stepping_mode = STEP_NONE;
    set_ctx(debugUI, ctx);

    json request;
    request["seq"] = 10;
    request["type"] = "request";
    request["command"] = "setBreakpoints";
    request["arguments"]["source"]["path"] = "/mock/test.t";
    request["arguments"]["lines"] = json::array({42});

    testing::internal::CaptureStdout();
    debugUI.handle_set_breakpoints(request);
    std::string output = testing::internal::GetCapturedStdout();

    auto msgs = parse_dap_stream(output);
    ASSERT_EQ(msgs.size(), 1u);
    json resp = msgs[0];

    EXPECT_EQ(resp["command"], "setBreakpoints");
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("breakpoints"));
    ASSERT_EQ(resp["body"]["breakpoints"].size(), 1);
    EXPECT_TRUE(resp["body"]["breakpoints"][0]["verified"].get<bool>());
    EXPECT_EQ(resp["body"]["breakpoints"][0]["line"], 42);
    EXPECT_EQ(resp["body"]["breakpoints"][0]["id"], 1);

    delete get_ctx(debugUI);
    set_ctx(debugUI, nullptr);
}

TEST_F(DapDebugUITest, HandleSetBreakpointsUnknownFileReturnsUnverified) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    auto *ctx = new dbgcxdef();
    ctx->vmg = VMGLOB_ADDR;
    ctx->in_debugger = 1;
    ctx->stepping_mode = STEP_NONE;
    set_ctx(debugUI, ctx);

    json request;
    request["seq"] = 11;
    request["type"] = "request";
    request["command"] = "setBreakpoints";
    request["arguments"]["source"]["path"] = "/mock/does_not_exist.t";
    request["arguments"]["lines"] = json::array({1});

    testing::internal::CaptureStdout();
    debugUI.handle_set_breakpoints(request);
    std::string output = testing::internal::GetCapturedStdout();

    auto msgs = parse_dap_stream(output);
    ASSERT_EQ(msgs.size(), 1u);
    json resp = msgs[0];
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_EQ(resp["body"]["breakpoints"].size(), 1);
    EXPECT_FALSE(resp["body"]["breakpoints"][0]["verified"].get<bool>());

    delete get_ctx(debugUI);
    set_ctx(debugUI, nullptr);
}

TEST_F(DapDebugUITest, HandleStackTraceUsesMockSourceInfo) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    auto *ctx = new dbgcxdef();
    ctx->vmg = VMGLOB_ADDR;
    ctx->in_debugger = 1;
    ctx->stepping_mode = STEP_NONE;
    set_ctx(debugUI, ctx);
    set_image_file(debugUI, "/mock/game.t3");

    json request;
    request["seq"] = 20;
    request["type"] = "request";
    request["command"] = "stackTrace";
    request["arguments"]["startFrame"] = 0;
    request["arguments"]["levels"] = 5;

    testing::internal::CaptureStdout();
    debugUI.handle_stack_trace(request);
    std::string output = testing::internal::GetCapturedStdout();

    auto msgs = parse_dap_stream(output);
    ASSERT_EQ(msgs.size(), 1u);
    json resp = msgs[0];
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("stackFrames"));
    ASSERT_EQ(resp["body"]["stackFrames"].size(), 1);
    EXPECT_EQ(resp["body"]["stackFrames"][0]["id"], 1000);
    EXPECT_EQ(resp["body"]["stackFrames"][0]["line"], 42);
    EXPECT_EQ(resp["body"]["stackFrames"][0]["name"], "test.t");
    EXPECT_EQ(resp["body"]["totalFrames"], 1);

    delete get_ctx(debugUI);
    set_ctx(debugUI, nullptr);
}

TEST_F(DapDebugUITest, HandleEvaluateReturnsMockResult) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    auto *ctx = new dbgcxdef();
    ctx->vmg = VMGLOB_ADDR;
    ctx->in_debugger = 1;
    ctx->stepping_mode = STEP_NONE;
    set_ctx(debugUI, ctx);

    json request;
    request["seq"] = 30;
    request["type"] = "request";
    request["command"] = "evaluate";
    request["arguments"]["expression"] = "1+1";
    request["arguments"]["frameId"] = 1000;

    testing::internal::CaptureStdout();
    debugUI.handle_evaluate(request);
    std::string output = testing::internal::GetCapturedStdout();

    auto msgs = parse_dap_stream(output);
    ASSERT_EQ(msgs.size(), 1u);
    json resp = msgs[0];
    EXPECT_TRUE(resp["success"].get<bool>());
    EXPECT_TRUE(resp["body"]["result"].get<std::string>().find("<mock result") != std::string::npos);

    delete get_ctx(debugUI);
    set_ctx(debugUI, nullptr);
}

TEST_F(DapDebugUITest, SteppingRequestsUpdateContextAndResumeExecution) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    auto *ctx = new dbgcxdef();
    ctx->vmg = VMGLOB_ADDR;
    ctx->in_debugger = 1;
    ctx->stepping_mode = STEP_NONE;
    set_ctx(debugUI, ctx);

    json req;
    req["type"] = "request";

    // stepIn
    req["seq"] = 40;
    req["command"] = "stepIn";
    testing::internal::CaptureStdout();
    debugUI.handle_step_in(req);
    (void)testing::internal::GetCapturedStdout();
    EXPECT_EQ(get_ctx(debugUI)->stepping_mode, STEP_INTO);
    EXPECT_EQ(get_ctx(debugUI)->in_debugger, 0);

    // next
    get_ctx(debugUI)->in_debugger = 1;
    req["seq"] = 41;
    req["command"] = "next";
    testing::internal::CaptureStdout();
    debugUI.handle_next(req);
    (void)testing::internal::GetCapturedStdout();
    EXPECT_EQ(get_ctx(debugUI)->stepping_mode, STEP_OVER);
    EXPECT_EQ(get_ctx(debugUI)->in_debugger, 0);

    // stepOut
    get_ctx(debugUI)->in_debugger = 1;
    req["seq"] = 42;
    req["command"] = "stepOut";
    testing::internal::CaptureStdout();
    debugUI.handle_step_out(req);
    (void)testing::internal::GetCapturedStdout();
    EXPECT_EQ(get_ctx(debugUI)->stepping_mode, STEP_OUT);
    EXPECT_EQ(get_ctx(debugUI)->in_debugger, 0);

    // continue clears stepping mode
    get_ctx(debugUI)->in_debugger = 1;
    req["seq"] = 43;
    req["command"] = "continue";
    testing::internal::CaptureStdout();
    debugUI.handle_continue(req);
    (void)testing::internal::GetCapturedStdout();
    EXPECT_EQ(get_ctx(debugUI)->stepping_mode, STEP_NONE);
    EXPECT_EQ(get_ctx(debugUI)->in_debugger, 0);

    delete get_ctx(debugUI);
    set_ctx(debugUI, nullptr);
}

TEST_F(DapDebugUITest, ScopesReturnsLocalsAndVariablesEnumerateMockLocals) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    // Ensure helper_ exists so variables handler succeeds.
    set_helper(debugUI, std::make_unique<CFrobDebugHelper>());
    auto *ctx = new dbgcxdef();
    ctx->vmg = VMGLOB_ADDR;
    ctx->in_debugger = 1;
    ctx->stepping_mode = STEP_NONE;
    set_ctx(debugUI, ctx);
    get_helper(debugUI)->init(get_ctx(debugUI));

    // scopes
    json scopes_req;
    scopes_req["seq"] = 50;
    scopes_req["type"] = "request";
    scopes_req["command"] = "scopes";
    scopes_req["arguments"]["frameId"] = 1000;

    testing::internal::CaptureStdout();
    debugUI.handle_scopes(scopes_req);
    std::string scopes_out = testing::internal::GetCapturedStdout();
    auto scopes_msgs = parse_dap_stream(scopes_out);
    ASSERT_EQ(scopes_msgs.size(), 1u);
    json scopes_resp = scopes_msgs[0];
    EXPECT_TRUE(scopes_resp["success"].get<bool>());
    ASSERT_EQ(scopes_resp["body"]["scopes"].size(), 1);
    EXPECT_EQ(scopes_resp["body"]["scopes"][0]["name"], "Locals");
    EXPECT_EQ(scopes_resp["body"]["scopes"][0]["variablesReference"], 1000);

    // variables
    json vars_req;
    vars_req["seq"] = 51;
    vars_req["type"] = "request";
    vars_req["command"] = "variables";
    vars_req["arguments"]["variablesReference"] = 1000;

    testing::internal::CaptureStdout();
    debugUI.handle_variables(vars_req);
    std::string vars_out = testing::internal::GetCapturedStdout();
    auto vars_msgs = parse_dap_stream(vars_out);
    ASSERT_EQ(vars_msgs.size(), 1u);
    json vars_resp = vars_msgs[0];
    EXPECT_TRUE(vars_resp["success"].get<bool>());
    ASSERT_TRUE(vars_resp["body"].contains("variables"));
    ASSERT_GE(vars_resp["body"]["variables"].size(), 2);
    EXPECT_EQ(vars_resp["body"]["variables"][0]["name"], "localVar");

    get_helper(debugUI)->terminate();
    set_helper(debugUI, std::unique_ptr<CFrobDebugHelper>());
    delete get_ctx(debugUI);
    set_ctx(debugUI, nullptr);
}

TEST_F(DapDebugUITest, ListLinkedFilesReturnsMockFiles) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();

    json req;
    req["seq"] = 60;
    req["type"] = "request";
    req["command"] = "frobd.listLinkedFiles";

    testing::internal::CaptureStdout();
    debugUI.handle_list_linked_files(req);
    std::string out = testing::internal::GetCapturedStdout();
    auto msgs = parse_dap_stream(out);
    ASSERT_EQ(msgs.size(), 1u);
    json resp = msgs[0];
    EXPECT_TRUE(resp["success"].get<bool>());
    ASSERT_TRUE(resp["body"].contains("files"));
    EXPECT_GE(resp["body"]["files"].size(), 2);
}

// ============================================================
// JSON Parsing Tests
// ============================================================

TEST_F(DapDebugUITest, ProcessRequestDispatchesCorrectly) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    // Test that different commands are routed to appropriate handlers
    json init_request;
    init_request["seq"] = 1;
    init_request["type"] = "request";
    init_request["command"] = "initialize";
    init_request["arguments"] = json::object();
    
    // Capture stdout
    testing::internal::CaptureStdout();
    
    debugUI.process_request(init_request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Should see initialize response
    EXPECT_TRUE(output.find("initialize") != std::string::npos);
}

TEST_F(DapDebugUITest, UnknownCommandReturnsError) {
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    json request;
    request["seq"] = 99;
    request["type"] = "request";
    request["command"] = "unknownCommand";
    
    // Capture stdout
    testing::internal::CaptureStdout();
    
    debugUI.process_request(request);
    
    std::string output = testing::internal::GetCapturedStdout();
    
    // Should see a response, though success may be false
    EXPECT_TRUE(output.find("Content-Length: ") == 0);
    
    // Parse and check it's marked as failure
    size_t body_start = output.find("\r\n\r\n") + 4;
    std::string json_str = output.substr(body_start);
    json response = json::parse(json_str);
    
    EXPECT_EQ(response["type"], "response");
    EXPECT_FALSE(response["success"].get<bool>());
}

// ============================================================
// Integration Tests (require mock VM setup)
// ============================================================

TEST_F(DapDebugUITest, ConstructorSetsUpSignalHandlers) {
    // Just verify we can construct without crashing
    testing::internal::CaptureStderr();
    CDapDebugUITestable debugUI;
    testing::internal::GetCapturedStderr();
    
    // Signal handlers should be installed
    // (Hard to test directly without sending actual signals)
    SUCCEED();
}

TEST_F(DapDebugUITest, DestructorCleansUp) {
    // Create and destroy in scope
    testing::internal::CaptureStderr();
    {
        CDapDebugUITestable debugUI;
    }
    testing::internal::GetCapturedStderr();
    
    // Should not crash
    SUCCEED();
}

// (No custom main: we link with GTest::gtest_main.)
