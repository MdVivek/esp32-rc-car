// ===============================
// main/main.cpp
// ===============================

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <dirent.h>

#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_http_server.h"

#include "esp_vfs.h"
#include "esp_littlefs.h"

#include "driver/gpio.h"
#include "driver/ledc.h"

extern "C" {
#include "cJSON.h"
}

static const char *TAG = "rc_server";

/* =====================================================
 *                  TB661 PIN MAPPING
 * ===================================================== */

// Shared standby
#define STBY GPIO_NUM_19

// LEFT FRONT
#define LF_IN1 GPIO_NUM_23
#define LF_IN2 GPIO_NUM_22
#define LF_PWM GPIO_NUM_21

// LEFT BACK
#define LB_IN1 GPIO_NUM_18
#define LB_IN2 GPIO_NUM_5
#define LB_PWM GPIO_NUM_17

// RIGHT FRONT
#define RF_IN1 GPIO_NUM_27
#define RF_IN2 GPIO_NUM_26
#define RF_PWM GPIO_NUM_25

// RIGHT BACK
#define RB_IN1 GPIO_NUM_33
#define RB_IN2 GPIO_NUM_32
#define RB_PWM GPIO_NUM_14

/* =====================================================
 *                  PWM CONFIG
 * ===================================================== */

#define PWM_FREQ_HZ 10000
#define PWM_RES LEDC_TIMER_8_BIT
#define PWM_MAX_DUTY 255

#define PWM_TIMER LEDC_TIMER_0
#define PWM_MODE LEDC_HIGH_SPEED_MODE

static int speed_cmd = 0;   // -10 .. +10
static int steer_cmd = 0;   // -10 .. +10

/* =====================================================
 *              SPEED → PWM CONVERSION
 * ===================================================== */

static inline int pwm_from_speed(int speed)
{
    int s = abs(speed);
    if (s > 10) s = 10;
    return (s * PWM_MAX_DUTY) / 10;
}

/* =====================================================
 *                  MOTOR INIT
 * ===================================================== */

static void motor_init(void)
{
    gpio_config_t io{};
    io.mode = GPIO_MODE_OUTPUT;
    io.pin_bit_mask =
        (1ULL << LF_IN1) | (1ULL << LF_IN2) |
        (1ULL << LB_IN1) | (1ULL << LB_IN2) |
        (1ULL << RF_IN1) | (1ULL << RF_IN2) |
        (1ULL << RB_IN1) | (1ULL << RB_IN2) |
        (1ULL << STBY);

    ESP_ERROR_CHECK(gpio_config(&io));
    gpio_set_level(STBY, 1);
    vTaskDelay(pdMS_TO_TICKS(100));

    ledc_timer_config_t timer{};
    timer.speed_mode = PWM_MODE;
    timer.timer_num = PWM_TIMER;
    timer.freq_hz = PWM_FREQ_HZ;
    timer.duty_resolution = PWM_RES;
    timer.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&timer));

    const int pwm_gpios[4] = {LF_PWM, LB_PWM, RF_PWM, RB_PWM};
    const ledc_channel_t chs[4] = {
        LEDC_CHANNEL_0, LEDC_CHANNEL_1,
        LEDC_CHANNEL_2, LEDC_CHANNEL_3
    };

    for (int i = 0; i < 4; i++) {
        ledc_channel_config_t ch{};
        ch.channel = chs[i];
        ch.gpio_num = pwm_gpios[i];
        ch.speed_mode = PWM_MODE;
        ch.timer_sel = PWM_TIMER;
        ch.duty = 0;
        ESP_ERROR_CHECK(ledc_channel_config(&ch));
    }

    ESP_LOGI(TAG, "Motor driver initialized");
}

/* =====================================================
 *              LOW LEVEL HELPERS
 * ===================================================== */

#define DIR_PAIR(in1, in2, fwd) \
    gpio_set_level(in1, fwd);   \
    gpio_set_level(in2, !fwd)

#define SET_DUTY(ch, val)             \
    ledc_set_duty(PWM_MODE, ch, val); \
    ledc_update_duty(PWM_MODE, ch)

static inline void set_all_dirs(bool fwd)
{
    DIR_PAIR(LF_IN1, LF_IN2, fwd);
    DIR_PAIR(LB_IN1, LB_IN2, fwd);
    DIR_PAIR(RF_IN1, RF_IN2, fwd);
    DIR_PAIR(RB_IN1, RB_IN2, fwd);
}

static inline void set_all_pwm(int lf, int lb, int rf, int rb)
{
    SET_DUTY(LEDC_CHANNEL_0, lf);
    SET_DUTY(LEDC_CHANNEL_1, lb);
    SET_DUTY(LEDC_CHANNEL_2, rf);
    SET_DUTY(LEDC_CHANNEL_3, rb);
}

/* =====================================================
 *              CORE DRIVE MODEL (ONLY ONE)
 * ===================================================== */

