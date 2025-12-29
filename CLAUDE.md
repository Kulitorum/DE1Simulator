# DE1 BLE Simulator - Developer Notes

This file contains context for AI assistants working on this project.

## Current Status (Dec 2024)

- **Windows GUI**: Complete and working, connects to Pi via TCP
- **Pi Daemon**: Source embedded in Windows app, deployed via setup wizard
- **Setup Wizard**: Tested and working - opens terminal windows for SSH password entry
  - Step 1: Copies daemon source files to Pi via SCP ✅
  - Step 2: Runs installation script (installs Qt6, builds daemon, configures systemd) ✅
- **Auto-check**: App checks Pi connection on startup, prompts setup if needed
- **BLE Status**: Working! Decenza finds "DE1-SIM" and can connect ✅

### Recent Fixes (Dec 29, 2024)
- Added `rfkill unblock bluetooth` to setup scripts (fixes soft-blocked adapter)
- Added `hciconfig hci0 piscan` to enable discoverable mode (required for BLE scanning to find device)
- Added `btmgmt name 'DE1-SIM'` to set short adapter name (BLE names truncated to ~10 chars)
- Pi IP may change after reboot - use `DE1-Simulator.local` (mDNS) or check router DHCP
- **Added service UUID to advertising data** - helps apps filter/find the device
- **Fixed UART corruption issue** - Pi reboot required when `Frame reassembly failed` errors occur
- **Qt BLE advertising doesn't work on Pi 3** - Qt's `startAdvertising()` logs success but doesn't create BlueZ advertising instances. Fixed by using direct HCI commands in systemd ExecStartPost:
  ```bash
  hcitool -i hci0 cmd 0x08 0x000A 00  # Disable first
  hcitool -i hci0 cmd 0x08 0x0008 ...  # Set advertising data (Flags + UUID + Name)
  hcitool -i hci0 cmd 0x08 0x000A 01  # Enable advertising
  ```
- **Daemon now advertises on startup** - doesn't wait for Windows GUI connection
- Windows sees device as "DE1-Simula" (truncated from hostname "DE1-Simulator")

## Architecture Overview

This is a **split-architecture** BLE simulator for the Decent Espresso DE1 machine:

```
┌─────────────────────────────────────┐        TCP        ┌─────────────────────────┐
│         Windows GUI App             │◄─────────────────►│     Raspberry Pi        │
│         (main.cpp)                  │    Port 12345     │   (de1-ble-daemon.cpp)  │
│                                     │                   │                         │
│  - All simulation logic             │   Commands ──────►│  - BLE peripheral       │
│  - Profile handling                 │                   │  - Advertises "DE1-SIM" │
│  - State machine                    │◄────── Events ────│  - GATT server          │
│  - GUI controls                     │                   │  - BlueZ backend        │
└─────────────────────────────────────┘                   └─────────────────────────┘
                                                                    ▲
                                                                    │ BLE
                                                                    ▼
                                                          ┌─────────────────────────┐
                                                          │     DE1 App (Decenza)   │
                                                          └─────────────────────────┘
```

