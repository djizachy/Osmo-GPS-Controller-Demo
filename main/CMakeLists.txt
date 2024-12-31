idf_component_register(SRCS "app_main.c" 
                            "../utils/crc/custom_crc16.c" 
                            "../utils/crc/custom_crc32.c"
                            "../utils/json/cJSON.c"
                            "../utils/json/cJSON_Utils.c"
                            "../protocol/dji_protocol_parser.c"
                            "../protocol/dji_protocol_data_processor.c"
                            "../protocol/dji_protocol_data_descriptors.c"
                            "../protocol/dji_protocol_data_structures.c"
                            "../ble/ble.c"
                            "../data/data.c"
                            "../logic/logic.c"
                    PRIV_REQUIRES bt nvs_flash
                    INCLUDE_DIRS "." "../utils/crc" "../utils/json" "../protocol" "../ble" "../data" "../logic")
