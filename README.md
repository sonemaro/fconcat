# fconcat

[![Build Status](https://github.com/sonemaro/fconcat/workflows/Build%20fconcat/badge.svg)](https://github.com/sonemaro/fconcat/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Release](https://img.shields.io/github/v/release/sonemaro/fconcat)](https://github.com/sonemaro/fconcat/releases)
[![Platform Support](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)](https://github.com/sonemaro/fconcat/releases)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://github.com/sonemaro/fconcat)
[![Downloads](https://img.shields.io/github/downloads/sonemaro/fconcat/total)](https://github.com/sonemaro/fconcat/releases)

A high-performance file concatenation tool for combining directory trees into single files with intelligent handling of binary files, symbolic links, and Unicode filenames. Features a powerful streaming plugin system for content transformation and analysis.

## Installation

### Quick Install
```bash
curl -sSL https://raw.githubusercontent.com/sonemaro/fconcat/main/scripts/install.sh | bash
```

### From Source
```bash
git clone https://github.com/sonemaro/fconcat.git
cd fconcat
make
sudo make install
```

### Building with Plugin Support
```bash
make plugins-enabled
make plugins
```

### Pre-built Binaries
Download from the [releases page](https://github.com/sonemaro/fconcat/releases) for Linux, macOS, and Windows.

## Basic Usage

```bash
# Concatenate all files in a directory
fconcat ./my_project output.txt

# Handle different file types appropriately
fconcat ./mixed_project output.txt --binary-placeholder --symlinks follow

# Use plugins for content transformation
fconcat ./code output.txt --plugin ./plugins/remove_main.so
```

## Features

- **Binary File Detection**: Automatically detects and handles binary files with configurable options including skip, include, or placeholder modes
- **Symbolic Link Support**: Safe handling of symbolic links with cycle detection and multiple traversal strategies
- **Unicode Support**: Full Unicode filename support across platforms with proper encoding handling
- **Pattern Exclusion**: Efficient pattern-based file exclusion with wildcard support using hash table implementation
- **Plugin System**: Extensible streaming plugin architecture for content transformation, analysis, and processing
- **Cross-Platform**: Consistent behavior and performance on Linux, macOS, and Windows with platform-specific optimizations

## Command Line Options

```
fconcat <input_directory> <output_file> [options]

--exclude <patterns>        Exclude files matching patterns (supports * and ?)
--show-size, -s            Display file sizes in directory structure
--plugin <path>            Load streaming plugin from specified path
--interactive              Keep plugins active after processing completes

Binary file handling:
--binary-skip              Skip binary files entirely (default)
--binary-include           Include binary files in concatenation
--binary-placeholder       Show descriptive placeholders for binary files

Symbolic link handling:
--symlinks <mode>          skip, follow, include, or placeholder (default: skip)
                          skip: ignore all symbolic links
                          follow: follow symlinks with loop detection
                          include: include symlink targets as regular files
                          placeholder: show symlinks in structure but don't follow
```

## Output Format

The output file contains two distinct sections providing comprehensive project analysis:

**Directory Structure**: A hierarchical tree view of all files and directories with Unicode box-drawing characters, optional file sizes, and symbolic link indicators. This section provides immediate visual understanding of project organization and can include size information for capacity planning.

**File Contents**: Individual file contents with clear path headers and proper separation. Each file begins with a comment indicating its relative path, followed by the complete file contents, and ends with blank lines for visual separation.

## Plugin System

The fconcat plugin system provides a powerful streaming architecture for content transformation and analysis. Plugins process files in 4KB chunks, enabling memory-efficient handling of large files while maintaining the ability to perform complex transformations.

### Plugin Architecture

Plugins are implemented as shared libraries (`.so` on Linux/macOS, `.dll` on Windows) that export a `get_plugin()` function returning a `StreamingPlugin` structure. The plugin system supports:

- **Per-file initialization and cleanup** with custom state management
- **Streaming processing** with configurable chunk sizes for memory efficiency
- **Chain processing** where multiple plugins can be applied sequentially
- **Cross-platform compatibility** with consistent behavior across operating systems
- **Error handling** with graceful fallback to original content on plugin failures

### Plugin Development

Creating a plugin involves implementing the `StreamingPlugin` interface with specific callback functions for different processing phases:

```c
typedef struct {
    const char *name;
    const char *version;
    
    // Global lifecycle
    int (*init)(void);
    void (*cleanup)(void);
    
    // Per-file processing
    PluginContext *(*file_start)(const char *relative_path);
    int (*process_chunk)(PluginContext *ctx, const char *input, size_t input_size,
                         char **output, size_t *output_size);
    int (*file_end)(PluginContext *ctx, char **final_output, size_t *final_size);
    void (*file_cleanup)(PluginContext *ctx);
} StreamingPlugin;
```

The plugin context structure provides per-file state management and processing information:

```c
typedef struct {
    void *private_data;        // Plugin-specific state
    const char *file_path;     // Current file being processed
    size_t total_processed;    // Total bytes processed so far
    int plugin_index;          // Plugin position in chain
} PluginContext;
```

### Simple Plugin Example

Here's a complete example of a plugin that removes main() functions from C/C++ source files:

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>

// Plugin state for tracking main function removal
typedef struct {
    bool in_main_function;
    bool in_string;
    bool in_comment;
    int brace_count;
    int main_start_brace_level;
    bool is_c_file;
} RemoveMainState;

// Check if file is C/C++ source
static bool is_c_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    return (strcmp(ext, ".c") == 0 || strcmp(ext, ".cpp") == 0 ||
            strcmp(ext, ".cc") == 0 || strcmp(ext, ".cxx") == 0);
}

// Initialize plugin state for new file
static PluginContext *remove_main_file_start(const char *relative_path) {
    PluginContext *ctx = malloc(sizeof(PluginContext));
    if (!ctx) return NULL;
    
    RemoveMainState *state = malloc(sizeof(RemoveMainState));
    if (!state) {
        free(ctx);
        return NULL;
    }
    
    memset(state, 0, sizeof(RemoveMainState));
    state->is_c_file = is_c_file(relative_path);
    
    ctx->private_data = state;
    ctx->file_path = strdup(relative_path);
    ctx->total_processed = 0;
    
    return ctx;
}

// Process chunk of file data
static int remove_main_process_chunk(PluginContext *ctx, const char *input, size_t input_size,
                                     char **output, size_t *output_size) {
    RemoveMainState *state = (RemoveMainState *)ctx->private_data;
    
    // Pass through non-C files unchanged
    if (!state->is_c_file) {
        *output = malloc(input_size);
        if (!*output) return -1;
        memcpy(*output, input, input_size);
        *output_size = input_size;
        return 0;
    }
    
    // Process C/C++ files to remove main functions
    char *out = malloc(input_size + 100); // Extra space for comments
    size_t out_pos = 0;
    
    for (size_t i = 0; i < input_size; i++) {
        char c = input[i];
        
        // Handle strings and comments (simplified for example)
        if (c == '"' && !state->in_comment) {
            state->in_string = !state->in_string;
        } else if (c == '{' && !state->in_string && !state->in_comment) {
            state->brace_count++;
        } else if (c == '}' && !state->in_string && !state->in_comment) {
            state->brace_count--;
            if (state->in_main_function && state->brace_count == state->main_start_brace_level) {
                state->in_main_function = false;
                strcpy(out + out_pos, "\n// [main function removed]\n");
                out_pos += strlen("\n// [main function removed]\n");
                continue;
            }
        }
        
        // Detect main function start (simplified)
        if (!state->in_main_function && !state->in_string && !state->in_comment &&
            i + 5 < input_size && strncmp(input + i, "main(", 5) == 0) {
            state->in_main_function = true;
            state->main_start_brace_level = state->brace_count;
            continue;
        }
        
        // Add character to output if not inside main function
        if (!state->in_main_function) {
            out[out_pos++] = c;
        }
    }
    
    *output = out;
    *output_size = out_pos;
    return 0;
}

// Plugin interface
static StreamingPlugin remove_main_plugin = {
    .name = "Remove Main Function",
    .version = "1.0.0",
    .init = NULL,
    .cleanup = NULL,
    .file_start = remove_main_file_start,
    .process_chunk = remove_main_process_chunk,
    .file_end = NULL,
    .file_cleanup = NULL  // Implement proper cleanup in real plugin
};

// Plugin entry point
StreamingPlugin *get_plugin(void) {
    return &remove_main_plugin;
}
```

### Building Plugins

Compile plugins as shared libraries with position-independent code:

```bash
# Linux/macOS
gcc -Wall -Wextra -O3 -fPIC -shared -o remove_main.so remove_main.c

# Windows (MinGW)
gcc -Wall -Wextra -O3 -shared -o remove_main.dll remove_main.c
```

Using the provided Makefile:

```bash
make plugins-enabled  # Build fconcat with plugin support
make plugins          # Build all plugins in plugins/ directory
```

### Plugin Usage

Load plugins at runtime using the `--plugin` option:

```bash
# Single plugin
fconcat ./src output.txt --plugin ./plugins/remove_main.so

# Multiple plugins (processed in sequence)
fconcat ./src output.txt --plugin ./plugins/line_numbers.so --plugin ./plugins/syntax_highlight.so

# Interactive mode (keeps plugins loaded)
fconcat ./src output.txt --plugin ./plugins/tcp_server.so --interactive
```

## Examples

### Code Analysis for AI Processing
```bash
# Generate clean output suitable for AI analysis with main functions removed
fconcat ./my_react_app analysis.txt --exclude "node_modules/*" "*.map" "build/*" --plugin ./plugins/remove_main.so
```

### Documentation Generation
```bash
# Include all text files with size information and line numbering
fconcat ./docs documentation.txt --show-size --binary-skip --plugin ./plugins/line_numbers.so
```

### Security Audit
```bash
# Complete project analysis including symlinks with comment stripping
fconcat ./project audit.txt --symlinks follow --binary-placeholder --plugin ./plugins/remove_comments.so
```

### Large Codebase Processing
```bash
# Process Linux kernel source with multiple transformations
fconcat /usr/src/linux kernel_analysis.txt --exclude "*.o" "*.ko" --plugin ./plugins/remove_main.so --plugin ./plugins/function_analyzer.so
```

## Building

Requirements: C compiler with C11 support, pthread library, and optionally dlopen support for plugins.

```bash
make              # Standard build (static linking, no plugins)
make plugins-enabled  # Build with plugin support
make release      # Optimized build with link-time optimization
make debug        # Debug build with symbols and verbose output
```

### Cross-Compilation for Windows

```bash
# Install MinGW-w64 cross-compiler
sudo apt-get install gcc-mingw-w64

# Build for Windows
make windows64    # 64-bit Windows executable
make windows32    # 32-bit Windows executable
make windows-plugins64  # Build Windows plugins
```

## Performance Characteristics

The tool is designed for efficient processing of large directory structures with minimal memory usage. Performance characteristics include:

- **Constant memory usage** regardless of directory size or file count through streaming processing
- **Efficient binary detection** using configurable sample sizes and heuristic analysis
- **Optimized pattern matching** with hash table implementation for exclude patterns
- **Platform-specific optimizations** for directory traversal and file I/O operations
- **Plugin overhead** is minimal when no plugins are loaded, with graceful fallback on plugin errors

## License

MIT License. See LICENSE file for details.