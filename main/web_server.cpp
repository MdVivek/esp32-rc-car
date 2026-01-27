#include "web_server.h"
#include "motor_control.h"

#include "esp_log.h"
#include "esp_http_server.h"

extern "C" {
#include "cJSON.h"
}

static const char *TAG = "web_server";

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
                set_steer(a->valueint);
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

void start_server(void)
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

    ESP_LOGI(TAG, "HTTP server started");
}
