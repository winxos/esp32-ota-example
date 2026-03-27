idf_component_register(SRCS "app.c" "app_main.c" "log_utils.c" "ota_service.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_update esp_driver_gpio esp_driver_usb_serial_jtag vfs)
