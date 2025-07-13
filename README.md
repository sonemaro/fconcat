# fconcat

[![Build Status](https://github.com/sonemaro/fconcat/workflows/Build%20fconcat/badge.svg)](https://github.com/sonemaro/fconcat/actions)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![Release](https://img.shields.io/github/v/release/sonemaro/fconcat)](https://github.com/sonemaro/fconcat/releases)
[![Platform Support](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20Windows-blue)](https://github.com/sonemaro/fconcat/releases)
[![Language](https://img.shields.io/badge/language-C-blue.svg)](https://github.com/sonemaro/fconcat)
[![Downloads](https://img.shields.io/github/downloads/sonemaro/fconcat/total)](https://github.com/sonemaro/fconcat/releases)

A high-performance file concatenation tool for combining directory trees into single files. Features multi-threading support and intelligent handling of binary files, symbolic links, and Unicode filenames.

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

- **55x faster** than `find` + `cat` on typical codebases
- **3.8x faster** when including binary files and following symlinks
- Scales efficiently with multiple threads
- Constant memory usage regardless of project size

## Features

- **Binary File Detection**: Automatically detects and handles binary files with configurable options
- **Symbolic Link Support**: Safe handling of symbolic links with cycle detection
- **Unicode Support**: Full Unicode filename support across platforms
- **Pattern Exclusion**: Efficient pattern-based file exclusion with wildcard support
- **Cross-Platform**: Consistent performance on Linux, macOS, and Windows

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

The output contains two sections:

1. **Directory Structure**: Tree view of files and directories with optional file sizes
2. **File Contents**: Individual file contents with clear path headers

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


## Building

Requirements: C compiler with C11 support and pthread.

```bash
make              # Standard build
make release      # Optimized build
make debug        # Debug build
```

### Cross-Compilation for Windows

```bash
# Install dependencies
./scripts/setup-cross-compile.sh

# Build for Windows
make windows64    # 64-bit
make windows32    # 32-bit
```

## License

MIT License. See LICENSE file for details.