#include "cmd/ZSetCommands.h"
#include "cmd/CommandTable.h"
#include "net/Connection.h"
#include "proto/RespSerializer.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* WRONGTYPE =
    "WRONGTYPE Operation against a key holding the wrong kind of value";

/// Format a double to string using "%.17g" (matches Redis precision).
static std::string formatScore(double score) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.17g", score);
    return buf;
}

void ZSetCommands::registerAll(CommandTable& table) {
    // ZADD key score member [score member ...] — minimum 4 args
    table.registerCommand({"ZADD",   -4, true,  cmdZAdd});
    table.registerCommand({"ZSCORE",  3, false, cmdZScore});
    table.registerCommand({"ZRANK",   3, false, cmdZRank});
    // ZRANGE key start stop [WITHSCORES] — 4 or 5 args
    table.registerCommand({"ZRANGE", -4, false, cmdZRange});
    table.registerCommand({"ZCARD",   2, false, cmdZCard});
    table.registerCommand({"ZREM",   -3, true,  cmdZRem});
}

void ZSetCommands::cmdZAdd(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    // args: ZADD key score1 member1 [score2 member2 ...]
    if ((args.size() - 2) % 2 != 0) {
        RespSerializer::writeError(conn.outgoing(),
            "ERR wrong number of arguments for 'zadd' command");
        return;
    }

    HTEntry* entry = db.findEntry(args[1]);
    if (entry && entry->value.type != DataType::ZSET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    if (!entry) {
        db.setObject(args[1], RedisObject::createZSet());
        entry = db.findEntry(args[1]);
    }
    auto& zset = std::get<ZSetData>(entry->value.data);

    int64_t added = 0;
    for (size_t i = 2; i < args.size(); i += 2) {
        double score = std::strtod(args[i].c_str(), nullptr);
        const std::string& member = args[i + 1];

        auto it = zset.dict.find(member);
        if (it != zset.dict.end()) {
            // Member exists — update score if different.
            double oldScore = it->second;
            if (oldScore != score) {
                zset.skiplist.remove(member, oldScore);
                zset.skiplist.insert(member, score);
                it->second = score;
            }
            // Not counted as "added" — it's an update.
        } else {
            // New member.
            zset.skiplist.insert(member, score);
            zset.dict[member] = score;
            ++added;
        }
    }
    RespSerializer::writeInteger(conn.outgoing(), added);
}

void ZSetCommands::cmdZScore(Database& db, Connection& conn,
                             const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    if (entry->value.type != DataType::ZSET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& zset = std::get<ZSetData>(entry->value.data);

    auto it = zset.dict.find(args[2]);
    if (it == zset.dict.end()) {
        RespSerializer::writeNull(conn.outgoing());
    } else {
        RespSerializer::writeBulkString(conn.outgoing(),
                                        formatScore(it->second));
    }
}

void ZSetCommands::cmdZRank(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }
    if (entry->value.type != DataType::ZSET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& zset = std::get<ZSetData>(entry->value.data);

    auto it = zset.dict.find(args[2]);
    if (it == zset.dict.end()) {
        RespSerializer::writeNull(conn.outgoing());
        return;
    }

    // Compute rank by walking level 0 to find position.
    double score = it->second;
    auto range = zset.skiplist.rangeByRank(0, static_cast<int>(zset.skiplist.size()) - 1);
    int64_t rank = 0;
    for (const auto& [m, s] : range) {
        if (m == args[2] && s == score) {
            RespSerializer::writeInteger(conn.outgoing(), rank);
            return;
        }
        ++rank;
    }
    // Should not happen if dict and skiplist are in sync.
    RespSerializer::writeNull(conn.outgoing());
}

void ZSetCommands::cmdZRange(Database& db, Connection& conn,
                             const std::vector<std::string>& args) {
    // args: ZRANGE key start stop [WITHSCORES]
    bool withScores = false;
    if (args.size() == 5) {
        std::string flag = args[4];
        // Case-insensitive comparison.
        std::transform(flag.begin(), flag.end(), flag.begin(), ::toupper);
        if (flag == "WITHSCORES") {
            withScores = true;
        } else {
            RespSerializer::writeError(conn.outgoing(),
                "ERR syntax error");
            return;
        }
    }

    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeArrayHeader(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::ZSET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& zset = std::get<ZSetData>(entry->value.data);

    int start = std::stoi(args[2]);
    int stop  = std::stoi(args[3]);

    auto result = zset.skiplist.rangeByRank(start, stop);

    if (withScores) {
        RespSerializer::writeArrayHeader(conn.outgoing(),
            static_cast<int64_t>(result.size() * 2));
        for (const auto& [member, score] : result) {
            RespSerializer::writeBulkString(conn.outgoing(), member);
            RespSerializer::writeBulkString(conn.outgoing(),
                                            formatScore(score));
        }
    } else {
        RespSerializer::writeArrayHeader(conn.outgoing(),
            static_cast<int64_t>(result.size()));
        for (const auto& [member, score] : result) {
            RespSerializer::writeBulkString(conn.outgoing(), member);
        }
    }
}

void ZSetCommands::cmdZCard(Database& db, Connection& conn,
                            const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::ZSET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& zset = std::get<ZSetData>(entry->value.data);
    RespSerializer::writeInteger(conn.outgoing(),
                                 static_cast<int64_t>(zset.skiplist.size()));
}

void ZSetCommands::cmdZRem(Database& db, Connection& conn,
                           const std::vector<std::string>& args) {
    HTEntry* entry = db.findEntry(args[1]);
    if (!entry) {
        RespSerializer::writeInteger(conn.outgoing(), 0);
        return;
    }
    if (entry->value.type != DataType::ZSET) {
        RespSerializer::writeError(conn.outgoing(), WRONGTYPE);
        return;
    }
    auto& zset = std::get<ZSetData>(entry->value.data);

    int64_t removed = 0;
    for (size_t i = 2; i < args.size(); ++i) {
        auto it = zset.dict.find(args[i]);
        if (it != zset.dict.end()) {
            zset.skiplist.remove(it->first, it->second);
            zset.dict.erase(it);
            ++removed;
        }
    }
    // Auto-delete empty container.
    if (zset.dict.empty()) {
        db.del(args[1]);
    }
    RespSerializer::writeInteger(conn.outgoing(), removed);
}
