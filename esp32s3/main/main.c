#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"

// ESP32-S3-EYE: https://github.com/espressif/esp-who/blob/master/docs/en/get-started/ESP32-S3-EYE_Getting_Started_Guide.md
// GPIO3 that can be used to configure LED statuses
#define BLINK_GPIO GPIO_NUM_3

static const char *TAG = "blink";

void app_main(void)
{
    ESP_LOGI(TAG, "Configuring ESP32-S3-EYE...");

    gpio_reset_pin(BLINK_GPIO);

    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT_OD); // Set the GPIO as an OPEN-DRAIN output

    ESP_LOGI(TAG, "Start Blinking...");

    while (1)
    {
        gpio_set_level(BLINK_GPIO, 0); // Open-drain logic is inverted: LOW to sink current and turn the LED ON.
        ESP_LOGI(TAG, "LED ON");
        vTaskDelay(500 / portTICK_PERIOD_MS);

        gpio_set_level(BLINK_GPIO, 1); // HIGH-Z to stop sinking current and turn the LED OFF.
        ESP_LOGI(TAG, "LED OFF");
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
}