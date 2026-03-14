# Wireless Production Intercom

Production firmware for a wireless intercom system compatible with wired party line infrastructure. Provides full-duplex audio between a base station (wired to a party line) and a wireless belt pack with PTT and call signaling, emulating the behavior of an RS-701 belt pack.

Built on ESP32-S3 with WM8960 audio codec, Opus voice compression, and WiFi UDP transport. Designed for reliability in live theatre environments.

---

## System Architecture

```
Wired Party Line (30V DC + audio, 3-pin XLR)
        |
  Base Station (WiFi AP)
  - ESP32-S3 + WM8960
  - 600:600 transformer I/O
  - Always transmits partyline audio to pack
  - Injects pack audio into partyline
        |
    WiFi (UDP, Opus @ 24kbps)
        |
  Belt Pack (WiFi STA)
  - ESP32-S3 + WM8960
  - Headset mic/speaker (4-pin XLR)
  - PTT (latched/momentary)
  - Call button
  - Volume pot (digital via ADC)
  - Optional battery monitoring
```

**1:1 pairing** - each base serves exactly one pack. The wired party line sees the base as a normal station.

---

## Hardware

- **MCU:** ESP32-S3-WROOM-1U-N8R8 (8MB flash, 8MB PSRAM)
- **Audio codec:** WM8960 (I2C control @ 0x1A, I2S audio)
- **Base station:** Powered from 30V party line, audio through 600:600 transformers
- **Belt pack:** Headset interface, PTT/Call buttons, volume pot, optional LiPo battery

Custom PCB designs (hardware is closed-source).

---

## Building

### Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- Python 3.8+

### Quick Start

```bash
# Set up ESP-IDF
source ~/esp/esp-idf/export.sh

# Select device type in main/config.h:
#   #define BUILD_BASE_STATION   (or BUILD_BELT_PACK)

# Build and flash
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

Exit monitor: `Ctrl+]`

### Configuration

Configuration is split across three files:

| File | Purpose |
|------|---------|
| `main/config.h` | Device type selection (base or pack) |
| `main/config_common.h` | Shared: WiFi, audio, network, jitter buffer |
| `main/config_base.h` | Base: device ID, party line gains, call detect |
| `main/config_pack.h` | Pack: buttons, battery mode, sleep, tones, volume |

---

## Features

### Both Devices
- WM8960 codec driver (I2C + I2S, device-specific register config)
- Opus voice codec (16kHz mono, 20ms frames, 24kbps)
- Audio limiter (configurable threshold)
- Jitter buffer with Opus PLC for WiFi smoothing (configurable, default 2 frames / 40ms)
- UDP transport with sequence numbers and packet loss tracking
- Call signaling (button + LED + network) with 2s timeout
- Hardware watchdog (10s, auto-reboot on hang)
- Self-test on boot (I2C, ADC, GPIO, Opus, WiFi, NVS) with LED fault codes
- Status LED: OFF=good, slow blink=packet loss, fast blink=disconnected, solid=error

### Base Station
- WiFi Access Point (hidden SSID)
- Always transmits party line audio to connected pack
- Party line interface via 600:600 transformers (differential I2S output)
- Call detect from party line (ADC + voltage divider)
- Call TX to party line (MOSFET driver)
- PTT mirror LED (shows pack's PTT state)

### Belt Pack
- WiFi Station (auto-connects to paired base)
- PTT button with latched/momentary modes (200ms hold threshold, configurable timeout)
- Call button for signaling
- Digital volume control (10k pot via ADC, EMA smoothing + deadband)
- Audio tones (connected, disconnected, battery warnings, call)
- Sidetone (hear yourself through codec bypass path)
- Battery monitoring (3 modes: none, external, internal LiPo)
- Power management (light sleep with WiFi wake, deep sleep with button wake)

---

## PTT Behavior

| Action | Result |
|--------|--------|
| Quick press from IDLE | Enter LATCHED (TX on, stays on) |
| Quick press while LATCHED | Return to IDLE (TX off) |
| Hold from IDLE | Enter LATCHED, then MOMENTARY after 200ms |
| Release from MOMENTARY | Return to IDLE |
| Stuck TX for 5 min (configurable) | Force IDLE (safety timeout) |

---

## Project Structure

```
main/
  main.c                    Entry point, tasks, callbacks
  config.h                  Device type selector
  config_common.h           Shared configuration
  config_base.h             Base station config
  config_pack.h             Belt pack config
  test_mode_base.c          Base test mode (440Hz tone + RX monitor)
  test_mode_pack.c          Pack test mode (mic loopback with 2s delay)

  audio/
    audio_codec.c/h         WM8960 I2C/I2S driver
    audio_opus.c/h          Opus encode/decode wrapper
    audio_processor.c/h     Audio limiter
    audio_tones.c/h         Tone generator
    audio_jitter_buffer.c/h Receive jitter buffer

  network/
    wifi_manager.c/h        WiFi AP (base) / STA (pack)
    udp_transport.c/h       UDP packet TX/RX with stats

  hardware/
    gpio_control.c/h        LEDs and buttons
    ptt_control.c/h         PTT state machine
    battery.c/h             Battery ADC monitoring (pack)
    volume_control.c/h      Volume pot ADC (pack)
    clearcom_line.c/h       Party line interface (base)

  system/
    device_manager.c/h      Device state, status reporting
    diagnostics.c/h         Self-test framework
    power_manager.c/h       Sleep modes (pack)
    call_module.c/h         Call signaling logic
