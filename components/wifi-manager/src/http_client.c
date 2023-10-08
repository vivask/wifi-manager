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
#include <string.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/timers.h>
#include <freertos/event_groups.h>
#include <esp_tls.h>
#if CONFIG_MBEDTLS_CERTIFICATE_BUNDLE
#include <esp_crt_bundle.h>
#endif

#if CONFIG_USE_OTA
#include "ota.h"
#endif

#include "list.h"
#include "manager.h"
#include "flash.h"
#include "http_client.h"

#define DEFAULT_CACHE_SIZE      CONFIG_HTTP_CLIENT_TASK_CACHE_SIZE
#define MAX_HTTP_URL_SIZE       CONFIG_HTTP_CLIENT_MAX_URL_LEN
#define MAX_HTTP_OUTPUT_BUFFER  CONFIG_HTTP_CLIENT_MAX_RESPONSE_LEN

#ifdef CONFIG_IDF_TARGET_ESP32
#define MIN(a,b) (((a)<(b))?(a):(b))
#endif

static const char *TAG = "http_client";

// static char local_response_buffer[MAX_HTTP_OUTPUT_BUFFER] = {0};
static char *output_buffer;  // Buffer to store response of http request from event handler

static char url_buffer[MAX_HTTP_URL_SIZE] = {0};

static esp_http_client_handle_t client = NULL;

/* @brief task handle for the http client send task */
static TaskHandle_t task_http_client_send = NULL;
/* @brief task handle for the http client order task */
static TaskHandle_t task_http_client_order = NULL;
/* objects used to manipulate the http client send queue of events */
QueueHandle_t http_client_send_queue;
/* objects used to manipulate the http client connect queue of events */
QueueHandle_t http_client_order_queue;

EventGroupHandle_t http_client_events;

/* @brief callback response function pointer */
void (*cb_response_ptr)(const char*, int) = NULL;

/* @brief callback http client ready function pointer */
CallBackList* cb_ready_ptr = NULL;

/* @brief callback http client not ready function pointer */
CallBackList* cb_not_ready_ptr = NULL;

static char* get_full_path(const char* uri) {
    esp32_config_t* wifi_config = wifi_manager_get_config();
    memset(url_buffer, 0x00, MAX_HTTP_URL_SIZE);
    if (strlen(wifi_config->server_api) > 1) {
        strcpy(url_buffer, wifi_config->server_api);
    }
    strcat(url_buffer, uri);
    return url_buffer;
}

static BaseType_t is_wifi_connected() {
    EventBits_t uxBits = xEventGroupGetBits(http_client_events);
    return ((uxBits & HC_WIFI_OK) == 0) ? pdFALSE : pdTRUE;
}

BaseType_t http_client_send_order(uint32_t order){

    if (is_wifi_connected() !=  pdPASS) {
        return pdFALSE;
    }

	return xQueueSend( http_client_order_queue, &order, portMAX_DELAY);
}

static void cb_wifi_connect(void *pvParameter){
    xEventGroupSetBits(http_client_events, HC_WIFI_OK);
    if (task_http_client_order) {
        http_client_send_order(HC_ORDER_CONECT);
    }
}

static void cb_wifi_lost(void* pvParameter) {
    run_cb(cb_not_ready_ptr, NULL);
    xEventGroupClearBits(http_client_events, HC_WIFI_OK);
}

static void cb_ota_finish(bool fail) {
    if(fail){
        run_cb(cb_ready_ptr, NULL);
        xEventGroupSetBits(http_client_events, HC_STATUS_OK);
    }
}

void http_client_destroy() {   
    run_cb(cb_not_ready_ptr, NULL);
    xEventGroupClearBits(http_client_events, HC_WIFI_OK);

	vTaskDelete(task_http_client_send);
	task_http_client_send = NULL;

	vTaskDelete(task_http_client_order);
	task_http_client_order = NULL;

	vQueueDelete(http_client_send_queue);
	http_client_send_queue = NULL;

	vQueueDelete(http_client_order_queue);
	http_client_order_queue = NULL;
}

static void copy_trunc(char* dst, const char* src, size_t max_len) {
    dst[0] =  '\0';
    if (src) {
        size_t len = strlen(src);
        if (len >= max_len) {
            len = max_len - 1;
        }
        memcpy((void*)dst, (void*)src, len);
        dst[len] = '\0';
    }
}

BaseType_t http_client_send_message_to_front(uint16_t method, const char* uri, const char* data){

    if (is_wifi_connected() !=  pdPASS) {
        return pdFALSE;
    }

	http_client_request_t msg = {
        .method = method,
    };
    copy_trunc(msg.uri, uri, MAX_HTTP_URI_LEN);
    if (data && strlen(data) > 0) {
        copy_trunc(msg.data, data, MAX_REQUEST_DATA_LEN);
    }

	return xQueueSendToFront( http_client_send_queue, &msg, portMAX_DELAY);
}

