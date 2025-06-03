# fconcat

[![Build Status](https://github.com/sonemaro/fconcat/workflows/Build%20fconcat/badge.svg)](https://github.com/sonemaro/fconcat/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Release](https://img.shields.io/github/v/release/sonemaro/fconcat)](https://github.com/sonemaro/fconcat/releases)
[![Platform Support](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)](https://github.com/sonemaro/fconcat/releases)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://github.com/sonemaro/fconcat)
[![Downloads](https://img.shields.io/github/downloads/sonemaro/fconcat/total)](https://github.com/sonemaro/fconcat/releases)

A high-performance file concatenation tool designed for developers who need to quickly combine directory trees into single files. Built in C with multi-threading support and intelligent handling of binary files, symbolic links, and Unicode filenames.

## Why fconcat?

If you've ever tried to feed an entire codebase to an AI system, generate documentation from multiple files, or analyze project structure, you've probably reached for tools like `find` combined with `cat`, shell scripts, or wrote your own solution. These approaches work but they're slow, don't handle edge cases well, and often produce messy output.

fconcat was built to solve these problems properly. It's significantly faster than traditional approaches while providing clean, structured output and handling the complexities of modern filesystems correctly.

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

### Pre-built Binaries
Download from the [releases page](https://github.com/sonemaro/fconcat/releases) for Linux, macOS, and Windows.

## Basic Usage

```bash
# Concatenate all files in a directory
fconcat ./my_project output.txt

# Use multiple threads for large projects
fconcat ./linux_kernel output.txt --threads 8

# Handle different file types appropriately
fconcat ./mixed_project output.txt --binary-placeholder --symlinks follow
```

## Performance

fconcat is designed for speed. In benchmarks against traditional tools:

- **55x faster** than `find` + `cat` on typical codebases
- **3.8x faster** even when including binary files and following symlinks
- Scales well with multiple threads on large datasets
- Constant memory usage regardless of project size

The performance comes from several optimizations: streaming output to avoid memory bottlenecks, hash-table based pattern matching for exclusions, platform-optimized file I/O, and efficient multi-threading that doesn't create unnecessary overhead on small projects.

## Features

**Smart Binary Handling**: Automatically detects binary files using multiple heuristics and handles them according to your preference. You can skip them entirely, include them, or show placeholders.

**Symbolic Link Support**: Proper handling of symbolic links with cycle detection. Choose to skip them, follow them safely, include their targets, or show them as placeholders.

**Unicode Support**: Full Unicode filename support across platforms. Uses native Windows APIs for proper UTF-16 handling on Windows and UTF-8 throughout on Unix systems.

**Efficient Exclusion**: Pattern-based exclusion using hash tables for O(1) average lookup time. Supports wildcards and can handle thousands of patterns without performance degradation.

**Cross-Platform**: Builds and runs on Linux, macOS, and Windows with consistent behavior and performance characteristics.

## Command Line Options

```
fconcat <input_directory> <output_file> [options]

--exclude <patterns>     Exclude files matching patterns (supports * and ?)
--show-size, -s         Display file sizes in directory structure
--threads <n>, -t <n>   Number of worker threads (default: 4)

Binary file handling:
--binary-skip           Skip binary files (default)
--binary-include        Include binary files
--binary-placeholder    Show placeholders for binary files

Symbolic link handling:
--symlinks <mode>       skip, follow, include, or placeholder (default: skip)
```

## Output Format

fconcat generates structured output with two main sections:

**Directory Structure**: A tree view showing the organization of files and directories, optionally with file sizes.

**File Contents**: The actual content of each file, clearly separated with headers indicating the file path.

The output format is designed to be both human-readable and suitable for processing by other tools or AI systems.

## Examples

### Code Analysis
```bash
# Generate clean output for AI analysis
fconcat ./my_react_app analysis.txt --exclude "node_modules/*" "*.map" "build/*"
```

### Documentation Generation
```bash
# Include all text files with size information
fconcat ./docs documentation.txt --show-size --binary-skip
```

### Security Audit
```bash
# Complete project analysis including symlinks
fconcat ./project audit.txt --symlinks follow --binary-placeholder --threads 8
```

### Large Codebases
```bash
# Linux kernel analysis (fast, text-only)
fconcat /usr/src/linux kernel_source.txt --threads 16 --exclude "*.o" "*.ko"
```

## Technical Details

fconcat is implemented in C for performance and uses POSIX threads for parallelization. The architecture includes several key components:

**Work Queue System**: Multi-threaded directory traversal using a producer-consumer pattern with worker threads processing files independently.

**Streaming Output**: A separate writer thread handles output sequencing to prevent I/O blocking and maintain constant memory usage.

**Pattern Matching**: Exclude patterns are stored in hash tables with chained collision resolution for efficient lookup.

**Platform Abstraction**: Native file system APIs are used on each platform while maintaining a unified interface.

The tool is designed to handle edge cases properly: broken symbolic links, permission errors, and unusual filenames are handled gracefully without stopping processing.

## Environment Variables

Set `FCONCAT_VERBOSE=1` to enable detailed logging for debugging or performance analysis.

## Building

fconcat requires a C compiler with C11 support and pthread. The build system supports cross-compilation and generates optimized binaries for release builds.

```bash
make              # Standard build
make release      # Optimized build with LTO
make debug        # Debug build with symbols
make benchmark    # Build and run performance tests
```

## License

MIT License. See LICENSE file for details.