# DE1 BLE Simulator

This does not work yet. I decided to postprone the project.

A Bluetooth Low Energy simulator for the Decent Espresso DE1 machine. Test your DE1 apps without needing real hardware.

## Overview

This simulator emulates the DE1's BLE GATT server, allowing apps like [Decenza](https://github.com/your-repo/decenza) to connect and interact as if talking to a real machine. Perfect for development, testing, and demos.

## Architecture

```
┌─────────────────────────────────────┐        TCP        ┌─────────────────────────┐
│         Windows GUI App             │◄─────────────────►│     Raspberry Pi        │
│                                     │    Port 12345     │                         │
│  ┌───────────────────────────────┐  │                   │  ┌───────────────────┐  │
│  │ Simulation Engine             │  │                   │  │ BLE Daemon        │  │
│  │ • State machine               │  │   Commands ──────►│  │ • Advertises as   │  │
│  │ • Shot simulation             │  │                   │  │   "DE1-SIM"       │  │
│  │ • Profile handling            │  │◄────── Events ────│  │ • GATT server     │  │
│  └───────────────────────────────┘  │                   │  │ • BlueZ backend   │  │
│                                     │                   │  └───────────────────┘  │
│  ┌───────────────────────────────┐  │                   │                         │
│  │ GUI                           │  │                   │  Auto-starts on boot    │
│  │ • GHC buttons                 │  │                   │  Minimal footprint      │
│  │ • Live values display         │  │                   │  ~200 lines of code     │
│  │ • BLE log viewer              │  │                   │                         │
│  │ • Profile viewer              │  │                   └─────────────────────────┘
│  │ • Pi setup wizard             │  │                            ▲
│  └───────────────────────────────┘  │                            │
│                                     │                            │ BLE
└─────────────────────────────────────┘                            ▼
                                                          ┌─────────────────────────┐
                                                          │     Your DE1 App        │
                                                          │   (Decenza, etc.)       │
                                                          └─────────────────────────┘
```

**Why this architecture?**
- Windows lacks proper BLE peripheral support in Qt
- Raspberry Pi + BlueZ = rock-solid BLE peripheral
- All the nice GUI stays on your dev machine
- One-click Pi setup from the Windows app

## Features

- **Full DE1 BLE Protocol**: All characteristics, MMR registers, state machine
- **Shot Simulation**: Realistic pressure/flow/temperature curves
- **GHC Emulation**: Simulates Group Head Controller buttons
- **Profile Upload**: Receive and display profiles from your app
- **Real-time Logging**: See all BLE traffic for debugging
- **Easy Pi Setup**: Built-in wizard configures Raspberry Pi automatically

## Requirements

### Windows (GUI App)
- Windows 10/11
- Qt 6.8+ (for building from source)
- Pre-built releases available

### Raspberry Pi (BLE Daemon)
- Raspberry Pi 3/4/5 (or any Pi with Bluetooth)
- Raspberry Pi OS (64-bit Lite recommended)
- Network connection (Ethernet or WiFi)

## Quick Start

### 1. Prepare Your Raspberry Pi

