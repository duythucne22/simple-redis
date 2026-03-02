#include "cmd/CommandTable.h"
#include "cmd/PubSubRegistry.h"
#include "cmd/ServerCommands.h"
#include "net/Connection.h"
#include "net/EventLoop.h"
#include "net/Listener.h"
#include "persistence/AOFLoader.h"
#include "persistence/AOFWriter.h"
#include "proto/RespParser.h"
#include "proto/RespSerializer.h"
#include "store/Database.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <unordered_map>
#include <vector>
#include <sys/resource.h>  // setrlimit

// ── AOF configuration constants ────────────────────────────────────────────
static constexpr const char* kAOFFilename = "appendonly.aof";
static constexpr auto kAOFPolicy = AOFWriter::FsyncPolicy::EVERYSEC;

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

    // ── Server metrics (Phase 7) ───────────────────────────────────────
    ServerMetrics metrics;
    metrics.tcpPort = static_cast<uint16_t>(port);

    // Register INFO / DBSIZE / FLUSHDB.
    ServerCommands::registerAll(commandTable, metrics);

    // ── AOF persistence (Phase 4) ──────────────────────────────────────
    AOFWriter aofWriter(kAOFFilename, kAOFPolicy);

    // Register BGREWRITEAOF command (needs AOFWriter reference via capture).
    commandTable.registerCommand({"BGREWRITEAOF", 1, false,
        [&aofWriter](Database& cmdDb, Connection& conn,
                     const std::vector<std::string>& /*args*/) {
            aofWriter.triggerRewrite(cmdDb);
            RespSerializer::writeSimpleString(conn.outgoing(),
                "Background append only file rewriting started");
        }
    });

    // Load AOF on startup (replay commands to reconstruct database).
    {
        AOFLoader loader;
        int loaded = loader.load(kAOFFilename, commandTable, db);
        if (loaded > 0) {
            std::printf("DB loaded from AOF: %d commands replayed\n", loaded);
        }
    }

    // ── Pub/Sub Registry (Phase 6) ─────────────────────────────────────
    PubSubRegistry pubsubRegistry;

    // Register EXEC — needs CommandTable& and AOFWriter& to re-dispatch.
    commandTable.registerCommand({"EXEC", 1, false,
        [&commandTable, &aofWriter](Database& cmdDb, Connection& conn,
                                     const std::vector<std::string>& /*args*/) {
            if (!conn.txn.has_value()) {
                RespSerializer::writeError(conn.outgoing(),
                                           "ERR EXEC without MULTI");
                return;
            }

            auto& queued = conn.txn->queuedCommands;

            // Write the array header for the results.
            RespSerializer::writeArrayHeader(conn.outgoing(),
                                             static_cast<int64_t>(queued.size()));

            // Execute each queued command.
            for (auto& qcmd : queued) {
                commandTable.dispatch(cmdDb, conn, qcmd);

                // Log write commands to AOF.
                if (aofWriter.isEnabled() &&
                    commandTable.isWriteCommand(qcmd[0])) {
                    aofWriter.log(qcmd);
                }
            }

            // Clear transaction state.
            conn.txn.reset();
        }
    });

    // Register SUBSCRIBE — needs PubSubRegistry&.
    commandTable.registerCommand({"SUBSCRIBE", -2, false,
        [&pubsubRegistry](Database& /*cmdDb*/, Connection& conn,
                          const std::vector<std::string>& args) {
            // SUBSCRIBE channel [channel ...]
            for (size_t i = 1; i < args.size(); ++i) {
                size_t numSubs = pubsubRegistry.subscribe(args[i], conn);

                // Reply: ["subscribe", channelName, numSubscriptions]
                RespSerializer::writeArrayHeader(conn.outgoing(), 3);
                RespSerializer::writeBulkString(conn.outgoing(), "subscribe");
                RespSerializer::writeBulkString(conn.outgoing(), args[i]);
                RespSerializer::writeInteger(conn.outgoing(),
                                             static_cast<int64_t>(numSubs));
            }
        }
    });

    // Register UNSUBSCRIBE — needs PubSubRegistry&.
    commandTable.registerCommand({"UNSUBSCRIBE", -1, false,
        [&pubsubRegistry](Database& /*cmdDb*/, Connection& conn,
                          const std::vector<std::string>& args) {
            if (args.size() <= 1) {
                // Unsubscribe from all channels.
                if (conn.subscribedChannels.empty()) {
                    // No subscriptions — reply with 0 count.
                    RespSerializer::writeArrayHeader(conn.outgoing(), 3);
                    RespSerializer::writeBulkString(conn.outgoing(), "unsubscribe");
                    RespSerializer::writeNull(conn.outgoing());
                    RespSerializer::writeInteger(conn.outgoing(), 0);
                } else {
                    auto channels = conn.subscribedChannels;  // copy — set will be modified
                    for (const auto& ch : channels) {
                        size_t remaining = pubsubRegistry.unsubscribe(ch, conn);
                        RespSerializer::writeArrayHeader(conn.outgoing(), 3);
                        RespSerializer::writeBulkString(conn.outgoing(), "unsubscribe");
                        RespSerializer::writeBulkString(conn.outgoing(), ch);
                        RespSerializer::writeInteger(conn.outgoing(),
                                                     static_cast<int64_t>(remaining));
                    }
                }
            } else {
                for (size_t i = 1; i < args.size(); ++i) {
                    size_t remaining = pubsubRegistry.unsubscribe(args[i], conn);
                    RespSerializer::writeArrayHeader(conn.outgoing(), 3);
                    RespSerializer::writeBulkString(conn.outgoing(), "unsubscribe");
                    RespSerializer::writeBulkString(conn.outgoing(), args[i]);
                    RespSerializer::writeInteger(conn.outgoing(),
                                                 static_cast<int64_t>(remaining));
                }
            }
        }
    });

    // Register PUBLISH — needs PubSubRegistry&.
    commandTable.registerCommand({"PUBLISH", 3, false,
        [&pubsubRegistry](Database& /*cmdDb*/, Connection& conn,
                          const std::vector<std::string>& args) {
            // PUBLISH channel message
            size_t delivered = pubsubRegistry.publish(args[1], args[2]);
            RespSerializer::writeInteger(conn.outgoing(),
                                         static_cast<int64_t>(delivered));
        }
    });

    // ── Wire active expiry timer (Phase 3) + AOF tick (Phase 4) ────────
    // Every 100ms: expire keys, fsync if EVERYSEC, check rewrite child.
    eventLoop.setTimerCallback([&db, &aofWriter]() {
        db.activeExpireCycle(200);
        aofWriter.tick();
        aofWriter.checkRewriteComplete();
    }, 100);

    // ── Connection map: fd → Connection ────────────────────────────────
    std::unordered_map<int, std::unique_ptr<Connection>> connections;

    // ── Main loop ──────────────────────────────────────────────────────
    while (g_running) {
        // Update connected clients count for INFO command.
        metrics.connectedClients = connections.size();

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

                    // Uppercase command name for comparisons.
                    std::string cmdName = (*cmd)[0];
                    std::transform(cmdName.begin(), cmdName.end(),
                                   cmdName.begin(), ::toupper);

                    // ── Subscriber mode gate (Phase 6) ─────────────────
                    // In subscriber mode, only allow SUBSCRIBE, UNSUBSCRIBE,
                    // PING, and QUIT.
                    if (conn.inSubscribeMode() &&
                        cmdName != "SUBSCRIBE" && cmdName != "UNSUBSCRIBE" &&
                        cmdName != "PING" && cmdName != "QUIT") {
                        RespSerializer::writeError(conn.outgoing(),
                            "ERR Can't execute '" + (*cmd)[0] +
                            "': only (P)SUBSCRIBE / (P)UNSUBSCRIBE / "
                            "PING / QUIT are allowed in this context");
                        continue;
                    }

                    // ── Transaction queuing (Phase 6) ──────────────────
                    // If in MULTI mode, queue commands instead of executing
                    // (except EXEC, DISCARD, MULTI themselves).
                    if (conn.txn.has_value() &&
                        cmdName != "EXEC" && cmdName != "DISCARD" &&
                        cmdName != "MULTI") {
                        conn.txn->queuedCommands.push_back(*cmd);
                        RespSerializer::writeSimpleString(conn.outgoing(),
                                                          "QUEUED");
                        continue;
                    }

                    // \u2500\u2500 Timed dispatch (Phase 7) \u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500\u2500
                    auto dispatchStart = std::chrono::steady_clock::now();
                    commandTable.dispatch(db, conn, *cmd);
                    auto dispatchEnd = std::chrono::steady_clock::now();

                    int64_t durationUs =
                        std::chrono::duration_cast<std::chrono::microseconds>(
                            dispatchEnd - dispatchStart).count();
                    metrics.totalCommandsProcessed++;
                    metrics.recordLatency(durationUs);
                    metrics.maybeRecordSlowLog(durationUs, *cmd);

                    // INV-1: Log to AOF only AFTER successful dispatch,
                    // and only for write commands (not inside transactions
                    // — EXEC handler logs its own queued write commands).
                    if (cmdName != "EXEC" &&
                        aofWriter.isEnabled() &&
                        commandTable.isWriteCommand((*cmd)[0])) {
                        aofWriter.log(*cmd);
                    }
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

        // ── Sweep: enable EPOLLOUT for connections with pending output ──
        // Necessary because PUBLISH (and future cross-connection writes)
        // can fill a subscriber's outgoing buffer from another fd's handler.
        for (auto& [sfd, sptr] : connections) {
            if (!sptr->wantClose() && sptr->outgoing().readableBytes() > 0) {
                sptr->setWantWrite(true);
                uint32_t desired = 0;
                if (sptr->wantRead())  desired |= EPOLLIN;
                if (sptr->wantWrite()) desired |= EPOLLOUT;
                eventLoop.modFd(sfd, desired);
            }
        }

        // ── Cleanup closed connections ─────────────────────────────────
        std::vector<int> toClose;
        for (auto& [cfd, cptr] : connections) {
            if (cptr->wantClose()) {
                toClose.push_back(cfd);
            }
        }
        for (int cfd : toClose) {
            // Phase 6: Remove from pub/sub before destroying Connection.
            auto it2 = connections.find(cfd);
            if (it2 != connections.end()) {
                pubsubRegistry.removeConnection(*it2->second);
            }
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
