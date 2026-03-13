
cmake_minimum_required(VERSION 3.10)

# GoogleTest requires at least C++14
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Disable POSIX regex in GoogleTest (fixes macOS compilation issues)
add_definitions(-DGTEST_HAS_POSIX_RE=0)

# Fetch GoogleTest from GitHub
include(FetchContent)
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/tads2
    ${PROJECT_SOURCE_DIR}/tads3
    ${PROJECT_SOURCE_DIR}/tads3/unix
    ${PROJECT_BINARY_DIR}
)

# Coverage support
option(ENABLE_COVERAGE "Enable coverage reporting" OFF)
option(OPEN_COVERAGE_REPORT "Open coverage report in browser after generation" ON)
if(NOT OPEN_COVERAGE_REPORT_DEFAULT_MIGRATED)
    set(OPEN_COVERAGE_REPORT ON CACHE BOOL "Open coverage report in browser after generation" FORCE)
    set(OPEN_COVERAGE_REPORT_DEFAULT_MIGRATED TRUE CACHE INTERNAL "One-time migration for OPEN_COVERAGE_REPORT default")
endif()

# "make tests"
# Custom target to run tests easily
add_custom_target(tests

    # Remove old coverage files
    COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete

    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all tests with ctest"
    DEPENDS dapdebugui_basic_test dapdebugui_test
)
# TODO: remove textutils?


# Test executable for dapdebugui - basic protocol tests
add_executable(
    dapdebugui_basic_test
    dapdebugui_basic_test.cpp
    ../src/dap/dap_framing.cc
)
target_link_libraries(dapdebugui_basic_test GTest::gtest_main)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(dapdebugui_basic_test PRIVATE --coverage)
        target_link_options(dapdebugui_basic_test PRIVATE --coverage)
    endif()
endif()


# Discover tests
include(GoogleTest)
gtest_discover_tests(dapdebugui_basic_test DISCOVERY_MODE PRE_TEST)

# DAP Debug UI integration-ish unit tests (compiled with VM stubs)
add_executable(
    dapdebugui_test
    dapdebugui_test.cpp
    dapdebugui_testable.h
    mocks/mock_vm.cpp
    ../src/dap/dapdebugui.cc
    ../src/dap/dapdebugui_io.cc
    ../src/dap/dap_framing.cc
)
target_link_libraries(dapdebugui_test GTest::gtest_main)

# Ensure our stub VM headers in tests/mocks are preferred over the real TADS3
# headers when building this test binary.
target_include_directories(dapdebugui_test BEFORE PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/mocks
)

if(ENABLE_COVERAGE)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(dapdebugui_test PRIVATE --coverage)
        target_link_options(dapdebugui_test PRIVATE --coverage)
    endif()
endif()

gtest_discover_tests(dapdebugui_test DISCOVERY_MODE PRE_TEST)

# (Optional) Full integration tests and example tests can be added similarly if needed

# "make coverage"
# Coverage report generation target
if(ENABLE_COVERAGE)
    find_program(LCOV_EXEC lcov)
    find_program(GENHTML_EXEC genhtml)
    if(LCOV_EXEC AND GENHTML_EXEC)
        set(COVERAGE_REPORT_INDEX ${CMAKE_BINARY_DIR}/tests/coverage_html/index.html)
        set(COVERAGE_OPEN_COMMAND "")
        if(OPEN_COVERAGE_REPORT)
            if(APPLE)
                set(COVERAGE_OPEN_COMMAND COMMAND open ${COVERAGE_REPORT_INDEX})
            elseif(WIN32)
                set(COVERAGE_OPEN_COMMAND COMMAND cmd /c start "" "${COVERAGE_REPORT_INDEX}")
            else()
                find_program(XDG_OPEN_EXEC xdg-open)
                if(XDG_OPEN_EXEC)
                    set(COVERAGE_OPEN_COMMAND COMMAND ${XDG_OPEN_EXEC} ${COVERAGE_REPORT_INDEX})
                else()
                    message(STATUS "OPEN_COVERAGE_REPORT is ON, but no opener command found (e.g. xdg-open).")
                endif()
            endif()
        endif()

        add_custom_target(coverage

            # Remove old coverage files
            COMMAND find ${CMAKE_BINARY_DIR} -name "*.gcda" -delete

            COMMAND ${CMAKE_CTEST_COMMAND} --quiet --output-on-failure
            # Keep lcov/genhtml robust with mixed toolchain debug metadata.
            # lcov can emit paired diagnostics such as:
            #   "(inconsistent) ... Cannot derive function end line"
            #   "(inconsistent) ... failed to set end line for function"
            # and recommends duplicated suppression tokens
            # (e.g. inconsistent,inconsistent / unsupported,unsupported).
            COMMAND ${LCOV_EXEC} --quiet --capture --directory . --base-directory ${CMAKE_SOURCE_DIR} --no-external --output-file coverage.info --exclude '*/_deps/*' --exclude '*/tests/*_test.cpp*' --exclude '*/mocks/*' --ignore-errors format,unsupported,inconsistent,inconsistent
            COMMAND ${GENHTML_EXEC} --quiet coverage.info --output-directory coverage_html --ignore-errors inconsistent,inconsistent,corrupt,category,category,unsupported,unsupported
            ${COVERAGE_OPEN_COMMAND}
            
            COMMENT "Running tests and generating coverage report with lcov and genhtml"
            DEPENDS dapdebugui_basic_test dapdebugui_test
        )
    else()
        message(WARNING "lcov or genhtml not found, coverage report target will not be available.")
    endif()
endif()

