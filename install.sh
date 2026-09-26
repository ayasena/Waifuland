#!/bin/bash
#
# Waifuland Install Script
# Checks dependencies, downloads third-party libraries, and builds the project.
#
# Usage: ./install.sh [fedora|arch]
#   With a distro (auto-detected from /etc/os-release otherwise), build deps are
#   installed via dnf/pacman first. Sucrette's extras module calls it this way.
#   WAIFULAND_ACCEPT_LIVE2D_LICENSE=1 accepts the Cubism SDK licenses without a
#   prompt; with neither that nor a terminal, the install is skipped (exit 3,
#   which sucrette's extras module reports as "skipped", not "installed").
#

set -euo pipefail

# ─── Colors & Helpers ─────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
BOLD='\033[1m'
RESET='\033[0m'

info()    { echo -e "${BLUE}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*"; }
step()    { echo -e "\n${BOLD}===> $*${RESET}"; }

# Track overall status
ERRORS=0

# ─── Project Root ─────────────────────────────────────────────────────────────

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

SDK_DIR="CubismSdkForNative-5-r.5"
NPROC=$(nproc 2>/dev/null || echo 4)
DISTRO="${1:-$( { . /etc/os-release && echo "${ID_LIKE:-$ID}"; } 2>/dev/null | grep -oE 'fedora|arch' | head -1 || true)}"

# ─── Step 0: Install build dependencies (Fedora / Arch) ──────────────────────

case "$DISTRO" in
    fedora) step "Installing build dependencies (dnf)"
            sudo dnf install -y gcc-c++ make cmake pkgconf-pkg-config wayland-devel wayland-protocols-devel \
                mesa-libEGL-devel mesa-libGL-devel mesa-libGLU-devel curl unzip socat ;;
    arch)   step "Installing build dependencies (pacman)"
            sudo pacman -S --needed --noconfirm base-devel cmake pkgconf wayland wayland-protocols \
                libglvnd glu egl-wayland curl unzip socat ;;
esac

# ─── Step 1: Check system dependencies ───────────────────────────────────────

step "Checking system dependencies"

check_command() {
    local cmd="$1"
    local pkg_hint="${2:-$1}"
    if command -v "$cmd" &>/dev/null; then
        success "$cmd found: $(command -v "$cmd")"
    else
        error "$cmd not found. Install it (e.g. $pkg_hint)"
        ERRORS=$((ERRORS + 1))
    fi
}

check_pkg_config() {
    local lib="$1"
    local pkg_hint="${2:-$1}"
    if pkg-config --exists "$lib" 2>/dev/null; then
        success "$lib found ($(pkg-config --modversion "$lib" 2>/dev/null || echo 'version unknown'))"
    else
        error "$lib not found via pkg-config. Install it (e.g. $pkg_hint)"
        ERRORS=$((ERRORS + 1))
    fi
}

IS_MACOS=false
if [ "$(uname -s)" = "Darwin" ]; then
    IS_MACOS=true
fi

check_command cmake        "cmake"
check_command make         "make / Xcode Command Line Tools"
check_command g++          "g++ / clang++ (Xcode Command Line Tools)"
check_command curl         "curl"
check_command unzip        "unzip"

if [ "$IS_MACOS" = true ]; then
    success "macOS detected — using native Cocoa/Quartz windowing (no Wayland/EGL/pkg-config needed)"
else
    check_command pkg-config   "pkg-config / pkgconf"

    # wayland-scanner
    if command -v wayland-scanner &>/dev/null; then
        success "wayland-scanner found: $(command -v wayland-scanner)"
    elif pkg-config --exists wayland-scanner 2>/dev/null; then
        SCANNER=$(pkg-config --variable=wayland_scanner wayland-scanner 2>/dev/null || true)
        if [ -n "$SCANNER" ] && [ -x "$SCANNER" ]; then
            success "wayland-scanner found via pkg-config: $SCANNER"
        else
            error "wayland-scanner not found. Install wayland-devel / wayland"
            ERRORS=$((ERRORS + 1))
        fi
    else
        error "wayland-scanner not found. Install wayland-devel / wayland"
        ERRORS=$((ERRORS + 1))
    fi

    # Libraries via pkg-config
    check_pkg_config wayland-client  "libwayland-dev / wayland-devel"
    check_pkg_config wayland-egl     "libwayland-dev / wayland-devel"
    check_pkg_config wayland-cursor  "libwayland-dev / wayland-devel"
    check_pkg_config egl             "libegl-dev / mesa-libEGL-devel"
