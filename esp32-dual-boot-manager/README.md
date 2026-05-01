# ESP32 Dual Boot Manager

A complete Arduino IDE project for managing dual-boot firmware on the ESP32. Flash two independent firmware images to separate OTA partitions (app0 and app1) and switch between them via a full-featured web GUI — no serial connection required after initial flash.

---

## Features

- **Dual partition OTA** — flash firmware to app0 or app1 independently
- **Web-based boot switcher** — click to select which firmware boots next
- **WiFi manager** — connect to home network or use built-in AP hotspot
- **SPIFFS file manager** — upload, list, and delete files via browser
- **Dark-themed responsive UI** — works on desktop and mobile
- **REST API** — all operations available as JSON endpoints
- **No external CDN** — fully self-hosted web interface

---

## Partition Map

| Name     | Type | SubType | Offset     | Size       | Notes                    |
|----------|------|---------|------------|------------|--------------------------|
| nvs      | data | nvs     | 0x009000   | 0x5000     | WiFi credentials, NVS    |
| otadata  | data | ota     | 0x00E000   | 0x2000     | OTA boot tracking        |
| **app0** | app  | ota_0   | **0x10000**| **1,638,400 B** | Primary firmware    |
| **app1** | app  | ota_1   | **0x1A0000**| **1,048,576 B** | Secondary firmware |
| spiffs   | data | spiffs  | 0x2A0000   | 1,441,792 B | Web UI + file storage   |

**Important size limits:**
- `app0` maximum: **1,638,400 bytes (1.56 MB)**
- `app1` maximum: **1,048,576 bytes (1.00 MB)**

---

## Quick Start

### 1. Clone and open in Arduino IDE

```bash
git clone https://github.com/your-username/esp32-dual-boot-manager.git
cd esp32-dual-boot-manager
```

Open `esp32-dual-boot-manager/dual_boot_manager.ino` in Arduino IDE.

### 2. Configure Arduino IDE

| Setting          | Value                          |
|------------------|-------------------------------|
| Board            | ESP32 Dev Module               |
| Flash Size       | 4MB (32Mb)                     |
| Partition Scheme | Custom (`partitions.csv`)      |
| Upload Speed     | 921600                         |
| CPU Frequency    | 240MHz                         |

> **Custom partition scheme:** In Arduino IDE, go to **Tools → Partition Scheme → Custom** and ensure `partitions.csv` is in the sketch folder.

### 3. Flash firmware and SPIFFS

```bash
# Option A: Use the flash scripts (after first Arduino IDE export)
# Windows:
flash.bat

# Linux/macOS:
./flash.sh
```

**Or manually:**

1. **Sketch → Export Compiled Binary** to produce the `.bin` file
2. Upload SPIFFS data: **Tools → ESP32 Sketch Data Upload** (requires ESP32FS plugin)
3. The device will reboot and broadcast WiFi: `ESP32-DualBoot` / `admin1234`
4. Open browser at **http://192.168.4.1**

---

## Web GUI Pages

### Dashboard (`/index.html`)
- Shows currently running partition and next boot target
- Displays app0 / app1 state with colored badges (VALID / INVALID / EMPTY)
- System metrics: free heap, uptime, WiFi mode, IP address, flash size
- Full partition table with offsets and sizes
- Auto-refreshes every 3 seconds

### Boot Switcher (`/switch.html`)
- Two large buttons — one per partition
- Current boot target highlighted with accent border
- Disabled automatically for EMPTY or INVALID partitions
- Custom confirmation modal before switching
- 3-second countdown while device reboots
- Auto-reconnects after reboot

### OTA Flash (`/flash.html`)
- Upload a `.bin` firmware file to app0 or app1
- File size validated **before upload** against partition limits
- Real-time progress bar (XHR with progress events)
- Shows bytes written / total bytes / percentage
- "Set Boot + Reboot" button appears after successful flash

### WiFi Settings (`/wifi.html`)
- Current connection status (mode, SSID, IP)
- Scan for nearby networks — click to auto-fill SSID
- Save SSID + password to NVS flash
- Show/hide password toggle
- Switch between AP and STA modes

### File Manager (`/files.html`)
- SPIFFS usage bar (used / total in KB)
- List all files with sizes and delete button
- Upload new files with progress bar
- Delete confirmation modal

---

## API Reference

| Method | Endpoint | Description |
|--------|----------|-------------|
| GET | `/api/status` | System status JSON |
| GET | `/api/partitions` | All partitions with state |
| GET | `/api/switch?target=app0\|app1` | Switch boot partition and reboot |
| POST | `/api/flash?slot=app0\|app1` | OTA flash firmware (multipart) |
| GET | `/api/reboot` | Reboot the device |
| POST | `/api/wifi` | Save WiFi credentials |
| GET | `/api/wifi/scan` | Scan for nearby WiFi networks |
| GET | `/api/files` | List SPIFFS files |
| POST | `/api/upload` | Upload file to SPIFFS (multipart) |
| DELETE | `/api/file?name=<path>` | Delete file from SPIFFS |

### Example responses

**GET /api/status**
```json
{
  "running": "app0",
  "boot": "app0",
  "app0_state": "VALID",
  "app1_state": "EMPTY",
  "heap": 278432,
  "uptime": "0h 5m 12s",
  "wifi_mode": "AP",
  "ip": "192.168.4.1",
  "flash_size": 4194304
}
```

**GET /api/switch?target=app1**
```json
{ "success": true, "message": "Boot set to app1, rebooting..." }
```

