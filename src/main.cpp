#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "net/EventLoop.h"
#include "net/Listener.h"
#include "proto/RespParser.h"
#include "store/Database.h"

#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>
#include <sys/resource.h>  // setrlimit

// ── Global state (acceptable per understanding doc §10 — signal handler) ──
static volatile sig_atomic_t g_running = 1;

static void signalHandler(int /*sig*/) {
    g_running = 0;
}

int main(int argc, char* argv[]) {
    // ── Parse arguments ────────────────────────────────────────────────
    int port = 6379;
    if (argc > 1) {
        port = std::atoi(argv[1]);
    }

    // ── Signal setup ───────────────────────────────────────────────────
    std::signal(SIGPIPE, SIG_IGN);   // Prevent crash on write to closed socket.
    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    // ── Raise fd limit for 10,000 connections ──────────────────────────
    {
        struct rlimit rl{};
        rl.rlim_cur = 65536;
        rl.rlim_max = 65536;
        if (::setrlimit(RLIMIT_NOFILE, &rl) != 0) {
            // Fallback: at least raise the soft limit to the current hard limit.
            ::getrlimit(RLIMIT_NOFILE, &rl);
            rl.rlim_cur = rl.rlim_max;
            ::setrlimit(RLIMIT_NOFILE, &rl);
        }
    }

    // ── Create listener + event loop ───────────────────────────────────
    Listener  listener("0.0.0.0", port);
    EventLoop eventLoop;
    eventLoop.addFd(listener.fd(), EPOLLIN);

    std::printf("Listening on port %d\n", port);

    // ── Database + Command Engine ──────────────────────────────────────
    Database     db;
    CommandTable commandTable;
    RespParser   parser;

    // ── Wire active expiry timer (Phase 3) ─────────────────────────────
    // Every 100ms, expire up to 200 keys from the TTL heap.
    eventLoop.setTimerCallback([&db]() {
        db.activeExpireCycle(200);
    }, 100);

    // ── Connection map: fd → Connection ────────────────────────────────
    std::unordered_map<int, std::unique_ptr<Connection>> connections;

    // ── Main loop ──────────────────────────────────────────────────────
    while (g_running) {
        int n = eventLoop.poll(100);  // 100 ms timeout
        if (n < 0) break;            // epoll error

        for (int i = 0; i < n; ++i) {
            const auto& ev = eventLoop.event(i);
            int         fd = ev.data.fd;
            uint32_t events = ev.events;

            // ── Listener event: accept new connections ─────────────────
            if (fd == listener.fd()) {
                // Drain all pending connections (level-triggered).
                while (true) {
                    int clientFd = listener.acceptClient();
                    if (clientFd < 0) break;  // EAGAIN — no more pending

                    auto conn = std::make_unique<Connection>(clientFd);
                    eventLoop.addFd(clientFd, EPOLLIN);
                    connections[clientFd] = std::move(conn);
                }
                continue;
            }

            // ── Client event ───────────────────────────────────────────
            auto it = connections.find(fd);
            if (it == connections.end()) continue;  // stale event
            Connection& conn = *it->second;

            // Fatal error — close immediately.
            if (events & EPOLLERR) {
                conn.setWantClose(true);
                continue;
            }

            // Readable (EPOLLIN or EPOLLHUP — HUP may still have data).
            if (events & (EPOLLIN | EPOLLHUP)) {
                if (!conn.handleRead()) {
                    // EOF or error on read side.  Stop reading but keep
                    // the connection alive to flush any outgoing data.
                    conn.setWantRead(false);
                }
                // ── Parse/dispatch loop: handle pipelining ─────────────
                while (true) {
                    auto cmd = parser.parse(conn.incoming());
                    if (!cmd.has_value()) break;  // incomplete frame
                    if (cmd->empty()) continue;   // empty command (null array)
                    commandTable.dispatch(db, conn, *cmd);
                }
                if (conn.outgoing().readableBytes() > 0) {
                    conn.setWantWrite(true);
                }
            }

            // Writable
            if ((events & EPOLLOUT) && !conn.wantClose()) {
                if (!conn.handleWrite()) {
                    conn.setWantClose(true);
                } else if (conn.outgoing().readableBytes() == 0) {
                    conn.setWantWrite(false);
                }
            }

            // Close if read side is done and nothing left to write.
            if (!conn.wantRead() && conn.outgoing().readableBytes() == 0) {
                conn.setWantClose(true);
            }

            // ── Update epoll registration for this fd ──────────────────
            if (!conn.wantClose()) {
                uint32_t desired = 0;
                if (conn.wantRead())  desired |= EPOLLIN;
                if (conn.wantWrite()) desired |= EPOLLOUT;
                eventLoop.modFd(fd, desired);
            }
        }

        // ── Advance incremental rehashing ───────────────────────────────
        db.rehashStep();

        // ── Cleanup closed connections ─────────────────────────────────
        std::vector<int> toClose;
        for (auto& [cfd, cptr] : connections) {
            if (cptr->wantClose()) {
                toClose.push_back(cfd);
            }
        }
        for (int cfd : toClose) {
            eventLoop.removeFd(cfd);
            connections.erase(cfd);  // unique_ptr dtor closes the fd.
        }
    }

    // ── Clean shutdown ─────────────────────────────────────────────────
    for (auto& [cfd, cptr] : connections) {
        eventLoop.removeFd(cfd);
    }
    connections.clear();

    std::printf("Server shut down.\n");
    return 0;
}
