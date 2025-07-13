#!/bin/bash
# Setup script for cross-compilation dependencies

set -e

echo "Setting up cross-compilation environment for Windows..."

# Detect the Linux distribution
if command -v apt-get &> /dev/null; then
    # Debian/Ubuntu
    echo "Detected Debian/Ubuntu system"
    echo "Installing MinGW-w64 cross-compiler..."
    sudo apt-get update
    sudo apt-get install -y gcc-mingw-w64-x86-64 gcc-mingw-w64-i686
    
elif command -v dnf &> /dev/null; then
    # Fedora
    echo "Detected Fedora system"
    echo "Installing MinGW-w64 cross-compiler..."
    sudo dnf install -y mingw64-gcc mingw32-gcc
    
elif command -v yum &> /dev/null; then
    # RHEL/CentOS
    echo "Detected RHEL/CentOS system"
    echo "Installing MinGW-w64 cross-compiler..."
    sudo yum install -y mingw64-gcc mingw32-gcc
    
elif command -v pacman &> /dev/null; then
    # Arch Linux
    echo "Detected Arch Linux system"
    echo "Installing MinGW-w64 cross-compiler..."
    sudo pacman -S --noconfirm mingw-w64-gcc
    
elif command -v zypper &> /dev/null; then
    # openSUSE
    echo "Detected openSUSE system"
    echo "Installing MinGW-w64 cross-compiler..."
    sudo zypper install -y cross-x86_64-w64-mingw32-gcc cross-i686-w64-mingw32-gcc
    
else
    echo "Unknown Linux distribution. Please install MinGW-w64 manually:"
    echo "  - For 64-bit: x86_64-w64-mingw32-gcc"
    echo "  - For 32-bit: i686-w64-mingw32-gcc"
    exit 1
fi

echo ""
echo "Checking cross-compiler installation..."

# Check if the cross-compilers are available
if command -v x86_64-w64-mingw32-gcc &> /dev/null; then
    echo "✓ MinGW-w64 64-bit cross-compiler found"
    x86_64-w64-mingw32-gcc --version | head -1
else
    echo "✗ MinGW-w64 64-bit cross-compiler not found"
fi

if command -v i686-w64-mingw32-gcc &> /dev/null; then
    echo "✓ MinGW-w64 32-bit cross-compiler found"
    i686-w64-mingw32-gcc --version | head -1
else
    echo "✗ MinGW-w64 32-bit cross-compiler not found"
fi

echo ""
echo "Setup complete! You can now cross-compile for Windows using:"
echo "  make windows64    # Build for Windows 64-bit"
echo "  make windows32    # Build for Windows 32-bit"
echo "  make windows-all  # Build both versions"
