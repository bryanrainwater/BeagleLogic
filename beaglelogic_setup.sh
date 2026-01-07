#!/bin/bash
################################################################################
# BeagleLogic Management Script
#
# Unified script for installing, reinstalling, cleaning, and checking BeagleLogic.
# This script combines the functionality of:
#   - beaglelogic_fresh_install.sh (fresh installation)
#   - beaglelogic_reinstall.sh (rebuild and redeploy)
#   - beaglelogic_clean.sh (clean build artifacts)
#   - Installation status checking and verification
#
# Usage:
#   sudo ./beaglelogic_setup.sh                    # Interactive menu
#   sudo ./beaglelogic_setup.sh --install          # Fresh installation
#   sudo ./beaglelogic_setup.sh --reinstall        # Rebuild and redeploy (full clean)
#   sudo ./beaglelogic_setup.sh --rebuild-fast     # Fast rebuild (no clean)
#   sudo ./beaglelogic_setup.sh --status           # Check installation status
#   sudo ./beaglelogic_setup.sh --configure        # Configure firmware mode
#   sudo ./beaglelogic_setup.sh --clean            # Clean build artifacts
#   sudo ./beaglelogic_setup.sh --clean --all      # Clean including node_modules
#   sudo ./beaglelogic_setup.sh --clean --deep     # Deep clean (logs, venvs, etc.)
#   sudo ./beaglelogic_setup.sh --help             # Show help
#
# Options:
#   --install              Perform fresh installation
#   --reinstall            Rebuild and redeploy existing installation (full clean)
#   --rebuild-fast         Fast rebuild (incremental, no clean - for quick iterations)
#   --status               Check installation status and verify components
#   --configure            Configure firmware mode (Logic Analyzer / PRUDAQ ADC)
#   --clean                Clean build artifacts
#   --all                  (with --clean) Also clean node_modules
#   --deep                 (with --clean) Deep clean including logs/caches
#   --skip-space-check     (with --install) Skip disk space verification
#   -h, --help            Show this help message
#
# Author: Bryan Rainwater
# Based on original work by Kumar Abhishek <abhishek@theembeddedkitchen.net>
# License: GPL-2.0
# Date: December 2024
################################################################################

set -e  # Exit on any error

################################################################################
# Configuration Variables
################################################################################

# Source directory (where this script is located)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Installation directory - NOW SAME AS SOURCE DIRECTORY (no more copy to /opt)
INSTALL_DIR="${SCRIPT_DIR}"

# BeagleLogic configuration directory and file (tracks installation location)
BEAGLELOGIC_CONFIG_DIR="/etc/beaglelogic"
BEAGLELOGIC_CONFIG_FILE="${BEAGLELOGIC_CONFIG_DIR}/install.conf"

# System directories
PRU_SUPPORT_DIR="/usr/lib/ti/pru-software-support-package"
PRU_CGT_DIR="/usr/share/ti/cgt-pru"
LOG_FILE="/var/log/beaglelogic-install.log"
STATE_FILE="/var/lib/beaglelogic-install-state"
REBOOT_FLAG_FILE="/var/lib/beaglelogic-needs-reboot"

# Firmware paths for mode switching
FIRMWARE_DIR="/lib/firmware"
PRU1_SYMLINK="${FIRMWARE_DIR}/beaglelogic-pru1-fw"
LOGIC_FW="${FIRMWARE_DIR}/beaglelogic-pru1-logic"
PRUDAQ_CH0_FW="${FIRMWARE_DIR}/beaglelogic-pru1-prudaq-ch0"
PRUDAQ_CH1_FW="${FIRMWARE_DIR}/beaglelogic-pru1-prudaq-ch1"
PRUDAQ_CH01_FW="${FIRMWARE_DIR}/beaglelogic-pru1-prudaq-ch01"

# Additional config for reinstall
KERNEL_SRC="/lib/modules/$(uname -r)/build"
DTBINDSRC="/opt/source/dtb-6.x/include"
PRU_SOFTWARE_DIR="/opt/source/pru-software-support-package"

# Default user (debian is standard on BeagleBone Debian images)
DEFAULT_USER="${SUDO_USER:-debian}"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

# Command line flags
OPERATION=""
SKIP_SPACE_CHECK=false
CLEAN_NODE_MODULES=false
DEEP_CLEAN=false

# Component selection (for install)
INSTALL_WEB_SERVER="no"
INSTALL_TCP_SERVER="no"
INSTALL_DOCS="no"

# Component detection (for reinstall)
HAS_WEB_SERVER=false
HAS_TCP_SERVER=false
HAS_DOCS=false

################################################################################
# Help and Usage
################################################################################

show_help() {
    cat << EOF
BeagleLogic Management Script

Usage: sudo $0 [OPERATION] [OPTIONS]

Operations:
  --install              Perform fresh installation of BeagleLogic
  --reinstall            Rebuild and redeploy existing installation (full clean)
  --rebuild-fast         Fast rebuild (incremental, no clean - for quick iterations)
  --clean                Clean build artifacts and temporary files
  --status               Check installation status and verify components
  --configure            Configure firmware mode (Logic Analyzer vs PRUDAQ ADC)
  (no operation)         Show interactive menu

Options (for --install):
  --skip-space-check     Skip disk space verification (for testing/recovery)

Options (for --clean):
  --all                  Also clean node_modules directories
  --deep                 Deep clean including logs, caches, and Python venvs

General Options:
  -h, --help            Show this help message

Examples:
  sudo ./beaglelogic_setup.sh                        # Interactive menu
  sudo ./beaglelogic_setup.sh --install              # Fresh install
  sudo ./beaglelogic_setup.sh --reinstall            # Rebuild components (full clean)
  sudo ./beaglelogic_setup.sh --rebuild-fast         # Fast rebuild (no clean)
  sudo ./beaglelogic_setup.sh --status               # Check installation status
  sudo ./beaglelogic_setup.sh --configure            # Configure firmware mode
  sudo ./beaglelogic_setup.sh --clean                # Clean build files
  sudo ./beaglelogic_setup.sh --clean --all          # Clean including node_modules
  sudo ./beaglelogic_setup.sh --install --skip-space-check   # Install without space check

Components Installed:
  - PRU firmware (PRU0, PRU1, PRUDAQ variants)
  - Kernel module (beaglelogic.ko)
  - Device tree overlay
  - Test applications
  - Web server (optional - Node.js port 4000)
  - TCP server (optional - Node.js port 5555 for PulseView)
  - Documentation (optional - MkDocs port 8000)

EOF
    exit 0
}

################################################################################
# Command Line Argument Parsing
################################################################################

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case $1 in
        --install)
            OPERATION="install"
            shift
            ;;
        --reinstall)
            OPERATION="reinstall"
            shift
            ;;
        --rebuild-fast)
            OPERATION="rebuild_fast"
            shift
            ;;
        --clean)
            OPERATION="clean"
            shift
            ;;
        --status)
            OPERATION="status"
            shift
            ;;
        --configure)
            OPERATION="configure"
            shift
            ;;
        --skip-space-check)
            SKIP_SPACE_CHECK=true
            shift
            ;;
        --all)
            CLEAN_NODE_MODULES=true
            shift
            ;;
        --deep)
            CLEAN_NODE_MODULES=true
            DEEP_CLEAN=true
            shift
            ;;
        -h|--help)
            show_help
            ;;
        *)
            echo "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

################################################################################
# Logging Functions
################################################################################

log() {
    echo -e "${BLUE}[INFO]${NC} $1" | tee -a "${LOG_FILE}" 2>/dev/null || echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1" | tee -a "${LOG_FILE}" 2>/dev/null || echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1" | tee -a "${LOG_FILE}" 2>/dev/null || echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1" | tee -a "${LOG_FILE}" 2>/dev/null || echo -e "${RED}[ERROR]${NC} $1"
}

log_section() {
    echo "" | tee -a "${LOG_FILE}" 2>/dev/null || echo ""
    echo "========================================================================" | tee -a "${LOG_FILE}" 2>/dev/null || echo "========================================================================"
    echo -e "${GREEN}$1${NC}" | tee -a "${LOG_FILE}" 2>/dev/null || echo -e "${GREEN}$1${NC}"
    echo "========================================================================" | tee -a "${LOG_FILE}" 2>/dev/null || echo "========================================================================"
}

log_clean() {
    echo -e "${BLUE}[CLEAN]${NC} $1"
}

################################################################################
# Installation Status Check
################################################################################

check_installation_status() {
    log_section "BeagleLogic Installation Status Check"

    local ALL_OK=true

    # Check 1: Installation configuration file
    echo ""
    echo -e "${CYAN}[1/9] Checking installation configuration...${NC}"
    if [ -f "${BEAGLELOGIC_CONFIG_FILE}" ]; then
        local INSTALL_PATH=$(get_install_path)
        log_success "Configuration file found: ${BEAGLELOGIC_CONFIG_FILE}"
        log "Installation path: ${INSTALL_PATH}"
        if [ -f "${INSTALL_PATH}/beaglelogic_setup.sh" ]; then
            log_success "Installation directory exists and contains setup script"
        else
            log_warning "Installation path exists but setup script not found"
            ALL_OK=false
        fi
    else
        log_error "No installation configuration file found"
        log "Run installation first: sudo ./beaglelogic_setup.sh --install"
        ALL_OK=false
    fi

    # Check 2: Kernel module
    echo ""
    echo -e "${CYAN}[2/9] Checking kernel module...${NC}"
    if lsmod | grep -q beaglelogic; then
        log_success "Kernel module loaded"
        lsmod | grep beaglelogic | while read line; do
            log "  $line"
        done
    else
        log_error "Kernel module NOT loaded"
        log "Try: sudo modprobe beaglelogic"
        ALL_OK=false
    fi

    # Check 3: Device node
    echo ""
    echo -e "${CYAN}[3/9] Checking device node...${NC}"
    if [ -c "/dev/beaglelogic" ]; then
        log_success "Device node exists: /dev/beaglelogic"
        ls -l /dev/beaglelogic | while read line; do
            log "  $line"
        done
    else
        log_error "Device node NOT found: /dev/beaglelogic"
        ALL_OK=false
    fi

    # Check 4: Sysfs interface
    echo ""
    echo -e "${CYAN}[4/9] Checking sysfs interface...${NC}"
    if [ -d "/sys/devices/virtual/misc/beaglelogic" ]; then
        log_success "Sysfs interface available"
        log "  Path: /sys/devices/virtual/misc/beaglelogic"

        # Show some key attributes
        if [ -r "/sys/devices/virtual/misc/beaglelogic/samplerate" ]; then
            local SAMPLERATE=$(cat /sys/devices/virtual/misc/beaglelogic/samplerate 2>/dev/null || echo "N/A")
            log "  Sample rate: ${SAMPLERATE} Hz"
        fi

        if [ -r "/sys/devices/virtual/misc/beaglelogic/sampleunit" ]; then
            local SAMPLEUNIT=$(cat /sys/devices/virtual/misc/beaglelogic/sampleunit 2>/dev/null || echo "N/A")
            log "  Sample unit: ${SAMPLEUNIT} (0=8-bit, 1=16-bit)"
        fi
    else
        log_error "Sysfs interface NOT found"
        ALL_OK=false
    fi

    # Check 5: PRU firmware
    echo ""
    echo -e "${CYAN}[5/9] Checking PRU firmware...${NC}"
    local FIRMWARE_OK=true

    for FW in beaglelogic-pru0-fw beaglelogic-pru1-fw beaglelogic-pru1-logic; do
        if [ -f "/lib/firmware/${FW}" ]; then
            log_success "Firmware found: ${FW}"
        else
            log_error "Firmware missing: ${FW}"
            FIRMWARE_OK=false
            ALL_OK=false
        fi
    done

    if $FIRMWARE_OK; then
        log_success "All required firmware files present"
    fi

    # Check 6: Device tree overlay
    echo ""
    echo -e "${CYAN}[6/9] Checking device tree overlay...${NC}"
    if [ -f "/boot/overlays/beaglelogic-00A0.dtbo" ]; then
        log_success "Device tree overlay found: beaglelogic-00A0.dtbo"
    else
        log_warning "Device tree overlay not found (may be in different location)"
    fi

    # Check if overlay is enabled in boot configuration
    if grep -q "beaglelogic" /boot/uEnv.txt 2>/dev/null; then
        log_success "Overlay configured in /boot/uEnv.txt"
        grep beaglelogic /boot/uEnv.txt | while read line; do
            log "  $line"
        done
    else
        log_warning "Overlay may not be configured in /boot/uEnv.txt"
    fi

    # Check 7: Systemd services
    echo ""
    echo -e "${CYAN}[7/9] Checking systemd services...${NC}"
    local SERVICES_OK=true

    for SERVICE in beaglelogic-startup beaglelogic beaglelogic-tcp; do
        if systemctl list-unit-files | grep -q "${SERVICE}.service"; then
            local STATUS=$(systemctl is-active ${SERVICE}.service 2>/dev/null || echo "inactive")
            local ENABLED=$(systemctl is-enabled ${SERVICE}.service 2>/dev/null || echo "disabled")

            if [ "$STATUS" = "active" ]; then
                log_success "${SERVICE}.service: ${STATUS} (${ENABLED})"
            else
                log_warning "${SERVICE}.service: ${STATUS} (${ENABLED})"
            fi
        else
            log_warning "${SERVICE}.service: not installed"
        fi
    done

    # Check 8: Test applications
    echo ""
    echo -e "${CYAN}[8/9] Checking test applications...${NC}"
    if [ -f "${INSTALL_DIR}/testapp/simple-capture" ]; then
        if [ -x "${INSTALL_DIR}/testapp/simple-capture" ]; then
            log_success "Test applications built and executable"
        else
            log_warning "Test applications built but not executable"
        fi
    else
        log_warning "Test applications not found (may not have been built yet)"
    fi

    # Check 9: Recent kernel messages
    echo ""
    echo -e "${CYAN}[9/9] Recent kernel messages...${NC}"
    if dmesg | grep -i beaglelogic | tail -5 > /dev/null 2>&1; then
        log "Last 5 BeagleLogic kernel messages:"
        dmesg | grep -i beaglelogic | tail -5 | while read line; do
            echo "  $line"
        done
    else
        log "No recent BeagleLogic kernel messages found"
    fi

    # Summary
    echo ""
    log_section "Status Check Summary"

    if $ALL_OK; then
        echo -e "${GREEN}╔═══════════════════════════════════════════════════════════╗${NC}"
        echo -e "${GREEN}║                                                           ║${NC}"
        echo -e "${GREEN}║  ✓ BeagleLogic installation is HEALTHY                   ║${NC}"
        echo -e "${GREEN}║                                                           ║${NC}"
        echo -e "${GREEN}║  All critical components are installed and working        ║${NC}"
        echo -e "${GREEN}║                                                           ║${NC}"
        echo -e "${GREEN}╚═══════════════════════════════════════════════════════════╝${NC}"
    else
        echo -e "${RED}╔═══════════════════════════════════════════════════════════╗${NC}"
        echo -e "${RED}║                                                           ║${NC}"
        echo -e "${RED}║  ✗ BeagleLogic installation has ISSUES                   ║${NC}"
        echo -e "${RED}║                                                           ║${NC}"
        echo -e "${RED}║  Please review the errors above                          ║${NC}"
        echo -e "${RED}║                                                           ║${NC}"
        echo -e "${RED}╚═══════════════════════════════════════════════════════════╝${NC}"
    fi

    echo ""
    echo "For more details, check:"
    echo "  • Kernel messages:  dmesg | grep beaglelogic"
    echo "  • Module info:      modinfo beaglelogic"
    echo "  • Service logs:     journalctl -u beaglelogic-startup.service"
    echo ""
}