**Why this architecture?**
- Qt BLE peripheral mode is NOT implemented on Windows (tried, doesn't work)
- Raspberry Pi + BlueZ = rock-solid BLE peripheral support
- Windows app handles all the UI and simulation logic
- Pi daemon is minimal (~350 lines) - just BLE forwarding

## File Structure

```
DE1Simulator/
├── CMakeLists.txt              # Windows Qt project (Qt6::Network)
├── main.cpp                    # Windows GUI application (~1350 lines)
├── README.md                   # User documentation for GitHub
├── CLAUDE.md                   # This file (AI context)
├── DE1_SIMULATOR_PROMPT.md     # Original requirements
└── pi-daemon/
    ├── de1-ble-daemon.cpp      # Pi BLE daemon (~350 lines)
    ├── de1-ble-daemon.pro      # qmake project file for Pi
    ├── de1-ble-daemon.service  # systemd service file
    └── setup-pi.sh             # Installation script for Pi
```

## Building

### Windows GUI
```bash
cd DE1Simulator
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release
```

Uses `windeployqt` automatically to copy Qt DLLs.

### Pi Daemon
```bash
# On Raspberry Pi
cd ~/de1-daemon
qmake6
make
sudo ./de1-ble-daemon
```

Or use setup-pi.sh for full installation with systemd service.

## TCP Protocol (Windows ↔ Pi)

JSON over TCP, newline-delimited.

### Commands (Windows → Pi)
```json
{"cmd": "start"}                           // Start BLE advertising
{"cmd": "stop"}                            // Stop advertising
{"cmd": "notify", "char": "A00E", "data": "0200"}  // Send BLE notification
{"cmd": "update", "char": "A00E", "data": "0200"}  // Update characteristic value
```

### Events (Pi → Windows)
```json
{"event": "ready", "version": "1.0.0"}     // Daemon ready
{"event": "advertising"}                    // BLE advertising started
{"event": "connected", "client": "XX:XX:XX:XX:XX:XX"}  // BLE client connected
{"event": "disconnected"}                   // BLE client disconnected
{"event": "write", "char": "A002", "data": "02"}  // Char write from app
{"event": "read", "char": "A00E"}          // Char read from app
{"event": "error", "code": 1}              // BLE error
```

## DE1 BLE Protocol Summary

### Service UUID
`0000A000-0000-1000-8000-00805F9B34FB`

### Key Characteristics
| Short UUID | Name | Direction | Description |
|------------|------|-----------|-------------|
| A001 | VERSION | Read | Firmware version |
| A002 | REQUESTED_STATE | Write (from app) | App requests state change |
| A005 | READ_FROM_MMR | R/W/Notify | MMR register reads |
| A006 | WRITE_TO_MMR | Write (from app) | MMR register writes |
| A00B | SHOT_SETTINGS | R/W | Shot settings |
| A00D | SHOT_SAMPLE | Notify | Real-time shot data @ 5Hz |
| A00E | STATE_INFO | R/Notify | Machine state |
| A00F | HEADER_WRITE | Write (from app) | Profile header |
| A010 | FRAME_WRITE | Write (from app) | Profile frames |
| A011 | WATER_LEVELS | R/Notify | Water tank level |

### States
- 0x00: Sleep
- 0x02: Idle
- 0x04: Espresso
- 0x05: Steam
- 0x06: HotWater
- 0x0F: HotWaterRinse (Flush)

### SubStates
- 0: Ready
- 1: Heating
- 4: Preinfusion
- 5: Pouring
- 6: Ending
- 7: Steaming

### Key MMR Registers
- 0x80381C: GHC_INFO (GHC status 0-4)
- 0x80000C: MACHINE_MODEL (2 = DE1Plus)
- 0x803854: USB_CHARGER (1 = on)

### GHC Status Values
- 0 = Not installed (app CAN start operations)
- 1 = Present but unused (app CAN start)
- 2 = Installed but inactive (app CAN start)
- 3 = Present and active (app CANNOT start - must use GHC buttons)
- 4 = Debug mode (app CAN start)

## Binary Encoding (BinaryCodec)

- **U8P4**: value * 16 (pressure/flow in 1 byte)
- **U8P1**: value * 2 (temperature with 0.5° precision)
- **U16P8**: value * 256 (temperature, big-endian)
- **U16P12**: value * 4096 (high-precision pressure/flow)
- **U24P16**: value * 65536 (3-byte head temperature)
- **F8_1_7**: Frame duration (if bit7 set: 1s precision, else 0.1s)

### Shot Sample (19 bytes)
| Bytes | Field | Encoding |
|-------|-------|----------|
| 0-1 | SampleTime | uint16 BE, ÷100 for seconds |
| 2-3 | GroupPressure | uint16 BE, ÷4096 for bar |
| 4-5 | GroupFlow | uint16 BE, ÷4096 for mL/s |
| 6-7 | MixTemp | uint16 BE, ÷256 for °C |
| 8-10 | HeadTemp | uint24 BE, ÷65536 for °C |
| 11-12 | SetMixTemp | uint16 BE, ÷256 |
| 13-14 | SetHeadTemp | uint16 BE, ÷256 |
| 15 | SetGroupPressure | uint8, ÷16 |
| 16 | SetGroupFlow | uint8, ÷16 |
| 17 | FrameNumber | uint8 |
| 18 | SteamTemp | uint8 |

### Profile Header (5 bytes)
```
Byte 0: HeaderV (always 1)
Byte 1: NumberOfFrames
Byte 2: NumberOfPreinfuseFrames
Byte 3: MinimumPressure (U8P4)
Byte 4: MaximumFlow (U8P4)
```

### Profile Frame (8 bytes)
```
Byte 0: FrameIndex (0-19, or +32 for extension frame)
Byte 1: Flags (bit0=Flow mode, bit1=exit condition, bit4=water sensor, bit5=smooth)
Byte 2: SetVal (U8P4 - pressure or flow)
Byte 3: Temp (U8P1)
Byte 4: Duration (F8_1_7)
Byte 5: TriggerVal (U8P4 - exit threshold)
Bytes 6-7: MaxVol (U10P0 BE)
```

## Testing

1. Build and run Windows GUI
2. App auto-checks Pi on startup, prompts setup if needed
3. Use setup wizard (Tools → Setup Raspberry Pi) - opens terminal windows for passwords
4. Enter Pi's IP in Windows app, click Connect
5. Start Decenza app, scan for "DE1-SIM"
6. Connect and test operations

## Next Steps / TODO

- [x] BLE discovery working - Decenza finds "DE1-SIM"
- [x] BLE connection works - tablet successfully connects to Pi
- [x] GATT service discovery works - client finds all characteristics
- [x] MTU negotiation works - 512 byte MTU configured
- [ ] Test characteristic reads/writes with actual data
- [ ] Test shot simulation (pressure/flow curves)
- [ ] Test profile upload from Decenza
- [ ] Test GHC button states (dropdown values 0-4)

## Completed

- [x] Windows GUI application with TCP client
- [x] Pi BLE daemon with GATT server
- [x] TCP/JSON protocol between Windows and Pi
- [x] Setup wizard with terminal windows for SSH password entry
- [x] Auto-check Pi connection on startup
- [x] Embedded daemon source in Windows app
- [x] systemd service for auto-start on Pi boot
- [x] README.md and CLAUDE.md documentation
- [x] BLE advertising with service UUID (0000A000...)
- [x] BLE connection from Android/iOS clients
- [x] GATT service discovery
- [x] UART troubleshooting documentation

## Known Issues

1. **Windows BLE**: Qt BLE peripheral mode doesn't work on Windows - this is why we use Pi
2. **mDNS resolution**: `DE1-Simulator.local` may not resolve on Windows - use IP address
3. **BlueZ permissions**: Pi daemon needs root or CAP_NET_RAW capability
4. **UART Corruption (Pi 3)**: The BCM43438 Bluetooth chip communicates via UART. Under certain conditions (power issues, overheating, prolonged use), the UART link can become corrupted, causing BLE to silently fail. See "UART Troubleshooting" section below.

## BlueZ Configuration (Critical)

For BLE peripheral/advertising to work on Raspberry Pi, BlueZ requires specific configuration:

### 1. Experimental Mode
BlueZ must run with `--experimental` flag. The setup script modifies `/lib/systemd/system/bluetooth.service`:
```
ExecStart=/usr/sbin/bluetoothd --experimental
```
Note: The path to `bluetoothd` varies by distro - use `which bluetoothd` to find it.

### 2. Adapter Configuration
The Bluetooth adapter must be unblocked and have LE, advertising, and discoverable enabled:
```bash
rfkill unblock bluetooth    # Unblock if soft-blocked
btmgmt power on
btmgmt le on
btmgmt advertising on
btmgmt name 'DE1-SIM'       # Set short name (BLE names limited to ~10 chars)
hciconfig hci0 piscan       # Enable discoverable mode (CRITICAL for BLE scanning)
```

### 3. Verify Configuration
Check adapter status with:
```bash
btmgmt info
```
The `current settings` should include: `powered`, `le`, `advertising`, `discoverable`

### 4. Service Startup Order
The daemon service must wait for Bluetooth to be fully ready:
- `After=bluetooth.target` in systemd unit
- `ExecStartPre=/bin/sleep 2` to ensure BlueZ is initialized

### Troubleshooting BLE Issues
```bash
# Check daemon logs
sudo journalctl -u de1-ble-daemon -n 30

# Check bluetooth service
sudo systemctl status bluetooth

# Check adapter status
sudo btmgmt info

# Check if bluetooth is blocked
rfkill list bluetooth

# If advertising fails, restart everything:
sudo rfkill unblock bluetooth
sudo systemctl restart bluetooth
sleep 3
sudo btmgmt power on && btmgmt le on && btmgmt advertising on
sudo hciconfig hci0 up
sudo systemctl restart de1-ble-daemon
```

## UART Troubleshooting (Critical for Pi 3)

The Raspberry Pi 3's Bluetooth chip (BCM43438) connects to the SoC via UART. This link can become corrupted, causing BLE advertising to silently fail even though Qt reports success.

### Symptoms of UART Corruption
- BLE advertising shows as active (`btmgmt info` shows `advertising` in settings)
- But no devices can see the Pi when scanning
- `dmesg` shows errors like:
  ```
  Bluetooth: hci0: Frame reassembly failed (-84)
  Bluetooth: hci0: unexpected event 0x01 length: 214 > 1
  Bluetooth: hci0: Opcode 0x2005 failed: -22
  ```

### How to Diagnose
```bash
# Check for UART errors in kernel log
dmesg | grep -i "bluetooth\|hci" | grep -i "fail\|error"

# Test if LE scan works (should show nearby BLE devices)
sudo hcitool lescan
# If this shows "Set scan parameters failed: Input/output error", UART is corrupted
```

### The Fix: Reboot
When UART corruption occurs, **a full Pi reboot is required**:
```bash
sudo reboot
```
Simply restarting the Bluetooth service is NOT sufficient - the hardware state must be reset.

### Prevention
1. **Power supply**: Use a good quality 2.5A+ power supply
2. **Cooling**: Ensure adequate ventilation, especially in enclosures
3. **Monitor**: Add a health check that reboots if UART errors are detected:
   ```bash
   # Add to crontab to check every 5 minutes
   */5 * * * * dmesg | grep -q "Frame reassembly failed" && sudo reboot
   ```

### Verifying BLE Works After Reboot
```bash
# 1. Check no UART errors
dmesg | grep -i bluetooth | tail -10

# 2. Verify LE scan works
sudo timeout 5 hcitool lescan

# 3. Start daemon and verify advertising
sudo systemctl start de1-ble-daemon
sudo btmgmt info | grep "current settings"
# Should show: powered ssp br/edr le advertising secure-conn
```

## Reference Code

The DE1 protocol was studied from the Decenza codebase at `C:\code\de1-qt`:
- `src/ble/protocol/de1characteristics.h` - UUIDs, states, MMR addresses
- `src/ble/protocol/binarycodec.cpp` - encoding/decoding functions
- `src/ble/de1device.cpp` - characteristic parsing
- `src/ble/blemanager.cpp` - device discovery (looks for names starting with "DE1")

## Debugging

The simulator logs all events to the "BLE Log" tab:
- `[PI]` - Pi daemon events
- `[RX]` - Incoming writes from app
- `[TX]` - Outgoing notifications
- `[WARN]` - Blocked requests (e.g., GHC active)
- `[ERROR]` - Errors
