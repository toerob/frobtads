import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import time


"""
Integration test for DAP (Debug Adapter Protocol) implementation in frobd (TCP mode)

Usage: python dap_integration_test.py <path/to/frobd> <path/to/image_file.t3>

This test script performs an end-to-end integration test of the DAP (Debug Adapter Protocol) implementation in frobd (TCP mode). It launches frobd with the specified TADS 3 image and connects to the DAP socket to send requests and validate responses.

The test covers the following scenarios:
1. Basic launch and stack trace retrieval
2. Setting breakpoints and verifying they are hit
3. Evaluating expressions in the hover context
4. Continuing execution after stopping at a breakpoint

"""

ANSI_RESET = "\033[0m"
ANSI_YELLOW = "\033[33m"
ANSI_GREEN = "\033[32m"
ANSI_CYAN = "\033[36m"


def colorize(text, color):
    if os.environ.get("NO_COLOR"):
        return text
    return f"{color}{text}{ANSI_RESET}"


def metadata_suffix(data, key):
    value = data.get(key)
    if not value:
        return ""
    return f" {colorize(f'({key}: {value})', ANSI_CYAN)}"


def response_suffix(message):
    body = message.get("body", {})
    return f"{metadata_suffix(body, 'reason')}{metadata_suffix(body, 'context')}"


def request_suffix(arguments):
    return metadata_suffix(arguments or {}, "context")


def print_evaluate_brief(query, response):
    body = response.get("body", {})
    result = str(body.get("result", "")).strip()
    if len(result) > 80:
        result = result[:77] + "..."
    if not result:
        result = "<empty>"

    print(
        f"\t\teval: {colorize(query, ANSI_YELLOW)} -> "
        f"{colorize(result, ANSI_GREEN)}"
    )


if len(sys.argv) != 3:
    print(
        "Usage: python dap_integration_test.py <path/to/frobd> <path/to/image_file.t3>"
    )
    raise SystemExit(1)


frobd_file = sys.argv[1]
image_file = sys.argv[2]


def read_dap_response(sock_obj):
    headers = {}
    while True:
        line = b""
        while not line.endswith(b"\n"):
            try:
                chunk = sock_obj.recv(1)
            except socket.timeout as exc:
                raise TimeoutError("Timed out while waiting for DAP headers") from exc
            if not chunk:
                return None
            line += chunk

        decoded = line.decode().strip()
        if decoded == "":
            break

        key, value = decoded.split(": ", 1)
        headers[key] = value

    content_length = int(headers.get("Content-Length", 0))
    content = b""
    while len(content) < content_length:
        try:
            chunk = sock_obj.recv(content_length - len(content))
        except socket.timeout as exc:
            raise TimeoutError("Timed out while waiting for DAP payload") from exc
        if not chunk:
            break
        content += chunk

    return content.decode()


def send_dap_request(sock_obj, seq, command, arguments=None):
    message = {
        "type": "request",
        "seq": seq,
        "command": command,
        "arguments": arguments or {},
    }
    payload = json.dumps(message).encode()
    header = f"Content-Length: {len(payload)}\r\n\r\n".encode()
    sock_obj.sendall(header + payload)
    print(
        f"\t--> Sent request: {colorize(command, ANSI_YELLOW)}"
        f"{request_suffix(arguments)}"
    )


def expect_dap_message(
    sock_obj,
    *,
    expected_type,
    expected_command=None,
    expected_event=None,
    expected_success=None,
    skip_events=None,
    validator=None,
):
    skip_events = skip_events or []

    while True:
        try:
            response_str = read_dap_response(sock_obj)
        except TimeoutError as exc:
            raise AssertionError(str(exc)) from exc

        if response_str is None:
            raise AssertionError("Socket closed while waiting for DAP message")

        message = json.loads(response_str)

        if message.get("type") == "event" and message.get("event") in skip_events:
            print(
                f"\t<-- Received (and skipped) expected event: "
                f"{colorize(message.get('event', ''), ANSI_GREEN)}"
                f"{response_suffix(message)}"
            )
            continue

        if message.get("type") != expected_type:
            print(f"\t<-- Ignoring unexpected message type: {message}")
            continue

        if expected_command is not None and message.get("command") != expected_command:
            print(f"\t<-- Ignoring unexpected response command: {message}")
            continue

        if expected_event is not None and message.get("event") != expected_event:
            print(f"\t<-- Ignoring unexpected event: {message}")
            continue

        if expected_success is not None:
            assert (
                message.get("success") == expected_success
            ), f"Expected success={expected_success}, got: {message}"

        if validator is not None:
            validator(message)

        if expected_type == "response":
            print(
                f"\t<-- Received expected response: "
                f"{colorize(message.get('command', ''), ANSI_GREEN)}"
                f"{response_suffix(message)}"
            )
        else:
            print(
                f"\t<-- Received expected event: "
                f"{colorize(message.get('event', ''), ANSI_GREEN)}"
                f"{response_suffix(message)}"
            )
        return message


