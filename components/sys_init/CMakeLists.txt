idf_component_register(SRCS "sys_init.c"
                    PRIV_REQUIRES esp_driver_spi esp_driver_gpio pinmap esp_lcd espressif__esp_lcd_ssd1681 lvgl__lvgl nvs_flash
                    INCLUDE_DIRS "include")
