# Main component CMakeLists.txt for ClearCom Wireless System

idf_component_register(
        SRCS
        "main.c"
        "system/device_manager.c"
        # Phase 2 audio files:
        "audio/audio_codec.c"
        "audio/audio_opus.c"
        "audio/audio_processor.c"
        "audio/audio_tones.c"
        # Phase 3 network files:
        "network/wifi_manager.c"
        "network/udp_transport.c"
        # Phase 4 hardware files:
        "hardware/gpio_control.c"
        "hardware/battery.c"
        "hardware/clearcom_line.c"
        "hardware/ptt_control.c"
        # Phase 5 diagnostics:
        # "system/diagnostics.c"
        # "system/power_manager.c"
        # "system/call_module.c"

        INCLUDE_DIRS
        "."
        "audio"
        "network"
        "hardware"
        "system"

        REQUIRES
        nvs_flash
        esp_wifi
        esp_netif
        esp_timer
        esp_system
        driver
        esp-opus
        esp_adc
)

# Set C standard
target_compile_options(${COMPONENT_LIB} PRIVATE -std=gnu11)