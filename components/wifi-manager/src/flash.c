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
#include <nvs_flash.h>
#include <esp_vfs.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/event_groups.h>

#include "spiffs.h"
#include "ota.h"
#include "manager.h"
#include "ntp_client.h"
#include "flash.h"


#define STORE_BASE_PATH             "/"CONFIG_STORE_MOUNT_POINT   

#ifdef CONFIG_STORE_CHECK_ON_START
#define CHECK_STORE_ON_START        1
#else
#define CHECK_STORE_ON_START 	    0
#endif

#ifdef CONFIG_USE_FLASH_LOGGING  
#define LOG_FILE STORE_BASE_PATH    "/log.txt"

#ifdef CONFIG_WEB_USE_STORE
#define WWW_BASE_PATH               "/"CONFIG_WEB_STORE_MOUNT_POINT       
#endif

#define DEFAULT_CACHE_SIZE          CONFIG_FLASH_LOG_TASK_CACHE_SIZE

#define HC_WIFI_OK		            BIT1	// Set if wifi connection established
#endif

static const char *TAG = "flash_log";

#ifdef CONFIG_USE_FLASH_LOGGING  
/* objects used to manipulate the main queue of events */
QueueHandle_t flash_log_queue;
/* @brief task handle for the flash log task */
static TaskHandle_t task_flash_log = NULL;

EventGroupHandle_t flash_log_events;

BaseType_t flash_log_send_message(uint32_t order, log_message_t* msg, send_msg func){
    if (!task_flash_log) {
        ESP_LOGE(TAG, "Flash logging task not running");
        return pdFAIL;
    }

    flash_log_request_t r = {
        .order = order,
        .cb_ptr = func,
    };
    if (msg) {
        memcpy(&r.msg, msg, sizeof(log_message_t));
    }
	return xQueueSend( flash_log_queue, &r, portMAX_DELAY );
}
#endif

#ifdef CONFIG_USE_FLASH_LOGGING    
static void save_flash_log(log_message_t *msg){ 
    esp32_config_t* wifi_config = wifi_manager_get_config();
    if(wifi_config->ipv4_ntp) {
        /* Check HTTP client ready */
        EventBits_t uxBits = xEventGroupGetBits(flash_log_events);
        if( (uxBits & HC_WIFI_OK) == 0 ) {
            return;
        }
    }
    flash_log_send_message(FLASH_LOG_SAVE, msg, NULL);
}

void read_flash_log(send_msg func){
    flash_log_send_message(FLASH_LOG_READ, NULL, func);
}
#endif

void clear_flash_log(){
#ifdef CONFIG_USE_FLASH_LOGGING    
    flash_log_send_message(FLASH_LOG_CLEAR, NULL, NULL);
#endif
}

void FLASH_LOGE(const char* format, ...){
#ifdef CONFIG_USE_FLASH_LOGGING    
	log_message_t log;

    va_list arg;
    va_start(arg, format);
    vsprintf(log.message, format, arg);
    va_end(arg);

	log.type = 'E';
	log.date_time = get_local_datetime();
	save_flash_log(&log);
#endif
}

void FLASH_LOGW(const char* format, ...){
#ifdef CONFIG_USE_FLASH_LOGGING    
	log_message_t log;

    va_list arg;
    va_start(arg, format);
    vsprintf(log.message, format, arg);
    va_end(arg);

	log.type = 'W';
	log.date_time = get_local_datetime();
	save_flash_log(&log);
#endif
}

void FLASH_LOGI(const char* format, ...){
#ifdef CONFIG_USE_FLASH_LOGGING    
	log_message_t log;

    va_list arg;
    va_start(arg, format);
    vsprintf(log.message, format, arg);
    va_end(arg);

	log.type = 'I';
	log.date_time = get_local_datetime();
	save_flash_log(&log);
#endif
}

