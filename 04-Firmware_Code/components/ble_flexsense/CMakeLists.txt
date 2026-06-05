idf_component_register(SRCS "ble_flexsense.c" "ble_flexsense_gap.c" "ble_flexsense_svc.c"
                    INCLUDE_DIRS "include"
                    REQUIRES "bt" "cmd_flexsense" "battery")
