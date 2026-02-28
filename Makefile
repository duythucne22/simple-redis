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

# ── Server binary ──────────────────────────────────────────────────────────
MAIN_OBJ = $(BUILD_DIR)/main.o
SERVER   = $(BUILD_DIR)/simple-redis

# ── Unit test binaries ─────────────────────────────────────────────────────
TEST_BUFFER = $(BUILD_DIR)/test_buffer

# ── Targets ────────────────────────────────────────────────────────────────
.PHONY: all clean test

all: $(SERVER) $(TEST_BUFFER)

$(SERVER): $(NET_OBJS) $(MAIN_OBJ)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BUILD_DIR)/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -c -o $@ $<

$(TEST_BUFFER): tests/unit/test_buffer.cpp $(BUILD_DIR)/net/Buffer.o
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) -o $@ $^

test: $(TEST_BUFFER)
	@echo "=== Running unit tests ==="
	./$(TEST_BUFFER)

clean:
	rm -rf $(BUILD_DIR)
