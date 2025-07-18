# Compiler settings
CC ?= gcc
CFLAGS = -Wall -Wextra -Werror -O3
LDFLAGS = -pthread
LIBS = -lm

# Check if we're building with plugins
ifdef PLUGINS
    CFLAGS += -DWITH_PLUGINS
    LIBS += -ldl
    LDFLAGS := $(filter-out -static,$(LDFLAGS))
else
    # Default to static linking without plugins
    LDFLAGS += -static
endif

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
        LIBS += -lrt
    endif
endif

# Detect if we're cross-compiling
ifeq ($(CROSS_COMPILE),aarch64-linux-gnu-)
    CC = aarch64-linux-gnu-gcc
    CFLAGS += -march=armv8-a
    PLUGIN_SUFFIX = .so
    IS_CROSS_COMPILE = 1
else ifeq ($(CROSS_COMPILE),mingw64)
    CC = $(MINGW_PREFIX)-gcc
    TARGET_SUFFIX = .exe
    PLUGIN_SUFFIX = .dll
    LDFLAGS = -static-libgcc -static-libstdc++
    LIBS = -lm -lwinpthread -lws2_32
    CFLAGS := $(filter-out -march=native,$(CFLAGS))
    CFLAGS += -march=x86-64
    IS_CROSS_COMPILE = 1
    IS_WINDOWS_CROSS = 1
else ifeq ($(CROSS_COMPILE),mingw32)
    CC = $(MINGW32_PREFIX)-gcc
    TARGET_SUFFIX = .exe
    PLUGIN_SUFFIX = .dll
    LDFLAGS = -static-libgcc -static-libstdc++
    LIBS = -lm -lwinpthread -lws2_32
    CFLAGS := $(filter-out -march=native,$(CFLAGS))
    CFLAGS += -march=i686
    IS_CROSS_COMPILE = 1
    IS_WINDOWS_CROSS = 1
else
    CFLAGS += -march=native
    PLUGIN_SUFFIX = .so
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
    PLUGIN_SUFFIX = .dll
    LIBS := $(filter-out -lrt,$(LIBS))
    LIBS += -lwinpthread -lws2_32
    LDFLAGS = -static-libgcc -static-libstdc++
    LIBS := $(filter-out -ldl,$(LIBS))
    IS_WINDOWS_CROSS = 1
endif

# Executable name
TARGET = fconcat$(TARGET_SUFFIX)
SRCS = src/main.c src/concat.c
OBJS = $(SRCS:.c=.o)

# Plugin settings
PLUGIN_DIR = plugins
PLUGIN_CFLAGS = -Wall -Wextra -O3 -fPIC
PLUGIN_LDFLAGS = -shared
PLUGIN_LIBS = 

# Windows-specific plugin settings
ifdef IS_WINDOWS_CROSS
    PLUGIN_CFLAGS += -DWITH_PLUGINS
    PLUGIN_LDFLAGS = -shared
    PLUGIN_LIBS = -lkernel32
else
    PLUGIN_LDFLAGS = -shared
    PLUGIN_LIBS = 
endif

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
    MKDIR = mkdir
    # For native Windows, use .dll
    ifndef PLUGIN_SUFFIX
        PLUGIN_SUFFIX = .dll
    endif
else
    RM = rm -f
    MKDIR = mkdir -p
    # For Unix systems, use .so
    ifndef PLUGIN_SUFFIX
        PLUGIN_SUFFIX = .so
    endif
endif

