/* 
 * dapdebugui_io.cc - I/O implementation for DAP Debug UI
 * 
 * This file contains the socket and TCP communication implementation
 * separate from the main protocol handling.
 */
#include "common.h"
#include "dap/dapdebugui.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <csignal>
#include <poll.h>

/* Reference to external variables defined in dapdebugui.cc */
extern volatile sig_atomic_t g_dap_interrupted;
extern volatile sig_atomic_t g_dap_in_message_loop;
extern CDapDebugUI* g_dap_instance;
extern void dap_signal_handler(int signum);

/* Helper: install signal handler with sigaction (no SA_RESTART) so that
 * blocking syscalls like poll(), read(), accept() are interrupted by signals
 * and return EINTR.  std::signal() on macOS installs with SA_RESTART by
 * default, which prevents signals from breaking out of these calls. */
static void install_signal_handler(int signum, void (*handler)(int))
{
    struct sigaction sa;
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;  // Deliberately no SA_RESTART
    sigaction(signum, &sa, nullptr);
}

/* ============================================================
 * I/O Setup Methods
 * ============================================================ */

void CDapDebugUI::setup_stdio_io()
{
    /* Force stderr to be unbuffered for immediate output */
    std::cerr.setf(std::ios::unitbuf);
    
    /* Set global instance pointer for signal handler */
    g_dap_instance = this;
    
    /* Install signal handlers for graceful shutdown (without SA_RESTART) */
    install_signal_handler(SIGINT, dap_signal_handler);
    install_signal_handler(SIGTERM, dap_signal_handler);
    
    io_mode_ = IOMode::StdIO;
    io_fd_ = STDIN_FILENO;
    
    std::cerr << "[DAP Debug] Starting in DAP mode using stdin/stdout." << std::endl;
    std::cerr << "[DAP Debug] Press Ctrl+C to abort if no client connects." << std::endl;
}

void CDapDebugUI::setup_socket_io(const std::string& path)
{
    /* Force stderr to be unbuffered for immediate output */
    std::cerr.setf(std::ios::unitbuf);
    
    /* Set global instance pointer for signal handler */
    g_dap_instance = this;
    
    /* Install signal handlers for graceful shutdown (without SA_RESTART) */
    install_signal_handler(SIGINT, dap_signal_handler);
    install_signal_handler(SIGTERM, dap_signal_handler);
    
    io_mode_ = IOMode::Socket;
    socket_path_ = path;
    
    std::cerr << "[DAP Debug] Setting up Unix domain socket: " << path << std::endl;
    
    // Create socket
    int sock_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to create socket: " << strerror(errno) << std::endl;
        return;
    }
    
    // Remove existing socket file if it exists
    unlink(path.c_str());
    
    // Bind socket
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, path.c_str(), sizeof(addr.sun_path) - 1);
    
    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to bind socket: " << strerror(errno) << std::endl;
        close(sock_fd);
        return;
    }
    
    // Listen
    if (listen(sock_fd, 1) < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to listen on socket: " << strerror(errno) << std::endl;
        close(sock_fd);
        unlink(path.c_str());
        return;
    }
    
    listen_fd_ = sock_fd;
    
    std::cerr << "[DAP Debug] Waiting for DAP client to connect on: " << path << std::endl;
    std::cerr << "[DAP Debug] Press Ctrl+C to abort." << std::endl;
    
    // Wait for a connection with periodic timeout so signals can interrupt
    int client_fd = -1;
    while (client_fd < 0) {
        if (g_dap_interrupted) {
            std::cerr << "[DAP Debug] Accept interrupted by signal" << std::endl;
            close(sock_fd);
            listen_fd_ = -1;
            unlink(path.c_str());
            return;
        }

        struct pollfd pfd;
        pfd.fd = sock_fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 5000); // 5-second timeout
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[DAP Debug] ERROR: Poll before accept failed: " << strerror(errno) << std::endl;
            close(sock_fd);
            listen_fd_ = -1;
            unlink(path.c_str());
            return;
        }
        if (pr == 0) continue; // Timeout, retry

        client_fd = accept(sock_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) { client_fd = -1; continue; }
            std::cerr << "[DAP Debug] ERROR: Failed to accept connection: " << strerror(errno) << std::endl;
            close(sock_fd);
            listen_fd_ = -1;
            unlink(path.c_str());
            return;
        }
    }
    
    io_fd_ = client_fd;
    std::cerr << "[DAP Debug] DAP client connected via socket." << std::endl;
}

