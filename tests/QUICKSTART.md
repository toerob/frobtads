# Testing Setup Summary

This repo uses CMake + CTest with GoogleTest. Tests are optional and gated by the `ENABLE_TESTS` CMake option.

## Configure + Build

From the repository root:

```bash
cmake -S . -B build -DENABLE_TESTS=ON
cmake --build build
```

## Run Tests

Run everything:

```bash
ctest --test-dir build --output-on-failure
```

Run only the DAP-related tests (useful while iterating on `dapdebugui_*`):

```bash
ctest --test-dir build -R DapDebugUI --output-on-failure
```

Run a single test binary directly (from the build directory):

```bash
cd build
./tests/dapdebugui_test
./tests/dapdebugui_test --gtest_filter='DapDebugUITest.*'
./tests/dapdebugui_basic_test --gtest_filter='DapDebugUIBasicTest.*'
```

## Coverage (`make coverage`)

There is a `coverage` build target (often run as `make coverage`) that runs the unit tests and generates an HTML report via `lcov` + `genhtml`.

Prerequisites:

- `lcov` and `genhtml` available on PATH

Configure with coverage enabled:

```bash
cmake -S . -B build -DENABLE_TESTS=ON -DENABLE_COVERAGE=ON
cmake --build build
```

Generate a report:

```bash
cmake --build build --target coverage
```

Notes:

- The report is written under `build/tests/coverage_html/index.html`.
- If `lcov`/`genhtml` aren't found during configuration, the `coverage` target won't be created.
- To prevent auto-opening the report in a browser, configure with `-DOPEN_COVERAGE_REPORT=OFF`.

## Integration testing (via Python)

Two Python integration tests exercise DAP over Unix sockets and over TCP.

Prerequisites:

- A built `frobd` binary (commonly `./build/frobd`)
- A compiled TADS3 image file (`.t3`) to run (any small game/program is fine)
- Python 3

Unix domain socket test:

```bash
python3 tests/dap_integration_test_socket.py ./build/frobd /path/to/game.t3
```

## Notes

- `dapdebugui_basic_test` focuses on DAP `Content-Length` framing helpers.
- `dapdebugui_test` compiles the real adapter code against lightweight VM stubs/mocks under `tests/mocks/`.