BaseType_t http_client_send_message(uint16_t method, const char* uri, const char* data){

    if (is_wifi_connected() !=  pdPASS) {
        return pdFALSE;
    }

	http_client_request_t msg = {
        .method = method,
    };
    copy_trunc(msg.uri, uri, MAX_HTTP_URI_LEN);
    if (data && strlen(data) > 0) {
        copy_trunc(msg.data, data, MAX_REQUEST_DATA_LEN);
    }

	return xQueueSend( http_client_send_queue, &msg, portMAX_DELAY);
}

void http_client_set_response_callback(void (*func_ptr)(const char*, int) ){

	if(cb_response_ptr == NULL && cb_response_ptr != func_ptr){
		cb_response_ptr = func_ptr;
	}else {
        ESP_LOGW(TAG, "The callback response is already bound to another function");
    }
}

void http_client_set_ready_callback(void (*func_ptr)(void*) ){
    if (func_ptr) {
        push_cb(&cb_ready_ptr, func_ptr);
    }
}

void http_client_set_not_ready_callback(void (*func_ptr)(void*) ){
    if (func_ptr) {
        push_cb(&cb_not_ready_ptr, func_ptr);
    }
}

static char* get_http_method_name(esp_http_client_method_t method) {
    switch(method) {
    case HTTP_METHOD_GET:
        return "GET";
    case HTTP_METHOD_POST:
        return "POST";
    case HTTP_METHOD_PUT:
        return "PUT";
    case HTTP_METHOD_DELETE:
        return "DELETE";
    default:
        return "Unknown";
    }
}

static esp_err_t http_client_handler(esp_http_client_event_t *evt) {
    static int output_len;       // Stores number of bytes read
    static uint8_t error_count = 0;
    switch(evt->event_id) {
        case HTTP_EVENT_ERROR:
            error_count++;
            ESP_LOGW(TAG, "HTTP_EVENT_ERROR: %d", error_count);
            if (error_count == CONFIG_HTTP_CLIENT_MAX_ERROR) {
                http_client_send_order(HC_ORDER_DISCONNECT);
            }
            if (error_count > CONFIG_HTTP_CLIENT_MAX_TRY_CONNECT) {
                FLASH_LOGE("The number of http client errors has exceeded the allowable value");
                delayed_reboot(2000);
            }
            break;
        case HTTP_EVENT_ON_CONNECTED:
            error_count = 0;
            ESP_LOGW(TAG, "HTTP_EVENT_ON_CONNECTED");
            break;
        case HTTP_EVENT_HEADER_SENT:
            ESP_LOGD(TAG, "HTTP_EVENT_HEADER_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_HEADER, key=%s, value=%s", evt->header_key, evt->header_value);
            break;
        case HTTP_EVENT_ON_DATA:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_DATA, len=%d", evt->data_len);
            /*
             *  Check for chunked encoding is added as the URL for chunked encoding used in this example returns binary data.
             *  However, event handler can also be used in case chunked encoding is used.
             */
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // If user_data buffer is configured, copy the response into the buffer
                int copy_len = 0;
                if (evt->user_data) {
                    copy_len = MIN(evt->data_len, (MAX_HTTP_OUTPUT_BUFFER - output_len));
                    if (copy_len) {
                        memcpy(evt->user_data + output_len, evt->data, copy_len);
                    }
                } else {
                    const int buffer_len = esp_http_client_get_content_length(evt->client);
                    if (output_buffer == NULL) {
                        output_buffer = (char *) malloc(buffer_len);
                        output_len = 0;
                        if (output_buffer == NULL) {
                            ESP_LOGE(TAG, "Failed to allocate memory for output buffer");
                            return ESP_FAIL;
                        }
                    }
                    copy_len = MIN(evt->data_len, (buffer_len - output_len));
                    if (copy_len) {
                        memcpy(output_buffer + output_len, evt->data, copy_len);
                    }
                }
                output_len += copy_len;
            }

            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGD(TAG, "HTTP_EVENT_ON_FINISH");
            if (output_buffer != NULL) {
                // Response is accumulated in output_buffer. Uncomment the below line to print the accumulated response
                // ESP_LOG_BUFFER_HEX(TAG, output_buffer, output_len);
#ifndef CONFIG_USE_OTA
                if(cb_response_ptr) cb_response_ptr( output_buffer, output_len );
#else
                if(!ota(output_buffer, output_len)){
                    if(cb_response_ptr) cb_response_ptr( output_buffer, output_len );
                }else {
                    run_cb(cb_not_ready_ptr, NULL);
                    xEventGroupClearBits(http_client_events, HC_STATUS_OK);
                    firmware_upgrade(cb_ota_finish);
                }
#endif                
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "HTTP_EVENT_DISCONNECTED");

            /* callback */
            run_cb(cb_not_ready_ptr, NULL);
            
            xEventGroupClearBits(http_client_events, HC_STATUS_OK);
            int mbedtls_err = 0;
            esp_err_t err = esp_tls_get_and_clear_last_error((esp_tls_error_handle_t)evt->data, &mbedtls_err, NULL);
            if (err != 0) {
                ESP_LOGI(TAG, "Last esp error code: 0x%x", err);
                ESP_LOGI(TAG, "Last mbedtls failure: 0x%x", mbedtls_err);
            }
            if (output_buffer != NULL) {
                free(output_buffer);
                output_buffer = NULL;
            }
            output_len = 0;
            error_count = 0;
            vTaskDelay(5000 / portTICK_RATE_MS);
            http_client_send_order(HC_ORDER_CONECT);
            break;
    }
    return ESP_OK;
}

