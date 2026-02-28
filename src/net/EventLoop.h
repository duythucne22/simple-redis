#pragma once

#include <chrono>
#include <functional>
#include <sys/epoll.h>

/// Owns the epoll instance and provides a single-threaded event loop.
///
/// poll() runs one iteration of epoll_wait and fires the timer callback
/// when the configured interval elapses.
///
/// Must NOT know about: RESP, commands, the database, specific connection logic.
class EventLoop {
public:
    EventLoop();
    ~EventLoop();

    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;

    void addFd(int fd, uint32_t events);
    void modFd(int fd, uint32_t events);
    void removeFd(int fd);

    using TimerCallback = std::function<void()>;
    void setTimerCallback(TimerCallback cb, int intervalMs);

    /// Run one iteration: epoll_wait + timer check.
    /// Returns the number of ready events (>= 0), or 0 on EINTR.
    int poll(int timeoutMs);

    /// Access the i-th ready event from the most recent poll() call.
    const struct epoll_event& event(int i) const { return events_[i]; }

private:
    int epollFd_;
    static constexpr int kMaxEvents = 128;
    struct epoll_event events_[kMaxEvents];
    int numReady_ = 0;

    TimerCallback timerCb_;
    int timerIntervalMs_ = 0;
    std::chrono::steady_clock::time_point lastTimerFire_;
};
