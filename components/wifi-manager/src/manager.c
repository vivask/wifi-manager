/**
 * original author:  Tony Pottier
 * modification: 	 Viktar Vasiuk

   ----------------------------------------------------------------------
    Copyright (C) Viktar Vasiuk, 2023
   	Copyright (C) Tony Pottier, 2007
    
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

@see https://github.com/tonyp7/esp32-wifi-manager
@see https://github.com/vivask/esp32-wifi-manager
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <freertos/event_groups.h>
#include <freertos/timers.h>
#include <esp_wifi.h>
#include <esp_wpa2.h>
#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi_types.h>
#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/api.h>
#include <lwip/err.h>
#include <lwip/netdb.h>
#include <lwip/ip4_addr.h>
#include <esp_http_server.h>
#include <cJSON.h>

// #include "spiffs.h"
#include "flash.h"
#include "dns_server.h"
#include "http_server.h"
#include "ntp_client.h"
#include "storage.h"
#include "manager.h"

//#define SETUP_MODE
//#define DEBUG_MODE

/* --- PRINTF_BYTE_TO_BINARY macro's --- */
#ifdef DEBUG_MODE
#define PRINTF_BINARY_PATTERN_INT8 "%c%c%c%c%c%c%c%c"
#define PRINTF_BYTE_TO_BINARY_INT8(i)    \
    (((i) & 0x80ll) ? '1' : '0'), \
    (((i) & 0x40ll) ? '1' : '0'), \
    (((i) & 0x20ll) ? '1' : '0'), \
    (((i) & 0x10ll) ? '1' : '0'), \
    (((i) & 0x08ll) ? '1' : '0'), \
    (((i) & 0x04ll) ? '1' : '0'), \
    (((i) & 0x02ll) ? '1' : '0'), \
    (((i) & 0x01ll) ? '1' : '0')

#define PRINTF_BINARY_PATTERN_INT16 \
    PRINTF_BINARY_PATTERN_INT8              PRINTF_BINARY_PATTERN_INT8
#define PRINTF_BYTE_TO_BINARY_INT16(i) \
    PRINTF_BYTE_TO_BINARY_INT8((i) >> 8),   PRINTF_BYTE_TO_BINARY_INT8(i)
#define PRINTF_BINARY_PATTERN_INT32 \
    PRINTF_BINARY_PATTERN_INT16             PRINTF_BINARY_PATTERN_INT16
#define PRINTF_BYTE_TO_BINARY_INT32(i) \
    PRINTF_BYTE_TO_BINARY_INT16((i) >> 16), PRINTF_BYTE_TO_BINARY_INT16(i)
#define PRINTF_BINARY_PATTERN_INT64    \
    PRINTF_BINARY_PATTERN_INT32             PRINTF_BINARY_PATTERN_INT32
#define PRINTF_BYTE_TO_BINARY_INT64(i) \
    PRINTF_BYTE_TO_BINARY_INT32((i) >> 32), PRINTF_BYTE_TO_BINARY_INT32(i)
#endif
/* --- end macros --- */

#define STORE_BASE_PATH             "/"CONFIG_WEB_STORE_MOUNT_POINT   
#define DEFAULT_CACHE_SIZE 			CONFIG_WIFI_MANAGER_TASK_CACHE_SIZE
#define DEFAULT_TIMEZONE 			CONFIG_TIMEZONE


/* objects used to manipulate the main queue of events */
QueueHandle_t wifi_manager_queue;

/* @brief software timer to wait between each connection retry.
 * There is no point hogging a hardware timer for a functionality like this which only needs to be 'accurate enough' */
TimerHandle_t wifi_manager_retry_timer = NULL;

/* @brief software timer that will trigger shutdown of the AP after a succesful STA connection
 * There is no point hogging a hardware timer for a functionality like this which only needs to be 'accurate enough' */
TimerHandle_t wifi_manager_restart_timer = NULL;

SemaphoreHandle_t wifi_manager_json_mutex = NULL;
SemaphoreHandle_t wifi_manager_sta_ip_mutex = NULL;

char *wifi_manager_sta_ip = NULL;
uint16_t ap_num = MAX_AP_NUM;
wifi_ap_record_t *accessp_records;

char *accessp_json = NULL;
char *ip_info_json = NULL;

esp32_config_t* wifi_manager_config = NULL;
wifi_config_t* wifi_manager_sta_config = NULL;

/* @brief Array of callback function pointers */
void (**cb_ptr_arr)(void*) = NULL;

/* @brief tag used for ESP serial console messages */
static const char TAG[] = "wifi_manager";

/* @brief task handle for the main wifi_manager task */
static TaskHandle_t task_wifi_manager = NULL;

/* @brief netif object for the STATION */
static esp_netif_t* esp_netif_sta = NULL;

/* @brief netif object for the ACCESS POINT */
static esp_netif_t* esp_netif_ap = NULL;

/**
 * The actual WiFi settings in use
 */
static wifi_settings_t wifi_settings = {
	.ap_ssid = DEFAULT_AP_SSID,
	.ap_pwd = DEFAULT_AP_PASSWORD,
	.ap_channel = DEFAULT_AP_CHANNEL,
	.ap_ssid_hidden = DEFAULT_AP_SSID_HIDDEN,
	.ap_bandwidth = DEFAULT_AP_BANDWIDTH,
	.sta_only = DEFAULT_STA_ONLY,
	.sta_power_save = DEFAULT_STA_POWER_SAVE,
	.sta_static_ip = 0,
};

//const char wifi_manager_nvs_namespace[] = "espwifimgr";

static EventGroupHandle_t wifi_manager_event_group;

/* @brief indicate that the ESP32 is currently connected. */
const int WIFI_MANAGER_WIFI_CONNECTED_BIT = BIT0;

const int WIFI_MANAGER_AP_STA_CONNECTED_BIT = BIT1;

/* @brief Set automatically once the SoftAP is started */
const int WIFI_MANAGER_AP_STARTED_BIT = BIT2;

