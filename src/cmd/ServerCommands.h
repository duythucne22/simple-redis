#pragma once

#include "store/Database.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

class Connection;
class CommandTable;

// ── Latency histogram buckets ──────────────────────────────────────────────
//
//   Bucket 0: < 100 µs
//   Bucket 1: < 500 µs
//   Bucket 2: < 1 ms
//   Bucket 3: < 10 ms
//   Bucket 4: < 100 ms
//   Bucket 5: >= 100 ms
//
static constexpr int kLatencyBuckets = 6;
static constexpr int64_t kLatencyThresholds[] = {100, 500, 1000, 10000, 100000};

/// Determine the histogram bucket index for a given duration in microseconds.
inline int latencyBucketIndex(int64_t durationUs) {
    for (int i = 0; i < kLatencyBuckets - 1; ++i) {
        if (durationUs < kLatencyThresholds[i]) return i;
    }
    return kLatencyBuckets - 1;
}

// ── Slow log entry ─────────────────────────────────────────────────────────

struct SlowLogEntry {
    uint64_t id{0};
    int64_t  timestampUs{0};   // wall-clock µs since epoch
    int64_t  durationUs{0};    // execution time in µs
    std::vector<std::string> args;  // first N args of the command
};

static constexpr size_t kSlowLogMaxEntries = 128;

// ── Server-wide metrics ────────────────────────────────────────────────────
//
// Single instance created in main.cpp, referenced by ServerCommands for
// INFO output. All fields are updated on the main (single) thread — no
// need for atomics.

struct ServerMetrics {
    std::chrono::steady_clock::time_point startTime{
        std::chrono::steady_clock::now()};

    uint64_t totalCommandsProcessed{0};

    // Latency histogram (6 buckets).
    uint64_t latencyHistogram[kLatencyBuckets]{};

    // Circular slow log.
    SlowLogEntry slowLog[kSlowLogMaxEntries]{};
    size_t       slowLogNextIdx{0};
    uint64_t     slowLogCount{0};       // monotonic counter → used as ID
    int64_t      slowLogThresholdUs{10000};  // default 10 ms (Redis default)

    // External state injected by main.cpp.
    size_t   connectedClients{0};
    uint16_t tcpPort{6379};

    // ── helpers ──

    void recordLatency(int64_t durationUs) {
        latencyHistogram[latencyBucketIndex(durationUs)]++;
    }

    void maybeRecordSlowLog(int64_t durationUs,
                            const std::vector<std::string>& args) {
        if (durationUs < slowLogThresholdUs) return;
        auto& e       = slowLog[slowLogNextIdx % kSlowLogMaxEntries];
        e.id           = slowLogCount++;
        e.timestampUs  = std::chrono::duration_cast<std::chrono::microseconds>(
                             std::chrono::system_clock::now().time_since_epoch())
                             .count();
        e.durationUs   = durationUs;
        // Keep at most the first 6 args (like Redis SLOWLOG).
        e.args.clear();
        size_t n = std::min(args.size(), size_t{6});
        e.args.assign(args.begin(), args.begin() + static_cast<long>(n));
        slowLogNextIdx = (slowLogNextIdx + 1) % kSlowLogMaxEntries;
    }
};

// ── ServerCommands namespace ───────────────────────────────────────────────

namespace ServerCommands {

/// Register INFO, DBSIZE, FLUSHDB with the CommandTable.
/// INFO is captured as a lambda referencing the ServerMetrics instance.
void registerAll(CommandTable& table, ServerMetrics& metrics);

/// DBSIZE — returns the number of keys in the database.
void cmdDbsize(Database& db, Connection& conn,
               const std::vector<std::string>& args);

/// FLUSHDB — delete all keys.
void cmdFlushdb(Database& db, Connection& conn,
                const std::vector<std::string>& args);

/// INFO [section] — return server information.
/// Needs metrics reference → called via lambda capture.
void cmdInfo(Database& db, Connection& conn,
             const std::vector<std::string>& args,
             ServerMetrics& metrics);

}  // namespace ServerCommands
