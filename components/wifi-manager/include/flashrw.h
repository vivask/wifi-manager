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
#pragma once

#include <stdint.h>
#include <esp_err.h>


#ifdef __cplusplus
extern "C" {
#endif


/**
 * @brief saves to flash config file
*/
esp_err_t save_flash_data(void* data, const size_t sz, const char* fName);

/**
 * @brief saves to flash config file
*/
esp_err_t save_flash_json_data(const char* json, const char* fName);

/**
 * @brief read flashed file
*/
char* read_flash_json_data(const char* fName);

/**
 * @brief Return true if file exist
*/
bool is_flash_file_exist(const char* name);

/**
 * @brief clear file context
*/
void clear_flash_file(const char* name);

#ifdef __cplusplus
}

#endif

/**@}*/

