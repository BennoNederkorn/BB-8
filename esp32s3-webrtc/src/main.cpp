extern "C"
{
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_camera.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "esp_peer.h"
#include "esp_crt_bundle.h"
}

// -- CONFIGURATION --
#define BLINK_GPIO GPIO_NUM_3

// --- Globals ---
static const char *TAG = "ESP32S3_WEBRTC_APP";

// --- Main Application ---
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Configuring ESP32-S3-EYE...");

    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT_OD); // Set the GPIO as an OPEN-DRAIN output

    ESP_LOGI(TAG, "Start Blinking 5 times...");

    for (int i = 0; i < 5; i++)
    {
        gpio_set_level(BLINK_GPIO, 0);
        ESP_LOGI(TAG, "LED OFF");
        vTaskDelay(500 / portTICK_PERIOD_MS);

        gpio_set_level(BLINK_GPIO, 1);
        ESP_LOGI(TAG, "LED ON");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}
