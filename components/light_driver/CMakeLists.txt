
idf_component_register(SRCS "iot_led.c" "light_driver.c" "./light_driver.c" "./iot_led.c"
                    INCLUDE_DIRS "." "./include"
                    REQUIRES app_storage driver led_strip
)
