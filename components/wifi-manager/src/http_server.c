/**
 * author:  Viktar Vasiuk

   ----------------------------------------------------------------------
    Copyright (C) Viktar Vasiuk, 2023
    
    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    any later version.
     
    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    
    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------

@see https://github.com/vivask/wifi-manager
*/
#include <string.h>
#include <fcntl.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <cJSON.h>

#include "http_server.h"
#include "http_handlers.h"

#define WIFI_ACCESS_POINTS CONFIG_WEB_BASE_API"/wifi/ap"
#define ESP32_SETUP_WIFI CONFIG_WEB_BASE_API"/setup/wifi"
#define ESP32_SETUP_WIFI_CA CONFIG_WEB_BASE_API"/setup/wifi/ca"
#define ESP32_SETUP_WIFI_CRT CONFIG_WEB_BASE_API"/setup/wifi/crt"
#define ESP32_SETUP_WIFI_KEY CONFIG_WEB_BASE_API"/setup/wifi/key"
#define ESP32_SETUP_IPV4 CONFIG_WEB_BASE_API"/setup/ipv4"
#define ESP32_SETUP_HTTP CONFIG_WEB_BASE_API"/setup/http"
#define ESP32_SETUP_HTTP_CA CONFIG_WEB_BASE_API"/setup/http/ca"
#define ESP32_SETUP_HTTP_CRT CONFIG_WEB_BASE_API"/setup/http/crt"
#define ESP32_SETUP_HTTP_KEY CONFIG_WEB_BASE_API"/setup/http/key"
#define ESP32_CONNECT CONFIG_WEB_BASE_API"/connect"