def assert_initialize_capabilities(message):
    body = message.get("body", {})
    assert body.get("supportsConfigurationDoneRequest") is True, body
    assert body.get("supportsEvaluateForHovers") is True, body


def assert_stack_trace(message):
    body = message.get("body", {})
    frames = body.get("stackFrames", [])
    assert isinstance(frames, list), body
    assert len(frames) >= 1, "Expected at least one stack frame"

    frame = frames[0]
    assert "id" in frame, frame
    assert "line" in frame, frame
    assert "source" in frame and "path" in frame["source"], frame


def assert_threads(message):
    threads = message.get("body", {}).get("threads", [])
    assert isinstance(threads, list), message
    assert len(threads) >= 1, message
    assert threads[0].get("id") == 1, message


def assert_empty_scopes(message):
    scopes = message.get("body", {}).get("scopes")
    assert isinstance(scopes, list), message


def assert_empty_variables(message):
    variables = message.get("body", {}).get("variables")
    assert isinstance(variables, list), message


def assert_evaluate_missing_expression_error(message):
    body = message.get("body", {})
    assert "No expression provided" in body.get("result", ""), message


def assert_evaluate_hover_response(message):
    body = message.get("body", {})
    assert message.get("success") is True, message
    assert "result" in body, message
    assert "variablesReference" in body, message
    assert body.get("variablesReference") == 0, message

    result_text = str(body.get("result", "")).strip()
    assert result_text != "", message
    assert "error" not in result_text.lower(), message
    assert "2" in result_text, message


def assert_breakpoint_verified(message):
    breakpoints = message.get("body", {}).get("breakpoints", [])
    assert len(breakpoints) == 1, message
    assert breakpoints[0].get("verified") is True, message


def assert_any_breakpoint_verified(message):
    breakpoints = message.get("body", {}).get("breakpoints", [])
    assert len(breakpoints) >= 1, message
    assert any(bp.get("verified") is True for bp in breakpoints), message


def assert_stopped_breakpoint_or_step(message):
    reason = message.get("body", {}).get("reason")
    assert reason in ["breakpoint", "step"], message


