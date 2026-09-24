list(APPEND micropixel_sources
    "platform/boards/es3c28p-esp32s3/board_hardware.cpp"
    "platform/boards/es3c28p-esp32s3/display_hardware.cpp"
    "platform/boards/es3c28p-esp32s3/i2s_audio_sink.cpp"
    "platform/boards/es3c28p-esp32s3/platform.cpp"
    "platform/input/esp_lcd_touch_input.cpp"
    "platform/input/ft6336_report.cpp"
    "platform/input/gpio_key_input.cpp"
    "platform/transports/development_display_control.cpp"
    "platform/transports/usb_serial_jtag_local_control.cpp"
)

list(APPEND micropixel_requires
    espressif__esp_lcd_ili9341
    espressif__esp_lcd_touch_ft5x06
)
