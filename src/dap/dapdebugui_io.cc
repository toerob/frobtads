/* 
 * dapdebugui_io.cc - I/O implementation for DAP Debug UI
 * 
 * This file contains the socket and TCP communication implementation
 * separate from the main protocol handling.
 */

#include "dap/dapdebugui.h"
#include <iostream>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <csignal>

/* Reference to external variables defined in dapdebugui.cc */
extern volatile sig_atomic_t g_dap_interrupted;
extern CDapDebugUI* g_dap_instance;
extern void dap_signal_handler(int signum);

/* ============================================================
 * I/O Setup Methods
 * ============================================================ */

void CDapDebugUI::setup_stdio_io()
{
    /* Force stderr to be unbuffered for immediate output */
    std::cerr.setf(std::ios::unitbuf);
    
    /* Set global instance pointer for signal handler */
    g_dap_instance = this;
    
    /* Install signal handlers for graceful shutdown */
    std::signal(SIGINT, dap_signal_handler);
    std::signal(SIGTERM, dap_signal_handler);
    
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
    
    /* Install signal handlers for graceful shutdown */
    std::signal(SIGINT, dap_signal_handler);
    std::signal(SIGTERM, dap_signal_handler);
    
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
    
    // Accept connection
    int client_fd = accept(sock_fd, nullptr, nullptr);
    if (client_fd < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to accept connection: " << strerror(errno) << std::endl;
        close(sock_fd);
        unlink(path.c_str());
        return;
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
    
    /* Install signal handlers for graceful shutdown */
    std::signal(SIGINT, dap_signal_handler);
    std::signal(SIGTERM, dap_signal_handler);
    
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
    
    // Accept connection
    int client_fd = accept(sock_fd, nullptr, nullptr);
    if (client_fd < 0) {
        std::cerr << "[DAP Debug] ERROR: Failed to accept connection: " << strerror(errno) << std::endl;
        close(sock_fd);
        return;
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
    if (io_mode_ == IOMode::StdIO) {
        return read(STDIN_FILENO, buf, count);
    } else {
        return read(io_fd_, buf, count);
    }
}

ssize_t CDapDebugUI::write_bytes(const void* buf, size_t count)
{
    if (io_mode_ == IOMode::StdIO) {
        return write(STDOUT_FILENO, buf, count);
    } else {
        return write(io_fd_, buf, count);
    }
}
