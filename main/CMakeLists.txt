idf_component_register(SRCS "app_main.c" "usb_task.c" "ota_service.c"
                       INCLUDE_DIRS "."
                       REQUIRES app_update esp_driver_gpio esp_driver_usb_serial_jtag vfs)
