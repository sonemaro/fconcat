# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -O2
LDFLAGS = -static

# For cross-compilation support
ifeq ($(CROSS_COMPILE),aarch64-linux-gnu-)
    CC = aarch64-linux-gnu-gcc
endif

# Executable name
TARGET = fconcat
SRCS = src/main.c src/concat.c
OBJS = $(SRCS:.c=.o)

# Get version from git tag
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")
CFLAGS += -DVERSION=\"$(VERSION)\"

# Platform-specific adjustments
ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
    RM = del /Q
    LDFLAGS = 
else
    RM = rm -f
endif

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

debug:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "VERSION: $(VERSION)"