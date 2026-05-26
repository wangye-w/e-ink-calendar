#pragma once

#include "esp_log.h"
#include "esp_err.h"

#define LCD_HOST    SPI2_HOST
#define LCD_H_RES   200
#define LCD_V_RES   200

esp_err_t spi_bus_init(void);

esp_err_t lcd_screen_init(void);

esp_err_t u_nvs_init(void);