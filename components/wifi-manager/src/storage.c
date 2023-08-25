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
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <esp_log.h>
#include <cJSON.h>
#include <esp_wpa2.h>

#include "flashrw.h"
#include "manager.h"
#include "flash.h"
#include "storage.h"

#define STORE_BASE_PATH	"/"CONFIG_WEB_STORE_MOUNT_POINT   

#define WIFI_CONFIG_FILE STORE_BASE_PATH "/wifi_config.json"
#define WIFI_CA_FILE STORE_BASE_PATH "/wifi_ca.json"
#define WIFI_CRT_FILE STORE_BASE_PATH "/wifi_crt.json"
#define WIFI_KEY_FILE STORE_BASE_PATH "/wifi_key.json"

#define IPV4_CONFIG_FILE STORE_BASE_PATH "/ipv4_config.json"

#define HTTP_CONFIG_FILE STORE_BASE_PATH "/http_config.json"
#define HTTP_CA_FILE STORE_BASE_PATH "/http_ca.json"
#define HTTP_CRT_FILE STORE_BASE_PATH "/http_crt.json"
#define HTTP_KEY_FILE STORE_BASE_PATH "/http_key.json"

static const char TAG[] = "wifi_store";

static char* copy_json_item(cJSON* json, const char* item_name) {
	cJSON* item = cJSON_GetObjectItem(json, item_name);
	if (!item) {
		ESP_LOGW(TAG, "Json object [%s] not found", item_name);
		FLASH_LOGW("Json object [%s] not found", item_name);
		wifi_manager_remove_config();
	}else {
		size_t len = (!item->valuestring) ? 0 : strlen(item->valuestring)*sizeof(char) + 2;
		if (len > 2) {
			char* dest = (char*)malloc(len);
			memcpy((void*)dest, (void*)item->valuestring, len);
			return dest;
		}
	}
	return NULL;
}

static void wifi_json_string_to_config(esp32_config_t* config, const char* wifi_string) {
	cJSON *wifi_json = cJSON_Parse(wifi_string);
	config->wifi_ssid = copy_json_item(wifi_json, "wifi_ssid");
	config->wifi_wpa = copy_json_item(wifi_json, "wifi_wpa");
	config->wifi_identity = copy_json_item(wifi_json, "wifi_identity");
	config->wifi_username = copy_json_item(wifi_json, "wifi_username");
	config->wifi_password = copy_json_item(wifi_json, "wifi_password");
	config->wifi_auth = copy_json_item(wifi_json, "wifi_auth");
	config->wifi_inner = copy_json_item(wifi_json, "wifi_inner");
	cJSON_Delete(wifi_json);
}

static void restore_wifi_ca(esp32_config_t* config, const char* json_string) {
	cJSON *json = cJSON_Parse(json_string);
	config->wifi_ca = copy_json_item(json, "wifi_ca");
	cJSON_Delete(json);
}

static void restore_wifi_crt(esp32_config_t* config, const char* json_string) {
	cJSON *json = cJSON_Parse(json_string);
	config->wifi_crt = copy_json_item(json, "wifi_crt");
	cJSON_Delete(json);
}

static void restore_wifi_key(esp32_config_t* config, const char* json_string) {
	cJSON *json = cJSON_Parse(json_string);
	config->wifi_key = copy_json_item(json, "wifi_key");
	cJSON_Delete(json);
}

static void ipv4_json_string_to_config(esp32_config_t* config, const char* ipv4_string) {
	cJSON *ipv4_json = cJSON_Parse(ipv4_string);
	config->ipv4_method = copy_json_item(ipv4_json, "ipv4_method");
	config->ipv4_address = copy_json_item(ipv4_json, "ipv4_address");
	config->ipv4_mask = copy_json_item(ipv4_json, "ipv4_mask");
	config->ipv4_gate = copy_json_item(ipv4_json, "ipv4_gate");
	config->ipv4_dns1 = copy_json_item(ipv4_json, "ipv4_dns1");
	config->ipv4_dns2 = copy_json_item(ipv4_json, "ipv4_dns2");
	config->ipv4_zone = copy_json_item(ipv4_json, "ipv4_zone");
	config->ipv4_ntp = copy_json_item(ipv4_json, "ipv4_ntp");
	cJSON_Delete(ipv4_json);
}

