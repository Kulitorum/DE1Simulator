#!/bin/bash
#
# DE1 BLE Daemon - Raspberry Pi Setup Script
#
# This script installs dependencies, builds the daemon, and configures
# it to start automatically on boot.
#
# Usage: curl -sL <url>/setup-pi.sh | sudo bash
#    or: sudo ./setup-pi.sh
#

set -e

echo "========================================"
echo "DE1 BLE Daemon - Raspberry Pi Setup"
echo "========================================"
echo

# Check if running as root
if [ "$EUID" -ne 0 ]; then
    echo "Please run as root (sudo)"
    exit 1
fi

# Detect architecture
ARCH=$(uname -m)
echo "Detected architecture: $ARCH"

# Install dependencies
echo
echo "[1/6] Installing dependencies..."
apt update
apt install -y qt6-base-dev qt6-connectivity-dev bluez

# Configure BlueZ for BLE peripheral mode
echo
echo "[2/6] Configuring Bluetooth for BLE peripheral mode..."

# Find the bluetooth service file (location varies by distro)
BLUETOOTH_SERVICE=""
for f in /lib/systemd/system/bluetooth.service /usr/lib/systemd/system/bluetooth.service; do
    [ -f "$f" ] && BLUETOOTH_SERVICE="$f" && break
done

if [ -n "$BLUETOOTH_SERVICE" ]; then
    if ! grep -q -- "--experimental" "$BLUETOOTH_SERVICE"; then
        echo "Enabling BlueZ experimental mode..."
        # Append --experimental to existing ExecStart line (preserves correct path)
        sed -i '/^ExecStart=.*bluetoothd/ s/$/ --experimental/' "$BLUETOOTH_SERVICE"
    else
        echo "BlueZ experimental mode already enabled"
    fi
else
    echo "WARNING: Bluetooth service file not found"
fi

# Ensure Bluetooth starts on boot and is running
systemctl daemon-reload
systemctl enable bluetooth
systemctl restart bluetooth

# Wait for Bluetooth to be ready
echo "Waiting for Bluetooth adapter..."
sleep 3

# Configure the Bluetooth adapter for BLE peripheral mode
echo "Configuring Bluetooth adapter..."
rfkill unblock bluetooth 2>/dev/null || true
# Use timeouts to prevent hanging if Bluetooth is in a bad state
timeout 5 btmgmt power on || echo "Warning: btmgmt power on timed out"
timeout 5 btmgmt le on || echo "Warning: btmgmt le on timed out"
timeout 5 btmgmt advertising on || echo "Warning: btmgmt advertising on timed out"
timeout 3 btmgmt name 'DE1-SIM' || echo "Warning: btmgmt name timed out"
hciconfig hci0 up 2>/dev/null || true
hciconfig hci0 piscan 2>/dev/null || true  # Enable discoverable mode
sleep 1

# Verify adapter status
echo "Checking adapter status..."
btmgmt info | head -10

# Stop existing service if running
echo
echo "[3/6] Stopping existing service (if any)..."
systemctl stop de1-ble-daemon 2>/dev/null || true

# Check if source files exist in current directory
if [ -f "de1-ble-daemon.cpp" ]; then
    SRC_DIR="."
elif [ -f "/tmp/de1-daemon/de1-ble-daemon.cpp" ]; then
    SRC_DIR="/tmp/de1-daemon"
else
    echo "ERROR: Source files not found!"
    echo "Please copy de1-ble-daemon.cpp and de1-ble-daemon.pro to this directory"
    exit 1
fi

# Build the daemon
echo
echo "[4/6] Building daemon..."
cd "$SRC_DIR"
qmake6 de1-ble-daemon.pro
make clean 2>/dev/null || true
make -j$(nproc)

# Install binary
echo
echo "[5/6] Installing binary..."
cp de1-ble-daemon /usr/local/bin/
chmod +x /usr/local/bin/de1-ble-daemon

# Install and enable systemd service
echo
echo "[6/6] Configuring systemd service..."
if [ -f "de1-ble-daemon.service" ]; then
    cp de1-ble-daemon.service /etc/systemd/system/
elif [ -f "/tmp/de1-daemon/de1-ble-daemon.service" ]; then
    cp /tmp/de1-daemon/de1-ble-daemon.service /etc/systemd/system/
else
    # Create service file inline
    cat > /etc/systemd/system/de1-ble-daemon.service << 'EOF'
[Unit]
Description=DE1 BLE Simulator Daemon
After=bluetooth.target network-online.target
Wants=bluetooth.target

[Service]
Type=simple
TimeoutStartSec=30
ExecStartPre=/bin/sleep 2
ExecStartPre=/bin/bash -c "timeout 5 btmgmt power on || true; timeout 5 btmgmt le on || true; timeout 5 btmgmt advertising on || true; timeout 3 btmgmt name DE1-SIM || true; hciconfig hci0 piscan || true"
ExecStart=/usr/local/bin/de1-ble-daemon
Restart=on-failure
RestartSec=5

[Install]
WantedBy=multi-user.target
EOF
fi

systemctl daemon-reload
systemctl enable de1-ble-daemon
systemctl start de1-ble-daemon

# Check status
echo
echo "========================================"
echo "Setup complete!"
echo "========================================"
echo
echo "Service status:"
systemctl status de1-ble-daemon --no-pager || true
echo
echo "The daemon is now running and will start automatically on boot."
echo "Connect from the Windows DE1 Simulator app to control it."
echo
echo "Useful commands:"
echo "  systemctl status de1-ble-daemon  - Check status"
echo "  systemctl restart de1-ble-daemon - Restart daemon"
echo "  journalctl -u de1-ble-daemon -f  - View logs"
echo
echo "Troubleshooting:"
echo "  If BLE stops working, check for UART errors:"
echo "    dmesg | grep -i 'bluetooth.*fail'"
echo "  If you see 'Frame reassembly failed', reboot the Pi:"
echo "    sudo reboot"
echo
