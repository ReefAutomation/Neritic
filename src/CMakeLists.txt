# This file was automatically generated for projects
# without default 'CMakeLists.txt' file.

FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "."
    REQUIRES
        esp_wifi
        esp_netif
        esp_event
        esp_http_server
        esp_http_client
        app_update
        nvs_flash
        lwip
        esp_timer
        esp_driver_rmt
        led_strip
    EMBED_TXTFILES
        "../dist/index.html"
        "../dist/index.js.gz"
        "../dist/style.css.gz"
        "../.pio/.version"
        "../defaults/presets.json"
        "../defaults/timezones.json"
        "../defaults/config.json"
)
