# ClearCom Wireless System - Production Specification

**Version:** 1.0  
**Date:** February 8, 2026  
**Author:** Ben Hough  
**Purpose:** DIY wireless intercom system that emulates ClearCom RS-701 functionality

---

## Table of Contents

1. [System Overview](#system-overview)
2. [Hardware Specifications](#hardware-specifications)
3. [Firmware Architecture](#firmware-architecture)
4. [Audio Pipeline](#audio-pipeline)
5. [Network Protocol](#network-protocol)
6. [User Interface](#user-interface)
7. [Power Management](#power-management)
8. [Configuration](#configuration)
9. [Testing & Validation](#testing--validation)

---

## System Overview

### Purpose
Create a 1:1 wireless bridge for ClearCom party-line systems where each base station makes exactly one RS-701 belt pack wireless. The system must be transparent to the existing wired ClearCom infrastructure.

### Design Philosophy
- **Emulation, not modification:** Build from scratch to perfectly replicate RS-701 behavior
- **1:1 pairing:** Each base station serves exactly one belt pack
- **Transparent operation:** Wired ClearCom system sees base as normal RS-701
- **Professional quality:** <70ms latency, <1% packet loss, 12+ hour battery life

### System Components
```
┌─────────────────────────────────────────────────────────────┐
│                  WIRED CLEARCOM PARTY LINE                  │
│              (30V DC + Audio + GND on 3-pin XLR)            │
└────────────────────────┬────────────────────────────────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │   BASE STATION 1     │
              │   (Acts as RS-701)   │
              │   - ID: 0x80         │
              │   - Powered by line  │
              └──────────┬───────────┘
                         │
                    WiFi (UDP)
                 Opus @ 24kbps
                         │
                         ▼
              ┌──────────────────────┐
              │   BELT PACK 1        │
              │   (Wireless RS-701)  │
              │   - ID: 0x01         │
              │   - Battery powered  │
              └──────────┬───────────┘
                         │
                         ▼
              ┌──────────────────────┐
              │  CC-300/CC-400       │
              │  HEADSET (4-pin XLR) │
              └──────────────────────┘
```

---

## Hardware Specifications

### Base Station

#### Power Supply
- **Input:** 30V DC from ClearCom party line (via 3-pin XLR)
- **Regulation:** LM2596 buck converter (30V → 5V @ 2A)
- **ESP32 Power:** 5V via USB or direct 5V rail

#### Audio Interface (ClearCom Line)
- **Input:** Party line audio via 600Ω:600Ω transformer + NE5532 op-amp
- **Output:** To party line via NE5532 op-amp + 600Ω:600Ω transformer
- **Impedance:** 200Ω (matches ClearCom spec)
- **Level:** ~600mV RMS balanced audio

#### Audio Codec
- **Chip:** WM8960 (I2S + I2C)
- **Sample Rate:** 16kHz
- **Bit Depth:** 16-bit
- **Channels:** 2 (RX from line, TX to line)

#### Wireless
- **Module:** ESP32-S3 (integrated WiFi)
- **WiFi Mode:** Access Point (AP)
- **SSID:** Hidden network "ClearCom_Base"
- **Channel:** Configurable (default: 6)

#### Connectors
- **ClearCom Line:** 3-pin XLR male
- **Optional Passthrough:** 3-pin XLR female (parallel to male)

#### Indicators
- **Power LED:** Green (always on when powered)
- **Call LED:** Red (on when call received)
- **Status LED:** Tri-color or red (signal quality)
- **PTT Mirror LED:** Green (mirrors connected pack's PTT state) [Optional]

---

### Belt Pack

#### Power Supply
- **Battery:** 3.7V 3000mAh LiPo (11.1Wh)
- **Charging:** TP4056 or TP4057 USB-C (1A charge current)
- **Charge Time:** ~3.5 hours (0% → 100%)
- **Runtime Target:** 12+ hours active use (6 hour minimum)
- **Low Voltage Warning:** 3.3V (10% remaining)
- **Critical Voltage:** 3.0V (3% remaining)

#### Audio Codec
- **Chip:** WM8960 (I2S + I2C)
- **Sample Rate:** 16kHz
- **Bit Depth:** 16-bit
- **Mic Input:** Dynamic mic from headset (~2mV, needs preamp)
- **Speaker Output:** ~200mV RMS to headset

#### Wireless
- **Module:** ESP32-S3 (integrated WiFi)
- **WiFi Mode:** Station (STA)
- **Connects to:** Base station AP (1:1 pairing)

#### Headset Interface
- **Connector:** 4-pin XLR female
- **Pinout:**
    - Pin 1: Mic ground
    - Pin 2: Mic signal
    - Pin 3: Speaker ground
    - Pin 4: Speaker signal
- **Compatible Headsets:** ClearCom CC-300, CC-400

#### Controls
- **PTT Button:** Momentary push button
    - Quick press: Toggle latch on/off
    - Hold >200ms: Momentary PTT
- **Call Button:** Momentary push button
    - Press and hold: Send call signal
- **Volume Pot:** 10kΩ linear potentiometer (analog, between codec and headset)
- **Power Switch:** SPST toggle switch

#### Indicators
- **Power LED:** Green (on when powered)
- **PTT LED:** Green (solid when latched, on when momentary)
- **Call LED:** Red (on when call active)
- **Status LED:** Tri-color or red (signal quality: solid=excellent, slow blink=good, fast blink=poor)
- **Receive LED:** Blue (on when receiving audio) [Optional]

---

## Firmware Architecture

### Project Structure
```
clearcom_wireless/
├── main/
│   ├── main.c                    # Entry point, task orchestration
│   ├── config.h                  # Master config selector
│   ├── config_common.h           # Shared configuration
│   ├── config_base.h             # Base station specific
│   ├── config_pack.h             # Belt pack specific
│   │
│   ├── audio/
│   │   ├── audio_codec.c/h       # WM8960 I2S/I2C driver
│   │   ├── audio_opus.c/h        # Opus encode/decode wrapper
│   │   ├── audio_processor.c/h   # Audio pipeline, mixing, sidetone
│   │   ├── audio_limiter.c/h     # Anti-clipping protection
│   │   └── audio_tones.c/h       # Tone generation for feedback
│   │
│   ├── network/
│   │   ├── wifi_manager.c/h      # WiFi init, AP/STA, reconnect
│   │   └── udp_transport.c/h     # Packet TX/RX, sequencing, stats
│   │
│   ├── hardware/
│   │   ├── gpio_control.c/h      # PTT, Call buttons, all LEDs
│   │   ├── battery.c/h           # ADC monitoring, voltage estimation
│   │   └── clearcom_line.c/h     # Party line interface (base only)
│   │
│   ├── system/
│   │   ├── device_manager.c/h    # Device ID, pairing, state machine
│   │   ├── power_manager.c/h     # Sleep modes, watchdog
│   │   ├── diagnostics.c/h       # Self-test, health monitoring
│   │   └── call_module.c/h       # Call button logic (swappable)
│   │
│   └── CMakeLists.txt
│
├── components/
│   └── 78__esp-opus/            # Opus library (already installed)
│
├── sdkconfig.defaults           # ESP-IDF configuration
└── README.md                    # Build and usage instructions
```

### Task Architecture (FreeRTOS)
```c
Priority 5 (Critical):
  - audio_tx_task      // Capture mic, encode, send (belt pack)
  - audio_rx_task      // Receive, decode, play
  
Priority 4 (Important):
  - network_tx_task    // UDP packet transmission
  - network_rx_task    // UDP packet reception
  
Priority 3 (Normal):
  - gpio_monitor_task  // Button debounce, LED control
  - stats_task         // Logging and diagnostics
  
Priority 2 (Low):
  - battery_task       // Battery monitoring (belt pack only)
  
Priority 1 (Background):
  - watchdog_task      // System health monitoring
```

---

## Audio Pipeline

### Belt Pack Audio Flow

#### Transmit Path (Mic → Base)
```
Headset Mic (Dynamic, ~2mV)
    ↓
WM8960 ADC (with mic preamp, configurable gain)
    ↓
Digital audio samples (16kHz, 16-bit)
    ↓
Audio Limiter (prevent clipping)
    ↓
Opus Encoder (20ms frames, 24kbps)
    ↓
UDP Packet (sequenced)
    ↓
WiFi Transmission
```

#### Receive Path (Base → Speaker)
```
WiFi Reception
    ↓
UDP Packet (with sequence number)
    ↓
Opus Decoder (20ms frames, PLC if packet lost)
    ↓
Digital audio samples (16kHz, 16-bit)
    ↓
Mix with Sidetone (if PTT active)
    ↓
Audio Limiter (prevent clipping)
    ↓
WM8960 DAC
    ↓
Volume Pot (analog attenuation)
    ↓
Headset Speaker (~200mV RMS)
```

#### Sidetone (Mic Loopback)
When PTT is active, mix 30% of mic signal into headset output:
```c
speaker_output = (incoming_audio × 0.7) + (mic_input × 0.3)
```
This allows user to hear themselves naturally, matching RS-701 behavior.

---

### Base Station Audio Flow

#### From Party Line (RX)
```
ClearCom Party Line (600mV RMS balanced)
    ↓
600Ω:600Ω Transformer (isolation)
    ↓
NE5532 Op-amp (gain/buffer)
    ↓
WM8960 ADC
    ↓
Opus Encoder
    ↓
WiFi → Belt Pack
```

#### To Party Line (TX)
```
WiFi ← Belt Pack
    ↓
Opus Decoder
    ↓
WM8960 DAC
    ↓
NE5532 Op-amp (buffer/gain)
    ↓
600Ω:600Ω Transformer (isolation)
    ↓
ClearCom Party Line
```

---

## Network Protocol

### UDP Packet Structure
```c
typedef struct {
    uint32_t sequence;           // Incrementing packet number
    uint32_t timestamp;          // esp_timer_get_time() in microseconds
    uint16_t encoded_size;       // Actual Opus data size (variable)
    uint8_t  flags;              // Bit 0: PTT active, Bit 1: Call active
    uint8_t  reserved;           // Future use
    uint8_t  opus_data[MAX_OPUS_SIZE];  // Compressed audio
} __attribute__((packed)) audio_packet_t;

// Typical sizes:
// - Header: 12 bytes
// - Opus data: 60-80 bytes @ 24kbps
// - Total packet: ~72-92 bytes
```

### WiFi Configuration
- **Mode:** Base = AP, Pack = STA
- **SSID:** "ClearCom_Base" (hidden)
- **Password:** "clearcom123" (configurable)
- **Channel:** 6 (2.4GHz, configurable to 1, 6, or 11)
- **IP Addressing:**
    - Base: 192.168.4.1 (static)
    - Pack: 192.168.4.2 (DHCP)
- **UDP Port:** 5000

### Packet Loss Handling
- **Opus PLC:** Packet Loss Concealment fills in missing packets
- **Expected loss:** <1% in normal conditions
- **Acceptable loss:** Up to 5% (Opus can handle this gracefully)
- **Warning threshold:** >2% sustained loss triggers status LED

### Latency Budget
```
Mic capture:       5ms
Opus encode:      20ms
WiFi transmission: 25ms (includes buffering)
Opus decode:      20ms
Speaker output:    5ms
-------------------------
Total:           ~75ms (target: <70ms in practice)
```

---

## User Interface

### PTT Button Behavior

#### State Machine
```
States:
  - IDLE:      Mic off, not transmitting, LED off
  - LATCHED:   Mic on (toggled), transmitting, LED solid green
  - MOMENTARY: Mic on (held), transmitting, LED green

Transitions:
  Button Pressed:
    - If IDLE → LATCHED (rising edge)
    - If LATCHED and held >200ms → MOMENTARY
    
  Button Released:
    - If MOMENTARY → IDLE
    - If LATCHED → IDLE
```

#### Edge Cases
- Quick double-tap: IDLE → LATCHED → IDLE (toggle on then off)
- Hold from latch: LATCHED → MOMENTARY → IDLE on release
- Multiple quick presses: Each toggles latch state

---

### Call Button Behavior

**Simple Implementation (v1.0):**
- Press and hold → Light local Call LED + send call signal
- Release → Turn off local LED + stop signal
- Receive call signal → Light Call LED while signal active

**Future Enhancement:** Blink patterns, acknowledgment (module is swappable)

---

### LED Indicators

| LED | Base Station | Belt Pack | Behavior |
|-----|--------------|-----------|----------|
| Power | ✓ | ✓ | Solid green when powered |
| PTT | ✓ (mirror) | ✓ | Green: solid=latched, on=held |
| Call | ✓ | ✓ | Red when call button active |
| Status | ✓ | ✓ | Signal quality: solid=excellent, slow blink=good, fast blink=poor |
| Receive | ✓ (opt) | ✓ (opt) | Blue when receiving audio |

All LEDs fully configurable (enable/disable, brightness, blink rates)

---

### Audio Tones (Belt Pack Only)

Tones are fully configurable in `config_pack.h`:

```c
typedef struct {
    uint16_t frequency_hz;
    uint16_t duration_ms;
    uint8_t  repeat_count;
    uint16_t repeat_interval_ms;
} tone_config_t;
```

**Default Tones:**
- **Connected:** 800Hz, 100ms, 2 beeps
- **Disconnected:** 400Hz, 500ms, 1 beep
- **Battery 10%:** 600Hz, 100ms, 1 beep (once when threshold reached)
- **Battery 3%:** 600Hz, 100ms, 3 beeps (repeats every 60 seconds)
- **Call Received:** 1000Hz, 200ms, 1 beep

Tones play in headset, do NOT transmit over network.

---

## Power Management

### Belt Pack Power States

#### Active State
```
Components ON:
  - ESP32-S3 WiFi:    150mA
  - WM8960 active:     50mA
  - LEDs (avg):        10mA
  - Opus processing:   ~0mA (included in ESP32)
Total:              ~210mA

Battery life: 3000mAh / 210mA = 14.3 hours
```

#### Idle State (PTT off, listening)
```
Components ON:
  - ESP32-S3 WiFi:    120mA
  - WM8960 active:     30mA
  - LEDs (reduced):     5mA
Total:              ~155mA

Battery life: 3000mAh / 155mA = 19.4 hours
```

#### Light Sleep (no packets for 90 seconds)
```
Components:
  - ESP32-S3 sleep:    40mA
  - WM8960 sleep:       5mA
  - LEDs off:           0mA
Total:               ~45mA

Battery life: 3000mAh / 45mA = 66.7 hours
Wake time: <10ms (packet or button)
```

#### Deep Sleep (no packets for 20 minutes)
```
Components:
  - ESP32-S3 ULP:      10mA
  - Everything else off
Total:               ~10mA

Battery life: 3000mAh / 10mA = 300 hours (12.5 days)
Wake source: PTT button only (hardware interrupt)
Reinit time: ~2 seconds
```

### Battery Monitoring
- **Voltage Range:** 4.2V (full) → 3.0V (empty)
- **ADC Sampling:** Every 30 seconds
- **Warning Levels:**
    - 10% (3.3V): Single tone, visual indication
    - 3% (3.0V): Repeating tone every 60 seconds
- **Estimation:** Voltage-based lookup table (non-linear LiPo discharge curve)

### Base Station Power
- Powered by ClearCom 30V line (no sleep modes)
- LM2596 efficiency: ~85%
- Estimated consumption: ~300mA @ 30V = 9W

---

## Configuration

### Master Config File (`config.h`)
```c
// Select device type (uncomment ONE)
#define BUILD_BASE_STATION
// #define BUILD_BELT_PACK

// Include appropriate configs
#include "config_common.h"
#ifdef BUILD_BASE_STATION
    #include "config_base.h"
#else
    #include "config_pack.h"
#endif
```

### Common Config (`config_common.h`)
- WiFi settings (SSID, password, channel)
- Audio settings (sample rate, Opus bitrate)
- Network settings (UDP port, packet size)
- GPIO pin assignments
- LED configurations
- Debug/logging levels

### Base Station Config (`config_base.h`)
- Device ID (0x80, 0x81, ...)
- Paired belt pack ID
- Party line audio levels
- ClearCom interface settings

### Belt Pack Config (`config_pack.h`)
- Device ID (0x01, 0x02, ...)
- Paired base station ID
- PTT timing (hold threshold)
- Sidetone level
- Audio tones (full struct definitions)
- Battery thresholds
- Sleep timeouts

---

## Testing & Validation

### Self-Test on Boot

Runs automatically when `SELFTEST_ENABLE = 1`:

1. **NVS Check** - Validate config storage
2. **WM8960 Test** - I2C communication, register readback
3. **WiFi Test** - Radio initialization
4. **Battery Test** (pack only) - Voltage reading
5. **LED Test** - Flash each LED in sequence
6. **Speaker Test** - Tone sweep (200Hz → 2kHz)
7. **Opus Test** - Encode/decode loopback

**Results:**
- ✓ Pass: Single green LED blink, proceed to normal operation
- ✗ Fail: Rapid red blink, error code in serial log

Each test can be individually enabled/disabled in config.

---

### Unit Tests

**Audio Path:**
- Opus encode → decode loopback
- Sidetone mixing
- Limiter prevents >0dBFS

**Network:**
- WiFi connect/disconnect
- UDP packet sequencing
- Packet loss detection

**GPIO:**
- Button debouncing
- PTT state machine
- LED PWM control

---

### Integration Tests

**Audio + Network:**
- End-to-end latency measurement
- Packet loss vs audio quality
- Sustained operation (4+ hours)

**PTT + Audio:**
- State transitions
- Sidetone activation
- Latency during state changes

**Call + Network:**
- Signal propagation
- LED synchronization

---

### Field Validation

1. **ClearCom Integration:**
    - Connect base to real party line
    - Verify transparent operation
    - Test with multiple wired RS-701s on line

2. **Range Testing:**
    - Indoor: walls, floors, interference
    - Outdoor: line-of-sight
    - Microwave oven stress test

3. **Battery Life:**
    - Full charge/discharge cycle
    - Measure actual runtime
    - Validate sleep mode power consumption

4. **Theater Environment:**
    - Multi-hour dress rehearsal
    - Concurrent WiFi devices
    - RF interference from lighting, sound gear

---

## Success Criteria

| Metric | Target | Acceptable | Measured |
|--------|--------|------------|----------|
| End-to-end latency | <70ms | <100ms | TBD |
| Packet loss (normal) | <0.5% | <1% | 0.45% ✓ |
| Battery life | 12+ hours | 6+ hours | TBD |
| Indoor range | 60ft | 40ft | 60ft ✓ |
| Boot self-test | Pass all | Pass critical | TBD |
| PTT response | <50ms | <100ms | TBD |
| Audio quality | Transparent | Intelligible | TBD |

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-02-08 | Initial production specification |

---

**END OF SPECIFICATION**