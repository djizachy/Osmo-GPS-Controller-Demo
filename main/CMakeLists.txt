idf_component_register(SRCS "gattc_demo.c" 
                            "../utils/custom_crc16.c" 
                            "../utils/custom_crc32.c"
                    PRIV_REQUIRES bt nvs_flash
                    INCLUDE_DIRS "." "../utils")