#define REST_CHECK(a, str, goto_tag, ...)                                              \
    do                                                                                 \
    {                                                                                  \
        if (!(a))                                                                      \
        {                                                                              \
            ESP_LOGE(TAG, "%s(%d): " str, __FUNCTION__, __LINE__, ##__VA_ARGS__); \
            goto goto_tag;                                                             \
        }                                                                              \
    } while (0)


#define CHECK_FILE_EXTENSION(filename, ext) (strcasecmp(&filename[strlen(filename) - strlen(ext)], ext) == 0)

static const char *TAG = "REST";

static httpd_handle_t server_handle = NULL;
static httpd_config_t* server_config = NULL;

/* Set HTTP response content type according to file extension */
static esp_err_t set_content_type_from_file(httpd_req_t *req, const char *filepath)
{
    const char *type = "text/plain";
    if (CHECK_FILE_EXTENSION(filepath, ".html")) {
        type = "text/html";
    } else if (CHECK_FILE_EXTENSION(filepath, ".js")) {
        type = "application/javascript";
    } else if (CHECK_FILE_EXTENSION(filepath, ".css")) {
        type = "text/css";
    } else if (CHECK_FILE_EXTENSION(filepath, ".png")) {
        type = "image/png";
    } else if (CHECK_FILE_EXTENSION(filepath, ".ico")) {
        type = "image/x-icon";
    } else if (CHECK_FILE_EXTENSION(filepath, ".svg")) {
        type = "text/xml";
    }
    return httpd_resp_set_type(req, type);
}

/* Send HTTP response with the contents of the requested file */
static esp_err_t rest_common_get_handler(httpd_req_t *req)
{
    const char captive[] = "/generate_204";
    char filepath[FILE_PATH_MAX];
    ESP_LOGW(TAG, "URI : %s", req->uri);
    rest_server_context_t *rest_context = (rest_server_context_t *)req->user_ctx;
    strlcpy(filepath, rest_context->base_path, sizeof(filepath));
    if (req->uri[strlen(req->uri) - 1] == '/' || strncmp(req->uri, captive, sizeof(captive)) == 0) {
        strlcat(filepath, "/index.html", sizeof(filepath));
    } else {
        strlcat(filepath, req->uri, sizeof(filepath));
    }
    int fd = open(filepath, O_RDONLY, 0);
    if (fd == -1) {
        ESP_LOGE(TAG, "Failed to open file : %s", filepath);
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to read existing file");
        return ESP_FAIL;
    }

    set_content_type_from_file(req, filepath);

    char *chunk = rest_context->scratch;
    ssize_t read_bytes;
    do {
        /* Read file in chunks into the scratch buffer */
        read_bytes = read(fd, chunk, SCRATCH_BUFSIZE);
        if (read_bytes == -1) {
            ESP_LOGE(TAG, "Failed to read file : %s", filepath);
        } else if (read_bytes > 0) {
            /* Send the buffer contents as HTTP response chunk */
            if (httpd_resp_send_chunk(req, chunk, read_bytes) != ESP_OK) {
                close(fd);
                ESP_LOGE(TAG, "File sending failed!");
                /* Abort sending file */
                httpd_resp_sendstr_chunk(req, NULL);
                /* Respond with 500 Internal Server Error */
                httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to send file");
                return ESP_FAIL;
            }
        }
    } while (read_bytes > 0);
    /* Close file after sending complete */
    close(fd);
    ESP_LOGI(TAG, "File sending complete");
    /* Respond with an empty chunk to signal HTTP response completion */
    httpd_resp_send_chunk(req, NULL, 0);
    return ESP_OK;
}

esp_err_t start_rest_server(const char *base_path) 
{
    REST_CHECK(base_path, "wrong base path", err);
    rest_server_context_t *rest_context = calloc(1, sizeof(rest_server_context_t));
    REST_CHECK(rest_context, "No memory for rest context", err);
    strlcpy(rest_context->base_path, base_path, sizeof(rest_context->base_path));

    
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 13;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.lru_purge_enable = true;
    config.max_open_sockets = 5;

    server_config = &config;

    ESP_LOGI(TAG, "Starting HTTP Server");
    REST_CHECK(httpd_start(&server_handle, &config) == ESP_OK, "Start server failed", err_start);

    /* URI handler for fetching available wifi access points */
    httpd_uri_t wifi_access_points_get_uri = {
        .uri = WIFI_ACCESS_POINTS,
        .method = HTTP_GET,
        .handler = wifi_access_points_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &wifi_access_points_get_uri);

    /* URI handler for setting wifi esp32 */
    httpd_uri_t esp32_setup_wifi_post_uri = {
        .uri = ESP32_SETUP_WIFI,
        .method = HTTP_POST,
        .handler = esp32_setup_wifi_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_wifi_post_uri);

    /* URI handler for save wifi ca esp32 */
    httpd_uri_t esp32_setup_wifi_ca_post_uri = {
        .uri = ESP32_SETUP_WIFI_CA,
        .method = HTTP_POST,
        .handler = esp32_setup_wifi_ca_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_wifi_ca_post_uri);

    /* URI handler for save wifi crt esp32 */
    httpd_uri_t esp32_setup_wifi_crt_post_uri = {
        .uri = ESP32_SETUP_WIFI_CRT,
        .method = HTTP_POST,
        .handler = esp32_setup_wifi_crt_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_wifi_crt_post_uri);

    /* URI handler for save wifi key esp32 */
    httpd_uri_t esp32_setup_wifi_key_post_uri = {
        .uri = ESP32_SETUP_WIFI_KEY,
        .method = HTTP_POST,
        .handler = esp32_setup_wifi_key_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_wifi_key_post_uri);

    /* URI handler for setting ipv4 esp32 */
    httpd_uri_t esp32_setup_ipv4_post_uri = {
        .uri = ESP32_SETUP_IPV4,
        .method = HTTP_POST,
        .handler = esp32_setup_ipv4_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_ipv4_post_uri);

    /* URI handler for setting http esp32 */
    httpd_uri_t esp32_setup_http_post_uri = {
        .uri = ESP32_SETUP_HTTP,
        .method = HTTP_POST,
        .handler = esp32_setup_http_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_http_post_uri);

    /* URI handler for save http ca esp32 */
    httpd_uri_t esp32_setup_http_ca_post_uri = {
        .uri = ESP32_SETUP_HTTP_CA,
        .method = HTTP_POST,
        .handler = esp32_setup_http_ca_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_http_ca_post_uri);

    /* URI handler for save http crt esp32 */
    httpd_uri_t esp32_setup_http_crt_post_uri = {
        .uri = ESP32_SETUP_HTTP_CRT,
        .method = HTTP_POST,
        .handler = esp32_setup_http_crt_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_http_crt_post_uri);

    /* URI handler for save http key esp32 */
    httpd_uri_t esp32_setup_http_key_post_uri = {
        .uri = ESP32_SETUP_HTTP_KEY,
        .method = HTTP_POST,
        .handler = esp32_setup_http_key_post_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_setup_http_key_post_uri);

    /* URI handler for connect after configure complet */
    httpd_uri_t esp32_connect_get_uri = {
        .uri = ESP32_CONNECT,
        .method = HTTP_GET,
        .handler = esp32_sconnect_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &esp32_connect_get_uri);

    /* URI handler for getting web server files */
    httpd_uri_t common_get_uri = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = rest_common_get_handler,
        .user_ctx = rest_context
    };
    httpd_register_uri_handler(server_handle, &common_get_uri);

    return ESP_OK;
err_start:
    free(rest_context);
err:
    return ESP_FAIL;
}

void stop_rest_server() {
    if(server_handle != NULL){
        httpd_stop(server_handle);
        server_handle = NULL;
        server_config = NULL;
        ESP_LOGI(TAG, "Shutting down http server");
    }
}