```

---

## Network Protocol

**UDP packets** on port 5000:

```c
typedef struct {
    uint32_t sequence;      // Packet counter
    uint32_t timestamp;     // Microseconds
    uint16_t opus_size;     // Compressed audio size
    uint8_t  flags;         // Bit 0: PTT, Bit 1: Call
    uint8_t  reserved;
    uint8_t  opus_data[];   // Opus compressed audio (~60-80 bytes)
} audio_packet_t;
```

- Base: always transmitting
- Pack: transmits only when PTT active
- WiFi: hidden SSID, WPA2, channel 6 (configurable)

---

## Battery Modes (Belt Pack)

Set `BATTERY_MODE` in `config_pack.h`:

| Mode | ADC Monitoring | Sleep Modes | Use Case |
|------|---------------|-------------|----------|
| `BATTERY_NONE` | No | No | USB/bench power, initial testing |
| `BATTERY_EXTERNAL` | No | Yes | USB power bank |
| `BATTERY_INTERNAL` | Yes | Yes | Onboard LiPo with voltage divider |

---

## Status LED Reference

| Pattern | Meaning |
|---------|---------|
| OFF | Everything OK |
| Slow blink | Packet loss > 2% |
| Fast blink | WiFi disconnected |
| Solid ON | System error |
| N blinks + pause (boot) | Self-test fault code (N = test number) |

---

## Test Mode

Set `TEST_MODE_ENABLE 1` in `config_common.h`. Disables normal audio task to avoid I2S contention.

- **Base:** Outputs 440Hz sine wave to party line, monitors and logs input levels
- **Pack:** Microphone loopback with 2-second delay to verify codec

---

## Known Issues

- **Partition space:** Belt pack binary is near the 1MB app partition limit at 2MB flash config. The N8R8 module has 8MB flash -- reconfigure via `idf.py menuconfig` > Serial flasher config > Flash size > 8MB.
- **Battery ADC:** Reads floating pin as ~0V when no battery connected (expected). Set `BATTERY_MODE` to `BATTERY_NONE` to disable.
- **SA Query disconnects:** Brief WiFi drops when USB serial monitor is opened due to power fluctuation. Connection recovers automatically.

---

## License

Personal project - not for commercial distribution.

---

**Firmware Version:** 1.0.0