fi

if [ "$ERRORS" -gt 0 ]; then
    echo ""
    error "$ERRORS missing dependency(ies). Please install them and re-run this script."
    echo ""
    if [ "$IS_MACOS" = true ]; then
        info  "macOS:            xcode-select --install ; brew install cmake curl"
    else
        info  "Arch Linux:       sudo pacman -S --needed base-devel cmake pkgconf wayland wayland-protocols libglvnd glu egl-wayland curl unzip"
        info  "Ubuntu / Debian:  sudo apt install build-essential cmake pkg-config libwayland-dev wayland-protocols libegl-dev libgl-dev libglu1-mesa-dev curl unzip"
        info  "Fedora:           sudo dnf install gcc-c++ cmake pkgconf-pkg-config wayland-devel wayland-protocols-devel mesa-libEGL-devel mesa-libGL-devel mesa-libGLU-devel curl unzip"
    fi
    exit 1
fi

# ─── Step 2: Check Live2D Cubism SDK ─────────────────────────────────────────

step "Checking Live2D Cubism SDK"

if [ -d "$SDK_DIR" ] && [ -f "$SDK_DIR/Core/include/Live2DCubismCore.h" ]; then
    success "Cubism SDK found at ./$SDK_DIR"
else
    warn "Cubism SDK not found at ./$SDK_DIR"
    echo ""
    info "To automatically download the Live2D Cubism SDK, you must agree to the following licenses:"
    info "1. https://www.live2d.com/eula/live2d-proprietary-software-license-agreement_en.html"
    info "2. https://www.live2d.com/eula/live2d-open-software-license-agreement_en.html"
    echo ""
    if [ "${WAIFULAND_ACCEPT_LIVE2D_LICENSE:-}" = 1 ]; then
        CONSENT=y
        info "Accepted via WAIFULAND_ACCEPT_LIVE2D_LICENSE=1"
    elif [ -t 0 ]; then
        read -p "Do you agree to the Live2D Software License Agreements? [y/N] " CONSENT
    else
        warn "No terminal to ask for consent — skipping waifuland. After reading the licenses, re-run:"
        info "  WAIFULAND_ACCEPT_LIVE2D_LICENSE=1 bash $SCRIPT_DIR/install.sh $DISTRO"
        exit 3
    fi
    if [[ "$CONSENT" =~ ^[Yy]$ ]]; then
        info "Downloading Live2D Cubism SDK for Native 5-r.5..."
        SDK_URL="https://cubism.live2d.com/sdk-native/bin/CubismSdkForNative-5-r.5.zip?event=cubism_sdk_download&sdk_type=Native&user_status=update&user_type=&version=5-r.5&lang=en"
        if curl -fsSL -o "CubismSdkForNative-5-r.5.zip" "$SDK_URL"; then
            success "SDK downloaded"
            info "Extracting SDK..."
            unzip -oq "CubismSdkForNative-5-r.5.zip" -d "."
            rm -f "CubismSdkForNative-5-r.5.zip"
            if [ -d "$SDK_DIR" ] && [ -f "$SDK_DIR/Core/include/Live2DCubismCore.h" ]; then
                success "Cubism SDK successfully installed to ./$SDK_DIR"
            else
                error "SDK extraction failed or structure is incorrect."
                exit 1
            fi
        else
            error "Failed to download Cubism SDK. Please check your internet connection or download manually."
            exit 1
        fi
    else
        error "License agreement not accepted."
        echo ""
        info  "Please download the Live2D Cubism SDK for Native manually from:"
        info  "  https://www.live2d.com/en/sdk/about/"
        info  ""
        info  "Extract it to this directory so the structure looks like:"
        info  "  $(pwd)/$SDK_DIR/"
        exit 3   # declined is a choice, not a failure
    fi
fi

# ─── Step 3: Download third-party dependencies (GLEW & GLFW) ─────────────────

step "Setting up third-party dependencies (GLEW & GLFW)"

GLEW_VERSION=2.3.1
GLFW_VERSION=3.4
THIRDPARTY_DIR="$SCRIPT_DIR/thirdParty"

# GLEW
if [ -d "$THIRDPARTY_DIR/glew" ] && [ -f "$THIRDPARTY_DIR/glew/include/GL/glew.h" ]; then
    success "GLEW already present, skipping download"