################################################################################
# Firmware Mode Configuration
################################################################################

show_current_firmware_config() {
    log_section "Current BeagleLogic Firmware Configuration"

    if [ -L "${PRU1_SYMLINK}" ]; then
        CURRENT_TARGET=$(readlink -f "${PRU1_SYMLINK}")
        CURRENT_MODE=$(basename "${CURRENT_TARGET}")

        echo -e "PRU1 Firmware: ${GREEN}${CURRENT_MODE}${NC}"
        echo ""

        case "${CURRENT_MODE}" in
            "beaglelogic-pru1-logic")
                echo "Mode: Logic Analyzer (8-bit or 14-bit digital capture)"
                echo "  • 8-bit mode:  100 MHz sample rate, 8 channels"
                echo "  • 14-bit mode: 50 MHz sample rate, 14 channels"
                ;;
            "beaglelogic-pru1-prudaq-ch0")
                echo "Mode: PRUDAQ ADC - Channel 0 Only"
                echo "  • 2 analog input channels (AI0, AI1)"
                echo "  • 10 MHz sample rate"
                ;;
            "beaglelogic-pru1-prudaq-ch1")
                echo "Mode: PRUDAQ ADC - Channel 1 Only"
                echo "  • 2 analog input channels (AI2, AI3)"
                echo "  • 10 MHz sample rate"
                ;;
            "beaglelogic-pru1-prudaq-ch01")
                echo "Mode: PRUDAQ ADC - Both Channels"
                echo "  • 4 analog input channels (AI0, AI1, AI2, AI3)"
                echo "  • 5 MHz sample rate (interleaved)"
                ;;
            *)
                echo "Mode: Unknown (${CURRENT_MODE})"
                ;;
        esac
    else
        log_warning "No PRU1 firmware symlink found"
        echo "Expected: ${PRU1_SYMLINK}"
    fi

    echo ""
}

verify_firmware_files() {
    log "Verifying firmware files..."

    local missing=0

    for FW_FILE in "${LOGIC_FW}" "${PRUDAQ_CH0_FW}" "${PRUDAQ_CH1_FW}" "${PRUDAQ_CH01_FW}"; do
        if [ ! -f "${FW_FILE}" ]; then
            log_error "Missing: $(basename ${FW_FILE})"
            missing=$((missing + 1))
        fi
    done

    if [ ${missing} -gt 0 ]; then
        echo ""
        log_error "${missing} firmware file(s) missing"
        log_warning "Run the installation script to build firmware:"
        echo "  sudo ./beaglelogic_setup.sh --install"
        return 1
    fi

    log_success "All firmware files present"
    echo ""
    return 0
}

set_firmware_mode() {
    local target_fw=$1
    local mode_name=$2

    log "Switching to: ${mode_name}"

    # Remove existing symlink if present
    if [ -L "${PRU1_SYMLINK}" ] || [ -e "${PRU1_SYMLINK}" ]; then
        rm -f "${PRU1_SYMLINK}"
    fi

    # Create new symlink
    ln -s "$(basename ${target_fw})" "${PRU1_SYMLINK}"

    log_success "Firmware symlink updated"
    echo ""

    # Reload PRU firmware
    log "Reloading PRU firmware..."

    # Stop beaglelogic services
    systemctl stop beaglelogic-tcp.service 2>/dev/null || true
    systemctl stop beaglelogic.service 2>/dev/null || true

    # Reload kernel module to apply firmware change
    if lsmod | grep -q beaglelogic; then
        rmmod beaglelogic 2>/dev/null || true
        sleep 1
        modprobe beaglelogic 2>/dev/null || true
    else
        modprobe beaglelogic 2>/dev/null || true
    fi

    # Restart services
    systemctl start beaglelogic-tcp.service 2>/dev/null || true
    systemctl start beaglelogic.service 2>/dev/null || true

    log_success "PRU firmware reloaded"
    echo ""
}

select_firmware_mode() {
    log_section "BeagleLogic Firmware Mode Selection"

    echo "Select the firmware mode to load:"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}1)${NC} Logic Analyzer (Standard)"
    echo "   • 8-bit mode:  100 MHz, 8 channels"
    echo "   • 14-bit mode: 50 MHz, 14 channels"
    echo "   • Use with: PulseView, sigrok, web interface"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}2)${NC} PRUDAQ ADC - Channel 0 Only"
    echo "   • 2 analog inputs (AI0, AI1)"
    echo "   • 10 MHz sample rate"
    echo "   • Use with: PRUDAQ cape"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}3)${NC} PRUDAQ ADC - Channel 1 Only"
    echo "   • 2 analog inputs (AI2, AI3)"
    echo "   • 10 MHz sample rate"
    echo "   • Use with: PRUDAQ cape"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}4)${NC} PRUDAQ ADC - Both Channels"
    echo "   • 4 analog inputs (AI0, AI1, AI2, AI3)"
    echo "   • 5 MHz sample rate (interleaved)"
    echo "   • Use with: PRUDAQ cape"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo -e "${GREEN}5)${NC} Show Current Configuration (no change)"
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo ""

    read -p "Enter selection (1-5): " choice

    case $choice in
        1)
            set_firmware_mode "${LOGIC_FW}" "Logic Analyzer Mode"
            ;;
        2)
            set_firmware_mode "${PRUDAQ_CH0_FW}" "PRUDAQ ADC - Channel 0"
            ;;
        3)
            set_firmware_mode "${PRUDAQ_CH1_FW}" "PRUDAQ ADC - Channel 1"
            ;;
        4)
            set_firmware_mode "${PRUDAQ_CH01_FW}" "PRUDAQ ADC - Both Channels"
            ;;
        5)
            # Just show current config
            ;;
        *)
            log_error "Invalid selection"
            return 1
            ;;
    esac
}

configure_firmware() {
    check_root

    # Show current configuration
    show_current_firmware_config

    # Verify all firmware files exist
    if ! verify_firmware_files; then
        exit 1
    fi

    # Let user select mode
    select_firmware_mode

    # Show final configuration
    log_section "Configuration Complete"
    show_current_firmware_config

    echo ""
    log_success "BeagleLogic firmware configuration updated successfully!"
    echo ""
    log "Next steps:"
    echo "  • Verify device: ls -l /dev/beaglelogic"
    echo "  • Check module:  lsmod | grep beaglelogic"
    echo "  • View logs:     dmesg | grep beaglelogic | tail -10"
    echo ""
}

################################################################################
# Interactive Menu
################################################################################

show_menu() {
    clear
    echo -e "${CYAN}======================================================================${NC}"
    echo -e "${CYAN}           BeagleLogic Management Script${NC}"
    echo -e "${CYAN}======================================================================${NC}"
    echo ""
    echo "Select an operation:"
    echo ""
    echo "  1) Fresh Installation"
    echo "     • Complete installation of BeagleLogic from scratch"
    echo "     • Installs dependencies, builds firmware, kernel module"
    echo "     • Sets up systemd services"
    echo ""
    echo "  2) Reinstall/Rebuild"
    echo "     • Rebuild and redeploy existing installation"
    echo "     • Does not reinstall dependencies"
    echo "     • Respects previously selected components"
    echo ""
    echo "  3) Clean Build Artifacts"
    echo "     • Remove compiled files and build artifacts"
    echo "     • Prepare for fresh build or git commit"
    echo "     • Choose level: basic, all (node_modules), or deep"
    echo ""
    echo "  4) Check Installation Status"
    echo "     • Verify kernel module, device node, firmware"
    echo "     • Check systemd services and test applications"
    echo "     • Display comprehensive system status"
    echo ""
    echo "  5) Configure Firmware Mode"
    echo "     • Switch between Logic Analyzer and PRUDAQ ADC modes"
    echo "     • Select channel configuration for PRUDAQ"
    echo "     • View current firmware configuration"
    echo ""
    echo "  6) Exit"
    echo ""
    read -p "Enter selection (1-6): " choice

    case $choice in
        1)
            OPERATION="install"
            ;;
        2)
            ask_rebuild_type
            ;;
        3)
            OPERATION="clean"
            ask_clean_level
            ;;
        4)
            OPERATION="status"
            ;;
        5)
            OPERATION="configure"
            ;;
        6)
            echo "Exiting..."
            exit 0
            ;;
        *)
            echo -e "${RED}Invalid selection${NC}"
            sleep 2
            show_menu
            ;;
    esac
}

ask_clean_level() {
    echo ""
    echo "Select cleaning level:"
    echo ""
    echo "  1) Basic - Build artifacts only"
    echo "  2) All - Build artifacts + node_modules"
    echo "  3) Deep - Everything including logs and caches"
    echo ""
    read -p "Enter selection (1-3): " clean_choice

    case $clean_choice in
        1)
            CLEAN_NODE_MODULES=false
            DEEP_CLEAN=false
            ;;
        2)
            CLEAN_NODE_MODULES=true
            DEEP_CLEAN=false
            ;;
        3)
            CLEAN_NODE_MODULES=true
            DEEP_CLEAN=true
            ;;
        *)
            echo -e "${RED}Invalid selection, using basic clean${NC}"
            CLEAN_NODE_MODULES=false
            DEEP_CLEAN=false
            ;;
    esac
}

ask_rebuild_type() {
    echo ""
    echo "Select rebuild type:"
    echo ""
    echo "  1) Full Rebuild (with clean)"
    echo "     • Runs 'make clean' before building"
    echo "     • Slower but guaranteed fresh build"
    echo "     • Use for first build or after major changes"
    echo ""
    echo "  2) Fast Rebuild (incremental)"
    echo "     • Skips 'make clean' step"
    echo "     • Only rebuilds changed files"
    echo "     • Much faster for quick iterations"
    echo ""
    read -p "Enter selection (1-2): " rebuild_choice

    case $rebuild_choice in
        1)
            OPERATION="reinstall"
            ;;
        2)
            OPERATION="rebuild_fast"
            ;;
        *)
            echo -e "${RED}Invalid selection, using full rebuild${NC}"
            OPERATION="reinstall"
            ;;
    esac
}

################################################################################
# State Management Functions (for handling reboots)
################################################################################

get_install_state() {
    if [ -f "${STATE_FILE}" ]; then
        cat "${STATE_FILE}"
    else
        echo "0"
    fi
}

set_install_state() {
    echo "$1" > "${STATE_FILE}"
    log "Installation state set to: $1"
}

