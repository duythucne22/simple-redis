#pragma once

#include <string>

/// Manages the server's listening socket.
/// Binds to a given address:port and accepts new client connections.
/// The socket is non-blocking so accept won't stall the event loop.
class Listener {
public:
    Listener(const std::string& addr, int port);
    ~Listener();

    Listener(const Listener&) = delete;
    Listener& operator=(const Listener&) = delete;

    int fd() const { return fd_; }

    /// Accept one pending client connection.
    /// Returns a non-blocking client fd, or -1 if no connection is pending
    /// (EAGAIN) or on error.
    int acceptClient();

private:
    int fd_ = -1;
};
