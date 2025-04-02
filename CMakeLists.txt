cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(Skyboard-FB32)

# Add source files
idf_component_register(
    SRCS 
        "src/main.cpp"
        "src/config.cpp"
        "src/error_manager.cpp"
        "src/firebase_manager.cpp"
        "src/hardware_manager.cpp"
        "src/light_manager.cpp"
        "src/network_manager.cpp"
        "src/ota_manager.cpp"
        "src/preferences_manager.cpp"
        "src/sensor_coms.cpp"
        "src/system_manager.cpp"
        "src/unit_manager.cpp"
    INCLUDE_DIRS 
        "src"
    REQUIRES 
        driver
        esp_wifi
        nvs_flash
        esp_http_client
        esp_https_ota
        json
        freertos
)
