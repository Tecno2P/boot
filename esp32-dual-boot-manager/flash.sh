#!/usr/bin/env bash
# =============================================================
#  ESP32 Dual Boot Manager - Linux/macOS Flash Script
# =============================================================
set -euo pipefail

# ── Color helpers ─────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
RESET='\033[0m'

info()    { echo -e "${CYAN}[INFO]${RESET}  $*"; }
success() { echo -e "${GREEN}[OK]${RESET}    $*"; }
warn()    { echo -e "${YELLOW}[WARN]${RESET}  $*"; }
error()   { echo -e "${RED}[ERROR]${RESET} $*" >&2; }
die()     { error "$*"; exit 1; }

echo ""
echo -e "${BOLD}============================================================${RESET}"
echo -e "${BOLD}  ESP32 Dual Boot Manager - Flash Script${RESET}"
echo -e "${BOLD}============================================================${RESET}"
echo ""

# ── Check esptool ─────────────────────────────────────────────
ESPTOOL=""
if command -v esptool.py &>/dev/null; then
    ESPTOOL="esptool.py"
elif command -v esptool &>/dev/null; then
    ESPTOOL="esptool"
else
    die "esptool.py not found. Install with: pip install esptool"
fi
success "Found esptool: $ESPTOOL"

# ── Auto-detect serial port ───────────────────────────────────
PORT=""

detect_port() {
    local candidates=(
        /dev/ttyUSB0
        /dev/ttyUSB1
        /dev/ttyACM0
        /dev/ttyACM1
        /dev/cu.usbserial-*
        /dev/cu.SLAB_USBtoUART
        /dev/cu.usbmodem*
    )

    for p in "${candidates[@]}"; do
        # Expand globs
        for expanded in $p; do
            if [ -e "$expanded" ]; then
                PORT="$expanded"
                return 0
            fi
        done
    done
    return 1
}

echo "Detecting serial port..."
if detect_port; then
    success "Auto-detected port: $PORT"
else
    warn "No port auto-detected."
    echo "Available ports:"
    ls /dev/ttyUSB* /dev/ttyACM* /dev/cu.* 2>/dev/null || echo "  (none found)"
    echo ""
    read -rp "Enter port manually (e.g. /dev/ttyUSB0): " PORT
    [ -z "$PORT" ] && die "No port specified."
fi

[ -e "$PORT" ] || die "Port $PORT does not exist. Is the device connected?"

# ── Locate binary files ───────────────────────────────────────
find_binary() {
    local pattern="$1"
    find . -name "$pattern" -type f 2>/dev/null | head -1
}

FIRMWARE_BIN=$(find_binary "dual_boot_manager.ino.bin")
BOOTLOADER=$(find_binary "bootloader.bin")
PARTITIONS=$(find_binary "partitions.bin")
OTADATA=$(find_binary "boot_app0.bin")
SPIFFS_BIN=$(find_binary "spiffs.bin")

echo ""
echo "Binary files found:"
[ -n "$FIRMWARE_BIN" ] && success "Firmware:    $FIRMWARE_BIN" || warn "Firmware binary not found (export from Arduino IDE first)"
[ -n "$BOOTLOADER"   ] && success "Bootloader:  $BOOTLOADER"   || warn "Bootloader not found"
[ -n "$PARTITIONS"   ] && success "Partitions:  $PARTITIONS"   || warn "Partitions binary not found"
[ -n "$OTADATA"      ] && success "OTA Data:    $OTADATA"       || warn "OTA data binary not found"
[ -n "$SPIFFS_BIN"   ] && success "SPIFFS:      $SPIFFS_BIN"    || warn "SPIFFS binary not found"

echo ""
echo -e "Flash target: ${BOLD}$PORT${RESET}  |  Baud: ${BOLD}921600${RESET}  |  Chip: ${BOLD}esp32${RESET}"
echo ""

# ── Confirm ───────────────────────────────────────────────────
echo "This will flash to $PORT:"
echo "  0x1000   Bootloader"
echo "  0x8000   Partition Table"
echo "  0xe000   OTA Data"
echo "  0x10000  Firmware (app0)"
echo "  0x2A0000 SPIFFS (if present)"
echo ""
read -rp "Proceed? (y/N): " CONFIRM
[[ "${CONFIRM,,}" == "y" ]] || { echo "Cancelled."; exit 0; }

# ── Build flash args ──────────────────────────────────────────
FLASH_ARGS=(
    --chip esp32
    --port "$PORT"
    --baud 921600
    --before default_reset
    --after hard_reset
    write_flash
    -z
    --flash_mode dio
    --flash_freq 80m
    --flash_size 4MB
)

if [ -n "$BOOTLOADER" ]; then
    FLASH_ARGS+=(0x1000 "$BOOTLOADER")
else
    warn "Skipping bootloader (not found)."
fi

if [ -n "$PARTITIONS" ]; then
    FLASH_ARGS+=(0x8000 "$PARTITIONS")
else
    warn "Skipping partition table (not found)."
fi

if [ -n "$OTADATA" ]; then
    FLASH_ARGS+=(0xe000 "$OTADATA")
else
    warn "Skipping OTA data (not found)."
fi

if [ -n "$FIRMWARE_BIN" ]; then
    FLASH_ARGS+=(0x10000 "$FIRMWARE_BIN")
else
    warn "Skipping firmware (not found)."
fi

if [ -n "$SPIFFS_BIN" ]; then
    FLASH_ARGS+=(0x2A0000 "$SPIFFS_BIN")
else
    warn "Skipping SPIFFS (not found)."
    echo "        To generate SPIFFS: use 'mkspiffs' or Arduino ESP32 Sketch Data Upload tool"
fi

# ── Flash ─────────────────────────────────────────────────────
echo ""
echo "Running: $ESPTOOL ${FLASH_ARGS[*]}"
echo ""

if "$ESPTOOL" "${FLASH_ARGS[@]}"; then
    echo ""
    echo -e "${GREEN}${BOLD}============================================================${RESET}"
    echo -e "${GREEN}${BOLD}  Flash complete! Device is rebooting...${RESET}"
    echo -e "${GREEN}  Connect to WiFi: ESP32-DualBoot${RESET}"
    echo -e "${GREEN}  Password:        admin1234${RESET}"
    echo -e "${GREEN}  Web GUI:         http://192.168.4.1${RESET}"
    echo -e "${GREEN}${BOLD}============================================================${RESET}"
    echo ""
else
    echo ""
    die "Flash failed! Troubleshooting:
  - Hold BOOT button while pressing RESET to enter download mode
  - Check USB cable (data cable, not charge-only)
  - Try lower baud rate: edit BAUD=460800 in this script
  - Update esptool: pip install --upgrade esptool
  - Check permissions: sudo usermod -aG dialout \$USER (then logout/login)"
fi