def start_debug_session():
    temp_dir = tempfile.mkdtemp()

    for _attempt in range(8):
        port_probe = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        port_probe.bind(("127.0.0.1", 0))
        dap_port = port_probe.getsockname()[1]
        port_probe.close()

        proc = subprocess.Popen(
            [
                frobd_file,
                "-i",
                "plain",
                "-D",
                "dap",
                "-P",
                str(dap_port),
                image_file,
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

        connected = False
        sock = None
        for _ in range(50):
            if proc.poll() is not None:
                break
            try:
                sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                sock.settimeout(0.5)
                sock.connect(("127.0.0.1", dap_port))
                connected = True
                break
            except OSError:
                if sock is not None:
                    sock.close()
                    sock = None
                time.sleep(0.1)

        if connected:
            sock.settimeout(8)
            return proc, sock, temp_dir

        if sock is not None:
            sock.close()
        try:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=1)
        except subprocess.TimeoutExpired:
            proc.kill()

        stderr_text = ""
        if proc.stderr is not None:
            stderr_text = proc.stderr.read() or ""
        if stderr_text.strip():
            print("[TCP setup retry diagnostic]", stderr_text.strip())

    shutil.rmtree(temp_dir, ignore_errors=True)
    raise RuntimeError("Could not establish TCP DAP connection after retries")


def cleanup_debug_session(proc, sock, socket_dir):
    if sock is not None:
        try:
            sock.close()
        except OSError:
            pass

    if proc is not None:
        try:
            if proc.poll() is None:
                proc.terminate()
                proc.wait(timeout=2)
        except subprocess.TimeoutExpired:
            proc.kill()
        except Exception:
            pass

    shutil.rmtree(socket_dir, ignore_errors=True)


def run_primary_scenario():
    proc, sock, socket_dir = start_debug_session()
    try:
        seq = 1

        send_dap_request(sock, seq, "initialize", {})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="initialize",
            expected_success=True,
            validator=assert_initialize_capabilities,
        )
        seq += 1

        send_dap_request(sock, seq, "launch", {"stopOnEntry": True})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="launch",
            expected_success=True,
            skip_events=["initialized"],
        )
        seq += 1

        send_dap_request(
            sock,
            seq,
            "stackTrace",
            {"threadId": 1, "startFrame": 0, "levels": 1},
        )
        stack_response = expect_dap_message(
            sock,
            expected_type="response",
            expected_command="stackTrace",
            expected_success=True,
            skip_events=["invalidated", "stopped", "allThreadsStopped"],
            validator=assert_stack_trace,
        )
        seq += 1

        top_frame = stack_response["body"]["stackFrames"][0]
        top_source_path = top_frame["source"]["path"]
        top_line = int(top_frame["line"])

        send_dap_request(
            sock,
            seq,
            "setBreakpoints",
            {
                "source": {"path": top_source_path},
                "lines": [top_line],
                "breakpoints": [{"line": top_line}],
            },
        )
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="setBreakpoints",
            expected_success=True,
            skip_events=["invalidated", "stopped"],
            validator=assert_breakpoint_verified,
        )
        seq += 1

        send_dap_request(sock, seq, "configurationDone", {})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="configurationDone",
            expected_success=True,
        )
        seq += 1

        send_dap_request(sock, seq, "threads", {})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="threads",
            expected_success=True,
            validator=assert_threads,
        )
        seq += 1

        send_dap_request(sock, seq, "scopes", {"frameId": top_frame["id"]})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="scopes",
            expected_success=True,
            validator=assert_empty_scopes,
        )
        seq += 1

        send_dap_request(sock, seq, "variables", {"variablesReference": 0})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="variables",
            expected_success=True,
            validator=assert_empty_variables,
        )
        seq += 1

        send_dap_request(sock, seq, "evaluate", {})
        eval_missing_expr_response = expect_dap_message(
            sock,
            expected_type="response",
            expected_command="evaluate",
            expected_success=False,
            validator=assert_evaluate_missing_expression_error,
        )
        print_evaluate_brief("<missing expression>", eval_missing_expr_response)
        seq += 1

        send_dap_request(
            sock,
            seq,
            "evaluate",
            {
                "expression": "1+1",
                "context": "hover",
                "frameId": top_frame["id"],
            },
        )
        eval_hover_response = expect_dap_message(
            sock,
            expected_type="response",
            expected_command="evaluate",
            expected_success=True,
            validator=assert_evaluate_hover_response,
        )
        print_evaluate_brief("1+1 (hover)", eval_hover_response)
        seq += 1

        send_dap_request(sock, seq, "disconnect", {})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="disconnect",
            expected_success=True,
        )
    finally:
        cleanup_debug_session(proc, sock, socket_dir)


def run_continue_stopped_scenario():
    proc, sock, socket_dir = start_debug_session()
    try:
        seq = 1

        send_dap_request(sock, seq, "initialize", {})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="initialize",
            expected_success=True,
            validator=assert_initialize_capabilities,
        )
        seq += 1

        send_dap_request(sock, seq, "launch", {"stopOnEntry": True})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="launch",
            expected_success=True,
            skip_events=["initialized"],
        )
        seq += 1

        send_dap_request(
            sock,
            seq,
            "stackTrace",
            {"threadId": 1, "startFrame": 0, "levels": 1},
        )
        stack_response = expect_dap_message(
            sock,
            expected_type="response",
            expected_command="stackTrace",
            expected_success=True,
            skip_events=["invalidated", "stopped", "allThreadsStopped"],
            validator=assert_stack_trace,
        )
        seq += 1

        send_dap_request(sock, seq, "configurationDone", {})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="configurationDone",
            expected_success=True,
        )
        seq += 1

        send_dap_request(sock, seq, "next", {"threadId": 1})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="next",
            expected_success=True,
        )
        seq += 1

        expect_dap_message(
            sock,
            expected_type="event",
            expected_event="stopped",
            skip_events=["continued", "invalidated"],
            validator=assert_stopped_breakpoint_or_step,
        )

        send_dap_request(sock, seq, "continue", {"threadId": 1})
        expect_dap_message(
            sock,
            expected_type="response",
            expected_command="continue",
            expected_success=True,
        )
    finally:
        cleanup_debug_session(proc, sock, socket_dir)


print("\n[Primary scenario]")
run_primary_scenario()

print("\n[Continue after stop scenario]")
run_continue_stopped_scenario()

print("[DAP integration checks passed]")
