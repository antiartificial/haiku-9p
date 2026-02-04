#!/bin/bash
#
# 9P Filesystem Build Script for Haiku
#
# Usage:
#   ./build.sh              - Build the module
#   ./build.sh install      - Build and install
#   ./build.sh clean        - Clean build artifacts
#   ./build.sh setup        - Set up Haiku source tree integration
#
# This script can be run inside Haiku or on a Linux host with
# Haiku cross-compilation set up.
#

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SRC_DIR="$SCRIPT_DIR/src/add-ons/kernel/file_systems/9p"

# Detect if running on Haiku
if [ -f /boot/system/lib/libroot.so ]; then
    ON_HAIKU=true
    HAIKU_TOP="${HAIKU_TOP:-/boot/home/haiku}"
    INSTALL_DIR="/boot/system/non-packaged/add-ons/kernel/file_systems"
else
    ON_HAIKU=false
    HAIKU_TOP="${HAIKU_TOP:-$HOME/haiku}"
    INSTALL_DIR=""
fi

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

error() {
    echo -e "${RED}[ERROR]${NC} $1"
    exit 1
}

check_haiku_source() {
    if [ ! -d "$HAIKU_TOP/src" ]; then
        error "Haiku source not found at $HAIKU_TOP

Please either:
  1. Clone Haiku source:
     git clone https://review.haiku-os.org/haiku $HAIKU_TOP

  2. Or set HAIKU_TOP to your existing Haiku source:
     export HAIKU_TOP=/path/to/haiku
     ./build.sh"
    fi
}

cmd_setup() {
    info "Setting up Haiku source tree integration..."
    check_haiku_source

    TARGET_DIR="$HAIKU_TOP/src/add-ons/kernel/file_systems/9p"

    # Create symlink or copy
    if [ -L "$TARGET_DIR" ]; then
        info "Symlink already exists at $TARGET_DIR"
    elif [ -d "$TARGET_DIR" ]; then
        warn "Directory already exists at $TARGET_DIR"
        read -p "Replace with symlink? [y/N] " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            rm -rf "$TARGET_DIR"
            ln -s "$SRC_DIR" "$TARGET_DIR"
            info "Created symlink: $TARGET_DIR -> $SRC_DIR"
        fi
    else
        ln -s "$SRC_DIR" "$TARGET_DIR"
        info "Created symlink: $TARGET_DIR -> $SRC_DIR"
    fi

    # Check if Jamfile includes 9p
    PARENT_JAMFILE="$HAIKU_TOP/src/add-ons/kernel/file_systems/Jamfile"
    if grep -q "SubInclude.*9p" "$PARENT_JAMFILE" 2>/dev/null; then
        info "Jamfile already includes 9p"
    else
        info "Adding 9p to parent Jamfile..."
        echo 'SubInclude HAIKU_TOP src add-ons kernel file_systems 9p ;' >> "$PARENT_JAMFILE"
        info "Added 9p to $PARENT_JAMFILE"
    fi

    info "Setup complete! Now run: ./build.sh"
}

cmd_build() {
    info "Building 9p filesystem module..."
    check_haiku_source

    # Check if setup was done
    if [ ! -d "$HAIKU_TOP/src/add-ons/kernel/file_systems/9p" ]; then
        error "Please run './build.sh setup' first"
    fi

    cd "$HAIKU_TOP"

    # Configure if needed
    if [ ! -f "generated/build/BuildConfig" ]; then
        info "Configuring build (this may take a while first time)..."
        if $ON_HAIKU; then
            ./configure --use-gcc-pipe
        else
            # Cross-compilation - user needs to set this up
            error "Cross-compilation not configured.
Please run ./configure in $HAIKU_TOP first.
See: https://www.haiku-os.org/guides/building"
        fi
    fi

    # Build just the 9p module
    info "Running jam..."
    jam -q 9p

    # Find the built module
    MODULE=$(find generated -name "9p" -path "*/file_systems/*" -type f 2>/dev/null | head -1)

    if [ -n "$MODULE" ]; then
        info "Build successful!"
        info "Module location: $MODULE"
        echo "$MODULE" > "$SCRIPT_DIR/.last_build"
    else
        error "Build failed - module not found"
    fi
}

cmd_install() {
    if ! $ON_HAIKU; then
        error "Install can only be run on Haiku"
    fi

    # Build first
    cmd_build

    # Get module path
    if [ -f "$SCRIPT_DIR/.last_build" ]; then
        MODULE=$(cat "$SCRIPT_DIR/.last_build")
    else
        error "No build found. Run './build.sh' first"
    fi

    if [ ! -f "$MODULE" ]; then
        error "Module not found at $MODULE"
    fi

    info "Installing module..."

    # Create install directory if needed
    mkdir -p "$INSTALL_DIR"

    # Copy module
    cp "$MODULE" "$INSTALL_DIR/9p"

    info "Installed to: $INSTALL_DIR/9p"
    info ""
    info "To use:"
    info "  mkdir -p /mnt/host"
    info "  mount -t 9p -o tag=<your_tag> /mnt/host"
}

cmd_clean() {
    info "Cleaning..."

    if [ -d "$HAIKU_TOP" ]; then
        cd "$HAIKU_TOP"
        jam clean 9p 2>/dev/null || true
    fi

    rm -f "$SCRIPT_DIR/.last_build"

    info "Clean complete"
}

cmd_help() {
    cat << 'EOF'
9P Filesystem Build Script for Haiku

Usage:
    ./build.sh [command]

Commands:
    (none)      Build the module
    setup       Set up Haiku source tree integration (run first!)
    install     Build and install the module (Haiku only)
    clean       Clean build artifacts
    help        Show this help

Environment Variables:
    HAIKU_TOP   Path to Haiku source tree (default: ~/haiku or /boot/home/haiku)

Quick Start (on Haiku):
    1. Clone Haiku source (if not already):
       git clone https://review.haiku-os.org/haiku ~/haiku

    2. Clone this repo:
       git clone <this-repo> ~/9p

    3. Set up and build:
       cd ~/9p
       ./build.sh setup
       ./build.sh install

    4. Use (with QEMU -virtfs mount_tag=host):
       mkdir /mnt/host
       mount -t 9p -o tag=host /mnt/host
       ls /mnt/host

EOF
}

# Main
case "${1:-}" in
    setup)
        cmd_setup
        ;;
    install)
        cmd_install
        ;;
    clean)
        cmd_clean
        ;;
    help|--help|-h)
        cmd_help
        ;;
    "")
        cmd_build
        ;;
    *)
        error "Unknown command: $1 (try './build.sh help')"
        ;;
esac