static void http_json_string_to_config(esp32_config_t* config, const char* http_string) {
	cJSON *http_json = cJSON_Parse(http_string);
	config->server_address = copy_json_item(http_json, "server_address");
	config->server_port = cJSON_GetObjectItem(http_json, "server_port")->valueint;
	config->server_api = copy_json_item(http_json, "server_api");
	config->ota_api = copy_json_item(http_json, "ota_api");
	config->server_auth = copy_json_item(http_json, "server_auth");
	config->client_username = copy_json_item(http_json, "client_username");
	config->client_password = copy_json_item(http_json, "client_password");
	cJSON_Delete(http_json);
}

static void restore_http_ca(esp32_config_t* config, const char* json_string) {
	cJSON *json = cJSON_Parse(json_string);
	config->client_ca = copy_json_item(json, "client_ca");
	cJSON_Delete(json);
}

static void restore_http_crt(esp32_config_t* config, const char* json_string) {
	cJSON *json = cJSON_Parse(json_string);
	config->client_crt = copy_json_item(json, "client_crt");
	cJSON_Delete(json);
}

static void restore_http_key(esp32_config_t* config, const char* json_string) {
	cJSON *json = cJSON_Parse(json_string);
	config->client_key = copy_json_item(json, "client_key");
	cJSON_Delete(json);
}

bool wifi_manager_fetch_wifi_sta_config(esp32_config_t* esp_23config, 
										wifi_config_t* wifi_manager_sta_config, 
										wifi_settings_t* wifi_settings, 
										wifi_auth_mode_t* authmode){
	*authmode = WIFI_AUTH_OPEN;

	if(esp_23config == NULL){
		esp_23config = (esp32_config_t*)malloc(sizeof(esp32_config_t));
	}
	memset(esp_23config, 0x00, sizeof(wifi_config_t));

	if (wifi_manager_fetch_config(esp_23config) != ESP_OK) {
		return false;
	}

	if(wifi_manager_sta_config == NULL){
		wifi_manager_sta_config = (wifi_config_t*)malloc(sizeof(wifi_config_t));
	}
	memset(wifi_manager_sta_config, 0x00, sizeof(wifi_config_t));

	memcpy(wifi_manager_sta_config->sta.ssid, esp_23config->wifi_ssid, strlen(esp_23config->wifi_ssid));

	*authmode = (strcmp(esp_23config->wifi_wpa, "enterprise") == 0) ? WIFI_AUTH_WPA2_ENTERPRISE : WIFI_AUTH_WPA2_PSK;

	if(*authmode == WIFI_AUTH_WPA2_PSK) {
		memcpy(wifi_manager_sta_config->sta.password, esp_23config->wifi_password, strlen(esp_23config->wifi_password));
	} else {
		// Restore eap configure
		wifi_manager_set_eap_config();
	}

	// Restore ipv4 configure
	wifi_manager_set_ipv4_config();

	ESP_LOGI(TAG, "Configuration restore for SSID: %s", wifi_manager_sta_config->sta.ssid);

	return true;
}

esp_err_t wifi_manager_save_wifi_config(const char* json_string) {
	return save_flash_json_data(json_string, WIFI_CONFIG_FILE);
}

esp_err_t wifi_manager_save_wifi_ca(const char* json_string) {
	return save_flash_json_data(json_string, WIFI_CA_FILE);
}

esp_err_t wifi_manager_save_wifi_crt(const char* json_string) {
	return save_flash_json_data(json_string, WIFI_CRT_FILE);
}

esp_err_t wifi_manager_save_wifi_key(const char* json_string) {
	return save_flash_json_data(json_string, WIFI_KEY_FILE);
}

esp_err_t wifi_manager_save_ipv4_config(const char* json_string) {
	return save_flash_json_data(json_string, IPV4_CONFIG_FILE);
}

esp_err_t wifi_manager_save_http_config(const char* json_string) {
	return save_flash_json_data(json_string, HTTP_CONFIG_FILE);
}

esp_err_t wifi_manager_save_http_ca(const char* json_string) {
	return save_flash_json_data(json_string, HTTP_CA_FILE);
}

