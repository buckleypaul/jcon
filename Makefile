# jcon — minimal build + test Makefile.
#
# Targets:
#   make            build + run all test configurations
#   make test       same as default
#   make debug      run tests built with assertions active
#   make release    run tests built with NDEBUG (exercises usage-error latching)
#   make float      run tests built with JCON_ENABLE_FLOAT
#   make lib        build the object file for consumer linking
#   make clean      remove build artifacts

CC      ?= cc
CSTD    ?= -std=c11
WARN    ?= -Wall -Wextra -Wpedantic
INC     := -Iinclude
CFLAGS  ?= $(CSTD) $(WARN) $(INC)

SRC        := src/jcon.c
TEST_SRC   := tests/test_jcon.c
BUILD_DIR  := build

.PHONY: all test debug release float lib clean

all: test

test: debug release float
	@echo ""
	@echo "All test configurations passed."

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

# Debug build: assertions active, happy-path + I/O-error tests only.
$(BUILD_DIR)/test-debug: $(SRC) $(TEST_SRC) include/jcon.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -g -O0 $(SRC) $(TEST_SRC) -o $@

debug: $(BUILD_DIR)/test-debug
	@echo "--- debug build ---"
	@./$<

# Release build: NDEBUG, adds usage-error tests that rely on graceful latching.
$(BUILD_DIR)/test-release: $(SRC) $(TEST_SRC) include/jcon.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 -DNDEBUG $(SRC) $(TEST_SRC) -o $@

release: $(BUILD_DIR)/test-release
	@echo "--- release build (NDEBUG) ---"
	@./$<

# Float build: JCON_ENABLE_FLOAT on (still debug-style for easier failure output).
$(BUILD_DIR)/test-float: $(SRC) $(TEST_SRC) include/jcon.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -g -O0 -DJCON_ENABLE_FLOAT $(SRC) $(TEST_SRC) -o $@

float: $(BUILD_DIR)/test-float
	@echo "--- float build (JCON_ENABLE_FLOAT) ---"
	@./$<

# Standalone library object (handy for consumer integration checks).
$(BUILD_DIR)/jcon.o: $(SRC) include/jcon.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -O2 -c $(SRC) -o $@

lib: $(BUILD_DIR)/jcon.o

clean:
	rm -rf $(BUILD_DIR)
