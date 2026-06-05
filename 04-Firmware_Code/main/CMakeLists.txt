idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES ble_flexsense cmd_system cmd_nvs cmd_flexsense
                       PRIV_REQUIRES nvs_flash)
