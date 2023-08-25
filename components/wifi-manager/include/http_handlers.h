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

#define FILE_PATH_MAX (ESP_VFS_PATH_MAX + 128)
#define SCRATCH_BUFSIZE (10240)
#define MAX_ERROR_TEXT (256)

typedef struct rest_server_context {
    char base_path[ESP_VFS_PATH_MAX + 1];
    char scratch[SCRATCH_BUFSIZE];
} rest_server_context_t;

esp_err_t wifi_access_points_get_handler(httpd_req_t *req);

esp_err_t esp32_setup_wifi_ca_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_wifi_crt_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_wifi_key_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_wifi_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_ipv4_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_http_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_http_ca_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_http_crt_post_handler(httpd_req_t *req);

esp_err_t esp32_setup_http_key_post_handler(httpd_req_t *req);

#ifdef __cplusplus
}
#endif

/**@}*/
