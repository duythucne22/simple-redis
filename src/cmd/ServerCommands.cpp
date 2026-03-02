#include "cmd/ServerCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <sstream>
#include <unistd.h>    // getpid()

// ── Registration ───────────────────────────────────────────────────────────

void ServerCommands::registerAll(CommandTable& table, ServerMetrics& metrics) {
    table.registerCommand({"DBSIZE", 1, false, cmdDbsize});
    table.registerCommand({"FLUSHDB", -1, true, cmdFlushdb});
    table.registerCommand({"INFO", -1, false,
        [&metrics](Database& db, Connection& conn,
                   const std::vector<std::string>& args) {
            cmdInfo(db, conn, args, metrics);
        }});
}

// ── DBSIZE ─────────────────────────────────────────────────────────────────

void ServerCommands::cmdDbsize(Database& db, Connection& conn,
                               const std::vector<std::string>& /*args*/) {
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(db.dbsize()));
}

// ── FLUSHDB ────────────────────────────────────────────────────────────────

void ServerCommands::cmdFlushdb(Database& db, Connection& conn,
                                const std::vector<std::string>& /*args*/) {
    db.flushdb();
    RespSerializer::writeSimpleString(conn.outgoing(), "OK");
}

// ── INFO helpers ───────────────────────────────────────────────────────────

static void appendServerSection(std::ostringstream& ss,
                                const ServerMetrics& m) {
    using namespace std::chrono;
    auto uptime = steady_clock::now() - m.startTime;
    auto uptimeSec = duration_cast<seconds>(uptime).count();

    ss << "# Server\r\n";
    ss << "redis_version:simple-redis-0.7.0\r\n";
    ss << "process_id:" << ::getpid() << "\r\n";
    ss << "tcp_port:" << m.tcpPort << "\r\n";
    ss << "uptime_in_seconds:" << uptimeSec << "\r\n";
    ss << "uptime_in_days:" << (uptimeSec / 86400) << "\r\n";
    ss << "\r\n";
}

static void appendClientsSection(std::ostringstream& ss,
                                 const ServerMetrics& m) {
    ss << "# Clients\r\n";
    ss << "connected_clients:" << m.connectedClients << "\r\n";
    ss << "\r\n";
}

static void appendMemorySection(std::ostringstream& ss, const Database& db) {
    ss << "# Memory\r\n";
    ss << "used_memory:" << db.usedMemory() << "\r\n";
    ss << "\r\n";
}

static void appendStatsSection(std::ostringstream& ss,
                                const ServerMetrics& m) {
    ss << "# Stats\r\n";
    ss << "total_commands_processed:" << m.totalCommandsProcessed << "\r\n";

    // Latency histogram.
    ss << "latency_histogram_us_lt100:" << m.latencyHistogram[0] << "\r\n";
    ss << "latency_histogram_us_lt500:" << m.latencyHistogram[1] << "\r\n";
    ss << "latency_histogram_us_lt1000:" << m.latencyHistogram[2] << "\r\n";
    ss << "latency_histogram_us_lt10000:" << m.latencyHistogram[3] << "\r\n";
    ss << "latency_histogram_us_lt100000:" << m.latencyHistogram[4] << "\r\n";
    ss << "latency_histogram_us_gte100000:" << m.latencyHistogram[5] << "\r\n";

    // Slow log summary.
    size_t slowlogLen = m.slowLogCount < kSlowLogMaxEntries
                            ? static_cast<size_t>(m.slowLogCount)
                            : kSlowLogMaxEntries;
    ss << "slowlog_len:" << slowlogLen << "\r\n";
    ss << "\r\n";
}

static void appendKeyspaceSection(std::ostringstream& ss,
                                   const Database& db) {
    ss << "# Keyspace\r\n";
    size_t keys = db.dbsize();
    if (keys > 0) {
        size_t expires = db.expiryCount();
        ss << "db0:keys=" << keys << ",expires=" << expires << ",avg_ttl=0\r\n";
    }
    ss << "\r\n";
}

// ── INFO command ───────────────────────────────────────────────────────────

void ServerCommands::cmdInfo(Database& db, Connection& conn,
                             const std::vector<std::string>& args,
                             ServerMetrics& metrics) {
    std::string section;
    if (args.size() >= 2) {
        section = args[1];
        // Lowercase for case-insensitive comparison.
        for (auto& c : section) c = static_cast<char>(::tolower(c));
    }

    std::ostringstream ss;

    bool all = section.empty() || section == "all" || section == "everything";

    if (all || section == "server")   appendServerSection(ss, metrics);
    if (all || section == "clients")  appendClientsSection(ss, metrics);
    if (all || section == "memory")   appendMemorySection(ss, db);
    if (all || section == "stats")    appendStatsSection(ss, metrics);
    if (all || section == "keyspace") appendKeyspaceSection(ss, db);

    RespSerializer::writeBulkString(conn.outgoing(), ss.str());
}
