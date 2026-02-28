#pragma once

#include "net/Buffer.h"

#include <chrono>

/// Wraps a client file descriptor and owns its incoming/outgoing buffers.
/// Not copyable, not movable — always held via unique_ptr.
class Connection {
public:
    explicit Connection(int fd);
    ~Connection();

    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;

    int fd() const { return fd_; }

    /// Attempt to read data from the fd into the incoming buffer.
    /// Returns true if the connection is still alive, false on EOF or error.
    bool handleRead();

    /// Attempt to write data from the outgoing buffer to the fd.
    /// Returns true if the connection is still alive, false on error.
    bool handleWrite();

    Buffer& incoming() { return in_; }
    Buffer& outgoing() { return out_; }

    bool wantRead()  const { return wantRead_; }
    bool wantWrite() const { return wantWrite_; }
    bool wantClose() const { return wantClose_; }

    void setWantRead(bool v)  { wantRead_ = v; }
    void setWantWrite(bool v) { wantWrite_ = v; }
    void setWantClose(bool v) { wantClose_ = v; }

    void updateActivity() {
        lastActivity_ = std::chrono::steady_clock::now();
    }

    std::chrono::steady_clock::time_point lastActivity() const {
        return lastActivity_;
    }

private:
    static constexpr size_t kReadBufSize = 4096;

    int fd_;
    Buffer in_;
    Buffer out_;
    bool wantRead_  = true;
    bool wantWrite_ = false;
    bool wantClose_ = false;
    std::chrono::steady_clock::time_point lastActivity_;
};
