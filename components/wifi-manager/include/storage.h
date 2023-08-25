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
#pragma once

#include <stdint.h>
#include <esp_wifi.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EAP_CERTIFICATE_BUFFER_SIZE 	CONFIG_CERTIFICATE_BUFFER_SIZE
#define HTTPD_CERTIFICATE_BUFFER_SIZE 	CONFIG_CERTIFICATE_BUFFER_SIZE

typedef enum _wpa_authentication_t {
	WPA_TLS = 1,
	WPA_TTLS = 2,
	WPA_PEAP = 3
}wpa_authentication_t;

/**
 * @brief defines TTLS phase 2 authentication method
 */
typedef enum _ttls_authentication_t {
	MSCHAPV2 = 1,
	MSCHAP = 2,
	PAP = 3,
	CHAP = 4
}ttls_authentication_t;

/**
 * @brief Defines the maximum size of a SSID name. 32 is IEEE standard.
 * @warning limit is also hard coded in wifi_config_t. Never extend this value.
 */
#define MAX_SSID_SIZE						32

/**
 * @brief Defines the maximum size of a WPA2 passkey. 64 is IEEE standard.
 * @warning limit is also hard coded in wifi_config_t. Never extend this value.
 */
#define MAX_PASSWORD_SIZE					64

/**
 * The actual WiFi settings in use
 */
typedef struct _wifi_settings_t{
	uint8_t ap_ssid[MAX_SSID_SIZE];
	uint8_t ap_pwd[MAX_PASSWORD_SIZE];
	uint8_t ap_channel;
	uint8_t ap_ssid_hidden;
	wifi_bandwidth_t ap_bandwidth;
	bool sta_only;
	wifi_ps_type_t sta_power_save;
	bool sta_static_ip;
	esp_netif_ip_info_t sta_static_ip_config;
}wifi_settings_t;

//extern struct wifi_settings_t wifi_settings;

/**
 * @brief Structure used to store pap configuration.
*/
typedef struct _pap_config_t{
	uint8_t					ssid[32];
	wifi_auth_mode_t 		authmode;
	uint8_t					password[64];
	uint8_t					setup_mode;
}pap_config_t;


/**
 * @brief Structure used to store eap configuration.
*/
typedef struct _eap_config_t{
	uint8_t					ssid[32];
	wifi_auth_mode_t 		authmode;
	wpa_authentication_t  	authentication;
	ttls_authentication_t	ttlsauth;
	uint8_t					identity[32];
	uint8_t					username[32];
	uint8_t					password[64];
	uint8_t					ca[EAP_CERTIFICATE_BUFFER_SIZE];
	uint8_t					crt[EAP_CERTIFICATE_BUFFER_SIZE];
	uint8_t					key[EAP_CERTIFICATE_BUFFER_SIZE];
}eap_config_t;

/**
 * @brief Structure used to store IpV4 configuration.
*/
typedef struct _ipv4_config_t {
	uint8_t		address[16];
	uint8_t		mask[16];
	uint8_t		gate[16];
	uint8_t		dns[16];
	uint8_t		ntp[32];
	char		timezone[32];
} ipv4_config_t;

typedef enum _http_client_login_t {
	NO = 1,
	HTTP = 2,
	HTTPS = 3,
	CERTS = 4
}http_client_login_t;

/**
 * @brief Structure used to store esp32 configuration.
*/
typedef struct _esp32_config_t {
    char* wifi_ssid;
    char* wifi_wpa;
    char* wifi_identity;
    char* wifi_username;
    char* wifi_password;
    char* wifi_auth;
    char* wifi_inner;
    char* wifi_ca;
    char* wifi_crt;
    char* wifi_key;
    char* ipv4_method;
    char* ipv4_address;
    char* ipv4_mask;
    char* ipv4_gate;
    char* ipv4_dns1;
    char* ipv4_dns2;
    char* ipv4_zone;
    char* ipv4_ntp;
    char* server_address;
    int server_port;
    char* server_api;
    char* server_auth;
    char* client_username;
    char* client_password;
    char* client_ca;
    char* client_crt;
    char* client_key;
    char* ota_api;
}esp32_config_t;


/**
 * @brief fetch a previously STA wifi config in the flash ram storage.
 * @return true if a previously saved config was found, false otherwise.
 */
bool wifi_manager_fetch_wifi_sta_config(esp32_config_t* esp_23config, wifi_config_t* wifi_manager_sta_config, wifi_settings_t* wifi_settings, wifi_auth_mode_t* authmode);

/**
 * @brief Get STA wifi config.
 */
wifi_config_t* wifi_manager_get_wifi_sta_config();

/**
 * @brief saves the current wifi config to flash ram storage.
 */
esp_err_t wifi_manager_save_wifi_config(const char* json_string);

/**
 * @brief saves ca wifi file config to flash ram storage.
 */
esp_err_t wifi_manager_save_wifi_ca(const char* json_string);

/**
 * @brief saves crt wifi file config to flash ram storage.
 */
esp_err_t wifi_manager_save_wifi_crt(const char* json_string);

/**
 * @brief saves key wifi file config to flash ram storage.
 */
esp_err_t wifi_manager_save_wifi_key(const char* json_string);

/**
 * @brief saves the current ipv4 config to flash ram storage.
 */
esp_err_t wifi_manager_save_ipv4_config(const char* json_string);

/**
 * @brief saves the current http config to flash ram storage.
 */
esp_err_t wifi_manager_save_http_config(const char* json_string);

/**
 * @brief saves http ca file config to flash ram storage.
 */
esp_err_t wifi_manager_save_http_ca(const char* json_string);

/**
 * @brief saves http crt file config to flash ram storage.
 */
esp_err_t wifi_manager_save_http_crt(const char* json_string);

/**
 * @brief saves http key file config to flash ram storage.
 */
esp_err_t wifi_manager_save_http_key(const char* json_string);

/**
 * @brief fetch a previously config in the flash ram storage.
 */
esp_err_t wifi_manager_fetch_config(esp32_config_t* config);

esp32_config_t* wifi_manager_get_config();

/**
 * @brief remove config from the flash ram storage.
 */
void wifi_manager_remove_config();

/**
 * @brief free configure
 */
void free_esp32_config(esp32_config_t* config);


#ifdef __cplusplus
}

#endif

/**@}*/