#ifdef CONFIG_USE_FLASH_LOGGING 
static void flash_log_task(void* pvParameters) {
	flash_log_request_t msg;
	BaseType_t xStatus;

    /* main processing loop */
    for(;;){
        xStatus = xQueueReceive( flash_log_queue, &msg, portMAX_DELAY );
        if( xStatus == pdPASS ){
            switch(msg.order) {
                case FLASH_LOG_SAVE:{
                    EventBits_t uxBits = xEventGroupGetBits(flash_log_events);
                    if( (uxBits & HC_WIFI_OK) == 0 ) {
                        ESP_LOGW(TAG, "System time not set!");
                        break;
                    }

                    esp_err_t esp_err = ESP_FAIL;
                    size_t sz = sizeof(log_message_t);
                    FILE *f = fopen(LOG_FILE, "rb");
                    if(f == NULL){
                        f =  fopen(LOG_FILE, "wb");
                    }else{
                        fclose(f);
                        f =  fopen(LOG_FILE, "ab");
                    }
                    if (f == NULL) {
                        ESP_LOGE(TAG, "Failed to open file %s for writing", LOG_FILE);
                        esp_err = ESP_ERR_FLASH_OP_FAIL;
                    }else{
                        fseek(f, 0L, SEEK_END); 
                        size_t size = ftell(f);
                        fseek(f, 0L, SEEK_SET);
                        ESP_LOGW(TAG, "File %s size: %d", LOG_FILE, size);
                        size_t i;
                        if(size < CONFIG_LOG_FILE_MAX_SIZE){
                            fwrite(&msg.msg, sz, 1, f);				
                        }else{
                            esp_err = ESP_ERR_NVS_NOT_ENOUGH_SPACE;
                            ESP_LOGE(TAG, "File: %s  has exceeded the allowed size ", LOG_FILE);
                        }
                        fclose(f);
                        if(esp_err == ESP_OK) {
                            ESP_LOGD(TAG, "File: %s success wrote: %d bytes", LOG_FILE, i);
                            ESP_LOGD(TAG, "File size: %d bytes", size);
                        }
                    }
                    break;
                }
                case FLASH_LOG_READ: {
                    size_t sz = sizeof(log_message_t);
                    char* buffer = (char*)malloc(sz);
                    memset(buffer , 0x00, sz);

                    FILE *f = fopen(LOG_FILE, "rb");
                    if (f == NULL) {
                        ESP_LOGE(TAG, "Failed to open file %s for reading", LOG_FILE);
                    }else{
                        EventBits_t uxBits = xEventGroupGetBits(flash_log_events);
                        do{
                            if( (uxBits & HC_WIFI_OK) == 0 ) {
                                ESP_LOGW(TAG, "System time not set!");
                                break;
                            }
                            if(fread(buffer, sz, 1, f)) {
                                log_message_t log = {0};
                                memcpy(&log, buffer, sz);
                                msg.cb_ptr(log.date_time, log.type, log.message);
                                ESP_LOGD(TAG, "DT: %ld, TYPE: %c, MSG: %s", log.date_time, log.type, log.message);
                            }
                        }while( !feof(f) );
                        fclose(f);
                        if( (uxBits & HC_WIFI_OK) != 0 ) {
                            f = fopen(LOG_FILE, "wb");
                            fclose(f);
                        }
                    }
                    free(buffer);
                    break;
                }
                case FLASH_LOG_CLEAR: {
                    FILE *f = fopen(LOG_FILE, "wb");
                    fclose(f);
                    break;
                }
                default:
                    ESP_LOGE(TAG, "Unknown order: %d", msg.order);
            }
        }
    }
}
#endif

/**
  * @brief  "This callback function is called  when the wifi connected"
  * @param  pvParameters: Pointer to received data
  * @retval None
  */
static void cb_wifi_connect(void* pvParameters) {
#ifdef CONFIG_USE_FLASH_LOGGING  
    xEventGroupSetBits(flash_log_events, HC_WIFI_OK);
#endif
}

/**
  * @brief  "This callback function is called  when the wifi disconnected"
  * @param  pvParameters: Pointer to received data
  * @retval None
  */
static void cb_wifi_lost(void* pvParameters) {
#ifdef CONFIG_USE_FLASH_LOGGING  
    xEventGroupClearBits(flash_log_events, HC_WIFI_OK);
#endif
}

void init_flash() {
    esp_err_t ret = nvs_flash_init();
    if(ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());   
        ret = nvs_flash_init(); 
    }
    ESP_ERROR_CHECK(ret);

	/* mount spif file system on flash memory */
	ESP_ERROR_CHECK(init_spiffs(
		STORE_BASE_PATH,
		CONFIG_STORE_MAX_FILES,
		CHECK_STORE_ON_START
		));

    get_sha256_of_partitions(); 
}

void init_flash_log() {
#ifdef CONFIG_USE_FLASH_LOGGING    
	/* memory allocation */
	flash_log_queue = xQueueCreate( 4, sizeof(flash_log_request_t) );  

    /* create flash log group */
    flash_log_events = xEventGroupCreate();    

    /* subscribe to wifi manager events */
    wifi_manager_set_callback(WM_ORDER_HTTP_CLIENT_INIT, &cb_wifi_connect);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_wifi_lost);
    wifi_manager_set_callback(WM_ORDER_STOP_AP, &cb_wifi_lost);
    wifi_manager_set_callback(WM_ORDER_START_AP, &cb_wifi_lost);
    wifi_manager_set_callback(WM_EVENT_SCAN_DONE, &cb_wifi_lost);
    wifi_manager_set_callback(WM_ORDER_START_WIFI_SCAN, &cb_wifi_lost);

    /* create queue task */
    xTaskCreate(&flash_log_task, "flash_log_task", DEFAULT_CACHE_SIZE, NULL, WIFI_MANAGER_TASK_PRIORITY+1, &task_flash_log);
#endif
}