1. Download and install [Raspberry Pi Imager](https://www.raspberrypi.com/software/)
2. Insert your SD card and open the Imager
3. Choose **Raspberry Pi OS Lite (64-bit)** as the operating system
4. **IMPORTANT**: Click the **gear icon** (⚙️) to open OS customization settings:
   - **Set hostname**: `DE1-Simulator` (this enables `DE1-Simulator.local` access)
   - **Enable SSH**: Check "Enable SSH" and select "Use password authentication"
   - **Set username and password**: e.g., `pi` / `your-password`
   - **Configure WiFi** (optional): Enter your network credentials if not using Ethernet
5. Click **Save**, then **Write** to flash the image
6. Insert SD card into Pi, connect to network (Ethernet recommended), and power on
7. Wait ~60 seconds for first boot to complete

**Note**: SSH is disabled by default on fresh Raspberry Pi OS images. You MUST enable it in the Imager settings before flashing, or the setup wizard won't be able to connect.

### 2. Run the Windows App & Setup Pi

1. Download the latest release or build from source
2. Launch `DE1Simulator.exe`
3. On first run, the app checks if the Pi daemon is reachable
4. If not found, you'll be prompted to run the setup wizard
5. In the setup dialog:
   - **Step 1**: A terminal window opens - enter your Pi password to copy files
   - **Step 2**: Another terminal opens - enter password for installation
6. Wait for installation to complete (~3-5 minutes)

The wizard automatically:
- Copies the embedded daemon source code to your Pi
- Installs Qt6 and Bluetooth dependencies
- Builds the daemon
- Sets up a systemd service to auto-start on boot

**Note**: Each step opens a separate terminal window for password entry.

### 3. Connect and Use

1. In the main window, enter your Pi's IP address
2. Click **Connect**
3. Status should show "Connected to Pi - Advertising as DE1-SIM"

### 4. Test with Your App

1. Open your DE1 app (e.g., Decenza)
2. Scan for Bluetooth devices
3. Connect to "DE1-SIM"
4. The simulator will respond to all BLE commands

## Usage

### GHC Buttons

The five buttons simulate the Group Head Controller:

| Button | Action |
|--------|--------|
| **Power** | Toggle between Sleep and Idle |
| **Espresso** | Start espresso extraction (~30s simulated shot) |
| **Steam** | Start steaming (45s cycle) |
| **Hot Water** | Dispense hot water |
| **Flush** | Run flush/rinse cycle (10s) |

### GHC Status Dropdown

Controls whether your app can start operations:

| Value | Meaning | App Can Start? |
|-------|---------|----------------|
| 0 | Not installed (headless) | Yes |
| 1 | Present but unused | Yes |
| 2 | Installed but inactive | Yes |
| 3 | **Present and active** | **No** (use GHC buttons) |
| 4 | Debug mode | Yes |

Set to **3** to force your app to use the physical (simulated) buttons.

### Tabs

- **Main**: GHC buttons, state display, live values
- **BLE Log**: Real-time log of all BLE traffic
- **Profile**: View uploaded shot profiles

## Building from Source

### Windows

```bash
# Prerequisites: Qt 6.8+, CMake, Visual Studio 2022

cd DE1Simulator
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Or open `CMakeLists.txt` in Qt Creator and build with the MSVC kit.

The Pi daemon source code is embedded in the Windows app - no separate build needed.

## Protocol Reference

### TCP Protocol (Windows ↔ Pi)

The Windows app and Pi daemon communicate over TCP port 12345 using a simple JSON-based protocol.

**Commands (Windows → Pi):**
```json
{"cmd": "start"}                    // Start advertising
{"cmd": "stop"}                     // Stop advertising
{"cmd": "notify", "char": "A00E", "data": "0200"}  // Send notification
{"cmd": "respond", "char": "A005", "data": "..."}  // Respond to read
```

**Events (Pi → Windows):**
```json
{"event": "connected", "client": "XX:XX:XX:XX:XX:XX"}
{"event": "disconnected"}
{"event": "write", "char": "A002", "data": "02"}   // Char written
{"event": "read", "char": "A00E"}                   // Char read request
{"event": "subscribe", "char": "A00D"}              // Client subscribed
```

### BLE Characteristics

| UUID | Name | Properties | Description |
|------|------|------------|-------------|
| `A001` | VERSION | Read | Firmware version |
| `A002` | REQUESTED_STATE | Write | App requests state change |
| `A005` | READ_FROM_MMR | Read, Notify | Memory register reads |
| `A006` | WRITE_TO_MMR | Write | Memory register writes |
| `A00B` | SHOT_SETTINGS | Read, Write | Shot/steam settings |
| `A00D` | SHOT_SAMPLE | Notify | Real-time shot data (5Hz) |
| `A00E` | STATE_INFO | Read, Notify | Machine state |
| `A00F` | HEADER_WRITE | Write | Profile header |
| `A010` | FRAME_WRITE | Write | Profile frames |
| `A011` | WATER_LEVELS | Read, Notify | Water tank level |

All UUIDs use the base: `0000XXXX-0000-1000-8000-00805F9B34FB`

### MMR Registers

| Address | Name | Size | Description |
|---------|------|------|-------------|
| `0x80381C` | GHC_INFO | 1 | GHC status (0-4) |
| `0x803854` | USB_CHARGER | 1 | USB charger state |
| `0x80000C` | MACHINE_MODEL | 1 | Machine model (2=DE1Plus) |

### Data Encoding

Shot samples use big-endian encoding with fixed-point values:

| Field | Bytes | Encoding |
|-------|-------|----------|
| SampleTime | 0-1 | uint16 BE, ÷100 for seconds |
| GroupPressure | 2-3 | uint16 BE, ÷4096 for bar |
| GroupFlow | 4-5 | uint16 BE, ÷4096 for mL/s |
| MixTemp | 6-7 | uint16 BE, ÷256 for °C |
| HeadTemp | 8-10 | uint24 BE, ÷65536 for °C |
| SetMixTemp | 11-12 | uint16 BE, ÷256 for °C |
| SetHeadTemp | 13-14 | uint16 BE, ÷256 for °C |
| SetGroupPressure | 15 | uint8, ÷16 for bar |
| SetGroupFlow | 16 | uint8, ÷16 for mL/s |
| FrameNumber | 17 | uint8 |
| SteamTemp | 18 | uint8 |

## Troubleshooting

### Pi not found on network / hostname not resolving
- `DE1-Simulator.local` uses mDNS which may not work on all Windows setups
- Use the Pi's IP address directly instead (e.g., `192.168.1.xxx`)
- Find the IP via your router's DHCP client list, or run `hostname -I` on the Pi
- Ensure Pi is connected via Ethernet or WiFi

### BLE not advertising / Device not found
The setup script configures BlueZ automatically, but if scanning fails:

```bash
# Check if Bluetooth is blocked
rfkill list bluetooth
# If "Soft blocked: yes", unblock it:
sudo rfkill unblock bluetooth

# Check if experimental mode is enabled
grep ExecStart /lib/systemd/system/bluetooth.service
# Should show: --experimental flag

# Check adapter has advertising enabled
sudo btmgmt info
# current settings should include: powered, le, advertising

# If advertising is missing, enable it:
sudo rfkill unblock bluetooth
sudo btmgmt power on
sudo btmgmt le on
sudo btmgmt advertising on
sudo hciconfig hci0 up
sudo systemctl restart de1-ble-daemon

# Check daemon logs for errors
sudo journalctl -u de1-ble-daemon -n 30
```

### App connects but no data
- Check the BLE Log tab in the simulator
- Ensure the Pi daemon is connected (status shows "Connected to Pi")
- Verify your app is subscribing to notifications

### Permission denied on Pi
- The BLE daemon needs root for peripheral mode
- Run with `sudo` or set up capabilities:
  ```bash
  sudo setcap 'cap_net_raw,cap_net_admin=eip' ./de1-ble-daemon
  ```

## Contributing

Contributions welcome! Please:
1. Fork the repository
2. Create a feature branch
3. Submit a pull request

## License

MIT License - See [LICENSE](LICENSE) for details.

## Acknowledgments

- [Decent Espresso](https://decentespresso.com/) for the amazing machines
- The Decent community for protocol documentation
- Qt Project for the excellent Bluetooth stack
