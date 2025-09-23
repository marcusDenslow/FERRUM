CC = gcc
CFLAGS = -Wextra -g -Iinclude -Iinclude/core -Iinclude/input -Iinclude/history -Iinclude/search -Iinclude/ui -Iinclude/data -Iinclude/git -Iinclude/system -Iinclude/utils
LIBS = -lm -lncurses

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Get all .c files recursively from src directory and subdirectories
SOURCES = $(shell find $(SRC_DIR) -name "*.c")
# Generate object file names in build directory, preserving subdirectory structure
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = shell

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

.PHONY: all clean