/* @brief When set, means a client requested to connect to an access point.*/
const int WIFI_MANAGER_REQUEST_STA_CONNECT_BIT = BIT3;

/* @brief This bit is set automatically as soon as a connection was lost */
const int WIFI_MANAGER_STA_DISCONNECT_BIT = BIT4;

/* @brief When set, means the wifi manager attempts to restore a previously saved connection at startup. */
const int WIFI_MANAGER_REQUEST_RESTORE_STA_BIT = BIT5;

/* @brief When set, means a client requested to disconnect from currently connected AP. */
const int WIFI_MANAGER_REQUEST_WIFI_DISCONNECT_BIT = BIT6;

/* @brief When set, means a scan is in progress */
const int WIFI_MANAGER_SCAN_BIT = BIT7;

/* @brief When set, means user requested for a disconnect */
const int WIFI_MANAGER_REQUEST_DISCONNECT_BIT = BIT8;

/* @brief When set, means .*/
const int WIFI_MANAGER_CONFIGURE_MODE_BIT = BIT9;

void wifi_manager_timer_retry_cb( TimerHandle_t xTimer ){

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );

	/* Attempt to reconnect */
	ESP_LOGI(TAG, "Retry Timer Tick! Sending ORDER_CONNECT_STA with reason CONNECTION_REQUEST_AUTO_RECONNECT");
	wifi_manager_send_message(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_AUTO_RECONNECT);
}

void wifi_manager_timer_restart_cb( TimerHandle_t xTimer){

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );


	/* Attempt to reboot */
	ESP_LOGW(TAG, "Attempt to reboot");
	esp_restart();
}

void wifi_manager_scan_async(){
	wifi_manager_send_message(WM_ORDER_START_WIFI_SCAN, NULL);
}

void wifi_manager_disconnect_async(){
	wifi_manager_send_message(WM_ORDER_DISCONNECT_STA, NULL);
}

void wifi_manager_start(bool ap_mode){

	/* disable the default wifi logging */
	esp_log_level_set("wifi", ESP_LOG_NONE);

	/* initialize flash memory */
	// nvs_flash_init();


	/* memory allocation */
	wifi_manager_queue = xQueueCreate( 3, sizeof( queue_message) );
	wifi_manager_json_mutex = xSemaphoreCreateMutex();
	accessp_records = (wifi_ap_record_t*)malloc(sizeof(wifi_ap_record_t) * MAX_AP_NUM);
	accessp_json = (char*)malloc(MAX_AP_NUM * JSON_ONE_APP_SIZE + 4); /* 4 bytes for json encapsulation of "[\n" and "]\0" */
	wifi_manager_clear_access_points_json();
	ip_info_json = (char*)malloc(sizeof(char) * JSON_IP_INFO_SIZE);
	wifi_manager_clear_ip_info_json();

	wifi_manager_config = (esp32_config_t*)malloc(sizeof(esp32_config_t));
	memset(wifi_manager_config, 0x00, sizeof(esp32_config_t));

	wifi_manager_sta_config = (wifi_config_t*)malloc(sizeof(wifi_config_t));
	memset(wifi_manager_sta_config, 0x00, sizeof(wifi_config_t));

	memset(&wifi_settings.sta_static_ip_config, 0x00, sizeof(esp_netif_ip_info_t));

	cb_ptr_arr = malloc(sizeof(void (*)(void*)) * WM_MESSAGE_CODE_COUNT);
	for(int i=0; i<WM_MESSAGE_CODE_COUNT; i++){
		cb_ptr_arr[i] = NULL;
	}

	wifi_manager_sta_ip_mutex = xSemaphoreCreateMutex();
	wifi_manager_sta_ip = (char*)malloc(sizeof(char) * IP4ADDR_STRLEN_MAX);
	wifi_manager_safe_update_sta_ip_string((uint32_t)0);
	wifi_manager_event_group = xEventGroupCreate();

	/* create timer for to keep track of retries */
	wifi_manager_retry_timer = xTimerCreate( NULL, pdMS_TO_TICKS(WIFI_MANAGER_RETRY_TIMER), pdFALSE, ( void * ) 0, wifi_manager_timer_retry_cb);

	/* create timer for to keep track of AP shutdown */
	wifi_manager_restart_timer = xTimerCreate( NULL, pdMS_TO_TICKS(WIFI_MANAGER_RESTART_TIMER), pdFALSE, ( void * ) 0, wifi_manager_timer_restart_cb);

	/* setup mode on */
#ifdef SETUP_MODE 
	wifi_manager_remove_config();
#endif

	/* start wifi manager task */
	xTaskCreate(&wifi_manager, "wifi_manager", DEFAULT_CACHE_SIZE, NULL, WIFI_MANAGER_TASK_PRIORITY, &task_wifi_manager);	
}


void wifi_manager_clear_ip_info_json(){
	strcpy(ip_info_json, "{}\n");
}

