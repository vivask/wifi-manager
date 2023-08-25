#include <stdio.h>
#include <string.h>
#include <nvs_flash.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/event_groups.h>
#include <esp_log.h>
#include <cJSON.h>

#include "flash.h"
#include "manager.h"
#include "http_client.h"
#include "ntp_client.h"

#define HTTP_CLIENT_OK  BIT0	// Set if http connection established

static const char *TAG = "MAIN";

EventGroupHandle_t example_events;

/**
  * @brief  "HTTP client callback function"
  * @param  pvParameters: Pointer to received data
  * @retval None
  */
static void cb_http_client_response(const char* data, int len) {
  if (len > 0) {  
    ESP_LOG_BUFFER_HEX(TAG, data, len);       
    cJSON *root = cJSON_Parse(data);
    if (root) {   
      cJSON_Delete(root);
    }       
  }
}

/**
  * @brief  "This callback function is called  when the http client activates"
  * @param  pvParameters: Pointer to received data
  * @retval None
  */
static void cb_http_client_ready(void* pvParameters) {
  xEventGroupSetBits(example_events, HTTP_CLIENT_OK);
  ESP_LOGI(TAG,"HTTP Client connected");
}

/**
  * @brief  "This callback function is called  when the http client deactivates"
  * @param  pvParameters: Pointer to received data
  * @retval None
  */
static void cb_http_client_not_ready(void* pvParameters) {
  xEventGroupClearBits(example_events, HTTP_CLIENT_OK);
  ESP_LOGI(TAG, "HTTP Client disconnected");
}

/**
  * @brief  "Callback function send flash logging"
  * @param  t: Local esp32 time
  * @param  log_type: Type logging message
  * @param  msg: Logging message
  * @retval None
  */
#ifdef CONFIG_USE_FLASH_LOGGING
void static send_flash_logging_message(const time_t t, const char log_type, const char* msg) {
  char buff[32];

  buff[0] = log_type;
  buff[1] = '\0';
  time_t sntp_time = get_sntp_time_init();
  time_t sntp_tiks = get_sntp_tiks_init();
  const time_t datetime = (t < sntp_tiks) ? (sntp_time - (sntp_tiks - t)) : t;    

  cJSON *root = cJSON_CreateObject();
  cJSON_AddStringToObject(root, "type", buff);
  cJSON_AddStringToObject(root, "date_time", date_time_format(buff, datetime));
  cJSON_AddStringToObject(root, "message", msg);
  // send json sting to http server
  const char* json_string = cJSON_Print(root);
  http_client_send_message(HTTP_METHOD_POST, "/logging", json_string);
  free((void* )json_string);
  cJSON_Delete(root);
}
#endif

/**
  * @brief  "This function requests instructions from backend, sends flash log and other tasks such as polling peripherals"
  * @param  pvParameters: Pointer to received data
  * @retval None
  */
static void main_task(void* pvParameters) {

  for(;;) {
    xEventGroupWaitBits(
            example_events,         // The event group being tested.
            HTTP_CLIENT_OK,         // The bits within the event group to wait for.
            pdFALSE,                // HTTP_CLIENT_OK should be not cleared before returning.
            pdFALSE,                // Don't wait for both bits, either bit will do.
            portMAX_DELAY );        // Wait until the bit be set.                            

#ifdef CONFIG_USE_FLASH_LOGGING             
    // send flash logging
    read_flash_log(send_flash_logging_message);
    ESP_LOGD(TAG, "Flash log sending complet!");
#endif

      vTaskDelay(2000 / portTICK_RATE_MS);
  } 

	ESP_LOGI(TAG, "GET ORDERS TASK STOPPED");
  
	vTaskDelete( NULL );        
}

void app_main(void)
{
	/* initialize flash */
	init_flash();

  /* WIFI initialize */
  wifi_manager_start(false);

  /* HTTP Client initialize */
  http_client_initialize();

  /* Callbacks link */
  http_client_set_response_callback(&cb_http_client_response);
  http_client_set_ready_callback(&cb_http_client_ready);
  http_client_set_not_ready_callback(&cb_http_client_not_ready);

  /* Waiting for HTTP client initialization*/
  xEventGroupWaitBits(
          example_events,                // The event group being tested.
          HTTP_CLIENT_OK,               // The bits within the event group to wait for.
          pdFALSE,                    // HC_STATUS_OK should be not cleared before returning.
          pdFALSE,                    // Don't wait for both bits, either bit will do.
          portMAX_DELAY );            // Wait until the bit be set.          

  /* Initialithe peripheral an start another tasks*/

  
  xTaskCreate(&main_task, "main_task", 0x1000, NULL, CONFIG_WIFI_MANAGER_TASK_PRIORITY+3, NULL);

}
