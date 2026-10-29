idf_component_register(SRCS "../main.c" "motor.c" "hall.c" "protocol.c"
    INCLUDE_DIRS "." ".."
    PRIV_REQUIRES esp_driver_gpio esp_driver_ledc esp_timer)
