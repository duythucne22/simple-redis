#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

// Forward declaration — AOFWriter only needs Database for rewrite snapshot.
class Database;

/// Appends write commands to an Append-Only File in RESP format.
/// Manages fsync policy (ALWAYS, EVERYSEC, NO) and background rewrite via fork().
///
/// Sits in the persistence overlay layer. Must NOT include anything from net/.
/// Must NOT own any data — it only logs commands to disk.
class AOFWriter {
public:
    /// Fsync policy controls durability vs throughput tradeoff.
    enum class FsyncPolicy {
        ALWAYS,    // fsync after every log() — safest, slowest
        EVERYSEC,  // fsync once per second via tick() — default
        NO         // never fsync explicitly — OS decides
    };

    /// Open (or create) the AOF file for appending.
    /// Throws nothing — logs error and disables AOF on failure.
    AOFWriter(const std::string& filename,
              FsyncPolicy policy = FsyncPolicy::EVERYSEC);

    /// Flushes and closes the AOF file descriptor.
    ~AOFWriter();

    AOFWriter(const AOFWriter&) = delete;
    AOFWriter& operator=(const AOFWriter&) = delete;

    /// Append a command in RESP format: *N\r\n$len\r\narg\r\n...
    /// Called after every successful write command (SET, DEL, EXPIRE, etc.).
    void log(const std::vector<std::string>& args);

    /// Called once per event loop tick. If EVERYSEC and 1+ second has
    /// elapsed since last fsync, calls fsync(fd_).
    void tick();

    /// Trigger background rewrite: fork(), child writes compact snapshot,
    /// parent continues logging to old file, swap on child exit.
    /// Does nothing if a rewrite is already in progress.
    void triggerRewrite(Database& db);

    /// Non-blocking check: has the background rewrite child finished?
    /// If yes, appends rewrite buffer to new file, atomically swaps.
    /// Called from the event loop timer callback.
    void checkRewriteComplete();

    /// Return the AOF file path.
    const std::string& filename() const { return filename_; }

    /// Return true if AOF logging is active (file opened successfully).
    bool isEnabled() const { return fd_ >= 0; }

    /// Return true if a background rewrite is in progress.
    bool isRewriting() const { return isRewriting_; }

private:
    std::string filename_;
    int fd_ = -1;                    // file descriptor for AOF file
    FsyncPolicy policy_;
    std::chrono::steady_clock::time_point lastFsync_;

    // Background rewrite state
    pid_t rewriteChildPid_ = -1;     // PID of rewrite child, -1 = none
    std::string rewriteTempFile_;     // temp file child writes to
    bool isRewriting_ = false;       // true between fork() and swap
    std::vector<std::string> rewriteBuffer_;  // commands logged after fork

    /// Format a command as RESP and write to the given fd.
    /// Uses a write loop to handle partial writes.
    static void writeRespCommand(int fd, const std::vector<std::string>& args);

    /// Format a command as RESP into a string (for buffering during rewrite).
    static std::string formatRespCommand(const std::vector<std::string>& args);

    /// Write all bytes in buf to fd, handling partial writes.
    static void writeAll(int fd, const void* buf, size_t len);
};
