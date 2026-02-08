# ClearCom Wireless System

Production firmware for DIY wireless ClearCom RS-701 emulation.

## Quick Start

### Prerequisites
- ESP-IDF v5.5 or later
- ESP32-S3 development board
- USB cable for programming

### Configuration

Edit `main/config.h` to select device type:

```c
// For base station:
#define BUILD_BASE_STATION

// For belt pack:
// #define BUILD_BELT_PACK
```

Device-specific settings in:
- `main/config_base.h` - Base station configuration
- `main/config_pack.h` - Belt pack configuration
- `main/config_common.h` - Shared settings

### Build and Flash

```bash
# Navigate to project directory
cd clearcom_wireless

# Configure (first time only)
idf.py menuconfig

# Build
idf.py build

# Flash to ESP32-S3
idf.py -p /dev/ttyUSB0 flash monitor
```

## Project Status

### ✅ Phase 1: Foundation (COMPLETE)
- [x] Configuration system
- [x] Device manager
- [x] State machine
- [x] Basic logging

### 🔄 Phase 2: Audio (IN PROGRESS)
- [ ] WM8960 driver
- [ ] Opus integration
- [ ] Audio processor
- [ ] Sidetone mixing

### ⏳ Phase 3: Network (PENDING)
- [ ] WiFi manager
- [ ] UDP transport

### ⏳ Phase 4: Hardware (PENDING)
- [ ] GPIO control
- [ ] Battery monitoring
- [ ] ClearCom line interface

### ⏳ Phase 5: System (PENDING)
- [ ] Diagnostics
- [ ] Power management
- [ ] Call module

## Documentation

See `SPECIFICATION.md` for complete technical specifications.

## License

Personal project - not for commercial use.

## Notes

- Device IDs: Base stations 0x80+, Belt packs 0x01+
- 1:1 pairing model (one base per pack)
- Target latency: <70ms
- Target battery life: 12+ hours