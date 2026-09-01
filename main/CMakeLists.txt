# ============================================================================
# Single, unified component registration for the FoloToy AI Passport
# firmware. Demo pages, PC Controller (BLE HID presenter), FUI UI layer,
# and pixel-graphics helpers are all compiled into one image. There is
# no longer a separate presenter build profile: the legacy
# sdkconfig.presenter.defaults layer was folded into sdkconfig.defaults
# so this fork always builds the demo + presenter union.
#
# Source of truth: docs/software-design/pc-controller/requirements.md
# ============================================================================
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
         "pc_app_main.c"
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
    REQUIRES bsp bt nvs_flash esp_timer esp_hw_support esp_event esp_netif esp_wifi
)
