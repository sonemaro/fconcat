# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -O3
LDFLAGS = -static -pthread
LIBS = -lm

# Cross-compilation settings
MINGW_PREFIX ?= x86_64-w64-mingw32
MINGW32_PREFIX ?= i686-w64-mingw32

# Platform-specific library initialization
ifeq ($(OS),Windows_NT)
    # Native Windows build
    LIBS = -lm -lwinpthread -lws2_32
else
    # Unix-like systems
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Linux)
        LIBS = -lm -lrt
    else
        LIBS = -lm
    endif
endif

# Detect if we're cross-compiling
ifeq ($(CROSS_COMPILE),aarch64-linux-gnu-)
    CC = aarch64-linux-gnu-gcc
    # Use generic ARM64 optimization for cross-compilation
    CFLAGS += -march=armv8-a
else ifeq ($(CROSS_COMPILE),mingw64)
    CC = $(MINGW_PREFIX)-gcc
    TARGET_SUFFIX = .exe
    LDFLAGS = -static-libgcc -static-libstdc++ -static
    LIBS = -lm -lwinpthread -lws2_32
    CFLAGS := $(filter-out -march=native,$(CFLAGS))
    CFLAGS += -march=x86-64
else ifeq ($(CROSS_COMPILE),mingw32)
    CC = $(MINGW32_PREFIX)-gcc
    TARGET_SUFFIX = .exe
    LDFLAGS = -static-libgcc -static-libstdc++ -static
    LIBS = -lm -lwinpthread -lws2_32
    CFLAGS := $(filter-out -march=native,$(CFLAGS))
    CFLAGS += -march=i686
else
    # Use native optimization for native compilation
    CFLAGS += -march=native
endif

# For other cross-compilation scenarios
ifneq (,$(findstring aarch64,$(CC)))
    ifneq (,$(findstring linux-gnu,$(CC)))
        # Cross-compiling for ARM64
        CFLAGS += -march=armv8-a
        CFLAGS := $(filter-out -march=native,$(CFLAGS))
    endif
endif

# Windows cross-compilation detection
ifneq (,$(findstring mingw,$(CC)))
    TARGET_SUFFIX = .exe
    # Remove -lrt for Windows builds and ensure Windows libraries
    LIBS := $(filter-out -lrt,$(LIBS))
    LIBS += -lwinpthread -lws2_32
    LDFLAGS = -static-libgcc -static-libstdc++ -static
endif

# Executable name
TARGET = fconcat$(TARGET_SUFFIX)
SRCS = src/main.c src/concat.c
OBJS = $(SRCS:.c=.o)

# Benchmark settings
BENCH_DIR = benchmarks
BENCH_SRCS = $(BENCH_DIR)/bench_concat.c
BENCH_TARGET = bench_fconcat
BENCH_CFLAGS = -O3 -DNDEBUG -pthread
BENCH_ITERATIONS ?= 1000
BENCH_FILE_SIZE ?= 10M

# Get version from git tag
VERSION := $(shell git describe --tags --always --dirty 2>/dev/null || echo "unknown")
CFLAGS += -DVERSION=\"$(VERSION)\"

# Platform-specific settings
ifeq ($(OS),Windows_NT)
    RM = del /Q
else
    RM = rm -f
endif

.PHONY: all clean clean-all install benchmark bench-clean bench-report profile windows windows32 windows64 windows-all

all: $(TARGET)

# Windows cross-compilation targets
windows: windows64

windows64:
	$(MAKE) CROSS_COMPILE=mingw64 TARGET=fconcat-win64 clean all

windows32:
	$(MAKE) CROSS_COMPILE=mingw32 TARGET=fconcat-win32 clean all

# Build both Windows versions
windows-all: windows64 windows32

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	$(RM) $(OBJS) $(TARGET)

clean-all: clean
	$(RM) fconcat-win64.exe fconcat-win32.exe

install:
	mkdir -p $(DESTDIR)/usr/local/bin
	cp $(TARGET) $(DESTDIR)/usr/local/bin/

# Performance profiling
profile: CFLAGS += -pg -g
profile: LDFLAGS += -pg
profile: $(TARGET)

# Optimized build for releases
release: CFLAGS = -Wall -Wextra -Werror -O3 -DNDEBUG -flto
release: LDFLAGS += -flto
release: clean-march-flags
release: $(TARGET)

# Debug build
debug: CFLAGS = -Wall -Wextra -g -O0 -DDEBUG
debug: LDFLAGS = -pthread
debug: clean-march-flags  
debug: $(TARGET)

# Helper target to clean march flags and add appropriate ones
clean-march-flags:
	$(eval CFLAGS := $(filter-out -march=native -march=armv8-a,$(CFLAGS)))
	$(eval ifneq (,$(findstring aarch64,$(CC)))
		CFLAGS += -march=armv8-a
	else
		CFLAGS += -march=native
	endif)

benchmark: $(BENCH_TARGET)
	@echo "Running benchmarks..."
	@./$(BENCH_TARGET) $(BENCH_ITERATIONS) $(BENCH_FILE_SIZE)

$(BENCH_TARGET): $(BENCH_SRCS) src/concat.c
	@mkdir -p $(BENCH_DIR) 2>/dev/null || true
	$(CC) $(CFLAGS) $(BENCH_CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)

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

debug-info:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "LIBS: $(LIBS)"
	@echo "VERSION: $(VERSION)"

help:
	@echo "Available targets:"
	@echo "  all        - Build the fconcat executable (default)"
	@echo "  release    - Build optimized release version"
	@echo "  debug      - Build debug version with symbols"
	@echo "  profile    - Build with profiling support"
	@echo "  clean      - Clean build artifacts"
	@echo "  install    - Install the executable"
	@echo "  benchmark  - Run performance benchmarks"
	@echo "  bench-report - Run benchmarks and generate a detailed report"
	@echo "  bench-clean  - Clean benchmark artifacts"
	@echo "  debug-info - Show build configuration"
	@echo
	@echo "Cross-compilation targets:"
	@echo "  windows    - Build for Windows 64-bit (same as windows64)"
	@echo "  windows64  - Build for Windows 64-bit using MinGW-w64"
	@echo "  windows32  - Build for Windows 32-bit using MinGW-w64"
	@echo "  windows-all - Build both Windows 32-bit and 64-bit versions"
	@echo
	@echo "Cross-compilation options:"
	@echo "  MINGW_PREFIX   - MinGW 64-bit prefix (default: x86_64-w64-mingw32)"
	@echo "  MINGW32_PREFIX - MinGW 32-bit prefix (default: i686-w64-mingw32)"
	@echo
	@echo "Benchmark options:"
	@echo "  make benchmark BENCH_ITERATIONS=5000 BENCH_FILE_SIZE=100M"
	@echo
	@echo "Threading note:"
	@echo "  This version uses multi-threading and requires pthread support"