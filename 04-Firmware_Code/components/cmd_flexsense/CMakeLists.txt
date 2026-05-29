idf_component_register(SRCS "cmd_flexsense.c"
                    INCLUDE_DIRS .
                    REQUIRES console driver esp_adc sht31)