void CDapDebugUI::setup_tcp_io(int port)
{
    /* Force stderr to be unbuffered for immediate output */
    std::cerr.setf(std::ios::unitbuf);
    
    /* Set global instance pointer for signal handler */
    g_dap_instance = this;
    
    /* Install signal handlers for graceful shutdown (without SA_RESTART) */
    install_signal_handler(SIGINT, dap_signal_handler);
    install_signal_handler(SIGTERM, dap_signal_handler);
    
    io_mode_ = IOMode::TCP;
    
    std::cerr << "[DAP Debug] Setting up TCP socket on port " << port << std::endl;
    
    // Create socket
    int sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to create socket: " << strerror(errno) << std::endl;
        return;
    }
    
    // Set SO_REUSEADDR to avoid "address already in use" errors
    int opt = 1;
    setsockopt(sock_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    
    // Bind socket
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    if (bind(sock_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to bind socket: " << strerror(errno) << std::endl;
        close(sock_fd);
        return;
    }
    
    // Listen
    if (listen(sock_fd, 1) < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to listen on socket: " << strerror(errno) << std::endl;
        close(sock_fd);
        return;
    }
    
    listen_fd_ = sock_fd;
    
    std::cerr << "[DAP Debug] Waiting for DAP client to connect on TCP port " << port << std::endl;
    std::cerr << "[DAP Debug] Press Ctrl+C to abort." << std::endl;
    
    // Wait for a connection with periodic timeout so signals can interrupt
    int client_fd = -1;
    while (client_fd < 0) {
        if (g_dap_interrupted) {
            std::cerr << "[DAP Debug] Accept interrupted by signal" << std::endl;
            close(sock_fd);
            listen_fd_ = -1;
            return;
        }

        struct pollfd pfd;
        pfd.fd = sock_fd;
        pfd.events = POLLIN;
        int pr = poll(&pfd, 1, 5000); // 5-second timeout
        if (pr < 0) {
            if (errno == EINTR) continue;
            std::cerr << "[DAP Debug] ERROR: Poll before accept failed: " << strerror(errno) << std::endl;
            close(sock_fd);
            listen_fd_ = -1;
            return;
        }
        if (pr == 0) continue; // Timeout, retry

        client_fd = accept(sock_fd, nullptr, nullptr);
        if (client_fd < 0) {
            if (errno == EINTR) { client_fd = -1; continue; }
            std::cerr << "[DAP Debug] ERROR: Failed to accept connection: " << strerror(errno) << std::endl;
            close(sock_fd);
            listen_fd_ = -1;
            return;
        }
    }
    
    io_fd_ = client_fd;
    std::cerr << "[DAP Debug] DAP client connected via TCP." << std::endl;
}

void CDapDebugUI::close_io()
{
    if (io_mode_ == IOMode::Socket && !socket_path_.empty()) {
        unlink(socket_path_.c_str());
    }
    
    if (io_fd_ >= 0 && io_mode_ != IOMode::StdIO) {
        close(io_fd_);
        io_fd_ = -1;
    }
    
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
}

/* ============================================================
 * I/O Operations
 * ============================================================ */

ssize_t CDapDebugUI::read_bytes(void* buf, size_t count)
{
    int fd = (io_mode_ == IOMode::StdIO) ? STDIN_FILENO : io_fd_;
    while (true) {
        ssize_t n = read(fd, buf, count);
        if (n < 0 && errno == EINTR) {
            // Interrupted by signal — if we should stop, return error;
            // otherwise retry the read.
            if (g_dap_interrupted) return -1;
            continue;
        }
        return n;
    }
}

ssize_t CDapDebugUI::write_bytes(const void* buf, size_t count)
{
    int fd = (io_mode_ == IOMode::StdIO) ? STDOUT_FILENO : io_fd_;
    while (true) {
        ssize_t n = write(fd, buf, count);
        if (n < 0 && errno == EINTR) {
            if (g_dap_interrupted) return -1;
            continue;
        }
        return n;
    }
}
