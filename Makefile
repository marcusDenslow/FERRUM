CC = gcc
CFLAGS = -Wextra -g
LIBS = -lm -lncurses

# Get all .c files
SOURCES = $(wildcard *.c)
# Generate object file names from source file names
OBJECTS = $(SOURCES:.c=.o)
TARGET = shell

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: all clean