clear_install_state() {
    rm -f "${STATE_FILE}"
    rm -f "${REBOOT_FLAG_FILE}"
}

needs_reboot() {
    [ -f "${REBOOT_FLAG_FILE}" ]
}

mark_needs_reboot() {
    touch "${REBOOT_FLAG_FILE}"
    log_warning "System reboot will be required"
}

################################################################################
# Configuration File Management
################################################################################

save_install_config() {
    mkdir -p "${BEAGLELOGIC_CONFIG_DIR}"
    cat > "${BEAGLELOGIC_CONFIG_FILE}" <<EOF
# BeagleLogic Installation Configuration
# This file tracks where BeagleLogic is installed
# Generated on: $(date)

BEAGLELOGIC_PATH="${INSTALL_DIR}"
BEAGLELOGIC_VERSION="2.0"
INSTALL_DATE="$(date -Iseconds)"
INSTALLED_BY="${DEFAULT_USER}"
EOF
    log "Installation configuration saved to ${BEAGLELOGIC_CONFIG_FILE}"
}

load_install_config() {
    if [ -f "${BEAGLELOGIC_CONFIG_FILE}" ]; then
        source "${BEAGLELOGIC_CONFIG_FILE}"
        log "Loaded BeagleLogic configuration from ${BEAGLELOGIC_CONFIG_FILE}"
        log "BeagleLogic path: ${BEAGLELOGIC_PATH}"
        return 0
    else
        log_warning "No BeagleLogic configuration file found"
        return 1
    fi
}

get_install_path() {
    if [ -f "${BEAGLELOGIC_CONFIG_FILE}" ]; then
        grep "^BEAGLELOGIC_PATH=" "${BEAGLELOGIC_CONFIG_FILE}" | cut -d'=' -f2- | tr -d '"'
    else
        echo ""
    fi
}

################################################################################
# Validation Functions
################################################################################

check_root() {
    if [ "$EUID" -ne 0 ]; then
        log_error "This script must be run as root (use sudo)"
        exit 1
    fi
}

check_platform() {
    log "Checking platform compatibility..."

    # Check if running on ARM
    ARCH=$(uname -m)
    if [[ ! "$ARCH" =~ arm* ]]; then
        log_warning "Not running on ARM architecture (detected: $ARCH)"
        log_warning "This script is designed for BeagleBone Black (ARM)"
        read -p "Continue anyway? (y/N) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi

    # Check kernel version
    KERNEL_VERSION=$(uname -r)
    log "Detected kernel version: ${KERNEL_VERSION}"

    KERNEL_MAJOR=$(echo ${KERNEL_VERSION} | cut -d. -f1)
    KERNEL_MINOR=$(echo ${KERNEL_VERSION} | cut -d. -f2)

    if [ "${KERNEL_MAJOR}" -lt 6 ] || ([ "${KERNEL_MAJOR}" -eq 6 ] && [ "${KERNEL_MINOR}" -lt 12 ]); then
        log_error "Kernel version 6.12+ required (detected: ${KERNEL_VERSION})"
        log_error "Please upgrade your kernel before installing BeagleLogic"
        exit 1
    fi

    log_success "Platform check passed"
}

check_debian_version() {
    log "Checking Debian version..."

    if [ -f /etc/debian_version ]; then
        DEBIAN_VERSION=$(cat /etc/debian_version)
        log "Detected Debian version: ${DEBIAN_VERSION}"

        # Debian 13 = Trixie
        DEBIAN_MAJOR=$(echo ${DEBIAN_VERSION} | cut -d. -f1)
        if [ "${DEBIAN_MAJOR}" -lt 13 ]; then
            log_warning "Debian 13 (Trixie) is recommended (detected: ${DEBIAN_VERSION})"
            log_warning "Some packages may not be available on older versions"
        fi
    else
        log_warning "Could not detect Debian version"
    fi
}

check_beaglelogic_installed() {
    if [ ! -f "${BEAGLELOGIC_CONFIG_FILE}" ]; then
        log_error "BeagleLogic installation not found"
        log_error "No configuration file at: ${BEAGLELOGIC_CONFIG_FILE}"
        echo ""
        log_error "The --reinstall option is for rebuilding an EXISTING installation."
        log_error "Since no installation was found, you need to perform a fresh install instead."
        echo ""
        log "To install BeagleLogic, run:"
        echo "  sudo ./beaglelogic_setup.sh --install"
        echo ""
        log "Or run without options for an interactive menu:"
        echo "  sudo ./beaglelogic_setup.sh"
        echo ""
        exit 1
    fi

    # Load the config to verify installation path
    local INSTALLED_PATH=$(get_install_path)
    if [ -n "${INSTALLED_PATH}" ] && [ "${INSTALLED_PATH}" != "${INSTALL_DIR}" ]; then
        log_warning "BeagleLogic was installed at: ${INSTALLED_PATH}"
        log_warning "But you're running from: ${INSTALL_DIR}"
        log_warning "To reinstall from this location, run --install (not --reinstall)"
    fi
}

################################################################################
# Component Selection and Detection
################################################################################

ask_component_selection() {
    log_section "Component Selection"

    echo ""
    echo "This installer will now ask about optional components."
    echo "Skipping components can save significant disk space."
    echo ""

    # Web Server
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Web Server (Node.js - Port 4000)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  • Browser-based interface for captures"
    echo "  • Real-time visualization"
    echo "  • Disk space: ~400MB (with npm packages)"
    echo ""
    read -p "Install web server? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        INSTALL_WEB_SERVER="yes"
    else
        INSTALL_WEB_SERVER="no"
        log_warning "Skipping web server installation"
    fi

    # TCP Server (PulseView)
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "TCP Server (Node.js - Port 5555) - FOR PULSEVIEW"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  • Required for PulseView/sigrok integration"
    echo "  • Enables remote logic analyzer functionality"
    echo "  • Disk space: ~50MB"
    echo ""
    read -p "Install TCP server for PulseView? (Y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        INSTALL_TCP_SERVER="no"
        log_warning "Skipping TCP server installation"
    else
        INSTALL_TCP_SERVER="yes"
    fi

    # Documentation
    echo ""
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "Documentation Website (MkDocs - Port 8000)"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  • Local documentation server"
    echo "  • Same docs available on GitHub"
    echo "  • Disk space: ~500MB (Python + MkDocs + build)"
    echo ""
    read -p "Install documentation server? (y/N) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        INSTALL_DOCS="yes"
    else
        INSTALL_DOCS="no"
        log_warning "Skipping documentation build (docs available on GitHub)"
    fi

    echo ""
    log_section "Installation Plan"
    echo ""
    echo "The following components will be installed:"
    echo "  [✓] Core System (PRU, Kernel Module, Device Tree)"
    echo "  [✓] Test Applications"
    [ "$INSTALL_WEB_SERVER" = "yes" ] && echo "  [✓] Web Server (Port 4000)" || echo "  [✗] Web Server (skipped)"
    [ "$INSTALL_TCP_SERVER" = "yes" ] && echo "  [✓] TCP Server (Port 5555)" || echo "  [✗] TCP Server (skipped)"
    [ "$INSTALL_DOCS" = "yes" ] && echo "  [✓] Documentation" || echo "  [✗] Documentation (skipped)"
    echo ""

    read -p "Proceed with installation? (Y/n) " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Nn]$ ]]; then
        log "Installation cancelled by user"
        exit 0
    fi
}

detect_installed_components() {
    log_section "Detecting Installed Components"

    # Check for web server (Node.js)
    HAS_WEB_SERVER=false
    if [ -d "${INSTALL_DIR}/server/node_modules" ]; then
        HAS_WEB_SERVER=true
        log_success "Web server detected (node_modules present)"
    else
        log_warning "Web server not installed (no node_modules)"
    fi

    # Check for TCP server (Node.js)
    HAS_TCP_SERVER=false
    if [ -d "${INSTALL_DIR}/tcp-server-node/node_modules" ]; then
        HAS_TCP_SERVER=true
        log_success "TCP server detected (node_modules present)"
    else
        log_warning "TCP server not installed (no node_modules)"
    fi

    # Check for documentation
    HAS_DOCS=false
    if command -v mkdocs &> /dev/null && [ -d "${INSTALL_DIR}/docs" ]; then
        HAS_DOCS=true
        log_success "Documentation support detected (MkDocs installed)"
    else
        log_warning "Documentation not configured (MkDocs not installed or docs missing)"
    fi

    # Core components are always present if installation directory exists
    log_success "Core components detected (kernel module, PRU firmware, device tree)"

    echo ""
    log "Components to rebuild:"
    echo "  [✓] Kernel Module"
    echo "  [✓] Device Tree Overlay"
    echo "  [✓] PRU Firmware"
    echo "  [✓] Test Applications"
    [ "$HAS_WEB_SERVER" = true ] && echo "  [✓] Web Server (Port 4000)" || echo "  [ ] Web Server (not installed)"
    [ "$HAS_TCP_SERVER" = true ] && echo "  [✓] TCP Server (Port 5555)" || echo "  [ ] TCP Server (not installed)"
    [ "$HAS_DOCS" = true ] && echo "  [✓] Documentation (Port 8000)" || echo "  [ ] Documentation (not installed)"
    echo ""
}

check_disk_space() {
    # Skip check if --skip-space-check flag was provided
    if [ "$SKIP_SPACE_CHECK" = true ]; then
        log_warning "Skipping disk space check (--skip-space-check flag provided)"
        log_warning "Installation may fail if insufficient space is available"
        return
    fi

    log "Checking available disk space..."

    AVAILABLE_KB=$(df /opt | tail -1 | awk '{print $4}')
    AVAILABLE_MB=$((AVAILABLE_KB / 1024))

    # Calculate required space based on selected components
    REQUIRED_MB=800  # Base system
    [ "$INSTALL_WEB_SERVER" = "yes" ] && REQUIRED_MB=$((REQUIRED_MB + 400))
    [ "$INSTALL_TCP_SERVER" = "yes" ] && REQUIRED_MB=$((REQUIRED_MB + 50))
    [ "$INSTALL_DOCS" = "yes" ] && REQUIRED_MB=$((REQUIRED_MB + 500))

    if [ ${AVAILABLE_MB} -lt ${REQUIRED_MB} ]; then
        log_error "Insufficient disk space for selected components"
        log_error "Required: ${REQUIRED_MB}MB, Available: ${AVAILABLE_MB}MB"
        log_error "Shortfall: $((REQUIRED_MB - AVAILABLE_MB))MB"
        echo ""
        log_error "Recommendations to free up space:"
        log_error "  1. Clean package cache:     sudo apt-get clean && sudo apt-get autoremove"
        log_error "  2. Remove old journal logs:  sudo journalctl --vacuum-time=7d"
        log_error "  3. Deselect components:      Re-run installer and skip optional components"
        echo ""

        # Interactive bypass option
        echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo -e "${RED}                    ⚠️  WARNING ⚠️${NC}"
        echo -e "${RED}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
        echo ""
        echo -e "${YELLOW}Proceeding with insufficient disk space is DANGEROUS and may:${NC}"
        echo -e "${YELLOW}  • Cause installation to fail mid-process${NC}"
        echo -e "${YELLOW}  • Leave your system in an inconsistent state${NC}"
        echo -e "${YELLOW}  • Corrupt files or damage the filesystem${NC}"
        echo -e "${YELLOW}  • Prevent the system from booting properly${NC}"
        echo -e "${YELLOW}  • Require manual cleanup and recovery${NC}"
        echo ""
        echo -e "${RED}This option should ONLY be used for testing/development when you${NC}"
        echo -e "${RED}understand the risks and have a backup/recovery plan.${NC}"
        echo ""

        read -p "Do you want to proceed anyway? (type 'yes' to continue, anything else to abort): " -r
        echo

        if [ "$REPLY" = "yes" ]; then
            log_warning "User chose to bypass disk space check"
            log_warning "Installation proceeding with insufficient space - DANGEROUS!"
            echo ""
            sleep 2  # Give user time to reconsider
        else
            log_error "Installation aborted - insufficient disk space"
            log "Please free up space and try again, or use --skip-space-check flag for non-interactive bypass"
            exit 1
        fi
    fi

    log_success "Sufficient disk space: ${AVAILABLE_MB}MB available, ${REQUIRED_MB}MB required"
}

