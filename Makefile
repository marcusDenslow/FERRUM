CC = gcc
CFLAGS = -Wextra -g -Iinclude
LIBS = -lm -lncurses

# Directories
SRC_DIR = src
INC_DIR = include
BUILD_DIR = build

# Get all .c files from src directory
SOURCES = $(wildcard $(SRC_DIR)/*.c)
# Generate object file names in build directory
OBJECTS = $(SOURCES:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = shell

all: $(BUILD_DIR) $(TARGET)

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

.PHONY: all clean
