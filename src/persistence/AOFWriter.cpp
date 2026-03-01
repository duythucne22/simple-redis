#include "persistence/AOFWriter.h"
#include "store/Database.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <unordered_map>
#include <unordered_set>

// ── Constructor / Destructor ────────────────────────────────────────────────

AOFWriter::AOFWriter(const std::string& filename, FsyncPolicy policy)
    : filename_(filename), policy_(policy),
      lastFsync_(std::chrono::steady_clock::now()) {
    // Open for append, create if missing. Mode 0644 = owner rw, group/other r.
    fd_ = ::open(filename.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd_ < 0) {
        std::fprintf(stderr, "AOFWriter: failed to open '%s': %s\n",
                     filename.c_str(), std::strerror(errno));
        // fd_ stays -1, isEnabled() returns false. Server runs without AOF.
    }
}

AOFWriter::~AOFWriter() {
    if (fd_ >= 0) {
        ::fsync(fd_);
        ::close(fd_);
        fd_ = -1;
    }
}

// ── RESP formatting ─────────────────────────────────────────────────────────

std::string AOFWriter::formatRespCommand(const std::vector<std::string>& args) {
    // Format: *N\r\n$len\r\narg\r\n$len\r\narg\r\n...
    std::string result;
    result.reserve(64);  // reasonable starting size for small commands
    result += '*';
    result += std::to_string(args.size());
    result += "\r\n";
    for (const auto& arg : args) {
        result += '$';
        result += std::to_string(arg.size());
        result += "\r\n";
        result += arg;
        result += "\r\n";
    }
    return result;
}

void AOFWriter::writeAll(int fd, const void* buf, size_t len) {
    const uint8_t* ptr = static_cast<const uint8_t*>(buf);
    size_t written = 0;
    while (written < len) {
        ssize_t n = ::write(fd, ptr + written, len - written);
        if (n < 0) {
            if (errno == EINTR) continue;  // interrupted, retry
            std::fprintf(stderr, "AOFWriter: write error: %s\n",
                         std::strerror(errno));
            return;  // partial write — data at risk, but don't crash
        }
        written += static_cast<size_t>(n);
    }
}

void AOFWriter::writeRespCommand(int fd, const std::vector<std::string>& args) {
    std::string resp = formatRespCommand(args);
    writeAll(fd, resp.data(), resp.size());
}

// ── Core API ────────────────────────────────────────────────────────────────

void AOFWriter::log(const std::vector<std::string>& args) {
    // INV-1: Only called after successful command execution.
    if (fd_ < 0) return;  // AOF disabled

    std::string resp = formatRespCommand(args);

    // Write to the AOF file.
    writeAll(fd_, resp.data(), resp.size());

    // INV-4: fsync per policy.
    if (policy_ == FsyncPolicy::ALWAYS) {
        ::fsync(fd_);
    }

    // INV-5: During rewrite, also buffer for later append to new file.
    if (isRewriting_) {
        rewriteBuffer_.push_back(std::move(resp));
    }
}

void AOFWriter::tick() {
    // Only EVERYSEC needs periodic fsync.
    if (policy_ != FsyncPolicy::EVERYSEC) return;
    if (fd_ < 0) return;

    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                       now - lastFsync_).count();
    if (elapsed >= 1) {
        ::fsync(fd_);
        lastFsync_ = now;
    }
}

// ── Background Rewrite ──────────────────────────────────────────────────────

