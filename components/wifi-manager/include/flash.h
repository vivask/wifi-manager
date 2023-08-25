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
#include <esp_err.h>


#ifdef __cplusplus
extern "C" {
#endif

#define ENABLE_LOGGING CONFIG_USE_FLASH_LOGGING

#define FLASH_LOG_SAVE		0x00000001U
#define FLASH_LOG_READ		0x00000002U
#define FLASH_LOG_CLEAR		0x00000004U

#ifdef CONFIG_USE_FLASH_LOGGING
/**
 * @brief Structure used to store one logging message to flash.
 */
typedef struct{
	char message[CONFIG_LOG_MESSAGE_MAX_LEN];
	char type;
	time_t date_time;
}log_message_t;
#endif

typedef void (*send_msg)(const time_t, const char, const char*);

/**
 * @brief Structure used to store one message in the queue.
 */
typedef struct _flash_log_request_t {
	uint32_t 		order;
	log_message_t 	msg; 
	send_msg 		cb_ptr;
}flash_log_request_t;

/**
 * @brief saves to flash logging error message
*/
void init_flash();

/**
 * @brief saves to flash logging error message
*/
void FLASH_LOGE(const char* format, ...);

/**
 * @brief saves to flash logging warning message
*/
void FLASH_LOGW(const char* format, ...);

/**
 * @brief saves to flash logging info message
*/
void FLASH_LOGI(const char* format, ...);

/**
 * @brief read all flashed logging and send callback function send_message
*/
void read_flash_log(send_msg func);

/**
 * @brief clear flash logging file
*/
void clear_flash_log();


#ifdef __cplusplus
}

#endif

/**@}*/

