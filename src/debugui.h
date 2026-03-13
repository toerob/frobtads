/*
 * debugui.h - Abstract interface for TADS debugger UI implementations
 *
 * This interface allows different debugger frontends to be used with the
 * TADS VM, including:
 *   - DAP (Debug Adapter Protocol) debugger for IDE integration
 *   - Terminal-based debugger
 *   - Custom debugger implementations
 */

#ifndef DEBUGUI_H
#define DEBUGUI_H

#include "vmglob.h"

/*
 * Abstract debugger UI interface
 * This differs from the old CVmDebugUI class in that it is a pure virtual
 * interface, and does not contain any default implementations.  This allows for
 * cleaner separation of different debugger UI implementations, and makes it
 * easier to add new ones in the future without modifying a large base class.
 *
 * All debugger implementations must implement this interface.
 * The VM calls these methods at key points during execution.
 */
class DebuggerUI {
public:
  virtual ~DebuggerUI() = default;

  /* ============================================================
   * Lifecycle Methods
   * ============================================================ */

  /*
   * Initialize the debugger UI
   * Called once at startup before any other methods.
   *
   * Parameters:
   *   vmg_ - VM globals
   *   image_filename - Path to the .t3 image file being debugged
   */
  virtual void init(VMG_ const char *image_filename) = 0;

  /*
   * Post-load initialization
   * Called after the image file has been fully loaded and debug
   * information is available.
   *
   * Parameters:
   *   vmg0_ - VM globals
   */
  virtual void init_after_load(VMG0_) = 0;

  /*
   * Terminate the debugger UI
   * Called before the VM shuts down to allow cleanup of resources.
   *
   * Parameters:
   *   vmg0_ - VM globals
   */
  virtual void terminate(VMG0_) = 0;

  /* ============================================================
   * Debug Event Handlers
   * ============================================================ */

  /*
   * Main debugger command loop
   * Called when execution is paused (breakpoint, step, error, etc).
   * Should not return until execution should resume.
   *
   * Parameters:
   *   vmg_ - VM globals
   *   bp_number - Breakpoint number if stopped at a breakpoint, 0 otherwise
   *   error_code - Error code if stopped due to error, 0 otherwise
   *   pc - Pointer to program counter (can be updated to change execution
   * point)
   */
  virtual void cmd_loop(VMG_ int bp_number, int error_code,
                        const uchar **pc) = 0;

  /* ============================================================
   * Optional Event Notifications
   * These have default implementations that do nothing.
   * ============================================================ */

  /*
   * Notification: Breakpoint was hit
   * Called when execution stops at a breakpoint.
   * Default implementation does nothing.
   */
  virtual void on_breakpoint_hit(VMG_ int bp_num, const char *file,
                                 unsigned long line) {
    /* Default: no-op */
  }

  /*
   * Notification: Step completed
   * Called when a step-in, step-over, or step-out operation completes.
   * Default implementation does nothing.
   */
  virtual void on_step_complete(VMG_ const char *file, unsigned long line) {
    /* Default: no-op */
  }

  /*
   * Notification: Runtime error occurred
   * Called when a runtime error stops execution.
   * Default implementation does nothing.
   */
  virtual void on_error(VMG_ int error_code, const char *message) {
    /* Default: no-op */
  }

  /*
   * Notification: Execution resumed
   * Called when execution continues after being paused.
   * Default implementation does nothing.
   */
  virtual void on_execution_resumed(VMG0_) { /* Default: no-op */ }

  /*
   * Notification: Execution paused
   * Called when execution is paused (before cmd_loop is entered).
   * Default implementation does nothing.
   */
  virtual void on_execution_paused(VMG0_) { /* Default: no-op */ }

  /*
   * Check if a pause has been requested
   * Called periodically by the VM during execution to check if
   * the debugger UI wants to pause execution (e.g., user clicked pause).
   * Returns true if execution should pause, false otherwise.
   * Default implementation returns false (no pause requested).
   */
  virtual bool is_pause_requested() const { return false; }
};

#endif /* DEBUGUI_H */