static void http_client_try_to_send(const http_client_request_t* msg) {
    esp_err_t err;
    uint8_t try_to_send = CONFIG_HTTP_CLIENT_MAX_TRY_SEND;

    do {
        err = esp_http_client_perform(client);
        if (err != ESP_ERR_HTTP_FETCH_HEADER) {
            break;
        }   
        vTaskDelay(100 / portTICK_RATE_MS);
    }while(try_to_send--);

    int http_code = esp_http_client_get_status_code(client);
    if (err == ESP_OK && http_code == 200) {
        xEventGroupSetBits(http_client_events, HC_SEND_OK);
    } else {
        FLASH_LOGE("%s request failed: %s, http code: %d", 
            get_http_method_name(msg->method), 
            esp_err_to_name(err), 
            http_code);
        xEventGroupSetBits(http_client_events, HC_SEND_FAIL);
    }   
}

static void http_client_send_task( void * pvParameters ) {
	http_client_request_t msg;
	BaseType_t xStatus;

    /* main processing loop */
    for(;;){
        xEventGroupWaitBits(
                http_client_events,         // The event group being tested.
                HC_STATUS_OK,               // The bits within the event group to wait for.
                pdFALSE,                    // HC_STATUS_OK should be not cleared before returning.
                pdFALSE,                    // Don't wait for both bits, either bit will do.
                portMAX_DELAY );            // Wait until the bit be set.          
        xStatus = xQueueReceive( http_client_send_queue, &msg, portMAX_DELAY );
        if( xStatus == pdPASS ){
            esp_http_client_set_method(client, msg.method); 
            esp_http_client_set_url(client, get_full_path(msg.uri));
            esp_http_client_set_header(client, "Content-Type", "application/json");

            // ESP_LOGI(TAG, "%s", get_http_method_name(msg.method));

            switch(msg.method){
            case HTTP_METHOD_GET:
            case HTTP_METHOD_DELETE:
                http_client_try_to_send(&msg);
                break;
            case HTTP_METHOD_POST:                      
            case HTTP_METHOD_PUT:
                if(msg.data) {
                    esp_http_client_set_post_field(client, msg.data, strlen(msg.data));
                }
                http_client_try_to_send(&msg);
                break;
            default:
                FLASH_LOGE("Unknown method: %d", msg.method);
                break;
            } /* end of switch/case */
        } /* end of if status=pdPASS */
    } /* end of for loop */

	ESP_LOGW(TAG, "HTTP CLIENT SEND TASK STOPPED");

	vTaskDelete( NULL );    
}