void wifi_manager_generate_ip_info_json(update_reason_code_t update_reason_code, bool http_client_status) {
	wifi_config_t *config = wifi_manager_get_wifi_sta_config();
	if(config){
		memset(ip_info_json, 0x00, JSON_IP_INFO_SIZE);
		cJSON *root = cJSON_CreateObject();
		cJSON_AddStringToObject(root, "ssid", (char*)config->sta.ssid);
		if(update_reason_code == UPDATE_CONNECTION_OK){
			/* rest of the information is copied after the ssid */
			esp_netif_ip_info_t ip_info;
			ESP_ERROR_CHECK(esp_netif_get_ip_info(esp_netif_sta, &ip_info));

			char ip[IP4ADDR_STRLEN_MAX];
			char gw[IP4ADDR_STRLEN_MAX];
			char netmask[IP4ADDR_STRLEN_MAX];

			esp_ip4addr_ntoa(&ip_info.ip, ip, IP4ADDR_STRLEN_MAX);
			esp_ip4addr_ntoa(&ip_info.gw, gw, IP4ADDR_STRLEN_MAX);
			esp_ip4addr_ntoa(&ip_info.netmask, netmask, IP4ADDR_STRLEN_MAX);

			cJSON_AddStringToObject(root, "ip", ip);
			cJSON_AddStringToObject(root, "gw", gw);
			cJSON_AddStringToObject(root, "netmask", netmask);
			cJSON_AddNumberToObject(root, "urc", (int)update_reason_code);
			cJSON_AddNumberToObject(root, "httpc", http_client_status);
		}else {
			cJSON_AddStringToObject(root, "ip", "0");
			cJSON_AddStringToObject(root, "gw", "0");
			cJSON_AddStringToObject(root, "netmask", "0");
			cJSON_AddNumberToObject(root, "urc", (int)update_reason_code);
			cJSON_AddNumberToObject(root, "httpc", http_client_status);
		}
		const char* json_string = cJSON_Print(root);
		cJSON_Delete(root);
		strcpy(ip_info_json, json_string);
		free((void *)json_string);		
	}else {
		wifi_manager_clear_ip_info_json();
	}
}

void wifi_manager_clear_access_points_json(){
	strcpy(accessp_json, "[]\n");
}

void wifi_manager_generate_acess_points_json() {
	cJSON *items = cJSON_CreateArray();
	for(int i=0; i<ap_num;i++) {
		wifi_ap_record_t ap = accessp_records[i];
		cJSON *item = cJSON_CreateObject();
		cJSON_AddStringToObject(item, "ssid", (char*)ap.ssid);
		cJSON_AddNumberToObject(item, "chan", ap.primary);
		cJSON_AddNumberToObject(item, "rssi", ap.rssi);
		cJSON_AddNumberToObject(item, "auth", ap.authmode);
		cJSON_AddItemToArray(items, item);
	}
	const char* json_string = cJSON_Print(items);
	cJSON_Delete(items);
	strcpy(accessp_json, json_string);
	free((void *)json_string);
}

bool wifi_manager_lock_sta_ip_string(TickType_t xTicksToWait){
	if(wifi_manager_sta_ip_mutex){
		if( xSemaphoreTake( wifi_manager_sta_ip_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}
}

void wifi_manager_unlock_sta_ip_string(){
	xSemaphoreGive( wifi_manager_sta_ip_mutex );
}

void wifi_manager_safe_update_sta_ip_string(uint32_t ip){

	if(wifi_manager_lock_sta_ip_string(portMAX_DELAY)){

		esp_ip4_addr_t ip4;
		ip4.addr = ip;

		char str_ip[IP4ADDR_STRLEN_MAX];
		esp_ip4addr_ntoa(&ip4, str_ip, IP4ADDR_STRLEN_MAX);

		strcpy(wifi_manager_sta_ip, str_ip);

		ESP_LOGI(TAG, "Set STA IP String to: %s", wifi_manager_sta_ip);
		if(strcmp(wifi_manager_sta_ip, "0.0.0.0") != 0){
			FLASH_LOGI("Set STA IP Address to: %s", wifi_manager_sta_ip);	
		}

		wifi_manager_unlock_sta_ip_string();
	}
}

char* wifi_manager_get_sta_ip_string(){
	return wifi_manager_sta_ip;
}

bool wifi_manager_lock_json_buffer(TickType_t xTicksToWait){
	if(wifi_manager_json_mutex){
		if( xSemaphoreTake( wifi_manager_json_mutex, xTicksToWait ) == pdTRUE ) {
			return true;
		}
		else{
			return false;
		}
	}
	else{
		return false;
	}
}

void wifi_manager_unlock_json_buffer(){
	xSemaphoreGive( wifi_manager_json_mutex );
}

char* wifi_manager_get_ap_list_json(){
	return accessp_json;
}

/**
 * @brief Standard wifi event handler
 */
static void wifi_manager_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data){


	if (event_base == WIFI_EVENT){

		switch(event_id){

		case WIFI_EVENT_WIFI_READY:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_WIFI_READY");
			break;

		case WIFI_EVENT_SCAN_DONE:
			ESP_LOGD(TAG, "EVENT: WIFI_EVENT_SCAN_DONE");
	    	xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
			wifi_event_sta_scan_done_t* event_sta_scan_done = (wifi_event_sta_scan_done_t*)malloc(sizeof(wifi_event_sta_scan_done_t));
			*event_sta_scan_done = *((wifi_event_sta_scan_done_t*)event_data);
	    	wifi_manager_send_message(WM_EVENT_SCAN_DONE, event_sta_scan_done);
			break;

		case WIFI_EVENT_STA_START:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_STA_START");
			break;

		case WIFI_EVENT_STA_STOP:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_STA_STOP");
			break;

		case WIFI_EVENT_STA_CONNECTED:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_STA_CONNECTED");
			break;

		case WIFI_EVENT_STA_DISCONNECTED:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_STA_DISCONNECTED");

			wifi_event_sta_disconnected_t* wifi_event_sta_disconnected = (wifi_event_sta_disconnected_t*)malloc(sizeof(wifi_event_sta_disconnected_t));
			*wifi_event_sta_disconnected =  *( (wifi_event_sta_disconnected_t*)event_data );

			/* if a DISCONNECT message is posted while a scan is in progress this scan will NEVER end, causing scan to never work again. For this reason SCAN_BIT is cleared too */
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT | WIFI_MANAGER_SCAN_BIT);

			/* post disconnect event with reason code */
			wifi_manager_send_message(WM_EVENT_STA_DISCONNECTED, (void*)wifi_event_sta_disconnected );
			break;

		case WIFI_EVENT_STA_AUTHMODE_CHANGE:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_STA_AUTHMODE_CHANGE");
			break;

		case WIFI_EVENT_AP_START:
			ESP_LOGW(TAG, "WIFI_EVENT_AP_START");
			xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED_BIT);
			/* start the timer that will eventually shutdown the access point
				* We check first that it's actually running because in case of a boot and restore connection
				* the AP is not even started to begin with.
				*/
			TickType_t t = pdMS_TO_TICKS( WIFI_MANAGER_RESTART_TIMER );

			/* if for whatever reason user configured the shutdown timer to be less than 1 tick, the AP is stopped straight away */
			if(t > 0){
				ESP_LOGW(TAG, "TIMER RESTART ACTIVATED!");
				xTimerStart( wifi_manager_restart_timer, (TickType_t)0 );
			}
			else{
				wifi_manager_send_message(WM_ORDER_STOP_AP, (void*)NULL);
			}

			break;

		case WIFI_EVENT_AP_STOP:
			ESP_LOGW(TAG, "WIFI_EVENT_AP_STOP");
			xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_AP_STARTED_BIT);
			break;

		case WIFI_EVENT_AP_STACONNECTED:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_AP_STACONNECTED");
			break;

		case WIFI_EVENT_AP_STADISCONNECTED:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_AP_STADISCONNECTED");
			break;

		case WIFI_EVENT_AP_PROBEREQRECVED:
			ESP_LOGI(TAG, "EVENT: WIFI_EVENT_AP_PROBEREQRECVED");
			break;

		} /* end switch */
	}
	else if(event_base == IP_EVENT){

		switch(event_id){

		case IP_EVENT_STA_GOT_IP:
			ESP_LOGI(TAG, "EVENT: IP_EVENT_STA_GOT_IP");
	        xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_WIFI_CONNECTED_BIT);
	        ip_event_got_ip_t* ip_event_got_ip = (ip_event_got_ip_t*)malloc(sizeof(ip_event_got_ip_t));
			*ip_event_got_ip =  *( (ip_event_got_ip_t*)event_data );
	        wifi_manager_send_message(WM_EVENT_STA_GOT_IP, (void*)(ip_event_got_ip) );
			break;

		case IP_EVENT_GOT_IP6:
			ESP_LOGI(TAG, "EVENT: IP_EVENT_GOT_IP6");
			break;

		case IP_EVENT_STA_LOST_IP:
			ESP_LOGI(TAG, "EVENT: IP_EVENT_STA_LOST_IP");
			break;

		}
	}

}

