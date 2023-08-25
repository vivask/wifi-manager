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
 * @brief Initialize ntp client
 * @return true if initialize success, false otherwise.
 */
esp_err_t initialize_ntp(const char* timezone, const char* ntp_server_address);

/**
 * @brief Get curren system time
 */
time_t get_local_datetime();

/**
 * @brief Get time initialization
 */
time_t get_sntp_time_init();

/**
 * @brief Get ntp client status
 * @return true if ntp client initialize success, false otherwise.
 */
bool get_sntp_status();

time_t get_sntp_time_init();
time_t get_sntp_tiks_init();

char* date_time_format(char* date_time, time_t t);

#ifdef __cplusplus
}

#endif

/**@}*/

