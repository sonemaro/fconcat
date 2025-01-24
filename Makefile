CC = gcc
CFLAGS = -Wall -Wextra -O2
TARGET = fconcat
SRCS = src/main.c src/concat.c
OBJS = $(SRCS:.c=.o)

# Get version from git tag
VERSION := $(shell git describe --tags --always --dirty)
CFLAGS += -DVERSION=\"$(VERSION)\"

ifeq ($(OS),Windows_NT)
    TARGET := $(TARGET).exe
    RM = del /Q
else
    RM = rm -f
endif

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)

install:
	mkdir -p $(DESTDIR)/usr/local/bin
	cp $(TARGET) $(DESTDIR)/usr/local/bin/

