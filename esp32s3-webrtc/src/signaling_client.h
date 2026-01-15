#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    // Starts the WebSocket client connection to the given URI
    esp_err_t signaling_client_start(const char *uri);

    // Stops the WebSocket client
    esp_err_t signaling_client_stop(void);

    // Sends a JSON string message to the signaling server
    esp_err_t signaling_send_message(const char *json_message);

    // Checks if the client is currently connected
    bool signaling_is_connected(void);

#ifdef __cplusplus
}
#endif