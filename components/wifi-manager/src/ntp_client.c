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
#include <esp_sntp.h>

#include "flash.h"
#include "ntp_client.h"

static const char TAG[] = "ntp_client";

static time_t _sntp_init_time = 0;
static time_t _sntp_init_tiks = 0;
static bool _sntp_started = false;
static bool _sntp_init_fail = true;

static char TIMEZONE[64] = "undefined";

bool get_sntp_status() {
	return !_sntp_init_fail;
}

// void print_time(time_t time){
// 	struct tm timeinfo = {0};
// 	localtime_r(&time, &timeinfo);
// 	ESP_LOGI(TAG, "20%d-%d-%d %d:%d:%d", timeinfo.tm_year - 100, timeinfo.tm_mon + 1,
//                  timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
// }

char* date_time_format(char* date_time, time_t t) {
    time_t now = (t==0) ? get_local_datetime() : t;
	struct tm ti = {0};	
    localtime_r(&now, &ti);
    sprintf(date_time, "20%02d-%02d-%02dT%02d:%02d:%02d+03:00", ti.tm_year - 100, ti.tm_mon + 1,
           ti.tm_mday, ti.tm_hour, ti.tm_min, ti.tm_sec);
    return date_time;
}

esp_err_t initialize_ntp(const char* timezone, const char* ntp_server_address)
{   
    if (strlen(timezone) > sizeof(TIMEZONE)) {
        ESP_LOGE(TAG, "Invalid timezone: %s", timezone);
        return ESP_FAIL;
    }
    strcpy(TIMEZONE, timezone);

    ESP_LOGI(TAG, "TIMEZONE: %s", TIMEZONE);

    if(!_sntp_started){
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        //char* ntp_server_address = wifi_manager_get_ntp_server_address();
        ESP_LOGI(TAG, "Initializing SNTP: %s", ntp_server_address);
        if(ntp_server_address){
            sntp_setservername(0, ntp_server_address);
        }else{
			ESP_LOGE(TAG, "NTP Server: %s initializing fail", ntp_server_address);
            return ESP_FAIL;
        }
        _sntp_started = true;
        sntp_init();
    }else{
        ESP_LOGI(TAG, "Syncing System Time");
        sntp_sync_time(NULL);
    }

    time_t now = 0;
    struct tm timeinfo = {0};
	int retry = 0;
    const int retry_count = 15;

	time(&_sntp_init_tiks);

    while(timeinfo.tm_year < (2016 - 1900) && ++retry <= retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)",
                retry, retry_count);
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
		_sntp_init_tiks++;
    }   

	_sntp_init_fail = (retry > retry_count);

	//ESP_LOGI(TAG, "SNTP init: %d, Tiks: %d, Retry: %d", _sntp_init_fail, retry, retry_count);
	//ESP_LOGI(TAG, "Set Timezone: %s", wifi_manger_config_ipv4->timezone);
	setenv("TZ", TIMEZONE, 1);
    tzset();
    time(&now);
	_sntp_init_time = now;
    localtime_r(&now, &timeinfo);


	ESP_LOGI(TAG, "Now: 20%02d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year - 100, timeinfo.tm_mon + 1,
                 timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
	FLASH_LOGI("SNTP Iniialized: 20%02d-%02d-%02d %02d:%02d:%02d", timeinfo.tm_year - 100, timeinfo.tm_mon + 1,
                 timeinfo.tm_mday, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

    return ESP_OK;    
}

time_t get_local_datetime()
{
    int retry = 0;
    const int retry_count = 15;
    time_t now = 0;
    struct tm timeinfo = {0};
    setenv("TZ", TIMEZONE, 1);
    tzset();
    while(timeinfo.tm_year < (2016 - 1900) && ++retry <= retry_count)
    {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }   
	return now; 
}

time_t get_sntp_time_init() {
	return _sntp_init_time;	
}

time_t get_sntp_tiks_init() {
	return _sntp_init_tiks;	
}