check_old_kernels() {
    log "Checking for old kernel versions..."

    RUNNING_KERNEL=$(uname -r | cut -d- -f1,2)

    # Find old kernel packages (not matching running kernel)
    OLD_KERNELS=$(dpkg --list 2>/dev/null | awk '{ print $2 }' | \
                  grep -E 'linux-image-[0-9]+|linux-headers-[0-9]+' | \
                  grep -v "$RUNNING_KERNEL" || true)

    if [ -z "$OLD_KERNELS" ]; then
        log "No old kernel versions found."
        return 0
    fi

    # Calculate space that would be freed
    FREED_SPACE=$(dpkg-query -W -f='${Installed-Size}\t${Package}\n' $OLD_KERNELS 2>/dev/null | \
                  awk '{sum+=$1} END {print int(sum/1024)}' || echo "unknown")

    log_warning "Found old kernel versions that could be removed:"
    echo "$OLD_KERNELS" | sed 's/^/  - /'
    log_warning "Estimated space to be freed: ${FREED_SPACE} MB"

    # Check if a newer kernel is installed but not running
    LATEST_KERNEL=$(dpkg --list 2>/dev/null | awk '{ print $2 }' | \
                    grep -E '^linux-image-[0-9]' | \
                    sort -V | tail -1 | sed 's/linux-image-//')

    if [ "$RUNNING_KERNEL" != "$(echo $LATEST_KERNEL | cut -d- -f1,2)" ]; then
        log_warning "WARNING: You are running kernel $RUNNING_KERNEL, but $LATEST_KERNEL is installed."
        log_warning "It's recommended to reboot into the latest kernel before installation."
        echo ""
        read -p "Reboot now to use latest kernel? [y/N] " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            log "Rebooting to latest kernel. Please re-run installation script after reboot."
            reboot
            exit 0
        fi
    fi

    # Offer to remove old kernels
    echo ""
    read -p "Remove old kernel versions to free ${FREED_SPACE} MB? [y/N] " -n 1 -r
    echo
    if [[ $REPLY =~ ^[Yy]$ ]]; then
        log "Removing old kernel versions..."
        apt-get purge -y $OLD_KERNELS || log_warning "Failed to remove some kernel packages (non-critical)"
        apt-get autoremove -y || log_warning "Failed to autoremove packages (non-critical)"
        log_success "Old kernels removed. Freed approximately ${FREED_SPACE} MB."
    else
        log "Skipping kernel cleanup. You can manually remove them later with:"
        log "  sudo apt purge $(echo $OLD_KERNELS | tr '\n' ' ')"
    fi
}

################################################################################
# INSTALL OPERATION - Phase 1: Dependencies and Setup
################################################################################

install_system_dependencies() {
    log_section "PHASE 1: Installing System Dependencies"

    log "Updating package lists..."
    apt-get update || { log_error "Failed to update package lists"; exit 1; }

    log "Installing build essentials..."
    apt-get install -y \
        build-essential \
        gcc \
        g++ \
        make \
        git \
        wget \
        curl \
        ca-certificates \
        || { log_error "Failed to install build essentials"; exit 1; }

    log "Installing kernel development packages..."
    KERNEL_VERSION=$(uname -r)
    apt-get install -y \
        linux-headers-${KERNEL_VERSION} \
        device-tree-compiler \
        cpp \
        || {
            log_error "Failed to install kernel headers for ${KERNEL_VERSION}"
            log_error "Make sure you're using an official BeagleBoard image with proper repositories"
            log_error "Try: apt-cache search linux-headers"
            exit 1
        }

    log "Installing PRU development tools..."
    apt-get install -y ti-pru-cgt-v2.3 \
        || apt-get install -y ti-pru-cgt-installer \
        || { log_error "Failed to install PRU compiler"; exit 1; }

    log "Installing Node.js and npm..."
    apt-get install -y nodejs npm \
        || { log_error "Failed to install Node.js"; exit 1; }

    log "Installing documentation tools..."
    apt-get install -y \
        python3 \
        python3-pip \
        python3-venv \
        || { log_error "Failed to install Python"; exit 1; }

    log "Installing optional tools (sigrok, etc.)..."
    apt-get install -y \
        sigrok-cli \
        clang \
        || log_warning "Some optional packages failed to install (non-critical)"

    log "Cleaning up package cache to save disk space..."
    apt-get clean || log_warning "Failed to clean package cache (non-critical)"
    apt-get autoremove -y || log_warning "Failed to autoremove packages (non-critical)"

    # Display disk space saved
    log "Disk space cleanup complete"

    log_success "System dependencies installed successfully"
}

setup_pru_compiler_symlink() {
    log "Setting up PRU compiler symlink..."

    if [ ! -d "${PRU_CGT_DIR}/bin" ]; then
        log "Creating symlink: ${PRU_CGT_DIR}/bin -> /usr/bin/"
        mkdir -p "${PRU_CGT_DIR}"
        ln -sf /usr/bin/ "${PRU_CGT_DIR}/bin"
        log_success "PRU compiler symlink created"
    else
        log "PRU compiler symlink already exists"
    fi
}

install_pru_software_support() {
    log_section "PHASE 2: Installing PRU Software Support Package"

    if [ -d "${PRU_SUPPORT_DIR}" ]; then
        log "PRU Software Support Package already exists at ${PRU_SUPPORT_DIR}"
        log "Checking if update is needed..."
    fi

    # Create directory
    mkdir -p /usr/lib/ti/

    # Clone or update pru-software-support-package
    if [ ! -d "${PRU_SUPPORT_DIR}/.git" ]; then
        log "Cloning pru-software-support-package from GitHub..."
        git clone https://git.ti.com/cgit/pru-software-support-package/pru-software-support-package \
            "${PRU_SUPPORT_DIR}" \
            || { log_error "Failed to clone pru-software-support-package"; exit 1; }
        log_success "PRU Software Support Package cloned successfully"
    else
        log "Updating existing pru-software-support-package..."
        cd "${PRU_SUPPORT_DIR}"
        git pull || log_warning "Failed to update pru-software-support-package (continuing with existing version)"
        cd "${SCRIPT_DIR}"
    fi

    log_success "PRU Software Support Package ready at ${PRU_SUPPORT_DIR}"
}

################################################################################
# INSTALL OPERATION - Phase 2: Compile and Install PRU Firmware
################################################################################

compile_pru_firmware() {
    log_section "PHASE 4: Compiling PRU Firmware"

    cd "${INSTALL_DIR}/firmware"

    # Export PRU_CGT environment variable
    export PRU_CGT="${PRU_CGT_DIR}"
    log "Set PRU_CGT=${PRU_CGT}"

    # Thorough cleanup to prevent stale build artifacts
    log "Performing thorough cleanup of previous builds..."
    make clean || log_warning "Clean failed (may be first build)"

    # Remove any remaining build artifacts that make clean might miss
    rm -rf release/ 2>/dev/null || true
    find . -name "*.pp" -delete 2>/dev/null || true
    find . -name "*.object" -delete 2>/dev/null || true
    find . -name "*.out" -delete 2>/dev/null || true
    find . -name "*.map" -delete 2>/dev/null || true

    log "Build artifacts cleaned"

    # Verify source files exist
    log "Verifying source files..."
    REQUIRED_FILES=(
        "beaglelogic-pru0.c"
        "beaglelogic-pru0-core.asm"
        "beaglelogic-pru1.c"
        "beaglelogic-pru1-core.asm"
        "Makefile"
    )

    for file in "${REQUIRED_FILES[@]}"; do
        if [ ! -f "$file" ]; then
            log_error "Missing required source file: $file"
            log_error "Current directory: $(pwd)"
            log_error "Directory contents:"
            ls -la
            exit 1
        fi
    done
    log_success "All source files present"

    # Build PRU0 and PRU1 firmware
    log "Building PRU0 and PRU1 firmware..."
    make || { log_error "Failed to build PRU firmware"; exit 1; }

    log_success "PRU firmware compiled successfully"
}

compile_prudaq_firmware() {
    log "Compiling PRUDAQ firmware variants..."

    cd "${INSTALL_DIR}/firmware/custom/prudaq"

    # Export PRU_CGT environment variable
    export PRU_CGT="${PRU_CGT_DIR}"

    # Thorough cleanup to prevent stale build artifacts
    log "Performing thorough cleanup of previous PRUDAQ builds..."
    make clean || log_warning "Clean failed (may be first build)"

    # Remove any remaining build artifacts that make clean might miss
    rm -rf release/ 2>/dev/null || true
    find . -name "*.pp" -delete 2>/dev/null || true
    find . -name "*.object" -delete 2>/dev/null || true
    find . -name "*.out" -delete 2>/dev/null || true
    find . -name "*.map" -delete 2>/dev/null || true

    log "PRUDAQ build artifacts cleaned"

    # Build PRUDAQ variants
    log "Building PRUDAQ variants (ch0, ch1, ch01)..."
    make || { log_error "Failed to build PRUDAQ firmware"; exit 1; }

    log_success "PRUDAQ firmware compiled successfully"
}

install_pru_firmware() {
    log "Installing PRU firmware to /lib/firmware..."

    cd "${INSTALL_DIR}/firmware"
    export PRU_CGT="${PRU_CGT_DIR}"

    # Install main firmware
    make install || { log_error "Failed to install PRU firmware"; exit 1; }

    # Install PRUDAQ firmware
    cd "${INSTALL_DIR}/firmware/custom/prudaq"
    make install || { log_error "Failed to install PRUDAQ firmware"; exit 1; }

    log_success "PRU firmware installed to /lib/firmware"

    # Verify installation
    log "Verifying firmware installation..."
    EXPECTED_FILES=(
        "/lib/firmware/beaglelogic-pru0-fw"
        "/lib/firmware/beaglelogic-pru1-logic"
        "/lib/firmware/beaglelogic-pru1-fw"
        "/lib/firmware/beaglelogic-pru1-prudaq-ch0"
        "/lib/firmware/beaglelogic-pru1-prudaq-ch1"
        "/lib/firmware/beaglelogic-pru1-prudaq-ch01"
    )

    MISSING_FILES=0
    for FILE in "${EXPECTED_FILES[@]}"; do
        if [ ! -f "${FILE}" ]; then
            log_error "Missing firmware file: ${FILE}"
            MISSING_FILES=$((MISSING_FILES + 1))
        fi
    done

    if [ ${MISSING_FILES} -gt 0 ]; then
        log_error "${MISSING_FILES} firmware file(s) missing"
        exit 1
    fi

    log_success "All firmware files verified"
}

################################################################################
# INSTALL OPERATION - Phase 4: Kernel Module
################################################################################

compile_kernel_module() {
    log_section "PHASE 5: Compiling Kernel Module"

    cd "${INSTALL_DIR}/kernel"

    # Clean any previous builds
    log "Cleaning previous kernel module builds..."
    make clean || log_warning "Clean failed (may be first build)"

    # Build kernel module
    log "Building beaglelogic.ko kernel module..."
    make || { log_error "Failed to build kernel module - ensure kernel headers are installed"; exit 1; }

    # Verify module was created
    if [ ! -f "${INSTALL_DIR}/kernel/beaglelogic.ko" ]; then
        log_error "Kernel module beaglelogic.ko was not created"
        exit 1
    fi

    log_success "Kernel module compiled successfully"
}

install_kernel_module() {
    log "Installing kernel module..."

    cd "${INSTALL_DIR}/kernel"

    # Copy module to kernel modules directory
    KERNEL_VERSION=$(uname -r)
    MODULE_DIR="/lib/modules/${KERNEL_VERSION}/extra"

    mkdir -p "${MODULE_DIR}"
    cp -v beaglelogic.ko "${MODULE_DIR}/" \
        || { log_error "Failed to copy kernel module"; exit 1; }

    # Update module dependencies
    log "Updating module dependencies..."
    depmod -a || { log_error "Failed to update module dependencies"; exit 1; }

    log_success "Kernel module installed to ${MODULE_DIR}"
}

configure_module_autoload() {
    log "Configuring kernel module to load at boot..."

    # Add to /etc/modules-load.d/
    echo "beaglelogic" > /etc/modules-load.d/beaglelogic.conf

    log_success "Module configured to auto-load at boot"
}

################################################################################
# INSTALL OPERATION - Phase 5: Device Tree Overlay
################################################################################

compile_device_tree_overlay() {
    log_section "PHASE 6: Compiling Device Tree Overlay"

    cd "${INSTALL_DIR}/kernel"

    # Clean previous overlay builds
    log "Cleaning previous overlay builds..."
    rm -f beaglelogic-00A0.dtbo beaglelogic-00A0.dtso

    # Check if device tree bindings are available
    DTBINDSRC="/opt/source/dtb-6.x/include"
    if [ ! -d "${DTBINDSRC}" ]; then
        log_warning "Device tree bindings not found at ${DTBINDSRC}"
        log "Using kernel headers for device tree compilation..."
        DTBINDSRC="/usr/src/linux-headers-$(uname -r)/include"
    fi

    # Build overlay
    log "Building device tree overlay..."

    # Try the preprocessor method first (kernel 6.17+ preferred)
    if cpp -nostdinc -I "${DTBINDSRC}" -I arch -undef -x assembler-with-cpp \
        beaglelogic-00A0.dts > beaglelogic-00A0.dtso 2>/dev/null; then
        log "Preprocessing completed successfully"
        dtc -@ -O dtb -o beaglelogic-00A0.dtbo beaglelogic-00A0.dtso \
            || { log_error "Failed to compile device tree overlay"; exit 1; }
    else
        # Fallback to simple method without preprocessor
        log_warning "Preprocessor method failed, trying direct compilation..."
        dtc -@ -I dts -O dtb -o beaglelogic-00A0.dtbo beaglelogic-00A0.dts \
            || { log_error "Failed to compile device tree overlay"; exit 1; }
    fi

    # Verify overlay was created
    if [ ! -f "beaglelogic-00A0.dtbo" ]; then
        log_error "Device tree overlay was not created"
        exit 1
    fi

    log_success "Device tree overlay compiled successfully"
}

