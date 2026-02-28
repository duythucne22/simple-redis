#include "net/EventLoop.h"

#include <cerrno>
#include <stdexcept>
#include <unistd.h>  // close

EventLoop::EventLoop() {
    epollFd_ = ::epoll_create1(0);
    if (epollFd_ < 0) {
        throw std::runtime_error("epoll_create1() failed");
    }
    lastTimerFire_ = std::chrono::steady_clock::now();
}

EventLoop::~EventLoop() {
    if (epollFd_ >= 0) {
        ::close(epollFd_);
    }
}

void EventLoop::addFd(int fd, uint32_t events) {
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    ::epoll_ctl(epollFd_, EPOLL_CTL_ADD, fd, &ev);
}

void EventLoop::modFd(int fd, uint32_t events) {
    struct epoll_event ev{};
    ev.events  = events;
    ev.data.fd = fd;
    ::epoll_ctl(epollFd_, EPOLL_CTL_MOD, fd, &ev);
}

void EventLoop::removeFd(int fd) {
    ::epoll_ctl(epollFd_, EPOLL_CTL_DEL, fd, nullptr);
}

void EventLoop::setTimerCallback(TimerCallback cb, int intervalMs) {
    timerCb_         = std::move(cb);
    timerIntervalMs_ = intervalMs;
    lastTimerFire_   = std::chrono::steady_clock::now();
}

int EventLoop::poll(int timeoutMs) {
    // Clamp timeout to the next timer deadline so we don't oversleep.
    int actualTimeout = timeoutMs;
    if (timerCb_ && timerIntervalMs_ > 0) {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - lastTimerFire_)
                           .count();
        int remaining = timerIntervalMs_ - static_cast<int>(elapsed);
        if (remaining <= 0) {
            actualTimeout = 0;  // Timer is overdue — don't block.
        } else if (remaining < actualTimeout) {
            actualTimeout = remaining;
        }
    }

    int n = ::epoll_wait(epollFd_, events_, kMaxEvents, actualTimeout);

    if (n < 0) {
        if (errno == EINTR) {
            numReady_ = 0;
            return 0;  // Interrupted by signal — not an error.
        }
        numReady_ = 0;
        return -1;  // Real error — caller decides how to handle.
    }
    numReady_ = n;

    // Fire the timer callback if the interval has elapsed.
    if (timerCb_ && timerIntervalMs_ > 0) {
        auto now     = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           now - lastTimerFire_)
                           .count();
        if (elapsed >= timerIntervalMs_) {
            timerCb_();
            lastTimerFire_ = now;
        }
    }

    return n;
}