wifi_config_t* wifi_manager_get_wifi_sta_config(){
	return wifi_manager_sta_config;
}

void wifi_manager_connect_async(){
	/* in order to avoid a false positive on the front end app we need to quickly flush the ip json
	 * There'se a risk the front end sees an IP or a password error when in fact
	 * it's a remnant from a previous connection
	 */
	if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
		wifi_manager_clear_ip_info_json();
		wifi_manager_unlock_json_buffer();
	}
	xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_CONFIGURE_MODE_BIT);
	wifi_manager_send_message(WM_ORDER_LOAD_AND_RESTORE_STA, NULL);
}

char* wifi_manager_get_ip_info_json(){
	return ip_info_json;
}

void free_esp32_config(esp32_config_t* config) {
	free((void *)config->wifi_ssid);
	free((void *)config->wifi_wpa);
	free((void *)config->wifi_identity);
	free((void *)config->wifi_password);
	free((void *)config->wifi_auth);
	free((void *)config->wifi_inner);
	free((void *)config->wifi_ca);
	free((void *)config->wifi_crt);
	free((void *)config->wifi_key);
	free((void *)config->ipv4_method);
	free((void *)config->ipv4_address);
	free((void *)config->ipv4_mask);
	free((void *)config->ipv4_gate);
	free((void *)config->ipv4_dns1);
	free((void *)config->ipv4_dns2);
	free((void *)config->ipv4_zone);
	free((void *)config->ipv4_ntp);
	free((void *)config->server_address);
	free((void *)config->server_port);
	free((void *)config->server_auth);
	free((void *)config->ota_api);
	free((void *)config->client_username);
	free((void *)config->client_password);
	free((void *)config->client_ca);
	free((void *)config->client_crt);
	free((void *)config->client_key);
}

void wifi_manager_destroy(){

	vTaskDelete(task_wifi_manager);
	task_wifi_manager = NULL;

	/* heap buffers */
	free(accessp_records);
	accessp_records = NULL;
	free(accessp_json);
	accessp_json = NULL;
	free(ip_info_json);
	ip_info_json = NULL;
	free(wifi_manager_sta_ip);
	wifi_manager_sta_ip = NULL;
	if(wifi_manager_sta_config){
		free(wifi_manager_sta_config);
		wifi_manager_sta_config = NULL;
	}
	if(wifi_manager_config){
		free_esp32_config(wifi_manager_config);
		free(wifi_manager_config);
		wifi_manager_config = NULL;
	}

	/* RTOS objects */
	vSemaphoreDelete(wifi_manager_json_mutex);
	wifi_manager_json_mutex = NULL;
	vSemaphoreDelete(wifi_manager_sta_ip_mutex);
	wifi_manager_sta_ip_mutex = NULL;
	vEventGroupDelete(wifi_manager_event_group);
	wifi_manager_event_group = NULL;
	vQueueDelete(wifi_manager_queue);
	wifi_manager_queue = NULL;
}