install_device_tree_overlay() {
    log "Installing device tree overlay to /lib/firmware..."

    cd "${INSTALL_DIR}/kernel"

    cp -v beaglelogic-00A0.dtbo /lib/firmware/ \
        || { log_error "Failed to install device tree overlay"; exit 1; }

    log_success "Device tree overlay installed"
}

configure_uboot_overlay() {
    log "Configuring U-Boot to load BeagleLogic overlay..."

    UENV_FILE="/boot/uEnv.txt"

    if [ ! -f "${UENV_FILE}" ]; then
        log_warning "U-Boot environment file not found at ${UENV_FILE}"
        log_warning "Overlay must be loaded manually or via alternative method"
        return 0
    fi

    # Check if already configured
    if grep -q "beaglelogic-00A0.dtbo" "${UENV_FILE}"; then
        log "U-Boot already configured for BeagleLogic overlay"
        return 0
    fi

    # Backup original uEnv.txt
    cp "${UENV_FILE}" "${UENV_FILE}.backup.$(date +%Y%m%d_%H%M%S)"

    # Enable U-Boot overlays (CRITICAL: required for device tree overlays to load)
    log "Enabling U-Boot overlays..."
    sed -i -e "s:#enable_uboot_overlays:enable_uboot_overlays:" "${UENV_FILE}"

    # Disable video overlays (conflicts with PRU pins)
    sed -i -e "s:#disable_uboot_overlay_video:disable_uboot_overlay_video:" "${UENV_FILE}"

    # Comment out default PRU overlay if present
    sed -i -e "s:uboot_overlay_pru:#uboot_overlay_pru:" "${UENV_FILE}"

    # Add BeagleLogic overlay
    if ! grep -q "Load BeagleLogic Cape" "${UENV_FILE}"; then
        echo "" >> "${UENV_FILE}"
        echo "#Load BeagleLogic Cape" >> "${UENV_FILE}"
        echo "uboot_overlay_pru=/lib/firmware/beaglelogic-00A0.dtbo" >> "${UENV_FILE}"
    fi

    log_success "U-Boot configured to load BeagleLogic overlay"
    mark_needs_reboot
}

################################################################################
# INSTALL OPERATION - Phase 6: Test Applications
################################################################################

compile_test_applications() {
    log_section "PHASE 7: Compiling Test Applications"

    cd "${INSTALL_DIR}/testapp"

    # Verify required files are present
    if [ ! -f "beaglelogic.c" ] || [ ! -f "libbeaglelogic.h" ]; then
        log_error "Required files missing in testapp directory"
        exit 1
    fi

    # Clean previous builds
    log "Cleaning previous builds..."
    make clean 2>/dev/null || true

    # Build unified test application (beaglelogic-testapp)
    log "Building unified test application (beaglelogic-testapp)..."
    make || { log_error "Failed to compile test application"; exit 1; }

    # Verify beaglelogic-testapp binary was created
    if [ ! -f "${INSTALL_DIR}/testapp/beaglelogic-testapp" ]; then
        log_error "Binary beaglelogic-testapp was not created"
        exit 1
    fi

    log_success "Unified test application (beaglelogic-testapp) compiled successfully"
}

################################################################################
# INSTALL OPERATION - Phase 7: Web Server
################################################################################

build_web_server() {
    log_section "PHASE 8: Building Web Server (Node.js)"

    cd "${INSTALL_DIR}/server"

    # Remove existing node_modules if they exist (fresh install)
    if [ -d "node_modules" ]; then
        log "Removing existing node_modules..."
        rm -rf node_modules
    fi

    # Set ownership before npm install (must be owned by user, not root)
    log "Setting directory ownership for npm install..."
    chown -R ${DEFAULT_USER}:${DEFAULT_USER} "${INSTALL_DIR}/server"

    # Install npm dependencies as the default user (not root)
    log "Installing npm dependencies (this may take a few minutes)..."
    log_warning "Note: You may see deprecation warnings for old Express 3.x dependencies"
    log_warning "These warnings are non-critical and do not affect functionality"
    su - ${DEFAULT_USER} -c "cd ${INSTALL_DIR}/server && npm install" \
        || { log_error "Failed to install npm dependencies"; exit 1; }

    # Clean npm cache to save disk space
    log "Cleaning npm cache to save disk space..."
    su - ${DEFAULT_USER} -c "npm cache clean --force" || log_warning "Failed to clean npm cache (non-critical)"

    # Verify webapp directory exists (server serves files from ../webapp/)
    if [ ! -d "${INSTALL_DIR}/webapp" ]; then
        log_warning "webapp directory not found - creating placeholder..."
        mkdir -p "${INSTALL_DIR}/webapp"
        cat > "${INSTALL_DIR}/webapp/index.html" << 'EOF'
<!DOCTYPE html>
<html>
<head>
    <title>BeagleLogic</title>
</head>
<body>
    <h1>BeagleLogic Web Interface</h1>
    <p>Web interface placeholder. The actual web application needs to be implemented.</p>
    <p>For now, use:</p>
    <ul>
        <li><a href="http://192.168.7.2:8000">Documentation (MkDocs)</a></li>
        <li>PulseView with TCP server (port 5555)</li>
        <li>Command-line tools in /opt/BeagleLogic/testapp/</li>
    </ul>
</body>
</html>
EOF
    fi

    log_success "Web server dependencies installed"
}

################################################################################
# INSTALL OPERATION - Phase 8: TCP Server
################################################################################

install_tcp_server() {
    log_section "PHASE 9: Installing TCP Server (Node.js) for PulseView"

    cd "${INSTALL_DIR}/tcp-server-node"

    # Set ownership before npm install (must be owned by user, not root)
    log "Setting directory ownership for npm install..."
    chown -R ${DEFAULT_USER}:${DEFAULT_USER} "${INSTALL_DIR}/tcp-server-node"

    # Install npm dependencies
    log "Installing npm dependencies for TCP server..."
    su - ${DEFAULT_USER} -c "cd ${INSTALL_DIR}/tcp-server-node && npm install" \
        || { log_error "Failed to install TCP server dependencies"; exit 1; }

    # Clean npm cache to save disk space
    log "Cleaning npm cache to save disk space..."
    su - ${DEFAULT_USER} -c "npm cache clean --force" || log_warning "Failed to clean npm cache (non-critical)"

    # Verify server.js exists
    if [ ! -f "${INSTALL_DIR}/tcp-server-node/server.js" ]; then
        log_error "TCP server script (server.js) not found"
        exit 1
    fi

    log_success "TCP server installed successfully"
}

################################################################################
# INSTALL OPERATION - Phase 9: Documentation
################################################################################

build_documentation() {
    log_section "PHASE 10: Building Documentation Website"

    cd "${INSTALL_DIR}"

    # Check if Python requirements file exists
    if [ ! -f "requirements-docs.txt" ]; then
        log_warning "requirements-docs.txt not found, skipping documentation build"
        return 0
    fi

    # Install mkdocs and dependencies
    log "Installing MkDocs and dependencies..."
    pip3 install --break-system-packages -r requirements-docs.txt \
        || log_warning "Failed to install documentation dependencies (non-critical)"

    # Build documentation
    if command -v mkdocs &> /dev/null; then
        log "Building documentation with MkDocs..."
        log_warning "Note: MkDocs may show warnings about external links (e.g., ../README.md)"
        log_warning "These warnings are informational - links work fine in the repository"
        mkdocs build || log_warning "MkDocs build failed (non-critical)"
        log_success "Documentation built successfully"
    else
        log_warning "MkDocs not available, skipping documentation build"
    fi
}

################################################################################
# INSTALL OPERATION - Phase 10: System Integration
################################################################################

create_beaglelogic_group() {
    log_section "PHASE 11: Configuring System Integration"

    log "Creating beaglelogic group..."
    groupadd -f beaglelogic

    log "Adding ${DEFAULT_USER} to beaglelogic group..."
    usermod -aG beaglelogic ${DEFAULT_USER}

    log_success "User ${DEFAULT_USER} added to beaglelogic group"
}

install_udev_rules() {
    log "Installing udev rules..."

    if [ -f "${INSTALL_DIR}/scripts/90-beaglelogic.rules" ]; then
        cp -v "${INSTALL_DIR}/scripts/90-beaglelogic.rules" /etc/udev/rules.d/

        # Reload udev rules
        udevadm control --reload-rules
        udevadm trigger

        log_success "Udev rules installed"
    else
        log_warning "Udev rules file not found, skipping"
    fi
}

install_systemd_services() {
    log "Installing systemd services..."

    # Copy service files and substitute DIR placeholder
    for SERVICE_FILE in beaglelogic.service beaglelogic-startup.service beaglelogic-tcp.service; do
        if [ -f "${INSTALL_DIR}/scripts/${SERVICE_FILE}" ]; then
            log "Installing ${SERVICE_FILE}..."
            sed "s:DIR:${INSTALL_DIR}:g" "${INSTALL_DIR}/scripts/${SERVICE_FILE}" \
                > "/lib/systemd/system/${SERVICE_FILE}"
            chown root:root "/lib/systemd/system/${SERVICE_FILE}"
            chmod 644 "/lib/systemd/system/${SERVICE_FILE}"
        else
            log_warning "${SERVICE_FILE} not found, skipping"
        fi
    done

    # Create MkDocs documentation server service (dynamically generated with correct path)
    log "Creating MkDocs documentation service..."
    cat > /lib/systemd/system/beaglelogic-docs.service <<EOF
[Unit]
Description=BeagleLogic Documentation Server (MkDocs)
After=network.target

[Service]
Type=simple
WorkingDirectory=${INSTALL_DIR}
ExecStart=/usr/local/bin/mkdocs serve --dev-addr=0.0.0.0:8000
Restart=on-failure
RestartSec=5
User=${DEFAULT_USER}

[Install]
WantedBy=multi-user.target
EOF
    chown root:root /lib/systemd/system/beaglelogic-docs.service
    chmod 644 /lib/systemd/system/beaglelogic-docs.service

    # Copy environment files
    for ENV_FILE in beaglelogic beaglelogic-tcp; do
        if [ -f "${INSTALL_DIR}/scripts/${ENV_FILE}" ]; then
            cp -v "${INSTALL_DIR}/scripts/${ENV_FILE}" "/etc/default/"
        fi
    done

    # Ensure startup script is executable
    if [ -f "${INSTALL_DIR}/scripts/beaglelogic-startup.sh" ]; then
        chmod +x "${INSTALL_DIR}/scripts/beaglelogic-startup.sh"
    fi

    # Reload systemd
    log "Reloading systemd daemon..."
    systemctl daemon-reload

    # Enable services
    log "Enabling systemd services to auto-start on boot..."

    # Enable each service individually with proper error handling
    if systemctl enable beaglelogic-startup.service; then
        log_success "Enabled beaglelogic-startup.service"
    else
        log_error "Failed to enable beaglelogic-startup.service"
    fi

    if systemctl enable beaglelogic.service; then
        log_success "Enabled beaglelogic.service"
    else
        log_error "Failed to enable beaglelogic.service"
    fi

    if systemctl enable beaglelogic-tcp.service; then
        log_success "Enabled beaglelogic-tcp.service"
    else
        log_error "Failed to enable beaglelogic-tcp.service"
    fi

    if systemctl enable beaglelogic-docs.service; then
        log_success "Enabled beaglelogic-docs.service"
    else
        log_error "Failed to enable beaglelogic-docs.service"
    fi

    log_success "Systemd services installed and enabled"
}

set_permissions() {
    log "Setting file permissions..."

    # Set ownership of entire installation directory
    log "Setting ownership to ${DEFAULT_USER}:${DEFAULT_USER}..."
    chown -R ${DEFAULT_USER}:${DEFAULT_USER} "${INSTALL_DIR}"

    # Make scripts executable
    find "${INSTALL_DIR}/scripts" -type f -name "*.sh" -exec chmod +x {} \;

    # Make test application executable
    if [ -d "${INSTALL_DIR}/testapp" ]; then
        chmod +x "${INSTALL_DIR}/testapp"/beaglelogic-testapp 2>/dev/null || true
    fi

    # Make ptof executable
    if [ -f "${INSTALL_DIR}/pspec/ptof" ]; then
        chmod +x "${INSTALL_DIR}/pspec/ptof"
    fi

    # Verify ownership was set correctly
    OWNER=$(stat -c '%U' "${INSTALL_DIR}" 2>/dev/null || echo "unknown")
    if [ "$OWNER" = "${DEFAULT_USER}" ]; then
        log_success "Permissions set correctly (owner: ${DEFAULT_USER})"
    else
        log_warning "Ownership verification failed (owner: ${OWNER}, expected: ${DEFAULT_USER})"
    fi
}

################################################################################
# INSTALL OPERATION - Post-Installation Functions
################################################################################

