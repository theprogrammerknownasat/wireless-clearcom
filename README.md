# ClearCom Wireless System

**Production firmware for DIY wireless ClearCom RS-701 belt pack emulation**

A complete wireless intercom system providing professional-grade full-duplex audio communication between a base station (connected to a wired ClearCom party line) and a wireless belt pack with PTT/call functionality.

---

## 🎯 Project Goals

**Primary Objective:** Create a reliable, low-latency wireless interface to professional ClearCom intercom systems, enabling crew members to communicate wirelessly while maintaining compatibility with existing wired infrastructure.

**Key Requirements:**
- **Latency:** <70ms end-to-end for natural conversation
- **Battery Life:** 12+ hours continuous operation (belt pack)
- **Audio Quality:** Broadcast-grade voice clarity with Opus codec
- **Reliability:** Enterprise WiFi with automatic reconnection
- **Compatibility:** Seamless integration with ClearCom RS-701 party line systems

---

## 📋 System Overview

### Architecture
```
┌─────────────────┐         WiFi          ┌─────────────────┐
│   Base Station  │◄──────────────────────►│    Belt Pack    │
│                 │     UDP Audio Stream   │                 │
│  ┌───────────┐  │                        │  ┌───────────┐  │
│  │ ClearCom  │  │                        │  │ Headset   │  │
│  │ Party Line│◄─┤                        │  │ Mic/Spkr  │  │
│  └───────────┘  │                        │  └───────────┘  │
│                 │                        │                 │
│  ESP32-S3       │                        │  ESP32-S3       │
│  WM8960 Codec   │                        │  WM8960 Codec   │
│  LINE I/O       │                        │  MIC/SPEAKER    │
└─────────────────┘                        │  PTT Button     │
                                           │  Call Button    │
                                           │  LiPo Battery   │
                                           └─────────────────┘
```

### Device Types

**Base Station (ID: 0x80+)**
- WiFi Access Point (192.168.4.1)
- Connects to ClearCom party line (balanced line input/output)
- Receives audio from party line, transmits to belt pack via WiFi
- Receives audio from belt pack, injects into party line
- Mirrors belt pack PTT state on LED

**Belt Pack (ID: 0x01+)**
- WiFi Station (connects to base AP)
- Headset interface (mic input, speaker output)
- PTT button with latched/momentary modes (200ms hold threshold)
- Call button for signaling
- Battery monitoring with low/critical warnings
- Power management (light sleep after 90s idle, deep sleep after 20min)

---

## 🚀 Quick Start

### Prerequisites

**Hardware:**
- 2x ESP32-S3 development boards (one for base, one for pack)
- 2x WM8960 audio codec breakout boards
- USB cables for programming
- (Optional) Breadboard and jumper wires for testing

**Software:**
- [ESP-IDF v5.5.2](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/) or later
- Python 3.8+
- Git

### Installation

```bash
# Clone repository
git clone <repository-url>
cd clearcom_production

# Set up ESP-IDF environment (if not already configured)
. $HOME/esp/esp-idf/export.sh

# Install dependencies
cd main
idf.py add-dependency "esp-opus^1.0.0"
```

### Configuration

**1. Select Device Type**

Edit `main/config.h`:

```c
// Build as BASE STATION:
#define BUILD_BASE_STATION
// #define BUILD_BELT_PACK

// Build as BELT PACK:
// #define BUILD_BASE_STATION
#define BUILD_BELT_PACK
```

**2. Device-Specific Settings**

Configuration is split across three files:

- **`config_common.h`** - Shared settings (WiFi, audio, network)
- **`config_base.h`** - Base station only (party line gains, mirror LED)
- **`config_pack.h`** - Belt pack only (buttons, battery, power management)

**3. Hardware Simulation Mode**

For testing without physical WM8960 codec:

```c
// config_common.h
#define SIMULATE_HARDWARE 1  // Use fake audio data
```

Set to `0` when real hardware is connected.

### Build and Flash

```bash
# Build firmware
idf.py build

# Flash to ESP32-S3 and monitor serial output
idf.py -p /dev/ttyUSB0 flash monitor

# Or separate commands:
idf.py -p /dev/ttyUSB0 flash
idf.py -p /dev/ttyUSB0 monitor
```

**Exit monitor:** `Ctrl+]`

---

## 📊 Project Status

### ✅ Phase 1-6: Core System (COMPLETE)

**Infrastructure:**
- [x] Build system with device-type conditional compilation
- [x] Configuration management (common/base/pack split)
- [x] Device manager with state machine
- [x] Self-test diagnostics framework
- [x] Professional logging system

