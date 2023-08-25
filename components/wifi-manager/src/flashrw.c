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

#include "flashrw.h"

static const char *TAG = "flashrw";

esp_err_t save_flash_data(void* data, const size_t sz, const char* fName){
	esp_err_t esp_err = ESP_FAIL;
    FILE *f = fopen(fName, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for writing", fName);
        esp_err = ESP_ERR_FLASH_OP_FAIL;
    }else{
        fwrite(data, sz, 1, f);
        fclose(f);
        ESP_LOGI(TAG, "File: %s wrote success", fName);
        esp_err = ESP_OK;
    }
	return esp_err;
}

esp_err_t save_flash_json_data(const char* json, const char* fName) {
	esp_err_t esp_err = ESP_FAIL;
    FILE *f = fopen(fName, "wb");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for writing", fName);
        esp_err = ESP_ERR_FLASH_OP_FAIL;
    }else{
        //int wrote = fprintf(f, "%s", json);
        int wrote = fwrite(json, strlen(json)*sizeof(char), 1, f);
        fclose(f);
        if (wrote == 0) {
            ESP_LOGE(TAG, "File: %s write error", fName);
            esp_err = ESP_FAIL;
        }else {
            ESP_LOGI(TAG, "File: %s wrote success", fName);
            esp_err = ESP_OK;
        }
    }
	return esp_err;
}

char* read_flash_json_data(const char* fName) {
	char* json = NULL;
    FILE *f = fopen(fName, "r");
    if (f == NULL) {
        ESP_LOGE(TAG, "Failed to open file %s for reading", fName);
    }else{
        fseek(f, 0L, SEEK_END); 
        size_t size = ftell(f);
        if (size == 0) {
            ESP_LOGE(TAG, "File %s is empty", fName);
            fclose(f);
            return NULL;
        }
        fseek(f, 0L, SEEK_SET);
        json = (char*)malloc(size+1);
        fread(json, size, 1, f);
        fclose(f);
        json[size] = '\0';
    }
	return json;
}

bool is_flash_file_exist(const char* name) {
	FILE *f = fopen(name, "r");
	if (f != NULL) {
		fclose(f);
		return true;
	}
	return false;
}

void clear_flash_file(const char* name){
    FILE *f = fopen(name, "w");
    fclose(f);
}