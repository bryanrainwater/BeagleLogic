#!/bin/bash
# Complete PulseView + BeagleLogic Installation Script for macOS
# This script builds libsigrok with BeagleLogic support and PulseView from source

set -e  # Exit on any error

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo_info() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

echo_warn() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

echo_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if running on macOS
if [[ "$OSTYPE" != "darwin"* ]]; then
    echo_error "This script is for macOS only!"
    echo_error "For Linux/WSL, use install-linux.sh"
    exit 1
fi

# Installation directory
BUILD_DIR="$HOME/sigrok-build"
INSTALL_PREFIX="/usr/local"

echo_info "This script will:"
echo "  1. Install all required dependencies via Homebrew"
echo "  2. Build and install libserialport"
echo "  3. Build and install libsigrok with BeagleLogic support and C++ bindings"
echo "  4. Build and install libsigrokdecode"
echo "  5. Build and install PulseView"
echo ""
echo "Installation directory: $BUILD_DIR"
echo "Install prefix: $INSTALL_PREFIX"
echo ""
read -p "Continue? (y/n) " -n 1 -r
echo
if [[ ! $REPLY =~ ^[Yy]$ ]]; then
    exit 1
fi

# Create build directory
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# ============================================================================
# STEP 1: Install Homebrew (if not installed)
# ============================================================================
if ! command -v brew &> /dev/null; then
    echo_info "Homebrew not found. Installing Homebrew..."
    /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

    # Add Homebrew to PATH for Apple Silicon Macs
    if [[ $(uname -m) == 'arm64' ]]; then
        echo 'eval "$(/opt/homebrew/bin/brew shellenv)"' >> ~/.zprofile
        eval "$(/opt/homebrew/bin/brew shellenv)"
    fi
else
    echo_info "Homebrew already installed"
fi

# ============================================================================
# STEP 2: Install Dependencies via Homebrew
# ============================================================================
echo_info "Step 1/5: Installing dependencies via Homebrew..."

# Update Homebrew
brew update

# Core build tools
brew install \
    git \
    autoconf \
    automake \
    libtool \
    pkg-config \
    cmake

# libsigrok dependencies (including C++ bindings requirements)
brew install \
    glib \
    glibmm \
    libzip \
    libusb \
    libftdi \
    check \
    doxygen \
    python@3 \
    boost \
    swig

# Qt5 for PulseView
brew install qt@5

# Set Qt5 environment variables
export PATH="/usr/local/opt/qt@5/bin:$PATH"
export LDFLAGS="-L/usr/local/opt/qt@5/lib"
export CPPFLAGS="-I/usr/local/opt/qt@5/include"
export PKG_CONFIG_PATH="/usr/local/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"

# For Apple Silicon Macs
if [[ $(uname -m) == 'arm64' ]]; then
    export PATH="/opt/homebrew/opt/qt@5/bin:$PATH"
    export LDFLAGS="-L/opt/homebrew/opt/qt@5/lib"
    export CPPFLAGS="-I/opt/homebrew/opt/qt@5/include"
    export PKG_CONFIG_PATH="/opt/homebrew/opt/qt@5/lib/pkgconfig:$PKG_CONFIG_PATH"
fi

echo_info "Dependencies installed successfully"

# ============================================================================
# STEP 3: Build and Install libserialport
# ============================================================================
echo_info "Step 2/5: Building libserialport..."

cd "$BUILD_DIR"
if [ -d "libserialport" ]; then
    echo_warn "libserialport directory exists, removing..."
    rm -rf libserialport
fi

git clone https://github.com/sigrokproject/libserialport.git
cd libserialport
./autogen.sh
./configure --prefix="$INSTALL_PREFIX"
make -j$(sysctl -n hw.ncpu)
sudo make install

echo_info "libserialport installed successfully"

# ============================================================================
# STEP 4: Build and Install libsigrok (with BeagleLogic + C++ bindings)
# ============================================================================
echo_info "Step 3/5: Building libsigrok with BeagleLogic support..."

cd "$BUILD_DIR"
if [ -d "libsigrok" ]; then
    echo_warn "libsigrok directory exists, removing..."
    rm -rf libsigrok
fi

git clone https://github.com/sigrokproject/libsigrok.git
cd libsigrok
./autogen.sh

# CRITICAL: Enable C++ bindings and BeagleLogic driver
echo_info "Configuring libsigrok..."
./configure \
    --prefix="$INSTALL_PREFIX" \
    --enable-cxx \
    --enable-beaglelogic 2>&1 | tee /tmp/libsigrok-configure.log

echo_info "Checking configure results..."

# Check if configure succeeded
if [ ! -f Makefile ]; then
    echo_error "Configure failed! No Makefile generated."
    echo_error "Check /tmp/libsigrok-configure.log for details"
    tail -50 /tmp/libsigrok-configure.log
    exit 1
