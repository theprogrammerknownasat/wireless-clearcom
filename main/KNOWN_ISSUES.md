# Known Issues

## SA Query Disconnects During Development
**Status:** Deferred to hardware testing
**Symptom:** WiFi briefly disconnects when USB serial monitor is opened or pack is plugged in
**Root Cause:** USB power fluctuation causes brief brownout, WiFi stack detects it and triggers SA Query timeout
**Impact:** Minimal - connection recovers automatically within 500ms
**Next Steps:** Test with stable bench power supply and final hardware
**Workaround:** Ignore during development, connection re-establishes quickly

## ADC Battery Readings Invalid Without Battery
**Status:** Expected behavior
**Symptom:** Battery shows 0.2V CRITICAL
**Root Cause:** ADC reading floating pin (no battery connected)
**Impact:** None - battery monitoring code works correctly
**Next Steps:** Test with actual LiPo battery