void wifi_manager_filter_unique( wifi_ap_record_t * aplist, uint16_t * aps) {
	int total_unique;
	wifi_ap_record_t * first_free;
	total_unique=*aps;

	first_free=NULL;

	for(int i=0; i<*aps-1;i++) {
		wifi_ap_record_t * ap = &aplist[i];

		/* skip the previously removed APs */
		if (ap->ssid[0] == 0) continue;

		/* remove the identical SSID+authmodes */
		for(int j=i+1; j<*aps;j++) {
			wifi_ap_record_t * ap1 = &aplist[j];
			if ( (strcmp((const char *)ap->ssid, (const char *)ap1->ssid)==0) && 
			     (ap->authmode == ap1->authmode) ) { /* same SSID, different auth mode is skipped */
				/* save the rssi for the display */
				if ((ap1->rssi) > (ap->rssi)) ap->rssi=ap1->rssi;
				/* clearing the record */
				memset(ap1,0, sizeof(wifi_ap_record_t));
			}
		}
	}
	/* reorder the list so APs follow each other in the list */
	for(int i=0; i<*aps;i++) {
		wifi_ap_record_t * ap = &aplist[i];
		/* skipping all that has no name */
		if (ap->ssid[0] == 0) {
			/* mark the first free slot */
			if (first_free==NULL) first_free=ap;
			total_unique--;
			continue;
		}
		if (first_free!=NULL) {
			memcpy(first_free, ap, sizeof(wifi_ap_record_t));
			memset(ap,0, sizeof(wifi_ap_record_t));
			/* find the next free slot */
			for(int j=0; j<*aps;j++) {
				if (aplist[j].ssid[0]==0) {
					first_free=&aplist[j];
					break;
				}
			}
		}
	}
	/* update the length of the list */
	*aps = total_unique;
}

BaseType_t wifi_manager_send_message_to_front(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSendToFront( wifi_manager_queue, &msg, portMAX_DELAY);
}

BaseType_t wifi_manager_send_message(message_code_t code, void *param){
	queue_message msg;
	msg.code = code;
	msg.param = param;
	return xQueueSend( wifi_manager_queue, &msg, portMAX_DELAY);
}

void wifi_manager_set_callback(message_code_t message_code, void (*func_ptr)(void*) ){

	if(cb_ptr_arr && message_code < WM_MESSAGE_CODE_COUNT){
		cb_ptr_arr[message_code] = func_ptr;
	}
}

esp_netif_t* wifi_manager_get_esp_netif_ap(){
	return esp_netif_ap;
}

esp_netif_t* wifi_manager_get_esp_netif_sta(){
	return esp_netif_sta;
}