**Audio Pipeline:**
- [x] WM8960 codec driver (with simulation mode)
- [x] Opus encoder/decoder integration (16kHz, 20ms frames, 24kbps)
- [x] Audio processor with limiter
- [x] Tone generator for signaling

**Network:**
- [x] WiFi manager (AP mode for base, STA mode for pack)
- [x] UDP transport with packet stats (TX/RX/loss tracking)
- [x] Automatic reconnection handling
- [x] PMF (Protected Management Frames) configuration

**Hardware:**
- [x] GPIO control with device-specific LED/button mapping
- [x] PTT state machine (latched/momentary modes)
- [x] Battery monitoring with voltage-to-percentage calculation
- [x] Power management (light/deep sleep)
- [x] ClearCom party line interface (base only)

**System Services:**
- [x] Call button signaling
- [x] Status monitoring and reporting
- [x] Uptime tracking

### 🔧 Current Focus: Hardware Integration Testing

**What Works:**
- ✅ Both device types compile and boot successfully
- ✅ WiFi connection established between base and pack
- ✅ Network statistics tracking (TX/RX packets, RSSI)
- ✅ Device manager state transitions
- ✅ All subsystems initialize without errors
- ✅ Simulation mode for testing without hardware

**In Testing:**
- 🧪 GPIO button inputs (PTT, Call)
- 🧪 LED outputs (Power, Status, Call, PTT/Mirror, RX)
- 🧪 Audio pipeline end-to-end
- 🧪 Battery monitoring with real LiPo
- 🧪 Power management sleep modes

**Known Issues:**
- ⚠️ **SA Query WiFi Disconnects:** Brief disconnects when USB serial monitor is opened or pack is plugged in. Root cause: USB power fluctuation causing brownout detection. **Impact:** Minimal - connection recovers in <500ms. **Status:** Deferred to stable power testing.
- ⚠️ **ADC Battery Readings:** Shows CRITICAL when no battery connected (expected - reading floating pin). Works correctly with battery.
- ⚠️ **Opus Self-Test Fails:** Self-test runs before Opus init, always fails in simulation mode. Benign during development.

### ⏳ Phase 7: Production Hardware Validation (NEXT)

- [ ] PCB design and fabrication
- [ ] WM8960 codec integration with real audio
- [ ] ClearCom line transformer interface
- [ ] Battery charging circuit
- [ ] Enclosure design

### ⏳ Phase 8: Field Testing (PENDING)

- [ ] End-to-end audio latency measurement
- [ ] Battery life testing (12+ hour target)
- [ ] Range testing
- [ ] Multi-device interference testing
- [ ] Production environment validation

---

## 🔌 Hardware Pinout

### Common Pins (Both Devices)

**WM8960 Audio Codec (I2S/I2C):**
```
I2S_BCK  → GPIO 7   (Bit Clock)
I2S_WS   → GPIO 8   (Word Select / LRCK)
I2S_DOUT → GPIO 9   (Data Out to WM8960)
I2S_DIN  → GPIO 6   (Data In from WM8960)
I2C_SDA  → GPIO 4   (I2C Data)
I2C_SCL  → GPIO 5   (I2C Clock)
```

**Common LEDs:**
```
LED_POWER  → GPIO 10  (Power indicator)
LED_STATUS → GPIO 12  (System status)
LED_CALL   → GPIO 11  (Call signaling)
```

### Base Station Specific

**LEDs:**
```
LED_PTT_MIRROR → GPIO 13  (Mirrors pack's PTT state)
```

**ClearCom Interface:**
- LINE_IN (balanced) via WM8960 line input
- LINE_OUT (balanced) via WM8960 line output
- Party line input gain: 20dB
- Party line output gain: 20dB

### Belt Pack Specific

**Buttons (Active-Low, Internal Pull-Up):**
```
BUTTON_PTT  → GPIO 13  (Push-to-Talk)
BUTTON_CALL → GPIO 14  (Call signaling)
```

**LEDs:**
```
LED_PTT     → GPIO 15  (PTT active indicator)
LED_RECEIVE → GPIO 16  (RX audio indicator - optional)
```

**Battery:**
```
BATTERY_ADC → ADC1 Channel 0
  Voltage thresholds:
  - Full:     4.2V (100%)
  - Low:      3.4V (warn user)
  - Critical: 3.2V (force shutdown)
```

---

## 🛠️ Testing Guide

### Serial Console Commands

Monitor real-time system status via UART (115200 baud):

