#include "net/Connection.h"

#include <cerrno>
#include <unistd.h>   // read, write, close

Connection::Connection(int fd)
    : fd_(fd),
      lastActivity_(std::chrono::steady_clock::now()) {}

Connection::~Connection() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

bool Connection::handleRead() {
    // Lazily allocate — an idle connection that never receives data
    // never allocates buffer memory.
    in_.ensureWritableBytes(kReadBufSize);

    ssize_t n = ::read(fd_, in_.writablePtr(), in_.writableBytes());
    if (n > 0) {
        in_.advanceWrite(static_cast<size_t>(n));
        updateActivity();
        return true;
    }
    if (n == 0) {
        // Peer closed the connection (EOF).
        return false;
    }
    // n < 0: check for non-blocking "would block" vs real error.
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return true;  // No data right now; try again on next event.
    }
    return false;  // Real I/O error.
}

bool Connection::handleWrite() {
    if (out_.readableBytes() == 0) {
        return true;  // Nothing to send.
    }

    ssize_t n = ::write(fd_, out_.readablePtr(), out_.readableBytes());
    if (n > 0) {
        out_.consume(static_cast<size_t>(n));
        updateActivity();
        return true;
    }
    if (n == 0) {
        return true;  // Not an error; try again next time.
    }
    // n < 0
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return true;  // Kernel buffer full; try again on next EPOLLOUT.
    }
    return false;  // Real I/O error (e.g., ECONNRESET).
}