void wifi_manager( void * pvParameters ){

	queue_message msg;
	BaseType_t xStatus;
	EventBits_t uxBits;
	uint8_t	retries = 0;
	wifi_auth_mode_t authmode;

	/* initialize the tcp stack */
	ESP_ERROR_CHECK(esp_netif_init());

	/* event loop for the wifi driver */
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_sta = esp_netif_create_default_wifi_sta();
	esp_netif_ap = esp_netif_create_default_wifi_ap();

	ESP_ERROR_CHECK( esp_netif_dhcpc_stop(esp_netif_sta) );


	/* default wifi config */
	wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

	/* event handler for the connection */
    esp_event_handler_instance_t instance_wifi_event;
    esp_event_handler_instance_t instance_ip_event;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_wifi_event));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, ESP_EVENT_ANY_ID, &wifi_manager_event_handler, NULL,&instance_ip_event));

	/* SoftAP - Wifi Access Point configuration setup */
	wifi_config_t ap_config = {
		.ap = {
			.ssid_len = 0,
			.channel = wifi_settings.ap_channel,
			.ssid_hidden = wifi_settings.ap_ssid_hidden,
			.max_connection = DEFAULT_AP_MAX_CONNECTIONS,
			.beacon_interval = DEFAULT_AP_BEACON_INTERVAL,
		},
	};
	memcpy(ap_config.ap.ssid, wifi_settings.ap_ssid , sizeof(wifi_settings.ap_ssid));

	/* if the password lenght is under 8 char which is the minium for WPA2, the access point starts as open */
	if(strlen( (char*)wifi_settings.ap_pwd) < WPA2_MINIMUM_PASSWORD_LENGTH){
		ap_config.ap.authmode = WIFI_AUTH_OPEN;
		memset( ap_config.ap.password, 0x00, sizeof(ap_config.ap.password) );
	}
	else{
		ap_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
		memcpy(ap_config.ap.password, wifi_settings.ap_pwd, sizeof(wifi_settings.ap_pwd));
	}

	/* DHCP AP configuration */
	esp_netif_dhcps_stop(esp_netif_ap); /* DHCP client/server must be stopped before setting new IP information. */
	esp_netif_ip_info_t ap_ip_info;
	memset(&ap_ip_info, 0x00, sizeof(ap_ip_info));
	inet_pton(AF_INET, DEFAULT_AP_IP, &ap_ip_info.ip);
	inet_pton(AF_INET, DEFAULT_AP_GATEWAY, &ap_ip_info.gw);
	inet_pton(AF_INET, DEFAULT_AP_NETMASK, &ap_ip_info.netmask);
	ESP_ERROR_CHECK(esp_netif_set_ip_info(esp_netif_ap, &ap_ip_info));
	ESP_ERROR_CHECK(esp_netif_dhcps_start(esp_netif_ap));

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
	ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_AP, &ap_config));
	ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, wifi_settings.ap_bandwidth));
	ESP_ERROR_CHECK(esp_wifi_set_ps(wifi_settings.sta_power_save));


	/* by default the mode is STA because wifi_manager will not start the access point unless it has to! */
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_start());

	/* start http server */
	start_rest_server(STORE_BASE_PATH);

	/* wifi scanner config */
	wifi_scan_config_t scan_config = {
		.ssid = 0,
		.bssid = 0,
		.channel = 0,
		.show_hidden = true
	};

	/* enqueue first event: load previous config */
	wifi_manager_send_message(WM_ORDER_LOAD_AND_RESTORE_STA, NULL);


	/* main processing loop */
	for(;;){
		xStatus = xQueueReceive( wifi_manager_queue, &msg, portMAX_DELAY );
		if( xStatus == pdPASS ){

			switch(msg.code){

			case WM_EVENT_SCAN_DONE: {
				wifi_event_sta_scan_done_t *evt_scan_done = (wifi_event_sta_scan_done_t*)msg.param;
				/* only check for AP if the scan is succesful */
				ESP_LOGD(TAG, "MESSAGE: WM_EVENT_SCAN_DONE");
				if(evt_scan_done->status == 0){
					/* As input param, it stores max AP number ap_records can hold. As output param, it receives the actual AP number this API returns.
					* As a consequence, ap_num MUST be reset to MAX_AP_NUM at every scan */
					ap_num = MAX_AP_NUM;
					ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(&ap_num, accessp_records));
					/* make sure the http server isn't trying to access the list while it gets refreshed */
					if(wifi_manager_lock_json_buffer( pdMS_TO_TICKS(1000) )){
						/* Will remove the duplicate SSIDs from the list and update ap_num */
						wifi_manager_filter_unique(accessp_records, &ap_num);
						wifi_manager_generate_acess_points_json();
						wifi_manager_unlock_json_buffer();
					}
					else{
						ESP_LOGE(TAG, "could not get access to json mutex in wifi_scan");
					}
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				free(evt_scan_done);
				}
				break;

			case WM_ORDER_START_WIFI_SCAN:
				ESP_LOGD(TAG, "MESSAGE: ORDER_START_WIFI_SCAN");

				/* if a scan is already in progress this message is simply ignored thanks to the WIFI_MANAGER_SCAN_BIT uxBit */
				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if(! (uxBits & WIFI_MANAGER_SCAN_BIT)){
					xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_SCAN_BIT);
					ESP_ERROR_CHECK(esp_wifi_scan_start(&scan_config, false));
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_ORDER_LOAD_AND_RESTORE_STA:
				ESP_LOGI(TAG, "MESSAGE: ORDER_LOAD_AND_RESTORE_STA");
				bool sta_config_load_success = wifi_manager_fetch_wifi_sta_config(wifi_manager_config, wifi_manager_sta_config, &wifi_settings, &authmode);
				
				if(sta_config_load_success){
					ESP_LOGI(TAG, "Restored authmode: %s", (authmode == WIFI_AUTH_WPA2_PSK) ? "WIFI_AUTH_WPA2_PSK" : "WIFI_AUTH_WPA2_ENTERPRISE");
					wifi_manager_send_message(WM_ORDER_CONNECT_STA, (void*)CONNECTION_REQUEST_RESTORE_CONNECTION);
				}else{
					/* no wifi saved: start soft AP! This is what should happen during a first run */
					ESP_LOGI(TAG, "No saved wifi found on startup. Starting access point.");
					wifi_manager_send_message(WM_ORDER_START_AP, NULL);
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				/* reset if esp32 configuration complet */
				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if(xTimerIsTimerActive(wifi_manager_restart_timer) == pdTRUE && uxBits & WIFI_MANAGER_CONFIGURE_MODE_BIT){
					ESP_LOGI(TAG, "ESP32 Configuration complete!");
					esp_restart();
				}

				break;

			case WM_ORDER_CONNECT_STA:
				ESP_LOGI(TAG, "MESSAGE: ORDER_CONNECT_STA");

				/* very important: precise that this connection attempt is specifically requested.
				 * Param in that case is a boolean indicating if the request was made automatically
				 * by the wifi_manager.
				 * */
				if((BaseType_t)msg.param == CONNECTION_REQUEST_USER) {
					xEventGroupSetBits(wifi_manager_event_group, 
						WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);
				}
				else if((BaseType_t)msg.param == CONNECTION_REQUEST_RESTORE_CONNECTION) {
					xEventGroupSetBits(wifi_manager_event_group, 
						WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
				}

				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if( ! (uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT) ){
					/* update config to latest and attempt connection */
					ESP_ERROR_CHECK(esp_wifi_set_config(ESP_IF_WIFI_STA, wifi_manager_get_wifi_sta_config()));

					/* if there is a wifi scan in progress abort it first
					   Calling esp_wifi_scan_stop will trigger a SCAN_DONE event which will reset this bit */
					if(uxBits & WIFI_MANAGER_SCAN_BIT){
						esp_wifi_scan_stop();
					}
					if (authmode == WIFI_AUTH_WPA2_ENTERPRISE) {
						ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_enable());
					}else {
						ESP_ERROR_CHECK(esp_wifi_sta_wpa2_ent_disable());
					}
					ESP_ERROR_CHECK(esp_wifi_connect());
				}

				/* callback */
				FLASH_LOGI("WiFi Connection established on personal mode");
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_EVENT_STA_DISCONNECTED:
				;wifi_event_sta_disconnected_t* wifi_event_sta_disconnected = (wifi_event_sta_disconnected_t*)msg.param;
				ESP_LOGI(TAG, "MESSAGE: EVENT_STA_DISCONNECTED with Reason code: %d", wifi_event_sta_disconnected->reason);

				/* reset saved sta IP */
				wifi_manager_safe_update_sta_ip_string((uint32_t)0);

				/* if there was a timer on to stop the AP, well now it's time to cancel that since connection was lost! */
				if(xTimerIsTimerActive(wifi_manager_restart_timer) == pdTRUE ){
					xTimerStop( wifi_manager_restart_timer, (TickType_t)0 );
				}

				uxBits = xEventGroupGetBits(wifi_manager_event_group);
				if( uxBits & WIFI_MANAGER_REQUEST_STA_CONNECT_BIT ){
					/* there are no retries when it's a user requested connection by design. This avoids a user hanging too much
					 * in case they typed a wrong password for instance. Here we simply clear the request bit and move on */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						wifi_manager_generate_ip_info_json( UPDATE_FAILED_ATTEMPT , false);
						wifi_manager_unlock_json_buffer();
					}

				}
				else if (uxBits & WIFI_MANAGER_REQUEST_DISCONNECT_BIT){
					/* user manually requested a disconnect so the lost connection is a normal event. Clear the flag and restart the AP */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

					/* erase configuration */
					if(wifi_manager_sta_config){
						ESP_LOGE(TAG, "Erase config");
						memset(wifi_manager_sta_config, 0x00, sizeof(wifi_config_t));
					}

					/* regenerate json status */
					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						wifi_manager_generate_ip_info_json( UPDATE_USER_DISCONNECT , false);
						wifi_manager_unlock_json_buffer();
					}

					/* start SoftAP */
					wifi_manager_send_message(WM_ORDER_START_AP, NULL);
				}
				else{
					/* lost connection ? */
					if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
						wifi_manager_generate_ip_info_json( UPDATE_LOST_CONNECTION , false);
						wifi_manager_unlock_json_buffer();
					}

					/* Start the timer that will try to restore the saved config */
					if(retries < WIFI_MANAGER_MAX_RETRY_START_AP){
						xTimerStart( wifi_manager_retry_timer, (TickType_t)0 );
					}

					/* if it was a restore attempt connection, we clear the bit */
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);

					/* if the AP is not started, we check if we have reached the threshold of failed attempt to start it */
					if( !(uxBits & WIFI_MANAGER_AP_STARTED_BIT) || (uxBits & WIFI_MANAGER_CONFIGURE_MODE_BIT) ){

						/* if the nunber of retries is below the threshold to start the AP, a reconnection attempt is made
						 * This way we avoid restarting the AP directly in case the connection is mementarily lost */
						if(retries < WIFI_MANAGER_MAX_RETRY_START_AP){
							retries++;
						}
						else{
							retries = 0;
							FLASH_LOGW("WiFi Connection lost");
							xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_CONFIGURE_MODE_BIT);
							wifi_manager_send_message(WM_ORDER_START_AP, NULL);
						}
					}
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
				
				free(wifi_event_sta_disconnected);

				break;

			case WM_ORDER_START_AP:
				ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));

				/* restart HTTP daemon */
				stop_rest_server();
				start_rest_server(STORE_BASE_PATH);

				/* start DNS */
				start_dns_server();

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			case WM_ORDER_STOP_AP:
				ESP_LOGW(TAG, "MESSAGE: ORDER_STOP_AP");


				uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* before stopping the AP, we check that we are still connected. There's a chance that once the timer
				 * kicks in, for whatever reason the esp32 is already disconnected.
				 */
				if(uxBits & WIFI_MANAGER_WIFI_CONNECTED_BIT){

					ESP_LOGW(TAG, "WIFI_MANAGER_WIFI_CONNECTED_BIT");

					/* set to STA only */
					esp_wifi_set_mode(WIFI_MODE_STA);

					/* stop DNS */
					stop_dns_server();

					/* restart HTTP daemon */
					stop_rest_server();
					start_rest_server(STORE_BASE_PATH);

					/* callback */
					if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);
					
				}

				break;

			case WM_EVENT_STA_GOT_IP:
				ESP_LOGI(TAG, "WM_EVENT_STA_GOT_IP");
				ip_event_got_ip_t* ip_event_got_ip = (ip_event_got_ip_t*)msg.param; 
				uxBits = xEventGroupGetBits(wifi_manager_event_group);

				/* reset connection requests bits -- doesn't matter if it was set or not */
				xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_STA_CONNECT_BIT);

				/* save IP as a string for the HTTP server host */
				wifi_manager_safe_update_sta_ip_string(ip_event_got_ip->ip_info.ip.addr);

				/* save wifi config in NVS if it wasn't a restored of a connection */
				if(uxBits & WIFI_MANAGER_REQUEST_RESTORE_STA_BIT){
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_RESTORE_STA_BIT);
				}

				/* reset number of retries */
				retries = 0;

				/* deactivate the reboot timer, well now it's time to cancel that since connection was lost! */
				if(xTimerIsTimerActive(wifi_manager_restart_timer) == pdTRUE ){
					ESP_LOGI(TAG, "TIMER REBOOT STOPED");
					xTimerStop( wifi_manager_restart_timer, (TickType_t)0 );
					xEventGroupClearBits(wifi_manager_event_group, WIFI_MANAGER_CONFIGURE_MODE_BIT);
					wifi_manager_send_message(WM_ORDER_STOP_AP, NULL);
				}
			
				wifi_manager_send_message(WM_ORDER_HTTP_CLIENT_INIT, NULL);

				/* callback and free memory allocated for the void* param */
				free(ip_event_got_ip);
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])( msg.param );
					
				break;

			case WM_ORDER_HTTP_CLIENT_INIT:
				ESP_LOGI(TAG, "WM_ORDER_HTTP_CLIENT_INIT");

				/* bring down HTPP & DNS hijack */
				stop_dns_server();
				stop_rest_server();

				/* SNTP initialize */
				if(strlen(wifi_manager_get_ntp_server_address())) {
					ESP_ERROR_CHECK(initialize_ntp(wifi_manager_config->ipv4_zone, wifi_manager_get_ntp_server_address()));
				}

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);
				break;
				
			case WM_ORDER_HTTPD_REQUEST:
				ESP_LOGI(TAG, "WM_ORDER_HTTPD_REQUEST");

				if(wifi_manager_lock_json_buffer( portMAX_DELAY )){
					wifi_manager_generate_ip_info_json( UPDATE_CONNECTION_OK , (BaseType_t)msg.param );
					wifi_manager_unlock_json_buffer();
				}
				else { 
					abort(); 
				}

				break;

			case WM_ORDER_DISCONNECT_STA:
				ESP_LOGI(TAG, "MESSAGE: ORDER_DISCONNECT_STA");

				/* precise this is coming from a user request */
				xEventGroupSetBits(wifi_manager_event_group, WIFI_MANAGER_REQUEST_DISCONNECT_BIT);

				/* order wifi discconect */
				ESP_ERROR_CHECK(esp_wifi_disconnect());

				/* callback */
				if(cb_ptr_arr[msg.code]) (*cb_ptr_arr[msg.code])(NULL);

				break;

			default:
				break;

			} /* end of switch/case */
		} /* end of if status=pdPASS */
	} /* end of for loop */

	ESP_LOGI(TAG, "WIFI MANAGER TASK STOPPED");

	vTaskDelete( NULL );

}