fi

# Look for C++ bindings in configure summary
if grep -q "^ - C++\.\.* yes$" /tmp/libsigrok-configure.log; then
    echo_info "✓ C++ bindings enabled successfully"
else
    echo_error "C++ bindings may not be enabled!"
    echo_error ""
    echo_error "Language bindings section:"
    grep -A 5 "Enabled language bindings:" /tmp/libsigrok-configure.log
    echo_error ""
    echo_error "If C++ bindings show 'no', install missing dependencies:"
    echo_error "  brew install glibmm swig"
    echo_error ""
    read -p "Continue anyway? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 1
    fi
fi

make -j$(sysctl -n hw.ncpu)
sudo make install

echo_info "libsigrok installed successfully with C++ bindings and BeagleLogic support"

# Verify BeagleLogic driver is available
echo_info "Verifying BeagleLogic driver..."

# Check if libsigrokcxx is available
if pkg-config --exists libsigrokcxx; then
    echo_info "✓ libsigrokcxx (C++ bindings) found"
else
    echo_warn "⚠ libsigrokcxx not found in pkg-config path"
fi

# Check BeagleLogic in the built source
if grep -q "beaglelogic" "$BUILD_DIR/libsigrok/libsigrok.pc" 2>/dev/null; then
    echo_info "✓ BeagleLogic driver is in libsigrok.pc"
elif [ -f "$BUILD_DIR/libsigrok/src/hardware/beaglelogic/protocol.lo" ]; then
    echo_info "✓ BeagleLogic driver was compiled"
else
    echo_warn "⚠ Could not verify BeagleLogic driver, but continuing..."
fi

# ============================================================================
# STEP 5: Build and Install libsigrokdecode
# ============================================================================
echo_info "Step 4/5: Building libsigrokdecode..."

cd "$BUILD_DIR"
if [ -d "libsigrokdecode" ]; then
    echo_warn "libsigrokdecode directory exists, removing..."
    rm -rf libsigrokdecode
fi

git clone https://github.com/sigrokproject/libsigrokdecode.git
cd libsigrokdecode
./autogen.sh
./configure --prefix="$INSTALL_PREFIX"
make -j$(sysctl -n hw.ncpu)
sudo make install

echo_info "libsigrokdecode installed successfully"

# ============================================================================
# STEP 6: Build and Install PulseView
# ============================================================================
echo_info "Step 5/5: Building PulseView..."

cd "$BUILD_DIR"
if [ -d "pulseview" ]; then
    echo_warn "pulseview directory exists, removing..."
    rm -rf pulseview
fi

git clone https://github.com/sigrokproject/pulseview.git
cd pulseview

# Create build directory
mkdir -p build
cd build

# Configure with CMake
cmake \
    -DCMAKE_INSTALL_PREFIX="$INSTALL_PREFIX" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$(brew --prefix qt@5)" \
    ..

# Check if configuration succeeded
if [ $? -ne 0 ]; then
    echo_error "CMake configuration failed!"
    echo_error "Check the output above for missing dependencies"
    exit 1
fi

make -j$(sysctl -n hw.ncpu)
sudo make install

echo_info "PulseView installed successfully"

# ============================================================================
# Verification
# ============================================================================
echo_info "Verifying installation..."

# Check if PulseView binary exists
if command -v pulseview &> /dev/null; then
    echo_info "✓ PulseView binary found: $(which pulseview)"
else
    echo_error "PulseView binary not found in PATH!"
    echo_error "You may need to add $INSTALL_PREFIX/bin to your PATH"
fi

# Test BeagleLogic driver (if sigrok-cli is available)
if command -v sigrok-cli &> /dev/null; then
    echo_info "Testing BeagleLogic driver availability..."
    if sigrok-cli --list-supported | grep -q beaglelogic; then
        echo_info "✓ BeagleLogic driver is available in sigrok-cli"
    else
        echo_warn "⚠ BeagleLogic driver not found in sigrok-cli"
    fi
fi

# ============================================================================
# Summary
# ============================================================================
echo ""
echo_info "============================================"
echo_info "Installation Complete!"
echo_info "============================================"
echo ""
echo "To run PulseView:"
echo "  pulseview"
echo ""
echo "To verify BeagleLogic driver:"
echo "  sigrok-cli --list-supported | grep -i beagle"
echo ""
echo "To connect to BeagleLogic over TCP:"
echo "  1. Start PulseView"
echo "  2. Click device dropdown → 'Connect to remote device'"
echo "  3. Driver: beaglelogic"
echo "  4. Connection: tcp/<beaglebone-ip>/5555"
echo "  5. Example: tcp/192.168.7.2/5555"
echo ""
echo "Build artifacts located in: $BUILD_DIR"
echo ""