static void apply_drive(void)
{
    bool forward = speed_cmd >= 0;
    int base = pwm_from_speed(speed_cmd);

    int steer = steer_cmd;
    if (steer > 10) steer = 10;
    if (steer < -10) steer = -10;

    int diff = (abs(steer) * base) / 10;

    int lf = base, lb = base, rf = base, rb = base;

    if (steer < 0) {        // left
        lf -= diff;
        lb -= diff;
    } else if (steer > 0) { // right
        rf -= diff;
        rb -= diff;
    }

    set_all_dirs(forward);
    set_all_pwm(lf, lb, rf, rb);

    ESP_LOGI(TAG, "APPLY speed=%d steer=%d | L=%d R=%d",
             speed_cmd, steer_cmd, lf, rf);
}

/* =====================================================
 *              MOTOR API (HTML COMPATIBLE)
 * ===================================================== */

extern "C" void set_speed(int speed)
{
    speed_cmd = speed;
    apply_drive();
}

extern "C" void stop_motors(void)
{
    speed_cmd = 0;
    steer_cmd = 0;
    set_all_pwm(0, 0, 0, 0);
}

extern "C" void forward(int speed)
{
    speed_cmd = speed;
    apply_drive();
}

extern "C" void turn_left(int speed)
{
    speed_cmd = speed;
    steer_cmd = -10;
    apply_drive();
}

extern "C" void turn_right(int speed)
{
    speed_cmd = speed;
    steer_cmd = 10;
    apply_drive();
}

/* =====================================================
 *              WIFI SOFTAP
 * ===================================================== */

static void wifi_init_softap(void)
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
}

/* =====================================================
 *              FILE SERVER
 * ===================================================== */

static esp_err_t static_file_handler(httpd_req_t *req)
{
    char path[256];
    strcpy(path, "/littlefs");
    strncat(path,
            strcmp(req->uri, "/") == 0 ? "/index.html" : req->uri,
            sizeof(path) - strlen(path) - 1);

    FILE *f = fopen(path, "r");
    if (!f)
        return httpd_resp_send_err(req, HTTPD_404_NOT_FOUND, "Not found");

    httpd_resp_set_type(req, "text/html");
    char buf[512];
    size_t r;
    while ((r = fread(buf, 1, sizeof(buf), f)) > 0)
        httpd_resp_send_chunk(req, buf, r);

    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

/* =====================================================
 *              WEBSOCKET HANDLER
 * ===================================================== */

static esp_err_t ws_handler(httpd_req_t *req)
{
    if (req->method == HTTP_GET && req->content_len == 0)
        return ESP_OK;

    httpd_ws_frame_t frame{};
    frame.type = HTTPD_WS_TYPE_TEXT;

    if (httpd_ws_recv_frame(req, &frame, 0) != ESP_OK)
        return ESP_FAIL;

    uint8_t *buf = (uint8_t *)malloc(frame.len + 1);
    frame.payload = buf;
    httpd_ws_recv_frame(req, &frame, frame.len);
    buf[frame.len] = 0;

    cJSON *root = cJSON_Parse((char *)buf);
    if (!root) {
        free(buf);
        return ESP_OK;
    }

    cJSON *cmd = cJSON_GetObjectItem(root, "cmd");

    if (cJSON_IsString(cmd)) {

        if (!strcmp(cmd->valuestring, "set")) {
            cJSON *v = cJSON_GetObjectItem(root, "value");
            if (cJSON_IsNumber(v))
                set_speed(v->valueint);
        }

        else if (!strcmp(cmd->valuestring, "steer")) {
            cJSON *a = cJSON_GetObjectItem(root, "angle");
            if (cJSON_IsNumber(a)) {
                steer_cmd = a->valueint;
                apply_drive();
            }
        }

        else if (!strcmp(cmd->valuestring, "move")) {
            cJSON *d = cJSON_GetObjectItem(root, "dir");
            if (cJSON_IsString(d) && !strcmp(d->valuestring, "stop"))
                stop_motors();
        }
    }

    cJSON_Delete(root);
    free(buf);
    return ESP_OK;
}

/* =====================================================
 *              HTTP SERVER
 * ===================================================== */

static void start_server(void)
{
    httpd_handle_t server;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    ESP_ERROR_CHECK(httpd_start(&server, &cfg));

    httpd_uri_t root{};
    root.uri = "/";
    root.method = HTTP_GET;
    root.handler = static_file_handler;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &root));

    httpd_uri_t ws{};
    ws.uri = "/ws";
    ws.method = HTTP_GET;
    ws.handler = ws_handler;
    ws.is_websocket = true;
    ESP_ERROR_CHECK(httpd_register_uri_handler(server, &ws));
}

/* =====================================================
 *                  APP MAIN
 * ===================================================== */

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    esp_vfs_littlefs_conf_t fs{};
    fs.base_path = "/littlefs";
    fs.partition_label = "littlefs";
    ESP_ERROR_CHECK(esp_vfs_littlefs_register(&fs));

    motor_init();
    wifi_init_softap();
    start_server();

    ESP_LOGI(TAG, "RC CAR READY");
}