static inline void clear_buffer(uint8_t *buffer){
	if(buffer){
		free(buffer);
		buffer = NULL;
	}
}

static ttls_authentication_t phase2(esp32_config_t* config) {
	if (strcmp(config->wifi_inner, "mschapv2") ==0) {
		return MSCHAPV2;
	} else
	if (strcmp(config->wifi_inner, "mschap") ==0) {
		return MSCHAP;
	} else
	if (strcmp(config->wifi_inner, "pap") ==0) {
		return PAP;
	}
	return CHAP;
}

void wifi_manager_set_eap_config() {
	if (wifi_manager_config->wifi_username) {
		int len = strlen(wifi_manager_config->wifi_username);
		ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_username((unsigned char*)wifi_manager_config->wifi_username, len) );
	}
	if (wifi_manager_config->wifi_identity) {
		int len = strlen(wifi_manager_config->wifi_identity);
		ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_identity((unsigned char*)wifi_manager_config->wifi_identity, len) );
		if (!wifi_manager_config->wifi_username) {
			ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_username((unsigned char*)wifi_manager_config->wifi_identity, len) );
		}
	}
	if (wifi_manager_config->wifi_password) {
		ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_password((unsigned char*)wifi_manager_config->wifi_password, 
		strlen(wifi_manager_config->wifi_password)) );
	}
	if (wifi_manager_config->wifi_ca) {
		ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_ca_cert((unsigned char*)wifi_manager_config->wifi_ca, 
		strlen(wifi_manager_config->wifi_ca)+1) ); 
	}
	if (wifi_manager_config->wifi_auth && strcmp(wifi_manager_config->wifi_auth, "tls") == 0) {
		ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_cert_key((unsigned char*)wifi_manager_config->wifi_crt,  
		strlen(wifi_manager_config->wifi_crt)+1,
		(unsigned char*)wifi_manager_config->wifi_key, 
		strlen(wifi_manager_config->wifi_crt)+1, NULL, 0) );		
	}
	if (wifi_manager_config->wifi_inner && strcmp(wifi_manager_config->wifi_inner, "ttls") == 0) {
		ESP_ERROR_CHECK( esp_wifi_sta_wpa2_ent_set_ttls_phase2_method(phase2(wifi_manager_config)) );
	}
}

