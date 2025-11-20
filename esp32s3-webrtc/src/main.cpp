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
#define WIFI_SSID "not.a.virus.exe"
#define WIFI_PASSWORD "12345678!" // Yes I know, ...

// --- Globals ---
static const char *TAG = "ESP32S3_WEBRTC_APP";
static EventGroupHandle_t rtc_event_group;
const int WIFI_CONNECTED_BIT = BIT1;

// --- Function Prototypes ---
static void wifi_init_sta(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void init_nvs(void);

// --- Main Application ---
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "start app_main...");
    ESP_LOGI(TAG, "init the Non-Volatile Storage...");
    init_nvs();

    ESP_LOGI(TAG, "start connecting to wifi...");
    rtc_event_group = xEventGroupCreate();
    wifi_init_sta();

    ESP_LOGI(TAG, "waiting for WiFi...");
    xEventGroupWaitBits(rtc_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");

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

/*
 * Inside the NVS (Non-Volatile Storage) are the wifi credentials and
 * the radio frequency calibration data to tune the radio stored.
 * Without initialisation, the WiFi might fail to start or have poor range.
 * It is also needed because we changed the partition tables: see huge_app.csv
 */
static void init_nvs(void)
{
    esp_err_t ret = nvs_flash_init();                                             // Initialize the default NVS partition.
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) // If the storage is corrupted, wipe it clean and try again.
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
}

// --- WiFi ---
static void wifi_init_sta(void)
{
    // initialize the underlying TCP/IP stack (LwIP) and the system event loop
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta(); // Creates the network interface for Station mode

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // register event handlers
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {};
    strcpy((char *)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char *)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA)); // set client mode
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start()); // turn on WiFi radio
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Connecting to WiFi...");
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGI(TAG, "Reconnecting WiFi...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(rtc_event_group, WIFI_CONNECTED_BIT); // signal to the app_main that ESP32S3 is now online
    }
}
