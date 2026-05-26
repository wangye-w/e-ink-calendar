#include "app_wifi.h"
#include "app_sntp.h"
#include "sys_init.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"

void app_main(void)
{
    ESP_ERROR_CHECK(u_nvs_init());
    // wifi_init_sta();
    // app_sntp_init();

    ESP_ERROR_CHECK(spi_bus_init());
    ESP_ERROR_CHECK(lcd_screen_init());

    while(1)
    {
        vTaskDelay(100 / portTICK_PERIOD_MS);
    }
}
