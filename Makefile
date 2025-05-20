# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -O2

# Platform-specific settings
ifeq ($(OS),Windows_NT)
    TARGET := fconcat.exe
    RM = del /Q
    # No static linking on Windows
    LDFLAGS =
else
    TARGET = fconcat
    RM = rm -f
    
    # Check if macOS (Darwin)
    UNAME_S := $(shell uname -s 2>/dev/null || echo "unknown")
    ifeq ($(UNAME_S),Darwin)
        # No static linking on macOS
        LDFLAGS = 
    else
        # Static linking only on Linux
        LDFLAGS = -static
    endif
endif

# For cross-compilation support
ifeq ($(CROSS_COMPILE),aarch64-linux-gnu-)
    CC = aarch64-linux-gnu-gcc
endif

# Executable name
SRCS = src/main.c src/concat.c
OBJS = $(SRCS:.c=.o)

# Get version from git tag
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")
CFLAGS += -DVERSION=\"$(VERSION)\"

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)

install:
	mkdir -p $(DESTDIR)/usr/local/bin
	cp $(TARGET) $(DESTDIR)/usr/local/bin/