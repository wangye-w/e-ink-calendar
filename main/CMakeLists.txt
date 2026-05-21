idf_component_register(SRCS "app_sntp.c" "app_wifi.c" "e-ink-calendar.c"
                    PRIV_REQUIRES esp_wifi nvs_flash driver
                    INCLUDE_DIRS ".")