else
    info "Downloading GLEW $GLEW_VERSION..."
    if curl -fsSL -o "$THIRDPARTY_DIR/glew.zip" \
        "https://github.com/nigels-com/glew/releases/download/glew-$GLEW_VERSION/glew-$GLEW_VERSION.zip"; then
        success "GLEW downloaded"
    else
        error "Failed to download GLEW. Check your internet connection."
        exit 1
    fi
    info "Extracting GLEW..."
    unzip -oq "$THIRDPARTY_DIR/glew.zip" -d "$THIRDPARTY_DIR"
    rm -f "$THIRDPARTY_DIR/glew.zip"
    # Rename to 'glew' (remove version suffix)
    rm -rf "$THIRDPARTY_DIR/glew"
    mv "$THIRDPARTY_DIR/glew-"* "$THIRDPARTY_DIR/glew"
    success "GLEW $GLEW_VERSION extracted"
fi

# ─── Step 4: Configure with CMake ────────────────────────────────────────────

step "Configuring project with CMake"

BUILD_DIR="$SCRIPT_DIR/build"
mkdir -p "$BUILD_DIR"

if cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" 2>&1; then
    success "CMake configuration complete"
else
    error "CMake configuration failed. See output above for details."
    exit 1
fi

# ─── Step 5: Build ───────────────────────────────────────────────────────────

step "Building waifuland (using $NPROC parallel jobs)"

if make -C "$BUILD_DIR" -j"$NPROC" 2>&1; then
    success "Build complete"
else
    error "Build failed. See output above for details."
    exit 1
fi

# ─── Step 6: Install binary and default config ───────────────────────────────

step "Installing waifuland"

INSTALL_BIN_DIR="$HOME/.local/bin"
CONFIG_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/waifuland"
CONFIG_FILE="$CONFIG_DIR/config.json"

mkdir -p "$INSTALL_BIN_DIR"
install -m 755 "$BUILD_DIR/bin/waifuland" "$INSTALL_BIN_DIR/waifuland"
install -m 755 "$SCRIPT_DIR/waifuland-ctl" "$INSTALL_BIN_DIR/waifuland-ctl"
success "Installed waifuland + waifuland-ctl to $INSTALL_BIN_DIR"

if [ -f "$CONFIG_FILE" ]; then
    success "Config already exists at $CONFIG_FILE, leaving it untouched"
else
    mkdir -p "$CONFIG_DIR" "$CONFIG_DIR/models"
    cat > "$CONFIG_FILE" <<'EOF'
{
  "characters": []
}
EOF
    success "Wrote default config to $CONFIG_FILE"
fi

case ":$PATH:" in
    *":$INSTALL_BIN_DIR:"*) ;;
    *) warn "$INSTALL_BIN_DIR is not on your PATH. Add this to your shell profile:"
       info "  export PATH=\"\$HOME/.local/bin:\$PATH\"" ;;
esac

# systemd user unit (Linux): starts with the graphical session, restarts on crash.
if [ "$IS_MACOS" = false ] && command -v systemctl &>/dev/null; then
    UNIT_DIR="${XDG_CONFIG_HOME:-$HOME/.config}/systemd/user"
    mkdir -p "$UNIT_DIR"
    install -m 644 "$SCRIPT_DIR/share/waifuland.service" "$UNIT_DIR/waifuland.service"
    systemctl --user daemon-reload
    systemctl --user enable waifuland.service
    systemctl --user try-restart waifuland.service || true
    success "Enabled waifuland.service (systemctl --user status waifuland)"
fi

if [ "$IS_MACOS" = false ] && [[ "$(uname -m)" =~ ^(aarch64|arm64)$ ]]; then
    warn "Linux arm64 links Live2D's experimental Cubism Core build"
fi

# ─── Done ─────────────────────────────────────────────────────────────────────

echo ""
echo -e "${GREEN}${BOLD}============================================${RESET}"
echo -e "${GREEN}${BOLD}  Build & install successful!${RESET}"
echo -e "${GREEN}${BOLD}============================================${RESET}"
echo ""
echo -e "  Run it:"
echo -e "    ${BOLD}waifuland${RESET}"
echo -e "    ${BOLD}waifuland --models_dir /path/to/models${RESET}"
echo ""
echo -e "  Config:"
echo -e "    ${BOLD}$CONFIG_FILE${RESET}"
echo ""
echo -e "  Default model directory:"
echo -e "    ${BOLD}$CONFIG_DIR/models/<Name>/<Name>.model3.json${RESET}"
echo -e "  List each model under \"characters\" in config.json; fairyd [[cast]]"
echo -e "  members bind to them by overlay_model = \"<Name>\"."
echo ""
