#include "sys_init.h"
#include "pinmap.h"

#include <esp_lcd_panel_ssd1681.h>

#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static SemaphoreHandle_t panel_refreshing_sem = NULL;
esp_lcd_panel_handle_t panel_handle = NULL;
esp_lcd_panel_io_handle_t io_handle = NULL;

IRAM_ATTR bool epaper_flush_ready_callback(const esp_lcd_panel_handle_t handle, const void *edata, void *user_data)
{
    // lv_disp_drv_t *disp_driver = (lv_disp_drv_t *) user_data;
    // lv_disp_flush_ready(disp_driver);
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(panel_refreshing_sem, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken == pdTRUE) {
        return true;
    }
    return false;
}

esp_err_t spi_bus_init(void)
{
    esp_err_t ret;
    spi_bus_config_t buscfg = {
        .miso_io_num = LCD_PIN_MSIO,
        .mosi_io_num = LCD_PIN_MOSI,
        .sclk_io_num = LCD_PIN_SCLK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = LCD_H_RES * LCD_V_RES / 8,
    };

    ret = spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO);

    return ret;
}

esp_err_t lcd_screen_init(void)
{
    static lv_disp_drv_t disp_drv;
    panel_refreshing_sem = xSemaphoreCreateBinary();
    xSemaphoreGive(panel_refreshing_sem);

    esp_err_t ret = ESP_OK;
    do {
        esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = LCD_PIN_DC,
        .cs_gpio_num = LCD_PIN_CS,
        .pclk_hz = 20 * 1000 * 1000,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        };
        // 将 LCD 连接到 SPI 总线
        ret = esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle);
        if (ret != ESP_OK) break;

        esp_lcd_ssd1681_config_t epaper_ssd1681_config = {
            .busy_gpio_num = LCD_PIN_BUSY,
            .non_copy_mode = true,
        };
        esp_lcd_panel_dev_config_t panel_config = {
            .reset_gpio_num = LCD_PIN_RST,
            .flags.reset_active_high = false,
            .vendor_config = &epaper_ssd1681_config
        };
        gpio_install_isr_service(0);
        ret = esp_lcd_new_panel_ssd1681(io_handle, &panel_config, &panel_handle);
        if (ret != ESP_OK) break;

        // 屏幕复位
        ret = esp_lcd_panel_reset(panel_handle);
        if (ret != ESP_OK) break;
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // 初始化屏幕
        ret = esp_lcd_panel_init(panel_handle);
        if (ret != ESP_OK) break;
        vTaskDelay(100 / portTICK_PERIOD_MS);
        // --- Register the e-Paper refresh done callback
        epaper_panel_callbacks_t cbs = {
            .on_epaper_refresh_done = epaper_flush_ready_callback
        };
        epaper_panel_register_event_callbacks(panel_handle, &cbs, &disp_drv);
        // 点亮屏幕
        ret = esp_lcd_panel_disp_on_off(panel_handle, true);
        if (ret != ESP_OK) break;
        vTaskDelay(100 / portTICK_PERIOD_MS);

        // 配置屏幕
        ret  = esp_lcd_panel_mirror(panel_handle, false, false);
        ret &= esp_lcd_panel_swap_xy(panel_handle, false);
        ret &= esp_lcd_panel_invert_color(panel_handle, false); // 调整反色
        ret &= esp_lcd_panel_disp_on_off(panel_handle, true);
    } while (false);
    
    return ret;
}

esp_err_t u_nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    return ret;
}