**POST /api/flash?slot=app1**  
Body: multipart/form-data with `firmware` field containing `.bin` file
```json
{
  "success": true,
  "message": "Flash complete. 524288 bytes written to app1. Use /api/switch to boot.",
  "bytes": 524288,
  "slot": "app1"
}
```

**POST /api/wifi**  
Body: `{"ssid":"MyNetwork","pass":"mypassword"}`
```json
{ "success": true, "message": "Credentials saved. Reboot to connect." }
```

**GET /api/wifi/scan**
```json
[
  { "ssid": "HomeNetwork", "rssi": -52, "secure": true },
  { "ssid": "GuestWiFi",   "rssi": -71, "secure": false }
]
```

---

## How to Flash a Second Firmware via Web GUI

1. Compile your second sketch in Arduino IDE: **Sketch → Export Compiled Binary**
2. Locate the `.ino.bin` file in your sketch folder
3. Open the ESP32 Dual Boot Manager web GUI
4. Go to **OTA Flash** page
5. Select the target slot:
   - Use **APP1** for the secondary firmware (smaller 1 MB limit)
   - Use **APP0** for primary firmware (1.56 MB limit)
6. Click "Choose .bin file" and select your exported binary
7. Verify the file size is within the slot's limit
8. Click **Flash to APP1** (or APP0)
9. Wait for the progress bar to reach 100%
10. Click **Set Boot + Reboot** to switch to the new firmware

To switch back:
- Open the web GUI on the new firmware (or reconnect to this manager)
- Go to **Boot Switcher** and click the other partition

---

## How to Switch Boot Partition

1. Open the web GUI
2. Navigate to **Boot Switcher**
3. Both partitions are displayed with their current state:
   - `VALID` — firmware is installed and verified
   - `EMPTY` — no firmware in this slot
   - `INVALID` — firmware exists but failed verification
4. Click a **VALID** partition's button
5. Confirm in the dialog
6. Device reboots into the selected firmware (~5 seconds)

---

## Arduino IDE Settings

| Setting                    | Value                  |
|---------------------------|------------------------|
| Board                     | ESP32 Dev Module       |
| Upload Speed              | 921600                 |
| CPU Frequency             | 240MHz (WiFi/BT)       |
| Flash Frequency           | 80MHz                  |
| Flash Mode                | DIO                    |
| Flash Size                | 4MB (32Mb)             |
| Partition Scheme          | Custom                 |
| Core Debug Level          | None                   |
| PSRAM                     | Disabled               |

**Required libraries** (all built-in with ESP32 Arduino core):
- `WiFi.h`
- `WebServer.h`
- `SPIFFS.h`
- `Update.h`
- `Preferences.h`
- `esp_ota_ops.h`
- `esp_partition.h`

**SPIFFS upload plugin:** Install the [ESP32 Sketch Data Upload](https://github.com/me-no-dev/arduino-esp32fs-plugin) tool to upload the `data/` folder to SPIFFS from Arduino IDE.

---

## Flash Script Usage

### Windows (`flash.bat`)
```cmd
flash.bat
```
- Auto-detects COM port from registry
- Prompts for confirmation before flashing
- Searches recursively for binary files exported by Arduino IDE

### Linux/macOS (`flash.sh`)
```bash
chmod +x flash.sh
./flash.sh
```
- Auto-detects `/dev/ttyUSB0`, `/dev/ttyACM0`, or macOS `/dev/cu.*` ports
- Color-coded output (green = success, red = error)
- Exits with non-zero code on failure

**Flash addresses used by both scripts:**

| Component        | Address    |
|-----------------|------------|
| Bootloader      | 0x1000     |
| Partition table | 0x8000     |
| OTA data        | 0xE000     |
| Firmware (app0) | 0x10000    |
| SPIFFS          | 0x2A0000   |

---

## Troubleshooting

### Device won't enter bootloader mode
- Hold the **BOOT** (IO0) button, press **RESET (EN)**, then release BOOT
- Try a different USB cable (some cables are charge-only)
- On macOS, install the CP2102 or CH340 driver for your USB-Serial chip

### Can't connect to web GUI
- Connect to WiFi `ESP32-DualBoot` with password `admin1234`
- Open `http://192.168.4.1` in browser (not https)
- Disable VPN if active
- Try a different browser

### OTA flash fails
- Ensure firmware `.bin` is within the slot size limit
- app0 max: 1,638,400 bytes | app1 max: 1,048,576 bytes
- Try reducing flash size by disabling unused components in your sketch
- Use `Sketch → Export Compiled Binary` (not the compiled cache)

### SPIFFS not mounting
- The sketch calls `SPIFFS.begin(true)` — this auto-formats on fail
- Run **Tools → ESP32 Sketch Data Upload** to re-upload the data folder
- Check Serial Monitor (115200 baud) for `[SPIFFS]` messages

### WiFi won't connect in STA mode
- Double-check SSID/password (case sensitive)
- Ensure 2.4 GHz network (ESP32 does not support 5 GHz)
- After saving credentials, the device reboots to connect
- If connection fails, device falls back to AP mode automatically

### Partition state shows EMPTY after flash
- Reboot the device after flashing — the OTA state is updated on boot
- Confirm the flash completed without errors (check progress bar)

---

## CI/CD

GitHub Actions build CI is configured in `.github/workflows/build.yml`. It automatically compiles the sketch using the official ESP32 Arduino core on every push and pull request.

---

## License

MIT License

Copyright (c) 2024 ESP32 Dual Boot Manager Contributors

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
