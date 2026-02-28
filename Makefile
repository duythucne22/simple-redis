CXX = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -g
INCLUDES = -Isrc

BUILD_DIR = build

# ── Net layer source files ──────────────────────────────────────────────────
NET_SRCS = src/net/Buffer.cpp \
           src/net/Connection.cpp \
           src/net/Listener.cpp \
           src/net/EventLoop.cpp

NET_OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(NET_SRCS))

# ── Proto layer source files ────────────────────────────────────────────────
PROTO_SRCS = src/proto/RespParser.cpp \
             src/proto/RespSerializer.cpp

PROTO_OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(PROTO_SRCS))

# ── Store layer source files ────────────────────────────────────────────────
STORE_SRCS = src/store/RedisObject.cpp \
             src/store/HashTable.cpp \
             src/store/Database.cpp

STORE_OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(STORE_SRCS))

# ── Command layer source files ──────────────────────────────────────────────
CMD_SRCS = src/cmd/CommandTable.cpp \
           src/cmd/StringCommands.cpp \
           src/cmd/KeyCommands.cpp

CMD_OBJS = $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(CMD_SRCS))

# ── All object files (excluding main) ───────────────────────────────────────
ALL_OBJS = $(NET_OBJS) $(PROTO_OBJS) $(STORE_OBJS) $(CMD_OBJS)

# ── Server binary ──────────────────────────────────────────────────────────
MAIN_OBJ = $(BUILD_DIR)/main.o
SERVER   = $(BUILD_DIR)/simple-redis

# ── Unit test binaries ─────────────────────────────────────────────────────
TEST_BUFFER      = $(BUILD_DIR)/test_buffer
TEST_RESP_PARSER = $(BUILD_DIR)/test_resp_parser
TEST_HASH_TABLE  = $(BUILD_DIR)/test_hash_table

# ── Targets ────────────────────────────────────────────────────────────────
.PHONY: all clean test

all: $(SERVER) $(TEST_BUFFER) $(TEST_RESP_PARSER) $(TEST_HASH_TABLE)

$(SERVER): $(ALL_OBJS) $(MAIN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(TEST_BUFFER): tests/unit/test_buffer.cpp $(BUILD_DIR)/net/Buffer.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(TEST_RESP_PARSER): tests/unit/test_resp_parser.cpp $(BUILD_DIR)/net/Buffer.o $(BUILD_DIR)/proto/RespParser.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

$(TEST_HASH_TABLE): tests/unit/test_hash_table.cpp $(BUILD_DIR)/store/HashTable.o $(BUILD_DIR)/store/RedisObject.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

test: $(TEST_BUFFER) $(TEST_RESP_PARSER) $(TEST_HASH_TABLE)
	@echo "=== Running unit tests ==="
	./$(TEST_BUFFER)
	./$(TEST_RESP_PARSER)
	./$(TEST_HASH_TABLE)

clean:
	rm -rf $(BUILD_DIR)
