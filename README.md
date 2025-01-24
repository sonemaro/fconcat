```markdown
# fconcat 📚

Stupidly simple tool to dump all your files into one big file. Shows folder structure at the top!

## Install

### Quick Install (Linux/macOS)
```bash
curl -sSL https://raw.githubusercontent.com/sonemaro/fconcat/main/scripts/install.sh | bash
```

### Manual Download
Grab the latest binary from [releases](https://github.com/sonemaro/fconcat/releases)

## Usage

### Basic
```bash
fconcat my_folder output.txt
```

### Skip Some Files
```bash
fconcat my_folder output.txt --exclude "*.log" "node_modules" ".git"
```

## Example Output
```
Directory Structure:
==================
📁 src/
  📄 main.c
  📄 helper.h
📁 docs/
  📄 readme.md

File Contents:
=============
// File: src/main.c
int main() { ... }

// File: src/helper.h
void help() { ... }

// File: docs/readme.md
# My Project
...
```

## Build from Source
```bash
git clone https://github.com/sonemaro/fconcat.git
cd fconcat
make
```

## License
MIT

---
Made with 🍵 by [@sonemaro](https://github.com/sonemaro)
```

Simple, clean, and covers everything someone needs to get started! Want me to add or change anything?