create_data_directory() {
    log "Creating data directory for ${DEFAULT_USER}..."

    DATA_DIR="/home/${DEFAULT_USER}/Data"
    mkdir -p "${DATA_DIR}"
    chown ${DEFAULT_USER}:${DEFAULT_USER} "${DATA_DIR}"

    log_success "Data directory created at ${DATA_DIR}"
}

update_initramfs() {
    log "Updating initramfs..."

    update-initramfs -u -k $(uname -r) \
        || log_warning "Failed to update initramfs (may not be critical)"

    log_success "Initramfs updated"
}

display_installation_summary() {
    log_section "INSTALLATION SUMMARY"

    echo ""
    echo "================================================================"
    echo "  BeagleLogic Installation Complete!"
    echo "================================================================"
    echo ""
    echo "Installation directory: ${INSTALL_DIR}"
    echo "PRU firmware location:  /lib/firmware"
    echo "Kernel module:          /lib/modules/$(uname -r)/extra/beaglelogic.ko"
    echo "Device tree overlay:    /lib/firmware/beaglelogic-00A0.dtbo"
    echo ""
    echo "Installed Components:"
    echo "  [✓] PRU Software Support Package"
    echo "  [✓] PRU Firmware (PRU0, PRU1, PRUDAQ variants)"
    echo "  [✓] Kernel Module (beaglelogic.ko)"
    echo "  [✓] Device Tree Overlay (beaglelogic-00A0.dtbo)"
    echo "  [✓] Test Applications (testapp/)"
    [ "${INSTALL_WEB_SERVER}" = "yes" ] && echo "  [✓] Web Server (Node.js - Port 4000)" || echo "  [✗] Web Server (not installed)"
    [ "${INSTALL_TCP_SERVER}" = "yes" ] && echo "  [✓] TCP Server (Node.js - Port 5555 for PulseView)" || echo "  [✗] TCP Server (not installed)"
    [ "${INSTALL_DOCS}" = "yes" ] && echo "  [✓] Documentation (MkDocs - Port 8000)" || echo "  [✗] Documentation (not installed)"
    echo "  [✓] Systemd Services"
    echo ""
    echo "Systemd Services (enabled for auto-start on boot):"
    echo "  • beaglelogic-startup.service - Initializes BeagleLogic at boot"
    [ "${INSTALL_WEB_SERVER}" = "yes" ] && echo "  • beaglelogic.service         - Web interface (port 4000)"
    [ "${INSTALL_TCP_SERVER}" = "yes" ] && echo "  • beaglelogic-tcp.service     - TCP server for PulseView (port 5555)"
    [ "${INSTALL_DOCS}" = "yes" ] && echo "  • beaglelogic-docs.service    - Documentation server (port 8000)"
    echo ""
    echo "Service Status:"
    systemctl is-enabled beaglelogic-startup.service 2>/dev/null && echo "  ✓ beaglelogic-startup.service: enabled" || echo "  ✗ beaglelogic-startup.service: disabled"
    [ "${INSTALL_WEB_SERVER}" = "yes" ] && { systemctl is-enabled beaglelogic.service 2>/dev/null && echo "  ✓ beaglelogic.service: enabled" || echo "  ✗ beaglelogic.service: disabled"; }
    [ "${INSTALL_TCP_SERVER}" = "yes" ] && { systemctl is-enabled beaglelogic-tcp.service 2>/dev/null && echo "  ✓ beaglelogic-tcp.service: enabled" || echo "  ✗ beaglelogic-tcp.service: disabled"; }
    [ "${INSTALL_DOCS}" = "yes" ] && { systemctl is-enabled beaglelogic-docs.service 2>/dev/null && echo "  ✓ beaglelogic-docs.service: enabled" || echo "  ✗ beaglelogic-docs.service: disabled"; }
    echo ""
    echo "Quick Start Commands:"
    echo "  • Check device:    ls -l /dev/beaglelogic"
    echo "  • View logs:       journalctl -u beaglelogic-startup.service"
    [ "${INSTALL_WEB_SERVER}" = "yes" ] && echo "  • Web interface:   http://192.168.7.2:4000/"
    [ "${INSTALL_TCP_SERVER}" = "yes" ] && echo "  • PulseView:       Connect to <beaglebone-ip>:5555"
    echo "  • Run test app:    cd ${INSTALL_DIR}/testapp && sudo ./beaglelogic-testapp"
    echo ""
    echo "Documentation:"
    echo "  • Main README:     ${INSTALL_DIR}/README.md"
    echo "  • Full docs:       ${INSTALL_DIR}/README_FULL.md"
    echo "  • Getting Started: ${INSTALL_DIR}/docs/02-getting-started.md"
    echo ""

    if needs_reboot; then
        echo "================================================================"
        echo "  ⚠️  REBOOT REQUIRED"
        echo "================================================================"
        echo ""
        echo "The system must be rebooted to:"
        echo "  • Load device tree overlay"
        echo "  • Apply U-Boot configuration changes"
        echo "  • Initialize PRU subsystem"
        echo ""
        echo "After reboot, verify installation with:"
        echo "  • lsmod | grep beaglelogic"
        echo "  • ls -l /dev/beaglelogic"
        echo "  • systemctl status beaglelogic-startup.service"
        echo ""

        read -p "Reboot now? (y/N) " -n 1 -r
        echo
        if [[ $REPLY =~ ^[Yy]$ ]]; then
            log "Rebooting system..."
            clear_install_state
            reboot
        else
            echo "Please reboot manually when ready: sudo reboot"
        fi
    else
        echo "Installation complete. System is ready to use."
        echo ""
    fi
}

################################################################################
# REINSTALL OPERATION - Rebuild Functions
################################################################################

rebuild_kernel_module() {
    local do_clean="${1:-clean}"  # Default to clean if not specified

    log_section "Rebuilding Kernel Module"

    cd "${INSTALL_DIR}/kernel"

    # Clean previous build (skip if fast rebuild)
    if [ "$do_clean" = "clean" ]; then
        log "Cleaning previous kernel module build..."
        make clean || true
    else
        log "Fast rebuild - skipping clean step..."
    fi

    # Build kernel module
    log "Building kernel module..."
    make || {
        log_error "Kernel module build failed"
        exit 1
    }

    # Deploy kernel module
    log "Deploying kernel module..."
    if [ -f beaglelogic.ko ]; then
        # Deploy to extra/ directory where the system loads it from
        mkdir -p /lib/modules/$(uname -r)/extra/
        cp -v beaglelogic.ko /lib/modules/$(uname -r)/extra/
        depmod -a
        log_success "Kernel module rebuilt and deployed to /lib/modules/$(uname -r)/extra/"
    else
        log_error "Kernel module build failed - beaglelogic.ko not found"
        exit 1
    fi
}

rebuild_device_tree() {
    log_section "Rebuilding Device Tree Overlay"

    cd "${INSTALL_DIR}/kernel"

    # Build device tree overlay
    log "Building device tree overlay..."
    make overlay || {
        log_error "Device tree overlay build failed"
        exit 1
    }

    # Deploy overlay
    log "Deploying device tree overlay..."
    if [ -f beaglelogic-00A0.dtbo ]; then
        cp -v beaglelogic-00A0.dtbo /lib/firmware/
        log_success "Device tree overlay rebuilt and deployed"
    else
        log_error "Device tree overlay build failed"
        exit 1
    fi
}

rebuild_pru_firmware() {
    local do_clean="${1:-clean}"  # Default to clean if not specified

    log_section "Rebuilding PRU Firmware"

    # Check if PRU compiler exists
    if [ ! -d "$PRU_CGT_DIR" ]; then
        log_error "PRU compiler not found at $PRU_CGT_DIR"
        log_error "Please run --install first to set up the build environment"
        exit 1
    fi

    # Check if PRU software support package exists
    if [ ! -d "$PRU_SUPPORT_DIR" ]; then
        log_error "PRU software support package not found at $PRU_SUPPORT_DIR"
        log_error "Please run --install first to set up the build environment"
        exit 1
    fi

    # Export PRU_CGT environment variable (same as compile_pru_firmware)
    export PRU_CGT="${PRU_CGT_DIR}"
    log "Set PRU_CGT=${PRU_CGT}"

    # Build default PRU firmware
    log "Building default PRU firmware..."
    cd "${INSTALL_DIR}/firmware"

    # Clean previous build (skip if fast rebuild)
    if [ "$do_clean" = "clean" ]; then
        log "Performing thorough cleanup of previous builds..."
        make clean || log_warning "Clean failed (may be first build)"
        rm -rf release/ 2>/dev/null || true
        find . -name "*.pp" -delete 2>/dev/null || true
        find . -name "*.object" -delete 2>/dev/null || true
        find . -name "*.out" -delete 2>/dev/null || true
        find . -name "*.map" -delete 2>/dev/null || true
        log "Build artifacts cleaned"
    else
        log "Fast rebuild - skipping clean step..."
    fi

    # Build PRU firmware
    log "Building PRU0 and PRU1 firmware..."
    make || {
        log_error "Default PRU firmware build failed"
        exit 1
    }

    # Deploy default firmware
    log "Deploying default PRU firmware..."
    if [ -f release/beaglelogic-pru0.out ] && [ -f release/beaglelogic-pru1.out ]; then
        cp -v release/beaglelogic-pru0.out /lib/firmware/beaglelogic-pru0-fw
        cp -v release/beaglelogic-pru1.out /lib/firmware/beaglelogic-pru1-logic
        ln -sfv /lib/firmware/beaglelogic-pru1-logic /lib/firmware/beaglelogic-pru1-fw
        log_success "Default PRU firmware rebuilt and deployed"
    else
        log_error "Default PRU firmware build incomplete (expected .out files)"
        ls -la release/ || true
        exit 1
    fi

    # Build PRUDAQ firmware variants
    if [ -d "${INSTALL_DIR}/firmware/custom/prudaq" ]; then
        log "Building PRUDAQ firmware variants..."
        cd "${INSTALL_DIR}/firmware/custom/prudaq"

        # Cleanup PRUDAQ build
        make clean || true
        rm -rf release/ 2>/dev/null || true

        make || {
            log_warning "PRUDAQ firmware build failed (non-critical)"
            return
        }

        # Deploy PRUDAQ firmware
        log "Deploying PRUDAQ firmware variants..."
        if [ -d release ]; then
            cp -v release/prudaq-ch0.out /lib/firmware/beaglelogic-pru1-prudaq-ch0 2>/dev/null || true
            cp -v release/prudaq-ch1.out /lib/firmware/beaglelogic-pru1-prudaq-ch1 2>/dev/null || true
            cp -v release/prudaq-ch01.out /lib/firmware/beaglelogic-pru1-prudaq-ch01 2>/dev/null || true
            log_success "PRUDAQ firmware variants deployed"
        fi
    fi
}

rebuild_test_apps() {
    local do_clean="${1:-clean}"  # Default to clean if not specified

    log_section "Rebuilding Test Applications"

    if [ -d "${INSTALL_DIR}/testapp" ]; then
        cd "${INSTALL_DIR}/testapp"

        # Clean previous build (skip if fast rebuild)
        if [ "$do_clean" = "clean" ]; then
            log "Cleaning previous test application build..."
            make clean || true
        else
            log "Fast rebuild - skipping clean step..."
        fi

        log "Building test applications..."
        make || {
            log_warning "Test application build failed (non-critical)"
            return
        }

        log_success "Test applications rebuilt"
    else
        log_warning "Test application directory not found, skipping"
    fi
}

rebuild_documentation() {
    if [ "$HAS_DOCS" = false ]; then
        log_warning "Documentation not installed, skipping rebuild"
        return
    fi

    log_section "Rebuilding Documentation"

    cd "${INSTALL_DIR}"

    if command -v mkdocs &> /dev/null; then
        log "Building documentation with MkDocs..."
        log_warning "Note: MkDocs may show warnings about external links - these are informational"
        mkdocs build || {
            log_warning "MkDocs build failed (non-critical)"
            return
        }
        log_success "Documentation rebuilt"
    else
        log_warning "MkDocs not installed, skipping documentation build"
    fi
}

reload_pru_firmware() {
    log "Reloading PRU firmware..."

    # Find PRU remoteproc devices
    # Note: remoteproc numbering can vary, so we check which ones are PRUs
    for rproc in /sys/class/remoteproc/remoteproc*; do
        if [ -d "$rproc" ]; then
            # Check if this is a PRU by looking at the name
            rproc_name=$(cat "$rproc/name" 2>/dev/null || echo "")
            if [[ "$rproc_name" == *"pru"* ]]; then
                rproc_num=$(basename "$rproc")
                log "  Restarting $rproc_num ($rproc_name)..."

                # Stop the PRU
                echo 'stop' > "$rproc/state" 2>/dev/null || true
                sleep 0.5

                # Start the PRU (will load new firmware)
                echo 'start' > "$rproc/state" 2>/dev/null || log_warning "Failed to start $rproc_num"

                # Check state
                state=$(cat "$rproc/state" 2>/dev/null || echo "unknown")
                if [ "$state" = "running" ]; then
                    log_success "  $rproc_num: running"
                else
                    log_warning "  $rproc_num: $state"
                fi
            fi
        fi
    done
}

