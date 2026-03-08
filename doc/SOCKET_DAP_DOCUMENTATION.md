# DAP Transport: stdio, Unix socket, TCP

This document is the canonical reference for running `frobd` in DAP mode over stdio, a Unix domain socket, or a TCP socket.

## Overview

`frobd` supports three DAP communication modes:

1. **stdio** (default) - DAP over stdin/stdout
2. **Unix domain socket** - DAP over a local socket file
3. **TCP socket** - DAP over a TCP listener (can be local or remote)

Using sockets instead of stdin/stdout keeps the DAP protocol on a separate channel, which avoids conflicts with interactive terminal input.

## Command-line options

### Common flags

- `-D dap` / `--debug-protocol dap`: enable DAP mode
- `-i plain` / `--interface plain`: recommended for IDE/integration usage (avoids curses UI interactions)

### Unix domain socket

- `-Q <path>` / `--dap-socket <path>`: Unix domain socket path
  - Default: `/tmp/tads-dap.sock`

Example:

```bash
./build/frobd -i plain -D dap --dap-socket /tmp/tads-dap.sock /path/to/game.t3
```

Behavior:

- Creates (and overwrites) the socket file at the given path
- Listens and waits until a DAP client connects
- Runs all DAP traffic over the accepted socket connection

### TCP socket

- `-P <port>` / `--dap-port <port>`: TCP port to listen on

Example:

```bash
./build/frobd -i plain -D dap --dap-port 9876 /path/to/game.t3
```

Behavior:

- Listens on the given port
- Binds to all interfaces by default (`INADDR_ANY`), which enables remote connections

If you enable TCP mode, treat it as a remote-debug interface (choose a safe port, and consider firewalling).

### stdio (legacy / reference)

```bash
./build/frobd -i plain -D dap /path/to/game.t3
```

This uses stdin/stdout for DAP, which can interfere with interactive terminal UI and user input.

## VS Code configuration

The VS Code extension uses the `dapMode`/`dapSocket`/`dapPort` launch configuration keys.

Example `launch.json`:

```json
{
  "type": "tads3",
  "request": "launch",
  "name": "Debug TADS3 (DAP socket)",
  "program": "${workspaceFolder}/game.t3",

  "frobd": "/path/to/frobd",

  "dapMode": "socket",
  "dapSocket": "/tmp/tads-dap.sock"
}
```

TCP mode example:

```json
{
  "type": "tads3",
  "request": "launch",
  "name": "Debug TADS3 (DAP TCP)",
  "program": "${workspaceFolder}/game.t3",

  "frobd": "/path/to/frobd",

  "dapMode": "tcp",
  "dapPort": 9876
}
```

## Implementation notes (frobtads_debugger)

- `src/main.cc`: CLI flags (`-P/--dap-port`, `-Q/--dap-socket`)
- `src/debuguifactory.cc`: selects stdio vs socket vs TCP DAP transport
- `src/dap/dapdebugui_io.cc`: transport setup and fd-backed read/write

### Breakpoint registry synchronization

When the DAP `setBreakpoints` request toggles breakpoints, successful changes are mirrored into the shared helper breakpoint registry.

This keeps breakpoint-related “read views” (for example: terminal breakpoint listings and source-print marker lookup) consistent with breakpoints set or removed via the DAP path.

## Testing (Python)

Two Python integration tests exercise DAP over Unix sockets and over TCP.

Prerequisites:

- A built `frobd` binary (commonly `./build/frobd`)
- A compiled TADS3 image file (`.t3`) to run (any small game/program is fine)
- Python 3

Unix domain socket test:

```bash
python3 tests/dap_integration_test_socket.py ./build/frobd /path/to/game.t3
```

TCP test:

```bash
python3 tests/dap_integration_test_tcp.py ./build/frobd /path/to/game.t3
```

Notes:

- The socket test creates a temporary socket path and starts `frobd` with `--dap-socket <temp>/frobd_dap.sock`.
- The TCP test auto-selects a free port and starts `frobd` with `-P <port>`.
- Set `NO_COLOR=1` if you want non-colored output.

## Testing (manual smoke checks)

If you only want to verify that the socket/port is reachable (not a full DAP exchange), you can connect with `nc`:

```bash
# Unix domain socket
nc -U /tmp/tads-dap.sock

# TCP
nc 127.0.0.1 9876
```

For actual DAP correctness, prefer the Python integration tests above.
