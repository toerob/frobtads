/*
 * mock_vm.cpp - Mock VM implementation for testing
 */

#include "mock_vm.h"

#include "tct3base.h"

// (tct3base.h defines CTcPrs; keep all stubs local to tests.)

// Global pointers used by the adapter under test
CVmDebugger *G_debugger = nullptr;
CVmSrcfTable *G_srcf_table = nullptr;
CVmCodePool *G_code_pool = nullptr;
CVmInterpreter *G_interpreter = nullptr;

// Global stub for globals-scope compilation (unused in these unit tests)
CTcPrs *G_prs = nullptr;

// Backing mock instances
CVmSrcfTable mock_srcf_table;
CVmCodePool mock_code_pool;
CVmDebugger mock_debugger;
CVmInterpreter mock_interpreter;

void mock_vm_setup() {
    // Point adapter globals at our backing instances
    G_debugger = &mock_debugger;
    G_srcf_table = &mock_srcf_table;
    G_code_pool = &mock_code_pool;
    G_interpreter = &mock_interpreter;
    
    // Add some mock source files
    mock_srcf_table.add_entry("/mock/test.t");
    mock_srcf_table.add_entry("/mock/main.t");
}

void mock_vm_teardown() {
    // Reset globals
    G_debugger = nullptr;
    G_srcf_table = nullptr;
    G_code_pool = nullptr;
    G_interpreter = nullptr;
}