restart_services() {
    log_section "Restarting Services"

    # Stop services FIRST to prevent conflicts during module reload
    log "Stopping BeagleLogic services..."
    systemctl stop beaglelogic-startup.service 2>/dev/null || true
    [ "$HAS_WEB_SERVER" = true ] && systemctl stop beaglelogic.service 2>/dev/null || true
    [ "$HAS_TCP_SERVER" = true ] && systemctl stop beaglelogic-tcp.service 2>/dev/null || true
    [ "$HAS_DOCS" = true ] && systemctl stop beaglelogic-docs.service 2>/dev/null || true

    # Unload kernel module if loaded
    log "Unloading kernel module (if loaded)..."
    rmmod beaglelogic 2>/dev/null || true

    # Reload PRU firmware if it was rebuilt
    reload_pru_firmware

    # Reload kernel module (needed for services to start properly)
    log "Reloading kernel module..."
    if modprobe beaglelogic; then
        log_success "Kernel module loaded"
        # Give the module time to initialize and create /dev/beaglelogic
        sleep 1
    else
        log_error "Failed to load kernel module - services will fail to start"
    fi

    # Reload systemd
    systemctl daemon-reload

    # Enable and start services (only those that are installed)
    log "Enabling and starting installed services..."

    # Startup service (always present)
    systemctl enable beaglelogic-startup.service || log_warning "Failed to enable beaglelogic-startup.service"
    systemctl start beaglelogic-startup.service || log_warning "Failed to start beaglelogic-startup.service"
    sleep 2  # Give startup service time to complete

    # Web server (conditional)
    if [ "$HAS_WEB_SERVER" = true ]; then
        systemctl enable beaglelogic.service || log_warning "Failed to enable beaglelogic.service"
        systemctl start beaglelogic.service || log_warning "Failed to start beaglelogic.service"
    fi

    # TCP server (conditional)
    if [ "$HAS_TCP_SERVER" = true ]; then
        systemctl enable beaglelogic-tcp.service || log_warning "Failed to enable beaglelogic-tcp.service"
        systemctl start beaglelogic-tcp.service || log_warning "Failed to start beaglelogic-tcp.service"
    fi

    # Documentation server (conditional)
    if [ "$HAS_DOCS" = true ]; then
        systemctl enable beaglelogic-docs.service || log_warning "Failed to enable beaglelogic-docs.service"
        systemctl start beaglelogic-docs.service || log_warning "Failed to start beaglelogic-docs.service"
    fi

    # Check service status
    log "Checking service status..."

    if systemctl is-active --quiet beaglelogic-startup.service; then
        log_success "beaglelogic-startup.service: active"
    else
        log_warning "beaglelogic-startup.service: inactive/failed"
    fi

    if [ "$HAS_WEB_SERVER" = true ]; then
        if systemctl is-active --quiet beaglelogic.service; then
            log_success "beaglelogic.service: active"
        else
            log_warning "beaglelogic.service: inactive/failed"
        fi
    fi

    if [ "$HAS_TCP_SERVER" = true ]; then
        if systemctl is-active --quiet beaglelogic-tcp.service; then
            log_success "beaglelogic-tcp.service: active"
        else
            log_warning "beaglelogic-tcp.service: inactive/failed"
        fi
    fi

    if [ "$HAS_DOCS" = true ]; then
        if systemctl is-active --quiet beaglelogic-docs.service; then
            log_success "beaglelogic-docs.service: active"
        else
            log_warning "beaglelogic-docs.service: inactive/failed"
        fi
    fi

    log_success "Service restart complete"
}

verify_installation() {
    log_section "Verifying Build Deployment"

    KERNEL_VERSION=$(uname -r)
    local has_errors=false

    # Check for duplicate kernel modules
    log "Checking for duplicate kernel modules..."
    local module_count=$(find /lib/modules/${KERNEL_VERSION} -name "beaglelogic.ko" 2>/dev/null | wc -l)
    if [ "$module_count" -eq 1 ]; then
        local module_path=$(find /lib/modules/${KERNEL_VERSION} -name "beaglelogic.ko" 2>/dev/null)
        log_success "✓ Found exactly 1 kernel module: $module_path"
    elif [ "$module_count" -gt 1 ]; then
        log_error "✗ Found $module_count copies of beaglelogic.ko - this will cause conflicts!"
        find /lib/modules/${KERNEL_VERSION} -name "beaglelogic.ko" -exec ls -lh {} \; 2>/dev/null
        echo ""
        log_warning "Fix: Remove old copies with:"
        log "  sudo find /lib/modules/${KERNEL_VERSION} -name 'beaglelogic.ko' -not -path '*/extra/*' -delete && sudo depmod -a"
        has_errors=true
    else
        log_warning "No kernel module found (may not have been built)"
    fi

    # Verify kernel module: built file matches deployed file
    log "Verifying kernel module deployment..."
    if [ -f "${INSTALL_DIR}/kernel/beaglelogic.ko" ] && [ -f "/lib/modules/${KERNEL_VERSION}/extra/beaglelogic.ko" ]; then
        local build_md5=$(md5sum "${INSTALL_DIR}/kernel/beaglelogic.ko" 2>/dev/null | awk '{print $1}')
        local deploy_md5=$(md5sum "/lib/modules/${KERNEL_VERSION}/extra/beaglelogic.ko" 2>/dev/null | awk '{print $1}')
        if [ "$build_md5" = "$deploy_md5" ]; then
            log_success "✓ Deployed module matches built module (MD5: ${build_md5:0:12})"
        else
            log_error "✗ Deployed module differs from built module!"
            log "  Built:    ${build_md5}"
            log "  Deployed: ${deploy_md5}"
            has_errors=true
        fi
    elif [ -f "${INSTALL_DIR}/kernel/beaglelogic.ko" ]; then
        log_error "✗ Module built but not deployed to /extra/"
        has_errors=true
    else
        log_warning "Module not found in build directory (may not have been rebuilt)"
    fi

    # Verify PRU firmware: built files match deployed files
    log "Verifying PRU firmware deployment..."
    for pru in 0 1; do
        local build_file="${INSTALL_DIR}/firmware/release/beaglelogic-pru${pru}.out"
        local deploy_file
        if [ "$pru" = "0" ]; then
            deploy_file="/lib/firmware/beaglelogic-pru0-fw"
        else
            deploy_file="/lib/firmware/beaglelogic-pru1-logic"
        fi

        if [ -f "$build_file" ] && [ -f "$deploy_file" ]; then
            local build_md5=$(md5sum "$build_file" 2>/dev/null | awk '{print $1}')
            local deploy_md5=$(md5sum "$deploy_file" 2>/dev/null | awk '{print $1}')
            if [ "$build_md5" = "$deploy_md5" ]; then
                log_success "✓ PRU${pru} firmware deployed correctly (MD5: ${build_md5:0:12})"
            else
                log_error "✗ PRU${pru} deployed differs from built!"
                has_errors=true
            fi
        elif [ -f "$build_file" ]; then
            log_error "✗ PRU${pru} firmware built but not deployed"
            has_errors=true
        fi
    done

    # Check currently loaded module
    log "Checking loaded module status..."
    if lsmod | grep -q beaglelogic; then
        log_warning "⚠ Old module currently loaded - REBOOT REQUIRED"
        modinfo beaglelogic 2>/dev/null | grep "filename:" || true
    else
        log "Module not loaded (will load on next boot)"
    fi

    # Summary
    echo ""
    echo "========================================================================"
    if [ "$has_errors" = true ]; then
        log_error "VERIFICATION FAILED - Issues found (see above)"
        log "Fix errors before rebooting"
        echo "========================================================================"
        return 1
    else
        log_success "VERIFICATION PASSED - All files deployed correctly"
        log "✓ Built files match deployed files"
        log "✓ Ready for reboot to load new code"
        echo "========================================================================"
        return 0
    fi
}

display_reinstall_summary() {
    log_section "Reinstallation Complete"
    echo ""
    log_success "BeagleLogic has been successfully rebuilt and redeployed"
    echo ""
    log "Available interfaces:"
    log "  - Device node: /dev/beaglelogic"

    # Show only installed components
    if [ "$HAS_WEB_SERVER" = true ]; then
        log "  - Web server: http://$(hostname -I | awk '{print $1}'):4000"
    fi

    if [ "$HAS_TCP_SERVER" = true ]; then
        log "  - TCP server (PulseView): port 5555"
    fi

    if [ "$HAS_DOCS" = true ]; then
        log "  - Documentation: http://$(hostname -I | awk '{print $1}'):8000"
    fi

    echo ""
    log "To test the installation:"
    log "  cd ${INSTALL_DIR}/testapp"
    log "  sudo ./beaglelogic-testapp"
    echo ""
    log_warning "If the kernel module did not load, you may need to:"
    log_warning "  1. Reboot the system, or"
    log_warning "  2. Manually load: sudo modprobe beaglelogic"
    echo ""
}

################################################################################
# CLEAN OPERATION - Cleanup Functions
################################################################################

clean_kernel_module() {
    log_section "Cleaning Kernel Module Build Artifacts"

    cd "${SCRIPT_DIR}/kernel"

    # Remove compiled kernel module files
    log_clean "Removing kernel module build artifacts..."
    rm -fv *.ko *.o *.mod *.mod.c *.mod.o *.cmd .*.cmd
    rm -fv Module.symvers modules.order
    rm -rfv .tmp_versions/

    # Remove device tree overlay artifacts
    log_clean "Removing device tree overlay artifacts..."
    rm -fv beaglelogic-00A0.dtbo beaglelogic-00A0.dtso

    log_success "Kernel module artifacts cleaned"
}

clean_pru_firmware() {
    log_section "Cleaning PRU Firmware Build Artifacts"

    # Clean main PRU firmware
    cd "${SCRIPT_DIR}/firmware"
    log_clean "Cleaning main PRU firmware..."
    if [ -f Makefile ]; then
        make clean 2>/dev/null || true
    fi
    rm -fv *.out *.obj *.asm *.lst *.map
    rm -rfv gen/

    # Clean PRUDAQ firmware
    if [ -d "${SCRIPT_DIR}/firmware/custom/prudaq" ]; then
        cd "${SCRIPT_DIR}/firmware/custom/prudaq"
        log_clean "Cleaning PRUDAQ firmware..."
        if [ -f Makefile ]; then
            make clean 2>/dev/null || true
        fi
        rm -fv *.out *.obj *.asm *.lst *.map
        rm -rfv gen/
    fi

    log_success "PRU firmware artifacts cleaned"
}

clean_test_applications() {
    log_section "Cleaning Test Applications"

    cd "${SCRIPT_DIR}/testapp"

    log_clean "Removing compiled test binaries..."
    if [ -f Makefile ]; then
        make clean 2>/dev/null || true
    fi
    rm -fv beaglelogic-testapp diagnose-mmap
    rm -fv *.o
    rm -fv *.bin

    log_success "Test application cleaned"
}

clean_node_dependencies() {
    log_section "Cleaning Node.js Dependencies"

    # Clean web server dependencies
    if [ -d "${SCRIPT_DIR}/server/node_modules" ]; then
        log_clean "Removing server/node_modules..."
        rm -rf "${SCRIPT_DIR}/server/node_modules"
        log_success "Removed server/node_modules"
    fi

    if [ -f "${SCRIPT_DIR}/server/package-lock.json" ]; then
        log_clean "Removing server/package-lock.json..."
        rm -f "${SCRIPT_DIR}/server/package-lock.json"
    fi

    # Clean TCP server dependencies
    if [ -d "${SCRIPT_DIR}/tcp-server-node/node_modules" ]; then
        log_clean "Removing tcp-server-node/node_modules..."
        rm -rf "${SCRIPT_DIR}/tcp-server-node/node_modules"
        log_success "Removed tcp-server-node/node_modules"
    fi

    if [ -f "${SCRIPT_DIR}/tcp-server-node/package-lock.json" ]; then
        log_clean "Removing tcp-server-node/package-lock.json..."
        rm -f "${SCRIPT_DIR}/tcp-server-node/package-lock.json"
    fi

    log_success "Node.js dependencies cleaned"
}

clean_documentation() {
    log_section "Cleaning Documentation Build Artifacts"

    # Clean MkDocs build directory
    if [ -d "${SCRIPT_DIR}/site" ]; then
        log_clean "Removing MkDocs site/ directory..."
        rm -rf "${SCRIPT_DIR}/site"
        log_success "Removed site/"
    fi

    # Clean Python cache
    log_clean "Removing Python cache files..."
    find "${SCRIPT_DIR}" -type d -name "__pycache__" -exec rm -rf {} + 2>/dev/null || true
    find "${SCRIPT_DIR}" -type f -name "*.pyc" -delete 2>/dev/null || true
    find "${SCRIPT_DIR}" -type f -name "*.pyo" -delete 2>/dev/null || true

    log_success "Documentation artifacts cleaned"
}

