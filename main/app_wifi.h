#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

/**
 * @brief 以 STA 模式初始化 WiFi 并阻塞等待连接结果。
 *
 * 连接成功时返回 WIFI_CONNECTED_BIT，重试耗尽时返回 WIFI_FAIL_BIT。
 */
EventBits_t wifi_init_sta(void);

#ifdef __cplusplus
}
#endif
