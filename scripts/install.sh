#!/bin/bash

# Installer script for fconcat
GITHUB_REPO="sonemaro/fconcat"
BINARY_NAME="fconcat"
INSTALL_DIR="/usr/local/bin"

# Determine system architecture
ARCH=$(uname -m)
case "$ARCH" in
    x86_64) ARCH="amd64" ;;
    aarch64|arm64) ARCH="arm64" ;;
    *) echo "Unsupported architecture: $ARCH"; exit 1 ;;
esac

# Determine OS
OS=$(uname -s | tr '[:upper:]' '[:lower:]')
case "$OS" in
    linux) OS="linux" ;;
    darwin) OS="darwin" ;;
    *) echo "Unsupported OS: $OS"; exit 1 ;;
esac

# Get latest release version
echo "Fetching latest release..."
LATEST_RELEASE=$(curl -s "https://api.github.com/repos/$GITHUB_REPO/releases/latest" | grep '"tag_name":' | sed -E 's/.*"([^"]+)".*/\1/')

if [ -z "$LATEST_RELEASE" ]; then
    echo "Error: Could not determine latest release"
    exit 1
fi

echo "Latest release: $LATEST_RELEASE"

# Download URL
DOWNLOAD_URL="https://github.com/$GITHUB_REPO/releases/download/$LATEST_RELEASE/${BINARY_NAME}_${OS}_${ARCH}"
if [ "$OS" = "windows" ]; then
    DOWNLOAD_URL="${DOWNLOAD_URL}.exe"
fi

# Create temporary directory
TMP_DIR=$(mktemp -d)
cd "$TMP_DIR" || exit 1

# Download binary
echo "Downloading $BINARY_NAME..."
if ! curl -L -o "$BINARY_NAME" "$DOWNLOAD_URL"; then
    echo "Error: Download failed"
    rm -rf "$TMP_DIR"
    exit 1
fi

# Make binary executable
chmod +x "$BINARY_NAME"

# Install binary
echo "Installing to $INSTALL_DIR..."
if ! sudo mv "$BINARY_NAME" "$INSTALL_DIR/"; then
    echo "Error: Installation failed"
    rm -rf "$TMP_DIR"
    exit 1
fi

# Clean up
cd - > /dev/null || exit
rm -rf "$TMP_DIR"

echo "Installation complete! Version: $LATEST_RELEASE"
echo "You can now use 'fconcat' command."

# Verify installation
if command -v fconcat >/dev/null; then
    echo "Verification: fconcat is properly installed"
    fconcat --version
else
    echo "Warning: fconcat was installed but might not be in PATH"
fi