clean_temporary_files() {
    log_section "Cleaning Temporary Files"

    cd "${SCRIPT_DIR}"

    # Remove editor backup files
    log_clean "Removing editor backup files..."
    find . -type f -name "*~" -delete 2>/dev/null || true
    find . -type f -name "*.swp" -delete 2>/dev/null || true
    find . -type f -name "*.swo" -delete 2>/dev/null || true
    find . -type f -name ".*.swp" -delete 2>/dev/null || true

    # Remove DS_Store files (macOS)
    find . -type f -name ".DS_Store" -delete 2>/dev/null || true

    # Remove temporary build files
    find . -type f -name "*.tmp" -delete 2>/dev/null || true

    log_success "Temporary files cleaned"
}

clean_logs_and_state() {
    log_section "Cleaning Logs and State Files (Deep Clean)"

    # Note: These are system-level files, only remove local copies
    cd "${SCRIPT_DIR}"

    # Remove local log files if they exist
    if [ -f "install.log" ]; then
        log_clean "Removing install.log..."
        rm -f install.log
    fi

    # Remove any test output files
    log_clean "Removing test output files..."
    find "${SCRIPT_DIR}/testapp" -type f -name "*.bin" -delete 2>/dev/null || true
    find "${SCRIPT_DIR}" -type f -name "test*.bin" -delete 2>/dev/null || true
    find "${SCRIPT_DIR}" -type f -name "capture*.bin" -delete 2>/dev/null || true

    log_success "Logs and state files cleaned"
}

clean_python_venv() {
    log_section "Cleaning Python Virtual Environments (Deep Clean)"

    if [ -d "${SCRIPT_DIR}/venv" ]; then
        log_clean "Removing Python venv..."
        rm -rf "${SCRIPT_DIR}/venv"
        log_success "Removed venv/"
    fi

    if [ -d "${SCRIPT_DIR}/.venv" ]; then
        log_clean "Removing Python .venv..."
        rm -rf "${SCRIPT_DIR}/.venv"
        log_success "Removed .venv/"
    fi
}

clean_git_ignored_files() {
    log_section "Checking for Git-Ignored Files"

    cd "${SCRIPT_DIR}"

    if [ -d ".git" ]; then
        log "Git repository detected. Checking for untracked files..."

        # Show what would be cleaned with git clean
        UNTRACKED=$(git clean -ndx 2>/dev/null || echo "")

        if [ -n "$UNTRACKED" ]; then
            log_warning "The following files are untracked/ignored by git:"
            echo "$UNTRACKED"
            echo ""
            log_warning "To remove these files, run: git clean -fdx"
        else
            log_success "No additional git-ignored files found"
        fi
    fi
}

display_clean_summary() {
    log_section "Cleanup Summary"

    echo ""
    echo "Cleanup completed successfully!"
    echo ""
    echo "Cleaned artifacts:"
    echo "  ✓ Kernel module build files (*.ko, *.o, etc.)"
    echo "  ✓ Device tree overlay files (*.dtbo, *.dtso)"
    echo "  ✓ PRU firmware build files (*.out, *.obj, etc.)"
    echo "  ✓ Test application binaries"
    echo "  ✓ MkDocs build directory (site/)"
    echo "  ✓ Python cache files (__pycache__, *.pyc)"
    echo "  ✓ Temporary files (*~, *.swp, etc.)"

    if [ "$CLEAN_NODE_MODULES" = true ]; then
        echo "  ✓ Node.js dependencies (node_modules/)"
        echo "  ✓ Package lock files (package-lock.json)"
    else
        echo "  - Node.js dependencies (skipped - use --all to clean)"
    fi

    if [ "$DEEP_CLEAN" = true ]; then
        echo "  ✓ Log files and test output"
        echo "  ✓ Python virtual environments"
    else
        echo "  - Logs and Python venvs (skipped - use --deep to clean)"
    fi

    echo ""
    echo "Repository is ready for fresh build!"
    echo ""
    echo "To rebuild:"
    echo "  sudo ./beaglelogic_setup.sh --install"
    echo ""
}

################################################################################
# Main Operations
################################################################################

run_fresh_install() {
    # Initialize log file
    echo "BeagleLogic Fresh Installation - $(date)" > "${LOG_FILE}"

    log_section "BeagleLogic Fresh Installation Script"
    log "Started at: $(date)"
    log "Script directory: ${SCRIPT_DIR}"
    log "Installation directory: ${INSTALL_DIR}"
    log "Default user: ${DEFAULT_USER}"

    # Pre-flight checks
    check_root
    check_platform
    check_debian_version

    # Check and optionally clean old kernels to free disk space
    check_old_kernels

    # Ask user what components to install
    ask_component_selection

    # Check disk space based on selected components
    check_disk_space

    # Get current installation state (for resume after reboot)
    CURRENT_STATE=$(get_install_state)
    log "Current installation state: ${CURRENT_STATE}"

    # Phase 1: Dependencies and Setup (State 0)
    if [ ${CURRENT_STATE} -le 0 ]; then
        install_system_dependencies
        setup_pru_compiler_symlink
        install_pru_software_support
        set_install_state 1
    fi

    # Phase 2: PRU Firmware (State 1)
    if [ ${CURRENT_STATE} -le 1 ]; then
        compile_pru_firmware
        compile_prudaq_firmware
        install_pru_firmware
        set_install_state 2
    fi

    # Phase 3: Kernel Module (State 2)
    if [ ${CURRENT_STATE} -le 2 ]; then
        compile_kernel_module
        install_kernel_module
        configure_module_autoload
        set_install_state 3
    fi

    # Phase 4: Device Tree Overlay (State 3)
    if [ ${CURRENT_STATE} -le 3 ]; then
        compile_device_tree_overlay
        install_device_tree_overlay
        configure_uboot_overlay
        set_install_state 4
    fi

    # Phase 5: Test Applications (State 4)
    if [ ${CURRENT_STATE} -le 4 ]; then
        compile_test_applications
        set_install_state 5
    fi

    # Phase 6: Web Server (State 5)
    if [ ${CURRENT_STATE} -le 5 ]; then
        if [ "${INSTALL_WEB_SERVER}" = "yes" ]; then
            build_web_server
        else
            log_warning "Skipping web server installation (user choice)"
        fi
        set_install_state 6
    fi

    # Phase 7: TCP Server (State 6)
    if [ ${CURRENT_STATE} -le 6 ]; then
        if [ "${INSTALL_TCP_SERVER}" = "yes" ]; then
            install_tcp_server
        else
            log_warning "Skipping TCP server installation (user choice)"
        fi
        set_install_state 7
    fi

    # Phase 8: Documentation (State 7)
    if [ ${CURRENT_STATE} -le 7 ]; then
        if [ "${INSTALL_DOCS}" = "yes" ]; then
            build_documentation
        else
            log_warning "Skipping documentation build (user choice)"
        fi
        set_install_state 8
    fi

    # Phase 9: System Integration (State 8)
    if [ ${CURRENT_STATE} -le 8 ]; then
        create_beaglelogic_group
        install_udev_rules
        install_systemd_services
        set_permissions
        set_install_state 9
    fi

    # Phase 10: Post-Installation (State 9)
    if [ ${CURRENT_STATE} -le 9 ]; then
        create_data_directory
        update_initramfs
        save_install_config
        set_install_state 10
    fi

    # Final ownership fix - ensure everything in INSTALL_DIR is owned by user
    log_section "Final Ownership Verification"
    log "Performing final ownership fix on ${INSTALL_DIR}..."
    chown -R ${DEFAULT_USER}:${DEFAULT_USER} "${INSTALL_DIR}" 2>/dev/null || true

    # Verify final ownership
    ROOT_OWNED_FILES=$(find "${INSTALL_DIR}" -user root 2>/dev/null | wc -l)
    if [ ${ROOT_OWNED_FILES} -gt 0 ]; then
        log_warning "Found ${ROOT_OWNED_FILES} files still owned by root in ${INSTALL_DIR}"
        log_warning "This is usually harmless, but you can fix with:"
        log_warning "  sudo chown -R ${DEFAULT_USER}:${DEFAULT_USER} ${INSTALL_DIR}"
    else
        log_success "All files in ${INSTALL_DIR} are owned by ${DEFAULT_USER}"
    fi

    # Display summary and completion
    display_installation_summary

    log "Installation completed at: $(date)"
    log_success "All installation phases completed successfully"

    # Clear state file if not rebooting
    if ! needs_reboot; then
        clear_install_state
    fi
}

run_reinstall() {
    log_section "BeagleLogic Reinstall Script"
    log "Installation directory: $INSTALL_DIR"
    log "Kernel version: $(uname -r)"
    echo ""

    # Check root
    check_root

    # Check if BeagleLogic is installed
    check_beaglelogic_installed

    # Detect which components are installed
    detect_installed_components

    # Execute rebuild steps with full clean
    rebuild_kernel_module "clean"
    rebuild_device_tree
    rebuild_pru_firmware "clean"
    rebuild_test_apps "clean"
    rebuild_documentation

    # Skip service restart during rebuild - reboot is cleaner and avoids timing issues
    log_section "Rebuild Complete"
    log_success "All components rebuilt successfully"
    echo ""
    log_warning "IMPORTANT: You must REBOOT for changes to take effect"
    log "The new kernel module and PRU firmware will load on next boot"
    echo ""

    verify_installation

    # Final message
    display_reinstall_summary

    echo ""
    echo "========================================================================"
    echo "  REBOOT REQUIRED"
    echo "========================================================================"
    echo ""
    echo "  Run: sudo reboot"
    echo ""
    echo "  After reboot, test with: cd ${INSTALL_DIR}/testapp && sudo ./beaglelogic-testapp"
    echo ""
}

run_rebuild_fast() {
    log_section "BeagleLogic Fast Rebuild (Incremental)"
    log "Installation directory: $INSTALL_DIR"
    log "Kernel version: $(uname -r)"
    log "Mode: Fast rebuild (no clean - only changed files will be rebuilt)"
    echo ""

    # Check root
    check_root

    # Check if BeagleLogic is installed
    check_beaglelogic_installed

    # Detect which components are installed
    detect_installed_components

    # Execute rebuild steps WITHOUT clean (incremental build)
    rebuild_kernel_module "fast"
    rebuild_device_tree
    rebuild_pru_firmware "fast"
    rebuild_test_apps "fast"
    rebuild_documentation

    # Skip service restart during rebuild - reboot is cleaner and avoids timing issues
    log_section "Fast Rebuild Complete"
    log_success "All components rebuilt successfully (incremental)"
    echo ""
    log_warning "IMPORTANT: You must REBOOT for changes to take effect"
    log "The new kernel module and PRU firmware will load on next boot"
    echo ""

    verify_installation

    # Final message
    display_reinstall_summary

    echo ""
    echo "========================================================================"
    echo "  REBOOT REQUIRED"
    echo "========================================================================"
    echo ""
    echo "  Run: sudo reboot"
    echo ""
    echo "  After reboot, test with: cd ${INSTALL_DIR}/testapp && sudo ./beaglelogic-testapp"
    echo ""
    echo "  Note: Fast rebuild skips 'make clean' for faster iteration."
    echo "  If you experience build issues, use --reinstall for a full clean rebuild."
    echo ""
}

run_clean() {
    log_section "BeagleLogic Clean Script"
    log "Working directory: ${SCRIPT_DIR}"

    if [ "$CLEAN_NODE_MODULES" = true ]; then
        log "Mode: FULL CLEAN (including node_modules)"
    elif [ "$DEEP_CLEAN" = true ]; then
        log "Mode: DEEP CLEAN (including logs and venvs)"
    else
        log "Mode: BUILD ARTIFACTS ONLY"
        log_warning "Use --all to clean node_modules, or --deep for complete clean"
    fi

    echo ""
    read -p "Continue with cleanup? (y/N) " -n 1 -r
    echo

    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        log_warning "Cleanup cancelled by user"
        exit 0
    fi

    # Always clean these
    clean_kernel_module
    clean_pru_firmware
    clean_test_applications
    clean_documentation
    clean_temporary_files

    # Optional cleanups
    if [ "$CLEAN_NODE_MODULES" = true ]; then
        clean_node_dependencies
    fi

    if [ "$DEEP_CLEAN" = true ]; then
        clean_logs_and_state
        clean_python_venv
    fi

    # Check for git-ignored files
    clean_git_ignored_files

    # Display summary
    display_clean_summary
}

################################################################################
# Main Entry Point
################################################################################

main() {
    # Show menu if no operation specified
    if [ -z "$OPERATION" ]; then
        show_menu
    fi

    # Execute selected operation
    case $OPERATION in
        install)
            run_fresh_install
            ;;
        reinstall)
            run_reinstall
            ;;
        rebuild_fast)
            run_rebuild_fast
            ;;
        clean)
            run_clean
            ;;
        status)
            check_installation_status
            ;;
        configure)
            configure_firmware
            ;;
        *)
            echo "No operation specified"
            show_help
            ;;
    esac
}

# Run main
main "$@"