static void http_client_order_task( void * pvParameters ) {
	uint32_t order;
	BaseType_t xStatus;

    /* main processing loop */
    for(;;){
        xStatus = xQueueReceive( http_client_order_queue, &order, portMAX_DELAY );
        if( xStatus == pdPASS ){
            EventBits_t uxBits = xEventGroupGetBits(http_client_events);

            switch(order){
                case HC_ORDER_DISCONNECT:
                    if ((uxBits & HC_STATUS_OK) != 0) {
                        esp_http_client_close(client);
                        esp_http_client_cleanup(client);
                        client = NULL;
                        ESP_LOGI(TAG, "HC_ORDER_DISCONNECT");
                    }
                    break;
                case HC_ORDER_CONECT: 
                    if ((uxBits & HC_STATUS_OK) == 0) {
                        esp32_config_t* wifi_config = wifi_manager_get_config();

                        esp_http_client_config_t http_client_config = {
                            .event_handler = http_client_handler,
                            .user_data = output_buffer,
                            .transport_type = HTTP_TRANSPORT_OVER_TCP,
                            .host = wifi_config->server_address,
                            .port = wifi_config->server_port,
                            .timeout_ms = 1000,
                            .method = HTTP_METHOD_GET,
                            .url = get_full_path(CONFIG_HTTP_CLIENT_CONNECT_PATH),
                        };
                        
                        if (strcmp(wifi_config->server_auth, "no") == 0 || strcmp(wifi_config->server_auth, "ssl") == 0) {
                            http_client_config.auth_type = HTTP_AUTH_TYPE_NONE;
                        }else {
                            http_client_config.auth_type = HTTP_AUTH_TYPE_BASIC;
                            http_client_config.max_authorization_retries = -1;
                        }

                        if (strcmp(wifi_config->server_auth, "basic") == 0) {
                            http_client_config.username = wifi_config->client_username;
                            http_client_config.password = wifi_config->client_password;
                        }else
                        if (strcmp(wifi_config->server_auth, "ssl") == 0) {
                            http_client_config.transport_type = HTTP_TRANSPORT_OVER_SSL;
                            http_client_config.cert_pem = wifi_config->client_ca;
                            http_client_config.cert_len = strlen(wifi_config->client_ca)+1;
                            http_client_config.client_cert_pem = wifi_config->client_crt;
                            http_client_config.client_cert_len = strlen(wifi_config->client_crt)+1;
                            http_client_config.client_key_pem = wifi_config->client_key;
                            http_client_config.client_key_len = strlen(wifi_config->client_key)+1;       
#ifdef CONFIG_SKIP_COMMON_NAME_CHECK
                            http_client_config.skip_cert_common_name_check = true;
#endif
                        }

                        do {
                            client = esp_http_client_init(&http_client_config);
                            esp_http_client_set_header(client, "Content-Type", "application/json");
                            esp_err_t err = esp_http_client_perform(client);
                            if (err == ESP_OK) {
                                ESP_LOGI(TAG, "Conection success!");
                                xEventGroupSetBits(http_client_events, HC_STATUS_OK);

                                /* callback */
                                run_cb(cb_ready_ptr, NULL);
                                break;
                            }else {
                                ESP_LOGE(TAG, "Conection fail: %s", esp_err_to_name(err));
                                vTaskDelay(2000 / portTICK_RATE_MS);
                            }                           
                        } while(1);
                    }      
                    break;
            } /* end of switch/case */
        } /* end of if status=pdPASS */
    } /* end of for loop */

    ESP_LOGW(TAG, "HTTP CLIENT ORDER TASK STOPPED");

	vTaskDelete( NULL );    
}

void http_client_initialize() {
	/* memory allocation */
	http_client_send_queue = xQueueCreate( 4, sizeof(http_client_request_t) );  
    http_client_order_queue = xQueueCreate( 4, sizeof(uint32_t) );  

    /* subscribe to wifi manager events */
    wifi_manager_set_callback(WM_ORDER_HTTP_CLIENT_INIT, &cb_wifi_connect);
    wifi_manager_set_callback(WM_EVENT_STA_DISCONNECTED, &cb_wifi_lost);
    wifi_manager_set_callback(WM_ORDER_STOP_AP, &cb_wifi_lost);
    wifi_manager_set_callback(WM_ORDER_START_AP, &cb_wifi_lost);
    wifi_manager_set_callback(WM_EVENT_SCAN_DONE, &cb_wifi_lost);
    wifi_manager_set_callback(WM_ORDER_START_WIFI_SCAN, &cb_wifi_lost);

    /* create http client event group */
    http_client_events = xEventGroupCreate();    
    
    /* create http client order task */
    xTaskCreate(&http_client_order_task, "http_client_order_task", DEFAULT_CACHE_SIZE, NULL, WIFI_MANAGER_TASK_PRIORITY+1, &task_http_client_order);

    /* Wait WiFi ready*/
    xEventGroupWaitBits(
            http_client_events,         // The event group being tested.
            HC_WIFI_OK,                 // The bits within the event group to wait for.
            pdFALSE,                    // HC_WIFI_OK should be not cleared before returning.
            pdFALSE,                    // Don't wait for both bits, either bit will do.
            portMAX_DELAY );            // Wait until the bit be set.        

    /* create http client send task */
    xTaskCreate(&http_client_send_task, "http_client_send_task", DEFAULT_CACHE_SIZE, NULL, WIFI_MANAGER_TASK_PRIORITY+2, &task_http_client_send);
}
