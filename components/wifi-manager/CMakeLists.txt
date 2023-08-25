if(IDF_VERSION_MAJOR GREATER_EQUAL 4)
    idf_component_register(
        SRC_DIRS src
        REQUIRES log wpa_supplicant lwip esp_http_server vfs nvs_flash spiffs json esp_http_client esp-tls esp_https_ota app_update
        INCLUDE_DIRS include
        )
else()
    set(COMPONENT_SRCDIRS src) 
    set(COMPONENT_ADD_INCLUDEDIRS include)
    set(COMPONENT_REQUIRES log wpa_supplicant lwip esp_http_server vfs nvs_flash spiffs json esp_http_client esp-tls esp_https_ota app_update)
    register_component()
endif()
set(WEB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../wifi-manager/dist")
if(CONFIG_WEB_USE_STORE)
    set(MOUNT_POINT ${CONFIG_WEB_STORE_MOUNT_POINT})
else()
    set(MOUNT_POINT ${CONFIG_STORE_MOUNT_POINT})
endif()
if(EXISTS ${WEB_DIR})
    spiffs_create_partition_image(${MOUNT_POINT} ${WEB_DIR} FLASH_IN_PROJECT)
else()
    message(FATAL_ERROR "${WEB_DIR} doesn't exit. Please run 'npm run build' in web source dir")
endif()