void AOFWriter::triggerRewrite(Database& db) {
    // Ignore if already rewriting.
    if (isRewriting_) return;

    isRewriting_ = true;
    rewriteBuffer_.clear();
    rewriteTempFile_ = "temp-rewrite-" + std::to_string(::getpid()) + ".aof";

    pid_t pid = ::fork();
    if (pid < 0) {
        // fork() failed.
        std::fprintf(stderr, "AOFWriter: fork() failed: %s\n",
                     std::strerror(errno));
        isRewriting_ = false;
        return;
    }

    if (pid == 0) {
        // ── CHILD PROCESS ──────────────────────────────────────────────
        // Write a compact snapshot of the database to the temp file.
        int tmpFd = ::open(rewriteTempFile_.c_str(),
                           O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (tmpFd < 0) {
            _exit(1);
        }

        // Iterate all keys and write type-appropriate reconstruction commands.
        auto allKeys = db.keys();
        for (const auto& key : allKeys) {
            HTEntry* entry = db.findEntry(key);
            if (!entry) continue;  // key expired between calls

            switch (entry->value.type) {
            case DataType::STRING: {
                // Write: SET key value
                writeRespCommand(tmpFd, {"SET", key, entry->value.asString()});
                break;
            }
            case DataType::LIST: {
                auto& list = std::get<std::deque<std::string>>(entry->value.data);
                // Write: RPUSH key elem1 elem2 ... (preserves order)
                if (!list.empty()) {
                    std::vector<std::string> cmd = {"RPUSH", key};
                    for (const auto& elem : list) {
                        cmd.push_back(elem);
                    }
                    writeRespCommand(tmpFd, cmd);
                }
                break;
            }
            case DataType::HASH: {
                auto& hash = std::get<std::unordered_map<std::string, std::string>>(entry->value.data);
                // Write: HSET key field1 value1 field2 value2 ...
                if (!hash.empty()) {
                    std::vector<std::string> cmd = {"HSET", key};
                    for (const auto& [field, value] : hash) {
                        cmd.push_back(field);
                        cmd.push_back(value);
                    }
                    writeRespCommand(tmpFd, cmd);
                }
                break;
            }
            case DataType::SET: {
                auto& set = std::get<std::unordered_set<std::string>>(entry->value.data);
                // Write: SADD key member1 member2 ...
                if (!set.empty()) {
                    std::vector<std::string> cmd = {"SADD", key};
                    for (const auto& member : set) {
                        cmd.push_back(member);
                    }
                    writeRespCommand(tmpFd, cmd);
                }
                break;
            }
            case DataType::ZSET: {
                auto& zset = std::get<ZSetData>(entry->value.data);
                // Write: ZADD key score1 member1 score2 member2 ...
                // Walk skiplist in order so replay recreates same ordering.
                if (!zset.dict.empty()) {
                    auto elems = zset.skiplist.rangeByRank(0, static_cast<int>(zset.skiplist.size()) - 1);
                    std::vector<std::string> cmd = {"ZADD", key};
                    for (const auto& [member, score] : elems) {
                        char buf[64];
                        std::snprintf(buf, sizeof(buf), "%.17g", score);
                        cmd.push_back(buf);
                        cmd.push_back(member);
                    }
                    writeRespCommand(tmpFd, cmd);
                }
                break;
            }
            }

            // If key has a TTL, write: PEXPIRE key <remaining_ms>
            int64_t remaining = db.ttl(key);
            if (remaining > 0) {
                writeRespCommand(tmpFd,
                    {"PEXPIRE", key, std::to_string(remaining)});
            }
        }

        ::fsync(tmpFd);
        ::close(tmpFd);
        _exit(0);  // _exit, not exit — avoid parent cleanup (INV, §12 rule 4)
    }

    // ── PARENT PROCESS ─────────────────────────────────────────────────
    rewriteChildPid_ = pid;
    // Continue normal operation. log() will also buffer to rewriteBuffer_.
}

void AOFWriter::checkRewriteComplete() {
    if (rewriteChildPid_ < 0) return;  // no rewrite in progress

    int status = 0;
    pid_t result = ::waitpid(rewriteChildPid_, &status, WNOHANG);

    if (result == 0) return;  // child still running

    if (result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
        // Child finished successfully.
        // Step 1: Append rewrite buffer to temp file.
        int tmpFd = ::open(rewriteTempFile_.c_str(),
                           O_WRONLY | O_APPEND, 0644);
        if (tmpFd >= 0) {
            for (const auto& entry : rewriteBuffer_) {
                writeAll(tmpFd, entry.data(), entry.size());
            }
            ::fsync(tmpFd);
            ::close(tmpFd);

            // Step 2: Atomic swap — rename temp file over the AOF file.
            if (::rename(rewriteTempFile_.c_str(), filename_.c_str()) == 0) {
                // Step 3: Reopen the AOF file for appending.
                ::close(fd_);
                fd_ = ::open(filename_.c_str(),
                             O_WRONLY | O_CREAT | O_APPEND, 0644);
                if (fd_ < 0) {
                    std::fprintf(stderr,
                        "AOFWriter: failed to reopen '%s' after rewrite: %s\n",
                        filename_.c_str(), std::strerror(errno));
                }
            } else {
                std::fprintf(stderr, "AOFWriter: rename failed: %s\n",
                             std::strerror(errno));
            }
        } else {
            std::fprintf(stderr,
                "AOFWriter: failed to open temp file for buffer append: %s\n",
                std::strerror(errno));
        }
    } else {
        // Child failed or was killed.
        std::fprintf(stderr, "AOFWriter: rewrite child failed (status %d)\n",
                     status);
        ::unlink(rewriteTempFile_.c_str());
    }

    // Clear rewrite state regardless of outcome.
    rewriteBuffer_.clear();
    isRewriting_ = false;
    rewriteChildPid_ = -1;
}
