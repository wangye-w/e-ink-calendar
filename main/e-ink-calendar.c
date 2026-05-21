#include "app_wifi.h"
#include "app_sntp.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/gpio.h"

void app_main(void)
{

  gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << 4),              // 使用位掩码指定引脚
        .mode = GPIO_MODE_OUTPUT,                // 设置模式为输出
        .pull_up_en = GPIO_PULLUP_DISABLE,       // 不需要内部上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,   // 不需要内部下拉
        .intr_type = GPIO_INTR_DISABLE           // 不需要中断
    };

    // 配置引脚
    gpio_config(&io_conf);

    // 在配置完成后，此引脚默认状态取决于 ESP32 的硬件逻辑
    // 如果需要确保是高，此时调用一次即可
    gpio_set_level(4, 1);
    //Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    wifi_init_sta();
    app_sntp_init();
}