esp_err_t wifi_manager_save_http_crt(const char* json_string) {
	return save_flash_json_data(json_string, HTTP_CRT_FILE);
}

esp_err_t wifi_manager_save_http_key(const char* json_string) {
	return save_flash_json_data(json_string, HTTP_KEY_FILE);
}

esp_err_t wifi_manager_fetch_config(esp32_config_t* config)	{
	// Restore wifi configure from flash
	const char* wifi_string = read_flash_json_data(WIFI_CONFIG_FILE);
	if (!wifi_string) {
		return ESP_FAIL;
	}
	wifi_json_string_to_config(config, wifi_string);
	ESP_LOGI(TAG, "Wifi config restored");
	free((void* ) wifi_string);

	// Restore wifi CA file from flash
	const char* wifi_ca_string = read_flash_json_data(WIFI_CA_FILE);
	if (!wifi_ca_string) {
		return ESP_FAIL;
	}
	restore_wifi_ca(config, wifi_ca_string);
	ESP_LOGI(TAG, "Wifi CA file restored");
	free((void* ) wifi_ca_string);

	// Restore wifi CRT file from flash
	const char* wifi_crt_string = read_flash_json_data(WIFI_CRT_FILE);
	if (!wifi_crt_string) {
		return ESP_FAIL;
	}
	restore_wifi_crt(config, wifi_crt_string);
	ESP_LOGI(TAG, "Wifi CRT file restored");
	free((void* ) wifi_crt_string);

	// Restore wifi KEY file from flash
	const char* wifi_key_string = read_flash_json_data(WIFI_KEY_FILE);
	if (!wifi_key_string) {
		return ESP_FAIL;
	}
	restore_wifi_key(config, wifi_key_string);
	ESP_LOGI(TAG, "Wifi KEY file restored");
	free((void* ) wifi_key_string);

	// Restore IPv4 configure from flash
	const char* ipv4_string = read_flash_json_data(IPV4_CONFIG_FILE);
	if (!ipv4_string) {
		return ESP_FAIL;
	}
	ipv4_json_string_to_config(config, ipv4_string);
	ESP_LOGI(TAG, "Ipv4 config restored");
	free((void* ) ipv4_string);

	// Restore HTTP configure from flash
	const char* http_string = read_flash_json_data(HTTP_CONFIG_FILE);
	if (!http_string) {
		return ESP_FAIL;
	}
	http_json_string_to_config(config, http_string);
	ESP_LOGI(TAG, "HTTP config restored");
	free((void* ) http_string);

	// Restore HTTP CA file from flash
	const char* http_ca_string = read_flash_json_data(HTTP_CA_FILE);
	if (!http_ca_string) {
		return ESP_FAIL;
	}
	restore_http_ca(config, http_ca_string);
	ESP_LOGI(TAG, "HTTP CA file restored");
	free((void* ) http_ca_string);

	// Restore HTTP CRT file from flash
	const char* http_crt_string = read_flash_json_data(HTTP_CRT_FILE);
	if (!http_crt_string) {
		return ESP_FAIL;
	}
	restore_http_crt(config, http_crt_string);
	ESP_LOGI(TAG, "HTTP CRT file restored");
	free((void* ) http_crt_string);

	// Restore HTTP KEY file from flash
	const char* http_key_string = read_flash_json_data(HTTP_KEY_FILE);
	if (!http_key_string) {
		return ESP_FAIL;
	}
	restore_http_key(config, http_key_string);
	ESP_LOGI(TAG, "HTTP KEY file restored");
	free((void* ) http_key_string);

	return ESP_OK; 
}

void wifi_manager_remove_config() {
	clear_flash_file(WIFI_CONFIG_FILE);
	clear_flash_file(WIFI_CA_FILE);
	clear_flash_file(WIFI_CRT_FILE);
	clear_flash_file(WIFI_KEY_FILE);
	clear_flash_file(IPV4_CONFIG_FILE);
	clear_flash_file(HTTP_CONFIG_FILE);
	clear_flash_file(HTTP_CA_FILE);
	clear_flash_file(HTTP_CRT_FILE);
	clear_flash_file(HTTP_KEY_FILE);
}