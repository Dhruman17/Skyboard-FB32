# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS 
        "main.cpp"
        "config.cpp"
        "error_manager.cpp"
        "firebase_manager.cpp"
        "hardware_manager.cpp"
        "light_manager.cpp"
        "network_manager.cpp"
        "ota_manager.cpp"
        "preferences_manager.cpp"
        "sensor_coms.cpp"
        "system_manager.cpp"
        "unit_manager.cpp"
    INCLUDE_DIRS 
        "."
    REQUIRES 
        driver
        esp_wifi
        nvs_flash
        esp_http_client
        esp_https_ota
        json
        freertos
)
