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
#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include <esp_http_server.h>
#include <esp_system.h>
#include <esp_log.h>
#include <esp_vfs.h>
#include <cJSON.h>
#include <esp_heap_caps.h>

#include "manager.h"
#include "storage.h"
#include "http_handlers.h"

static const char *TAG = "rest_handlers";

static char* get_request_buffer(httpd_req_t *req, esp_err_t* err){
    int total_len = req->content_len;
    int cur_len = 0;
    char *buf = ((rest_server_context_t *)(req->user_ctx))->scratch;
    int received = 0;
    if (total_len >= SCRATCH_BUFSIZE) {
        /* Respond with 500 Internal Server Error */
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "content too long");
        *err = ESP_FAIL;
        return NULL;
    }
    while (cur_len < total_len) {
        received = httpd_req_recv(req, buf + cur_len, total_len);
        if (received <= 0) {
            /* Respond with 500 Internal Server Error */
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to post control value");
            *err = ESP_FAIL;
            return NULL;
        }
        cur_len += received;
    }
    buf[total_len] = '\0';
    *err = ESP_OK;
    return buf;
}

static void response(httpd_req_t *req, bool status, const char* message) {
    httpd_resp_set_type(req, "application/json");
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", message);
    cJSON_AddBoolToObject(root, "status", true);
    const char *json_string = cJSON_Print(root);
    httpd_resp_sendstr(req, json_string);
    free((void *)json_string);
    cJSON_Delete(root);
}

esp_err_t wifi_access_points_get_handler(httpd_req_t *req){
    esp_err_t ret = ESP_OK;

	/* if we can get the mutex, write the last version of the AP list */
    if(wifi_manager_lock_json_buffer(( TickType_t ) 10)){
        httpd_resp_set_type(req, "application/json");
        char* ap_buf = wifi_manager_get_ap_list_json();
        httpd_resp_sendstr(req, ap_buf);
        wifi_manager_unlock_json_buffer();
    }else {
        const char* err_msg = "http_server_netconn_serve: GET /ap.json failed to obtain mutex";
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, err_msg);
        ESP_LOGE(TAG, "%s", err_msg);
        ret = ESP_FAIL;
    }

    /* request a wifi scan */
    wifi_manager_scan_async();

    return ret;
}

esp_err_t esp32_setup_wifi_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    const char* wifi_json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_wifi_config(wifi_json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wifi configure save fail");
        return ret;
    }

    response(req, true, "Wifi configure save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_wifi_ca_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    const char* json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_wifi_ca(json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wifi CA save fail");
        return ret;
    }

    response(req, true, "Wifi CA save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_wifi_crt_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    const char* json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_wifi_crt(json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wifi CRT save fail");
        return ret;
    }

    response(req, true, "Wifi CRT save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_wifi_key_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    const char* json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_wifi_key(json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Wifi KEY save fail");
        return ret;
    }

    response(req, true, "Wifi KEY save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_ipv4_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    char* ipv4_json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_ipv4_config(ipv4_json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "IPv4 configure save fail");
        return ret;
    }

    response(req, true, "IPv4 configure save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_http_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    char* http_json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_http_config(http_json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTTP configure save fail");
        return ret;
    }

    response(req, true, "HTTP configure save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_http_ca_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    char* http_json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_http_ca(http_json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTTP CA save fail");
        return ret;
    }

    response(req, true, "HTTP CA save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_http_crt_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    char* http_json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_http_crt(http_json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTTP CRT save fail");
        return ret;
    }

    response(req, true, "HTTP CRT save success!");

    return ESP_OK;        
}

esp_err_t esp32_setup_http_key_post_handler(httpd_req_t *req) {
    esp_err_t ret;
    char* http_json_buf = get_request_buffer(req, &ret);
    if( ret != ESP_OK ){
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Request data parse error");
        return ret;
    }

    ret = wifi_manager_save_http_key(http_json_buf);
    if( ret != ESP_OK ){
        ESP_LOGE(TAG, "File write error: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "HTTP KEY save fail");
        return ret;
    }

    response(req, true, "HTTP KEY save success!");

    return ESP_OK;        
}


esp_err_t esp32_sconnect_get_handler(httpd_req_t *req) {

    response(req, true, "ESP32 Connecting...");

    wifi_manager_connect_async();

    return ESP_OK;        
}