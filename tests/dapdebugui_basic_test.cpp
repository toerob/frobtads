/*
 * dapdebugui_basic_test.cpp - Basic unit tests for DAP message framing
 *
 * This test suite is intentionally self-contained (no TADS VM linking): it
 * validates the Content-Length framing used by the DAP transport.
 */

#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include "dap/dap_framing.h"
#include "json.hpp"



using json = nlohmann::json;

// ============================================================
// Test Fixtures
// ============================================================
class DapDebugUIBasicTest : public ::testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
    }

    // (No custom helpers: tests should exercise src/dap/dap_framing.*)
};

// ============================================================
// Message Protocol Tests
// ============================================================

TEST_F(DapDebugUIBasicTest, CreateDAPMessageFormatsCorrectly) {
    json test_msg;
    test_msg["type"] = "response";
    test_msg["seq"] = 1;
    test_msg["command"] = "initialize";
    test_msg["success"] = true;
    
    std::string dap_message = dap::frame_json_message(test_msg);
    
    // Verify format
    EXPECT_TRUE(dap_message.find("Content-Length: ") == 0);
    EXPECT_TRUE(dap_message.find("\r\n\r\n") != std::string::npos);
    EXPECT_TRUE(dap_message.find("\"type\":\"response\"") != std::string::npos);
}

TEST_F(DapDebugUIBasicTest, ParseDAPMessageCorrectly) {
    json original;
    original["type"] = "request";
    original["seq"] = 5;
    original["command"] = "launch";
    original["arguments"]["program"] = "test.t3";
    
    std::string dap_message = dap::frame_json_message(original);
    
    json parsed;
    ASSERT_TRUE(dap::parse_framed_json_message(dap_message, parsed));
    
    EXPECT_EQ(original["type"], parsed["type"]);
    EXPECT_EQ(original["seq"], parsed["seq"]);
    EXPECT_EQ(original["command"], parsed["command"]);
    EXPECT_EQ(original["arguments"]["program"], parsed["arguments"]["program"]);
}

TEST_F(DapDebugUIBasicTest, FrameJsonMessageContentLengthMatchesPayload) {
    json msg;
    msg["type"] = "event";
    msg["seq"] = 123;
    msg["event"] = "initialized";

    std::string framed = dap::frame_json_message(msg);

    const std::size_t header_end = framed.find("\r\n\r\n");
    ASSERT_NE(header_end, std::string::npos);

    const std::string header_line = framed.substr(0, framed.find("\r\n"));
    std::size_t content_length = 0;
    ASSERT_TRUE(dap::try_parse_content_length_line(header_line, content_length));

    const std::size_t body_start = header_end + 4;
    ASSERT_LE(body_start, framed.size());
    EXPECT_EQ(content_length, framed.size() - body_start);

    json parsed;
    ASSERT_TRUE(dap::parse_framed_json_message(framed, parsed));
    EXPECT_EQ(parsed["type"], "event");
    EXPECT_EQ(parsed["event"], "initialized");
}

TEST_F(DapDebugUIBasicTest, TryParseContentLengthLineAcceptsSpacesAndTabs) {
    std::size_t len = 0;
    EXPECT_TRUE(dap::try_parse_content_length_line("Content-Length: 12", len));
    EXPECT_EQ(len, 12u);

    len = 0;
    EXPECT_TRUE(dap::try_parse_content_length_line("Content-Length:\t34\t ", len));
    EXPECT_EQ(len, 34u);
}

TEST_F(DapDebugUIBasicTest, TryParseContentLengthLineRejectsInvalid) {
    std::size_t len = 0;
    EXPECT_FALSE(dap::try_parse_content_length_line("content-length: 5", len));
    EXPECT_FALSE(dap::try_parse_content_length_line("Content-Length:", len));
    EXPECT_FALSE(dap::try_parse_content_length_line("Content-Length: abc", len));
    EXPECT_FALSE(dap::try_parse_content_length_line("Content-Length: 12xyz", len));
}

TEST_F(DapDebugUIBasicTest, InvalidMessageMissingContentLength) {
    std::string invalid_message = "\r\n{\"type\":\"request\"}";
    json parsed;
    EXPECT_FALSE(dap::parse_framed_json_message(invalid_message, parsed));
}

TEST_F(DapDebugUIBasicTest, InvalidMessageMissingBody) {
    std::string invalid_message = "Content-Length: 20\r\n";
    json parsed;
    EXPECT_FALSE(dap::parse_framed_json_message(invalid_message, parsed));
}

TEST_F(DapDebugUIBasicTest, InvalidMessageContentLengthTooLarge) {
    // Header says 10 bytes but body only contains 2 bytes.
    std::string invalid_message = "Content-Length: 10\r\n\r\n{}";
    json parsed;
    EXPECT_FALSE(dap::parse_framed_json_message(invalid_message, parsed));
}

TEST_F(DapDebugUIBasicTest, InvalidMessageBodyIsNotJson) {
    std::string invalid_message = "Content-Length: 4\r\n\r\nnope";
    json parsed;
    EXPECT_FALSE(dap::parse_framed_json_message(invalid_message, parsed));
}

// Note: we link with GTest::gtest_main, so no custom main() here.
