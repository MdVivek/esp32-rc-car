// ===============================
// main/main.cpp
// ===============================

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_vfs.h"
#include "esp_littlefs.h"

#include "motor_control.h"
#include "wifi_config.h"
#include "web_server.h"

static const char *TAG = "rc_car";

/* =====================================================
 *                  APP MAIN
 * ===================================================== */

extern "C" void app_main(void)
{
    // Initialize NVS (non-volatile storage)
    ESP_ERROR_CHECK(nvs_flash_init());

    // Initialize and mount LittleFS for web UI files
    esp_vfs_littlefs_conf_t fs{};
    fs.base_path = "/littlefs";
    fs.partition_label = "littlefs";
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&fs));

    // Initialize motor driver (GPIO, PWM)
    motor_init();

    // Initialize WiFi access point
    wifi_init_softap();

    // Start HTTP server with WebSocket support
    start_server();

    ESP_LOGI(TAG, "RC CAR READY");
}
