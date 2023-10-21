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

@see https://github.com/vivask/esp32-wifi-manager
*/
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <esp_ota_ops.h>
#include <esp_http_client.h>
#include <esp_https_ota.h>
#include <cJSON.h>

#include "manager.h"
#include "flash.h"
#include "ota.h"

#define HASH_LEN                32

#ifdef CONFIG_USE_OTA
#define DEFAULT_CACHE_SIZE      CONFIG_OTA_TASK_CACHE_SIZE
#define OTA_RECV_TIMEOUT_MS     5000
#define OTA_RESTART_TIMER_MS    2000
#endif

static const char *TAG = "OTA";

#ifdef CONFIG_USE_OTA
/* @brief firmware file name */
static char* file = NULL;

/* @brief callback ota finish function pointer */
void (*cb_finish_ptr)(bool) = NULL;

bool ota(const char* data, int size) {
    bool ret = false;
    cJSON *root = cJSON_Parse(data);
    esp32_config_t* wifi_config = wifi_manager_get_config();
    cJSON* item = cJSON_GetObjectItem(root, wifi_config->esp_json_key);
    if (item && item->valuestring) {
        size_t len = strlen(item->valuestring);
        if (len) {
            if (file) {
                free(file);
            }else {
                file = (char*)malloc(len+1);
                memcpy((void*)file, (void*)item->valuestring, len);
                file[len] = '\0';
                ESP_LOGI(TAG, "Firmware file: %s", file);
            }
            ret = true;
        }
    }
    cJSON_Delete(root);
    return ret;
}

static esp_err_t _http_event_handler(esp_http_client_event_t *evt)
{
    switch (evt->event_id) {
    case HTTP_EVENT_ERROR:
        ESP_LOGD(TAG, "HTTP_EVENT_ERROR");
        break;
    case HTTP_EVENT_ON_CONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_CONNECTED");
        break;
    case HTTP_EVENT_HEADER_SENT:
        ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
        break;
    case HTTP_EVENT_ON_HEADER:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
        break;
    case HTTP_EVENT_ON_DATA:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
        break;
    case HTTP_EVENT_ON_FINISH:
        ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
        break;
    case HTTP_EVENT_DISCONNECTED:
        ESP_LOGD(TAG, "HTTP_EVENT_DISCONNECTED");
        break;
    }
    return ESP_OK;
}

static void firmware_upgrade_task(void  *pvParameter){
    if (!file) {
        ESP_LOGE(TAG, "Undefined firmware file");
        abort();
    }

    esp32_config_t* wifi_config = wifi_manager_get_config();
    
    esp_http_client_config_t config = {
        .event_handler = _http_event_handler,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .host = wifi_config->server_address,
        .port = wifi_config->server_port,
        .timeout_ms = OTA_RECV_TIMEOUT_MS,
        .url = file,
        .keep_alive_enable = true
    };

    if (strcmp(wifi_config->server_auth, "ssl") == 0) {
        config.cert_pem = wifi_config->client_ca;
        config.cert_len = strlen(wifi_config->client_ca)+1;
        config.client_cert_pem = wifi_config->client_crt;
        config.client_cert_len = strlen(wifi_config->client_crt)+1;
        config.client_key_pem = wifi_config->client_key;
        config.client_key_len = strlen(wifi_config->client_key)+1;  
#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
        config.skip_cert_common_name_check = true;
#endif
    }

    esp_err_t err = esp_https_ota(&config);

    if (err == ESP_OK) {
        FLASH_LOGI("Firmware upgrade success");
        if(cb_finish_ptr) cb_finish_ptr(false);
        /* restart after OTA_RESTART_TIMER_MS */
        delayed_reboot(OTA_RESTART_TIMER_MS);
    } else {
        FLASH_LOGE("Firmware upgrade failed");
        if(cb_finish_ptr) cb_finish_ptr(true);
    }

    vTaskDelete( NULL );
}

void ota_set_finish_callback( void (*func_ptr)(bool) ){

	if(cb_finish_ptr == NULL && cb_finish_ptr != func_ptr){
		cb_finish_ptr = func_ptr;
	}else {
        ESP_LOGW(TAG, "The callback ota finish is already bound to another function");
    }
}

void firmware_upgrade( void (*func_ptr)(bool) ){
    ota_set_finish_callback(func_ptr);
    xTaskCreate(&firmware_upgrade_task, "firmware_upgrade_task", DEFAULT_CACHE_SIZE, NULL, WIFI_MANAGER_TASK_PRIORITY+1, NULL);
}
#endif

static void print_sha256(const uint8_t *image_hash, const char *label)
{
    char hash_print[HASH_LEN * 2 + 1];
    hash_print[HASH_LEN * 2] = 0;
    for (int i = 0; i < HASH_LEN; ++i) {
        sprintf(&hash_print[i * 2], "%02x", image_hash[i]);
    }
    ESP_LOGI(TAG, "%s %s", label, hash_print);
}

void get_sha256_of_partitions(void)
{
    uint8_t sha_256[HASH_LEN] = { 0 };
    esp_partition_t partition;

    // get sha256 digest for bootloader
    partition.address   = ESP_BOOTLOADER_OFFSET;
    partition.size      = ESP_PARTITION_TABLE_OFFSET;
    partition.type      = ESP_PARTITION_TYPE_APP;
    esp_partition_get_sha256(&partition, sha_256);
    print_sha256(sha_256, "SHA-256 for bootloader: ");

    // get sha256 digest for running partition
    esp_partition_get_sha256(esp_ota_get_running_partition(), sha_256);
    print_sha256(sha_256, "SHA-256 for current firmware: ");
}