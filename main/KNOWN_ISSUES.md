# Known Issues

## SA Query Disconnects During Development
**Status:** Deferred to hardware testing
**Symptom:** WiFi briefly disconnects when USB serial monitor is opened or pack is plugged in
**Root Cause:** USB power fluctuation causes brief brownout, WiFi stack detects it and triggers SA Query timeout
**Impact:** Minimal - connection recovers automatically within 500ms
**Workaround:** Ignore during development, connection re-establishes quickly

## ADC Battery Readings Invalid Without Battery
**Status:** Expected behavior - set BATTERY_MODE to BATTERY_NONE in config_pack.h
**Symptom:** Battery shows 0.2V CRITICAL when no battery is connected
**Root Cause:** ADC reading floating pin (no battery connected)
**Impact:** None when BATTERY_MODE is BATTERY_NONE or BATTERY_EXTERNAL

## Partition Space Warning
**Status:** Fix available
**Symptom:** Build warns "smallest app partition is nearly full (3-5% free)"
**Root Cause:** Flash configured as 2MB but ESP32-S3-WROOM-1U-N8R8 has 8MB
**Fix:** Run `idf.py menuconfig` > Serial flasher config > Flash size > 8MB, then Partition Table > Single factory app (large)

## Latency Budget
**Status:** Not yet measured on hardware
**Estimate:** ~80-100ms end-to-end with jitter buffer enabled (2 frames / 40ms buffer + Opus encode/decode + WiFi transit). Set JITTER_BUFFER_FRAMES to 1 or JITTER_BUFFER_ENABLE to 0 in config_common.h to reduce latency at the cost of more audio artifacts.
