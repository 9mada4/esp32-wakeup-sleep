idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES esp32-wakeup-sleep esp_driver_gpio
)