```
========================================
  ClearCom Wireless System
  BELT PACK
========================================
Firmware: 1.0.0
Device ID: 0x01
Paired Base: 0x80

╔════════════════════════════════════════╗
║ BELT PACK - ID: 0x01 - Uptime: 00:08  ║
╠════════════════════════════════════════╣
║ State: 3 | WiFi: CONN | RSSI: -10 dBm ║
║ TX:    0 | RX:    0 | Lost:    0      ║
║ PTT: 0 | Call: 0 | Battery: 3.7V (85%)║
╚════════════════════════════════════════╝

Network: TX=0, RX=0, Loss=0.00%
```

**Status updates print every 5 seconds.**

### Button Testing (Without Breadboard)

Use female-to-female dupont wires to test buttons:

**PTT Button (GPIO 13):**
1. Connect one wire to GPIO 13, other to GND
2. Touch wires together to simulate button press
3. **Expected:** Status shows `PTT: 1`, LED_PTT lights up, TX packets increment

**Call Button (GPIO 14):**
1. Connect one wire to GPIO 14, other to GND
2. Touch wires together to simulate button press
3. **Expected:** Status shows `Call: 1`, LED_CALL lights up

### LED Testing

LEDs can be tested by connecting an LED + current-limiting resistor (220Ω-1kΩ) between the GPIO pin and GND:

```
GPIO Pin ──┬── LED (Anode)
           │
           └── Resistor (220Ω)
                │
               GND
```

**On boot:**
- LED_POWER should light immediately (solid ON)
- Other LEDs may blink during init

**During operation:**
- LED_STATUS blinks at different rates based on system state
- LED_PTT (pack) lights when PTT pressed
- LED_PTT_MIRROR (base) mirrors pack's PTT state
- LED_CALL lights when call button pressed

### WiFi Connection Test

**Expected behavior:**
1. Base starts as AP: `WiFi: CONN` immediately
2. Pack connects: `WiFi: CONN` after 2-3 seconds
3. RSSI shows actual signal strength: `-10 dBm` to `-30 dBm` typical
4. No disconnects under stable power conditions

**If pack won't connect:**
- Check `config_common.h` has matching SSID/password
- Verify base is running and shows `Access Point started`
- Check WiFi channel isn't congested (default: channel 6)

### Audio Pipeline Test (Simulation Mode)

With `SIMULATE_HARDWARE=1`:
- Fake audio data is generated instead of reading from codec
- Audio task runs at 50Hz (20ms frames)
- Opus encoder/decoder process data normally
- **To verify:** Check TX packets increment when PTT pressed

---

## 📁 Project Structure

```
clearcom_production/
├── main/
│   ├── main.c                 # Application entry point
│   ├── config.h               # Device type selection
│   ├── config_common.h        # Shared configuration
│   ├── config_base.h          # Base station config
│   ├── config_pack.h          # Belt pack config
│   │
│   ├── audio/                 # Audio subsystem
│   │   ├── codec_wm8960.c     # WM8960 driver
│   │   ├── opus_codec.c       # Opus encoder/decoder
│   │   ├── audio_processor.c  # Audio processing/limiter
│   │   └── tone_generator.c   # Call tone generation
│   │
│   ├── network/               # Network subsystem
│   │   ├── wifi_manager.c     # WiFi AP/STA management
│   │   └── udp_transport.c    # UDP packet transport
│   │
│   ├── hardware/              # Hardware interfaces
│   │   ├── gpio_control.c     # LEDs and buttons
│   │   ├── ptt_control.c      # PTT state machine
│   │   ├── battery.c          # Battery monitoring (pack)
│   │   └── clearcom_line.c    # Party line interface (base)
│   │
│   └── system/                # System services
│       ├── device_manager.c   # Device state management
│       ├── diagnostics.c      # Self-test framework
│       ├── power_manager.c    # Sleep management (pack)
│       └── call_module.c      # Call signaling
│
├── CMakeLists.txt
├── README.md                  # This file
├── SPECIFICATION.md           # Detailed technical specs
└── KNOWN_ISSUES.md            # Development notes
```

---

## 🔧 Configuration Reference

### WiFi Settings (`config_common.h`)

```c
#define WIFI_SSID            "ClearCom_Base"
#define WIFI_PASSWORD        "clearcom123"
#define WIFI_CHANNEL         6
#define WIFI_HIDDEN_SSID     1   // Hide SSID from broadcast
#define MAX_STA_CONN         1   // Only allow one pack per base
```

### Audio Settings (`config_common.h`)

