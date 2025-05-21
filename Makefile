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

# Benchmark settings
BENCH_DIR = benchmarks
BENCH_SRCS = $(BENCH_DIR)/bench_concat.c
BENCH_TARGET = bench_fconcat
BENCH_CFLAGS = -O3 -march=native -DNDEBUG
BENCH_ITERATIONS ?= 1000
BENCH_FILE_SIZE ?= 10M

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

.PHONY: all clean install benchmark bench-clean bench-report

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

benchmark: $(BENCH_TARGET)
	@echo "Running benchmarks..."
	@./$(BENCH_TARGET) $(BENCH_ITERATIONS) $(BENCH_FILE_SIZE)

$(BENCH_TARGET): $(BENCH_SRCS) src/concat.c
	@mkdir -p $(BENCH_DIR) 2>/dev/null || true
	$(CC) $(CFLAGS) $(BENCH_CFLAGS) -o $@ $^ $(LDFLAGS)

bench-clean:
	$(RM) $(BENCH_TARGET)
	$(RM) $(BENCH_DIR)/*.tmp

bench-report: benchmark
	@echo "Benchmark Report:"
	@echo "================="
	@echo "Version: $(VERSION)"
	@echo "Compiler: $(CC)"
	@echo "Optimization flags: $(BENCH_CFLAGS)"
	@echo "Iterations: $(BENCH_ITERATIONS)"
	@echo "File size: $(BENCH_FILE_SIZE)"
	@echo

debug:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "VERSION: $(VERSION)"

help:
	@echo "Available targets:"
	@echo "  all        - Build the fconcat executable"
	@echo "  clean      - Clean build artifacts"
	@echo "  install    - Install the executable"
	@echo "  benchmark  - Run performance benchmarks"
	@echo "  bench-report - Run benchmarks and generate a detailed report"
	@echo "  bench-clean  - Clean benchmark artifacts"
	@echo
	@echo "Benchmark options:"
	@echo "  make benchmark BENCH_ITmakeIONS=5000 BENCH_FILE_SIZE=100M"