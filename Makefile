CC = gcc
CFLAGS = -Wextra -g -Iinclude -Iinclude/core -Iinclude/input -Iinclude/history -Iinclude/search -Iinclude/ui -Iinclude/data -Iinclude/git -Iinclude/system -Iinclude/utils
LIBS = -lm -lncurses

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build
TEST_DIR = tests

# Get all .c files recursively from src directory and subdirectories
SOURCES = $(shell find $(SRC_DIR) -name "*.c")
# Generate object file names in build directory, preserving subdirectory structure
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = shell

# Test targets
TEST_TIMER = $(TEST_DIR)/test_timer

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

# Create build subdirectories and compile
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

# Test targets
test: test_timer

test_timer: $(TEST_TIMER)
	@echo "\n--- Running timer tests ---"
	@./$(TEST_TIMER)

$(TEST_TIMER): $(TEST_DIR)/test_timer.c $(TEST_DIR)/timer_testable.c
	@mkdir -p $(TEST_DIR)
	$(CC) $(CFLAGS) $^ -o $@

clean_tests:
	rm -f $(TEST_TIMER)

.PHONY: all clean test test_timer clean_tests