```c
#define SAMPLE_RATE          16000   // Hz
#define OPUS_FRAME_SIZE      320     // Samples (20ms @ 16kHz)
#define OPUS_BITRATE         24000   // bps
#define AUDIO_BUFFER_COUNT   8       // Ring buffer size
```

### Device Pairing (`config_common.h`)

```c
#define BASE_DEVICE_ID       0x80  // Base station ID
#define PACK_DEVICE_ID       0x01  // Belt pack ID
```

**Note:** 1:1 pairing model - each base is paired with exactly one pack.

### PTT Behavior (`config_pack.h`)

```c
#define PTT_HOLD_THRESHOLD_MS  200  // ms - hold for momentary mode
```

**PTT Modes:**
- **Latched:** Quick press (<200ms) toggles TX on/off
- **Momentary:** Hold (≥200ms) to TX, release to stop

### Battery Monitoring (`config_pack.h`)

```c
#define BATTERY_FULL_VOLTAGE      4.2   // V
#define BATTERY_EMPTY_VOLTAGE     3.0   // V
#define BATTERY_LOW_VOLTAGE       3.4   // V (warning)
#define BATTERY_CRITICAL_VOLTAGE  3.2   // V (force shutdown)
```

### Power Management (`config_pack.h`)

```c
#define ENABLE_LIGHT_SLEEP        1
#define LIGHT_SLEEP_TIMEOUT_SEC   90    // Idle time before sleep
#define DEEP_SLEEP_TIMEOUT_MIN    20    // Idle time before deep sleep
```

---

## 🐛 Troubleshooting

### Build Errors

**"Multiple device types defined"**
- Fix: Only define ONE of `BUILD_BASE_STATION` or `BUILD_BELT_PACK` in `config.h`

**"esp-opus component not found"**
- Fix: Run `idf.py add-dependency "esp-opus^1.0.0"` in `main/` directory

**"LED_PTT undeclared" (when building base)**
- Fix: This is expected - `LED_PTT` only exists for pack. Check that `DEVICE_TYPE_PACK` conditionals are correct.

### Runtime Issues

**Base: "WiFi: DISC" even though AP started**
- Status: Fixed in latest version - should show "CONN"
- If still seeing: Check `device_manager_update_wifi()` is called in WiFi event handler

**Pack: Can't connect to base**
- Check: Base shows `Access Point started` in logs
- Check: SSID/password match in both devices' config files
- Check: WiFi channel is available (scan with phone)

**Pack: Battery shows CRITICAL**
- Expected if no battery connected - ADC reads floating pin
- Connect 3.7V LiPo to test real battery monitoring

**TX/RX packets remain 0**
- Expected: Audio only transmits when PTT is pressed
- Test: Short GPIO 13 to GND to trigger PTT

**SA Query disconnects every 20-30 seconds**
- Known issue during USB development
- Defer to stable power testing
- Connection recovers automatically

---

## 📚 Additional Documentation

- **`SPECIFICATION.md`** - Complete technical specifications
- **`KNOWN_ISSUES.md`** - Development notes and deferred items
- **ESP-IDF Documentation:** https://docs.espressif.com/projects/esp-idf/

---

## 🎛️ Device IDs and Networking

**ID Assignment:**
- Base stations: `0x80` - `0xFF`
- Belt packs: `0x01` - `0x7F`

**Network Configuration:**
- Base IP: `192.168.4.1` (WiFi AP)
- Pack IP: `192.168.4.2` (DHCP assigned)
- UDP Port: `5000` (both devices)

**Pairing:**
- Each base is paired with exactly one pack (1:1 model)
- Pairing configured in `config_common.h`
- Future: Multi-pack support with ID-based routing

---

## ⚡ Performance Targets

| Metric | Target | Current Status |
|--------|--------|----------------|
| End-to-end latency | <70ms | Pending measurement |
| Audio quality | Broadcast-grade | Opus 24kbps @ 16kHz |
| WiFi range | 50m+ line-of-sight | Pending field test |
| Battery life (pack) | 12+ hours | Pending real battery test |
| Packet loss tolerance | <5% | UDP with stats tracking |
| Boot time | <5 seconds | ~4 seconds measured |

---

## 📝 License

Personal project - not for commercial use.

---

## 🙏 Acknowledgments

- ESP-IDF framework by Espressif Systems
- Opus audio codec by Xiph.Org Foundation
- WM8960 codec by Cirrus Logic

---

**Last Updated:** February 9, 2026  
**Firmware Version:** 1.0.0  
**Build:** 4f27161-dirty