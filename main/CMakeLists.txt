# ============================================================================
# Dual build-profile component registration (see
# docs/software-design/pc-controller/requirements.md §2 and §12).
#
# - Default profile (CONFIG_PC_CONTROLLER_APP not set): registers the demo
#   application. The else() branch below is preserved verbatim; default
#   profile build artifacts must remain byte-for-byte identical.
# - Presenter profile (sdkconfig.presenter.defaults sets
#   CONFIG_PC_CONTROLLER_APP=y): registers the PC Controller source set.
#   Note: Wi-Fi is disabled in that profile (CONFIG_ESP_WIFI_ENABLED=n), so
#   the presenter branch must never require esp_wifi / esp_netif.
# ============================================================================
if(CONFIG_PC_CONTROLLER_APP)
    # PC Controller (BLE HID presenter) application.
    # Source set (M3/M4 complete): assembly layer (pc_app_main.c), radio /
    # storage platform modules, the power manager and key-beep platform
    # modules (pc_power_mgr.c, pc_beep.c), the pure-logic modules (key
    # semantics, app FSM, power FSM, HID reports, host profiles, slide
    # counter, speech timer, string table) and the FUI UI layer (pc_ui.c
    # + pc_ui_fui.c theme core + the five pages). The temporary stub
    # pc_ui_stub.c has been replaced and removed. No esp_wifi / esp_netif
    # dependency (Wi-Fi is disabled in this profile).
    idf_component_register(
        SRCS "pc_app_main.c"
             "pc_ble_hid.c"
             "pc_storage.c"
             "pc_ui.c"
             "pc_ui_fui.c"
             "pc_ui_fui_standby.c"
             "pc_ui_fui_menu.c"
             "pc_ui_fui_present.c"
             "pc_ui_fui_pair.c"
             "pc_ui_fui_media.c"
             "pc_key_semantics.c"
             "pc_app_fsm.c"
             "pc_hid_reports.c"
             "pc_host_profiles.c"
             "pc_slide_counter.c"
             "pc_speech_timer.c"
             "pc_power_fsm.c"
             "pc_power_mgr.c"
             "pc_beep.c"
             "pc_strings.c"
        INCLUDE_DIRS "."
        # esp_hw_support: esp_light_sleep_start / esp_deep_sleep_start /
        # RTC_DATA_ATTR 唤醒 API(pc_power_mgr.c;默认档同一 API 由
        # demo_low_power.c 使用,见下方 else 分支)。
        REQUIRES bsp bt nvs_flash esp_timer esp_hw_support
    )
else()
    idf_component_register(
        SRCS "main.c"
             "ui_pixel.c"
             "ui_pixel_math.c"
             "demo_display.c"
             "demo_button.c"
             "demo_audio.c"
             "demo_battery.c"
             "demo_radio.c"
             "demo_wifi.c"
             "demo_ble.c"
             "demo_low_power.c"
        INCLUDE_DIRS "."
        REQUIRES bsp bt esp_event esp_hw_support esp_netif esp_timer esp_wifi nvs_flash
    )
endif()