static void wifi_manager_set_manual_ipv4_dns()
{
	if(wifi_manager_config->ipv4_dns1){
		esp_netif_dns_info_t dns;
		esp_ip4_addr_t ipv4;
		ESP_ERROR_CHECK( esp_netif_str_to_ip4((char*)wifi_manager_config->ipv4_dns1, &ipv4) );
		dns.ip.u_addr.ip4.addr = ipv4.addr;
		dns.ip.type = IPADDR_TYPE_V4;
		ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_sta, ESP_NETIF_DNS_MAIN, &dns));
		ESP_LOGI(TAG, "MAIN DNS: %s", wifi_manager_config->ipv4_dns1);
	}
	if(wifi_manager_config->ipv4_dns2){
		esp_netif_dns_info_t dns;
		esp_ip4_addr_t ipv4;
		ESP_ERROR_CHECK( esp_netif_str_to_ip4((char*)wifi_manager_config->ipv4_dns2, &ipv4) );
		dns.ip.u_addr.ip4.addr = ipv4.addr;
		dns.ip.type = IPADDR_TYPE_V4;
		ESP_ERROR_CHECK(esp_netif_set_dns_info(esp_netif_sta, ESP_NETIF_DNS_BACKUP, &dns));
		ESP_LOGI(TAG, "BACKUP DNS: %s", wifi_manager_config->ipv4_dns2);
	}
}

static void wifi_manager_set_manual_ipv4()
{
	esp_netif_ip_info_t ip_info = {0};
	esp_ip4_addr_t ipv4;
	ESP_ERROR_CHECK( esp_netif_str_to_ip4((char*)wifi_manager_config->ipv4_address, &ipv4) );
	ip_info.ip.addr = ipv4.addr;
	ESP_ERROR_CHECK( esp_netif_str_to_ip4((char*)wifi_manager_config->ipv4_mask, &ipv4) );
	ip_info.netmask.addr = ipv4.addr;
	ESP_ERROR_CHECK( esp_netif_str_to_ip4((char*)wifi_manager_config->ipv4_gate, &ipv4) );
	ip_info.gw.addr = ipv4.addr;	
	ESP_ERROR_CHECK( esp_netif_set_ip_info(esp_netif_sta, &ip_info) );
}

void wifi_manager_set_ipv4_config(){
	if(strcmp(wifi_manager_config->ipv4_method, "auto") == 0) {
		ESP_ERROR_CHECK( esp_netif_dhcpc_start(esp_netif_sta) );
	}else{
		wifi_manager_set_manual_ipv4();	
	}
	wifi_manager_set_manual_ipv4_dns();				
}

bool wifi_manager_dhcpc_is_off() {
	esp_netif_dhcp_status_t state;
	esp_err_t err = esp_netif_dhcpc_get_status(esp_netif_sta, &state);
	return (err == ESP_OK && state == ESP_NETIF_DHCP_STARTED) ? false : true;
}

char* wifi_manager_get_ntp_server_address() {
	return wifi_manager_config->ipv4_ntp;
}

void wifi_manager_start_setup_mode() {
	wifi_manager_remove_config();
	delayed_reboot(2000);
}

esp32_config_t* wifi_manager_get_config() {
	return wifi_manager_config;
}

static void delayed_reboot_cb( TimerHandle_t xTimer){

	/* stop the timer */
	xTimerStop( xTimer, (TickType_t) 0 );


	/* Attempt to reboot */
	ESP_LOGW(TAG, "Attempt to reboot");
	esp_restart();
}

void delayed_reboot(const uint32_t tick){
	FLASH_LOGW("Attempt to reboot");
	/* create timer for to keep track of esp restart */
	TimerHandle_t* timer = xTimerCreate( NULL, pdMS_TO_TICKS(tick), pdFALSE, ( void * ) 0, delayed_reboot_cb);
	xTimerStart( timer, (TickType_t)0 );
	while(1) vTaskDelay(1000 / portTICK_RATE_MS);
}