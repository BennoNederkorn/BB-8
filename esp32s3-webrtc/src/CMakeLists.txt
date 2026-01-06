# FILE(GLOB_RECURSE app_sources ${CMAKE_SOURCE_DIR}/src/*.*)
# idf_component_register(SRCS ${app_sources}
#                        INCLUDE_DIRS ".")
idf_component_register(SRCS "main.cpp" "signaling_client.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_wifi nvs_flash esp_netif esp_event mbedtls json espressif__esp32-camera espressif__esp_websocket_client espressif__esp_peer driver freertos log
                       EMBED_TXTFILES "cert.pem")