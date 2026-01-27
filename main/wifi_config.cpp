#include "wifi_config.h"
#include "motor_control.h"

#include "esp_log.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_config";

/* =====================================================
 *              WiFi EVENT HANDLER
 * ===================================================== */

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        ESP_LOGW(TAG, "Station disconnected - stopping motors");
        stop_motors();
    }
}

void wifi_init_softap(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap{};
    strcpy((char *)ap.ap.ssid, "RC-ESP32");
    ap.ap.authmode = WIFI_AUTH_OPEN;
    ap.ap.password[0] = 0;
    ap.ap.max_connection = 4;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_start());

    // Register WiFi event handler for station disconnect
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_AP_STADISCONNECTED,
                                               &wifi_event_handler, NULL));

    ESP_LOGI(TAG, "WiFi AP initialized - SSID: RC-ESP32");
}
