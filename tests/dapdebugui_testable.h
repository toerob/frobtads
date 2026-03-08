/*
 * dapdebugui_testable.h - Test wrapper for DAP Debug UI
 * 
 * This header provides access to private members for testing purposes.
 * DO NOT use this in production code - only in tests.
 */

#ifndef DAPDEBUGUI_TESTABLE_H
#define DAPDEBUGUI_TESTABLE_H

// Forward declare the test class before including the implementation
class DapDebugUITest;

// Now include the actual header
#include "../src/dap/dapdebugui.h"

// Create a test-friendly wrapper that exposes private methods
class CDapDebugUITestable : public CDapDebugUI {
public:
    // Expose send_message for testing
    using CDapDebugUI::send_message;
    using CDapDebugUI::send_event;
    using CDapDebugUI::send_response;
    using CDapDebugUI::process_request;
    using CDapDebugUI::handle_initialize;
    using CDapDebugUI::handle_launch;
    using CDapDebugUI::handle_attach;
    using CDapDebugUI::handle_disconnect;
    using CDapDebugUI::handle_set_breakpoints;
    using CDapDebugUI::handle_configuration_done;
    using CDapDebugUI::handle_continue;
    using CDapDebugUI::handle_next;
    using CDapDebugUI::handle_step_in;
    using CDapDebugUI::handle_step_out;
    using CDapDebugUI::handle_pause;
    using CDapDebugUI::handle_stack_trace;
    using CDapDebugUI::handle_scopes;
    using CDapDebugUI::handle_variables;
    using CDapDebugUI::handle_evaluate;
    using CDapDebugUI::handle_threads;
    using CDapDebugUI::handle_list_linked_files;
};

#endif /* DAPDEBUGUI_TESTABLE_H */
