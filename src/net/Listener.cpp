#include "net/Listener.h"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <arpa/inet.h>     // inet_pton, htons
#include <netinet/in.h>    // sockaddr_in
#include <sys/socket.h>    // socket, setsockopt, bind, listen, accept4
#include <unistd.h>        // close

Listener::Listener(const std::string& addr, int port) {
    // Create a non-blocking TCP socket.
    fd_ = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (fd_ < 0) {
        throw std::runtime_error(
            std::string("socket() failed: ") + std::strerror(errno));
    }

    // Allow address reuse so we can restart quickly after a crash.
    int opt = 1;
    ::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in saddr{};
    saddr.sin_family = AF_INET;
    saddr.sin_port   = htons(static_cast<uint16_t>(port));
    if (::inet_pton(AF_INET, addr.c_str(), &saddr.sin_addr) <= 0) {
        ::close(fd_);
        throw std::runtime_error("Invalid address: " + addr);
    }

    if (::bind(fd_, reinterpret_cast<struct sockaddr*>(&saddr),
               sizeof(saddr)) < 0) {
        ::close(fd_);
        throw std::runtime_error(
            std::string("bind() failed: ") + std::strerror(errno));
    }

    if (::listen(fd_, SOMAXCONN) < 0) {
        ::close(fd_);
        throw std::runtime_error(
            std::string("listen() failed: ") + std::strerror(errno));
    }
}

Listener::~Listener() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

int Listener::acceptClient() {
    struct sockaddr_in clientAddr{};
    socklen_t addrLen = sizeof(clientAddr);
    // accept4() with SOCK_NONBLOCK so the client fd is born non-blocking
    // — no extra fcntl() call needed.
    int clientFd = ::accept4(
        fd_,
        reinterpret_cast<struct sockaddr*>(&clientAddr),
        &addrLen,
        SOCK_NONBLOCK);
    return clientFd;  // -1 on EAGAIN / error
}
