#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi in AP (access point) mode
 * SSID: "RC-ESP32"
 * Security: Open (no password)
 */
void wifi_init_softap(void);

#ifdef __cplusplus
}
#endif
