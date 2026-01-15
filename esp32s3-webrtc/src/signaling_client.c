#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "signaling_client.h"

#define TAG "SIGNALING"

static esp_websocket_client_handle_t client = NULL;
static bool is_connected = false;

// Buffer to reassemble fragmented WebSocket frames
static char *rx_buffer = NULL;
static int rx_buffer_len = 0;

// Forward declaration of the function that handles complete JSON messages
// This function should be implemented in your main app logic or registered via callback
extern const uint8_t server_cert_pem_start[] asm("_binary_cert_pem_start");
extern const uint8_t server_cert_pem_end[] asm("_binary_cert_pem_end");

extern void handle_signaling_message(cJSON *root);

// Handles incoming WebSocket data events.
// Reassembles fragmented frames into a complete message buffer and parses it as JSON.
static void process_rx_data(const char *data, int len, int payload_len, int payload_offset)
{
    // 1. Allocate buffer on first frame
    if (rx_buffer == NULL)
    {
        rx_buffer = malloc(payload_len + 1);
        if (rx_buffer == NULL)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for RX buffer");
            return;
        }
        rx_buffer_len = 0;
    }

    // 2. Copy current chunk
    if (rx_buffer_len + len <= payload_len)
    {
        memcpy(rx_buffer + rx_buffer_len, data, len);
        rx_buffer_len += len;
    }
    else
    {
        ESP_LOGE(TAG, "RX buffer overflow");
        free(rx_buffer);
        rx_buffer = NULL;
        rx_buffer_len = 0;
        return;
    }

    // 3. Check if message is complete
    if (rx_buffer_len == payload_len)
    {
        rx_buffer[rx_buffer_len] = '\0'; // Null-terminate
        ESP_LOGI(TAG, "Received full message: %s", rx_buffer);

        cJSON *root = cJSON_Parse(rx_buffer);
        if (root)
        {
            handle_signaling_message(root);
            cJSON_Delete(root);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to parse JSON");
        }

        // Cleanup
        free(rx_buffer);
        rx_buffer = NULL;
        rx_buffer_len = 0;
    }
}

// WebSocket event handler callback
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id)
    {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_CONNECTED");
        is_connected = true;
        break;
    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_DISCONNECTED");
        is_connected = false;
        if (rx_buffer)
        {
            free(rx_buffer);
            rx_buffer = NULL;
            rx_buffer_len = 0;
        }
        break;
    case WEBSOCKET_EVENT_DATA:
        // Handle data events (text only for signaling)
        if (data->op_code == 0x1 || data->op_code == 0x0)
        { // Text frame or Continuation
            process_rx_data(data->data_ptr, data->data_len, data->payload_len, data->payload_offset);
        }
        break;
    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGI(TAG, "WEBSOCKET_EVENT_ERROR");
        break;
    }
}

// Initializes and starts the WebSocket client
esp_err_t signaling_client_start(const char *uri)
{
    if (client != NULL)
    {
        ESP_LOGW(TAG, "Client already started");
        return ESP_OK;
    }

    esp_websocket_client_config_t websocket_cfg = {};
    websocket_cfg.uri = uri;

    // Important for Render.com or other hosted services using SSL
    if (strncmp(uri, "wss://", 6) == 0)
    {
        // For development with a self-signed certificate, we embed the server's public cert.
        websocket_cfg.cert_pem = (const char *)server_cert_pem_start;
    }

    ESP_LOGI(TAG, "Connecting to %s...", websocket_cfg.uri);

    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, (void *)client);

    return esp_websocket_client_start(client);
}

esp_err_t signaling_client_stop(void)
{
    if (client == NULL)
        return ESP_OK;

    esp_websocket_client_stop(client);
    esp_websocket_client_destroy(client);
    client = NULL;
    is_connected = false;
    return ESP_OK;
}

// Sends a text message over the WebSocket connection
esp_err_t signaling_send_message(const char *json_message)
{
    if (!is_connected || client == NULL)
    {
        ESP_LOGE(TAG, "Cannot send, not connected");
        return ESP_FAIL;
    }

    int len = strlen(json_message);
    int ret = esp_websocket_client_send_text(client, json_message, len, portMAX_DELAY);

    if (ret < 0)
    {
        ESP_LOGE(TAG, "Failed to send message");
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool signaling_is_connected(void)
{
    return is_connected;
}

// Stub for the message handler - to be implemented in app_main or peer logic
__attribute__((weak)) void handle_signaling_message(cJSON *root)
{
    ESP_LOGW(TAG, "handle_signaling_message not implemented");
}