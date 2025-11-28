// -- CONFIGURATION --
#define BLINK_GPIO GPIO_NUM_3
#define WIFI_SSID "not.a.virus.exe"
#define WIFI_PASSWORD "12345678!" // Yes I know, ...
#define BOARD_ESP32S3_WROOM 1

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
#include "camera_pinout.h"
}

// --- Globals ---
static const char *TAG = "ESP32S3_WEBRTC_APP";
static EventGroupHandle_t rtc_event_group;
const int WIFI_CONNECTED_BIT = BIT1;
#if ESP_CAMERA_SUPPORTED
static camera_config_t camera_config = {
    .pin_pwdn = CAM_PIN_PWDN,
    .pin_reset = CAM_PIN_RESET,
    .pin_xclk = CAM_PIN_XCLK,
    .pin_sccb_sda = CAM_PIN_SIOD,
    .pin_sccb_scl = CAM_PIN_SIOC,

    .pin_d7 = CAM_PIN_D7,
    .pin_d6 = CAM_PIN_D6,
    .pin_d5 = CAM_PIN_D5,
    .pin_d4 = CAM_PIN_D4,
    .pin_d3 = CAM_PIN_D3,
    .pin_d2 = CAM_PIN_D2,
    .pin_d1 = CAM_PIN_D1,
    .pin_d0 = CAM_PIN_D0,
    .pin_vsync = CAM_PIN_VSYNC,
    .pin_href = CAM_PIN_HREF,
    .pin_pclk = CAM_PIN_PCLK,

    // XCLK 20MHz or 10MHz for OV2640 double FPS (Experimental)
    .xclk_freq_hz = 20000000,
    .ledc_timer = LEDC_TIMER_0,
    .ledc_channel = LEDC_CHANNEL_0,
    // PIXFORMAT_JPEG: camera compresses the image into a JPEG before sending it to the ESP32.
    // PIXFORMAT_RAW: A raw VGA image is ~600KB. A JPEG VGA image is ~30KB.
    .pixel_format = PIXFORMAT_RGB565, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,

    .jpeg_quality = 12,                  // 0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,                       // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode. // allocates space for two images in memory.
    .fb_location = CAMERA_FB_IN_PSRAM,   // forces the image buffer to use larger External RAM.
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY, // Buffer is only filled with new camera data if the CPU has finished reading the previous data.
    // .grab_mode = CAMERA_GRAB_LATEST;   // Captures the latest frame, discarding older ones.
};
#endif

// --- Function Prototypes ---
static void init_nvs(void);
static void wifi_init_sta(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void camera_init(void);
static void print_camera_image_hex(void);

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

    ESP_LOGI(TAG, "starting the camera...");
    camera_init();
    print_camera_image_hex();
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

// --- OV2640 Camera ---
static void camera_init(void)
{
    // https://github.com/espressif/esp32-camera?tab=readme-ov-file#initialization

    esp_err_t err = esp_camera_init(&camera_config); // apply the settings and boot up the camera
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed: 0x%x", err);
        return;
    }
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    ESP_LOGI(TAG, "Camera ready");
}

static void print_camera_image_hex(void)
{
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed - no frame buffer returned");
        return;
    }
    ESP_LOGI(TAG, "Image captured! Size: %d bytes, Format: %d", fb->len, fb->format);
    ESP_LOGI(TAG, "\n--- START JPEG HEX ---\n");
    for (size_t i = 0; i < fb->len; i++)
    {
        // Print 02x (e.g., "A5") without spaces to make it compact
        printf("%02x", fb->buf[i]);
    }
    ESP_LOGI(TAG, "\n--- END JPEG HEX ---\n");

    esp_camera_fb_return(fb);
}