# Plugin source files
PLUGIN_SOURCES = $(wildcard $(PLUGIN_DIR)/*.c)
PLUGIN_TARGETS = $(PLUGIN_SOURCES:.c=$(PLUGIN_SUFFIX))

.PHONY: all clean clean-all install plugins plugins-enabled plugins-clean windows windows32 windows64 windows-all windows-plugins windows-plugins32 windows-plugins64 windows-plugins-all benchmark bench-clean bench-report profile release debug help debug-info debug-plugins debug-plugins-only

all: $(TARGET)

# Build with plugin support
plugins-enabled:
	$(MAKE) PLUGINS=1 all

# Windows cross-compilation targets
windows: windows64

windows64:
	$(MAKE) CROSS_COMPILE=mingw64 TARGET=fconcat-win64 clean all

windows32:
	$(MAKE) CROSS_COMPILE=mingw32 TARGET=fconcat-win32 clean all

windows-all: windows64 windows32

# Windows plugin targets
windows-plugins: windows-plugins64

windows-plugins64:
	$(MAKE) CROSS_COMPILE=mingw64 PLUGINS=1 plugins

windows-plugins32:
	$(MAKE) CROSS_COMPILE=mingw32 PLUGINS=1 plugins

windows-plugins-all: windows-plugins64 windows-plugins32

$(TARGET): $(OBJS)
	$(CC) $(OBJS) $(LDFLAGS) $(LIBS) -o $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Plugin targets
plugins: $(PLUGIN_TARGETS)

$(PLUGIN_DIR)/%$(PLUGIN_SUFFIX): $(PLUGIN_DIR)/%.c
ifdef IS_WINDOWS_CROSS
	# Windows DLL compilation (cross-compile)
	$(CC) $(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS) $< -o $@ $(PLUGIN_LIBS)
else
	# Unix shared library compilation
	$(CC) $(PLUGIN_CFLAGS) $(PLUGIN_LDFLAGS) $< -o $@ $(PLUGIN_LIBS)
endif

$(PLUGIN_DIR):
	$(MKDIR) $(PLUGIN_DIR)

# Performance profiling
profile: CFLAGS += -pg -g
profile: LDFLAGS += -pg
profile: $(TARGET)

# Optimized build for releases
release: CFLAGS = -Wall -Wextra -Werror -O3 -DNDEBUG -flto
release: LDFLAGS += -flto
release: 
	$(eval CFLAGS += $(if $(findstring aarch64,$(CC)),-march=armv8-a,-march=native))
	$(MAKE) $(TARGET)

# Debug build
debug: CFLAGS = -Wall -Wextra -g -O0 -DDEBUG
debug: LDFLAGS = -pthread
debug: 
	$(eval CFLAGS += $(if $(findstring aarch64,$(CC)),-march=armv8-a,-march=native))
	$(MAKE) $(TARGET)

# Debug build with plugin support
debug-plugins: CFLAGS = -Wall -Wextra -g -O0 -DDEBUG -DWITH_PLUGINS
debug-plugins: LDFLAGS = -pthread -g
debug-plugins: PLUGIN_CFLAGS = -Wall -Wextra -O0 -g -fPIC
debug-plugins: 
	$(eval CFLAGS += $(if $(findstring aarch64,$(CC)),-march=armv8-a,-march=native))
	$(MAKE) $(TARGET)

# Debug plugins separately
debug-plugins-only: PLUGIN_CFLAGS = -Wall -Wextra -O0 -g -fPIC
debug-plugins-only: plugins

# Benchmark targets
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

clean:
	$(RM) $(OBJS) $(TARGET)

plugins-clean:
	$(RM) $(PLUGIN_DIR)/*.so
	$(RM) $(PLUGIN_DIR)/*.dll
	@echo "Plugin modules cleaned"

clean-all: clean plugins-clean bench-clean
	$(RM) fconcat-win64.exe fconcat-win32.exe

install:
	mkdir -p $(DESTDIR)/usr/local/bin
	cp $(TARGET) $(DESTDIR)/usr/local/bin/

debug-info:
	@echo "CC: $(CC)"
	@echo "CFLAGS: $(CFLAGS)"
	@echo "LDFLAGS: $(LDFLAGS)"
	@echo "LIBS: $(LIBS)"
	@echo "VERSION: $(VERSION)"
	@echo "PLUGIN_SUFFIX: $(PLUGIN_SUFFIX)"
	@echo "PLUGIN_TARGETS: $(PLUGIN_TARGETS)"
	@echo "IS_WINDOWS_CROSS: $(IS_WINDOWS_CROSS)"
	@echo "IS_CROSS_COMPILE: $(IS_CROSS_COMPILE)"

help:
	@echo "Available targets:"
	@echo "  all            - Build fconcat (statically linked, no plugins)"
	@echo "  plugins-enabled - Build fconcat with plugin support (dynamic linking)"
	@echo "  plugins        - Build all plugin modules"
	@echo "  release        - Build optimized release version"
	@echo "  debug          - Build debug version with symbols"
	@echo "  debug-plugins  - Build debug version with plugin support"
	@echo "  debug-plugins-only - Build only plugins with debug symbols"
	@echo "  profile        - Build with profiling support"
	@echo "  clean          - Clean build artifacts"
	@echo "  clean-all      - Clean everything including plugins and benchmarks"
	@echo "  plugins-clean  - Clean only plugin modules (.so and .dll)"
	@echo "  install        - Install the executable"
	@echo "  benchmark      - Run performance benchmarks"
	@echo "  bench-report   - Run benchmarks and generate a detailed report"
	@echo "  bench-clean    - Clean benchmark artifacts"
	@echo "  debug-info     - Show build configuration"
	@echo
	@echo "Cross-compilation targets:"
	@echo "  windows        - Build for Windows 64-bit (same as windows64)"
	@echo "  windows64      - Build for Windows 64-bit using MinGW-w64"
	@echo "  windows32      - Build for Windows 32-bit using MinGW-w64"
	@echo "  windows-all    - Build both Windows 32-bit and 64-bit versions"
	@echo
	@echo "Windows plugin targets:"
	@echo "  windows-plugins     - Build Windows 64-bit plugins (same as windows-plugins64)"
	@echo "  windows-plugins64   - Build Windows 64-bit plugins using MinGW-w64"
	@echo "  windows-plugins32   - Build Windows 32-bit plugins using MinGW-w64"
	@echo "  windows-plugins-all - Build both Windows 32-bit and 64-bit plugins"
	@echo
	@echo "Cross-compilation options:"
	@echo "  MINGW_PREFIX   - MinGW 64-bit prefix (default: x86_64-w64-mingw32)"
	@echo "  MINGW32_PREFIX - MinGW 32-bit prefix (default: i686-w64-mingw32)"
	@echo
	@echo "Plugin workflow (Linux):"
	@echo "  make plugins-enabled && make plugins"
	@echo "  ./fconcat --plugin ./plugins/line_numbers.so /path/to/src output.txt"
	@echo
	@echo "Plugin workflow (Windows cross-compile):"
	@echo "  make windows64 PLUGINS=1 && make windows-plugins64"
	@echo "  # Copy fconcat-win64.exe and plugins/*.dll to Windows"
	@echo "  # Run: .\\fconcat-win64.exe . output.txt --plugin .\\plugins\\line_numbers.dll"
	@echo
	@echo "Debug workflow:"
	@echo "  make debug-plugins && make debug-plugins-only"
	@echo "  gdb --args ./fconcat . output.txt --plugin ./plugins/remove_main.so"
	@echo
	@echo "Standard workflow (no plugins):"
	@echo "  make"
	@echo "  ./fconcat /path/to/src output.txt"
	@echo
	@echo "Benchmark options:"
	@echo "  make benchmark BENCH_ITERATIONS=5000 BENCH_FILE_SIZE=100M"
	@echo
	@echo "Threading note:"
	@echo "  This version uses multi-threading and requires pthread support"