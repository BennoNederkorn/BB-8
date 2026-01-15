// -- CONFIGURATION --
#define BLINK_GPIO GPIO_NUM_3
#define WIFI_SSID "not.a.virus.exe"
#define WIFI_PASSWORD "password" // Yes I know, ...
#define BOARD_ESP32S3_WROOM 1
#define SIGNAL_SERVER_URL "wss://90.124.192.158:3000/" // Raspbery Pi at home
#define STUN_SERVER_URL "stun:stun.l.google.com:19302"

#include <string>

extern "C"
{
#include "esp_timer.h"
    // #include "esp_random.h"
    // #include "esp_webrtc_defaults.h"
    // #include "media_lib_os.h"

#include <string.h>
#include "esp_system.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h" // Include for Mutex
#include "driver/gpio.h"
#include "esp_log.h"

#include "esp_wifi.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_camera.h"
#include "esp_websocket_client.h"
#include "cJSON.h"
#include "esp_peer.h"
#include "esp_peer_types.h"
#include "esp_crt_bundle.h"
#include "esp_peer_default.h"
#include "camera_pinout.h"
#include "signaling_client.h" // Include our signaling helper
#include "esp_sntp.h"
}

// --- Globals ---
static const char *TAG = "ESP32S3_WEBRTC_APP";
static EventGroupHandle_t rtc_event_group;
const int RTC_CONNECTED_BIT = BIT0;
const int WIFI_CONNECTED_BIT = BIT1;

static SemaphoreHandle_t peer_mutex = NULL; // Mutex to protect peer handle
static esp_peer_handle_t peer = NULL;
static bool is_streaming = false;
static uint16_t video_stream_id = 0;

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
    .pixel_format = PIXFORMAT_JPEG, // YUV422,GRAYSCALE,RGB565,JPEG
    .frame_size = FRAMESIZE_VGA,

    .jpeg_quality = 12,                  // 0-63, for OV series camera sensors, lower number means higher quality
    .fb_count = 1,                       // When jpeg mode is used, if fb_count more than one, the driver will work in continuous mode. // allocates space for two images in memory.
    .fb_location = CAMERA_FB_IN_PSRAM,   // forces the image buffer to use larger External RAM.
    .grab_mode = CAMERA_GRAB_WHEN_EMPTY, // Buffer is only filled with new camera data if the CPU has finished reading the previous data.
    // .grab_mode = CAMERA_GRAB_LATEST;   // Captures the latest frame, discarding older ones.
    .sccb_i2c_port = -1,
};
#endif

// --- Function Prototypes ---
static void init_nvs(void);
static void wifi_init_sta(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void camera_init(void);
static void setup_time(void);
static void print_camera_image_hex(void);
static void signaling_server_connect(void);
static void setup_webrtc_peer(void);
static void video_stream_task(void *pvParameters);
static void webrtc_main_task(void *pvParameters); // New task prototype

// WebRTC Callbacks
static int peer_msg_handler(esp_peer_msg_t *msg, void *ctx);
static int peer_state_handler(esp_peer_state_t state, void *ctx);
static int peer_channel_open_handler(esp_peer_data_channel_info_t *ch, void *ctx);

// Helper to send JSON
static void send_signaling_json(const char *type, const char *payload_str, cJSON *payload_obj);

// static void replace_all(std::string &str, const std::string &from, const std::string &to)
// {
//     if (from.empty())
//         return;
//     size_t start_pos = 0;
//     while ((start_pos = str.find(from, start_pos)) != std::string::npos)
//     {
//         str.replace(start_pos, from.length(), to);
//         start_pos += to.length();
//     }
// }

// --- Main Application ---
extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "start app_main...");

    // Enable verbose logging for the WebRTC peer to debug state machine issues
    esp_log_level_set("esp_peer", ESP_LOG_VERBOSE);
    esp_log_level_set("PEER_DEF", ESP_LOG_VERBOSE); // Enable logs for the default implementation

    ESP_LOGI(TAG, "init the Non-Volatile Storage...");
    init_nvs();

    // Create mutex for thread safety
    peer_mutex = xSemaphoreCreateMutex();

    ESP_LOGI(TAG, "start connecting to wifi...");
    rtc_event_group = xEventGroupCreate();
    wifi_init_sta();
    ESP_LOGI(TAG, "waiting for WiFi...");
    xEventGroupWaitBits(rtc_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "WiFi connected!");

    ESP_LOGI(TAG, "Setting up time for DTLS...");
    setup_time();

    gpio_reset_pin(BLINK_GPIO);
    gpio_set_direction(BLINK_GPIO, GPIO_MODE_OUTPUT_OD); // Set the GPIO as an OPEN-DRAIN output

    ESP_LOGI(TAG, "Start Blinking 5 times...");

    for (int i = 0; i < 5; i++)
    {
        gpio_set_level(BLINK_GPIO, 0);
        ESP_LOGI(TAG, "LED OFF");
        vTaskDelay(200 / portTICK_PERIOD_MS);

        gpio_set_level(BLINK_GPIO, 1);
        ESP_LOGI(TAG, "LED ON");
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }

    ESP_LOGI(TAG, "starting the camera...");
    camera_init();
    print_camera_image_hex();

    ESP_LOGI(TAG, "start connecting to the signaling server...");
    signaling_server_connect();

    // Start the video task (it will wait for is_streaming flag to be true)
    xTaskCreate(video_stream_task, "video_stream", 8192, NULL, 5, NULL);

    // Start the WebRTC engine task which drives the esp_peer stack
    xTaskCreate(webrtc_main_task, "webrtc_main", 8192, NULL, 5, NULL);
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
    // esp_wifi_start() returns almost immediately—it
    // it does not wait for a connection to be established.
    // It just turns on the WiFi radio.
    ESP_ERROR_CHECK(esp_wifi_start());
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

// --- System Time Setup ---
// Required for SSL/DTLS certificate validation during the WebRTC handshake.
static void setup_time(void)
{
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    time_t now = 0;
    struct tm timeinfo = {0};
    int retry = 0;
    const int retry_count = 15;
    while (timeinfo.tm_year < (2016 - 1900) && ++retry < retry_count)
    {
        ESP_LOGI(TAG, "Waiting for system time to be set... (%d/%d)", retry, retry_count);
        vTaskDelay(2000 / portTICK_PERIOD_MS);
        time(&now);
        localtime_r(&now, &timeinfo);
    }
    ESP_LOGI(TAG, "Time set: %s", asctime(&timeinfo));
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
    // Warm up the camera: Skip the first few frames to allow Auto White Balance (AWB)
    // and Auto Exposure Control (AEC) to settle.
    ESP_LOGI(TAG, "Warming up camera (skipping frames)...");
    for (int i = 0; i < 10; i++)
    {
        camera_fb_t *temp_fb = esp_camera_fb_get();
        if (temp_fb)
        {
            esp_camera_fb_return(temp_fb);
        }
    }
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb)
    {
        ESP_LOGE(TAG, "Camera capture failed - no frame buffer returned");
        return;
    }
    ESP_LOGI(TAG, "Image captured!");
    ESP_LOGI(TAG, "buffer size: %d bytes", fb->len);
    ESP_LOGI(TAG, "width x height: %dx%d", fb->width, fb->height);
    ESP_LOGI(TAG, "pixel data format: %d", fb->format);
    // printf("--- START JPEG HEX ---\n");
    // for (size_t i = 0; i < fb->len; i++)
    // {
    //     // Print 02x (e.g., "A5") without spaces to make it compact
    //     // uint8_t * buf
    //     printf("%02X", fb->buf[i]);
    //     if ((i + 1) % 32 == 0)
    //         printf("\n");
    // }
    // printf("\n--- END JPEG HEX ---\n");

    esp_camera_fb_return(fb);
}

// --- Signaling Server ---
// Configures and initiates a secure, auto-reconnecting WebSocket connection to the signaling server.
static void signaling_server_connect(void)
{
    signaling_client_start(SIGNAL_SERVER_URL);
}

// --- WebRTC Logic ---

// Configures the WebRTC peer instance, ICE servers, and registers callbacks.
static void setup_webrtc_peer(void)
{
    ESP_LOGI(TAG, "LOCK");
    xSemaphoreTake(peer_mutex, portMAX_DELAY); // Lock

    if (peer)
    {
        ESP_LOGI(TAG, "destroy existing peer");
        esp_peer_close(peer);
        peer = NULL;
        video_stream_id = 0;
    }

    // Setup STUN server to help devices behind NATs find each other.
    esp_peer_ice_server_cfg_t ice_server = {};
    ice_server.stun_url = (char *)STUN_SERVER_URL;

    esp_peer_cfg_t peer_cfg = {
        .server_lists = &ice_server,                       // Should set to actual stun/turn servers
        .server_num = 1,                                   //  Number of ICE server
        .role = ESP_PEER_ROLE_CONTROLLED,                  // The ESP32 is waiting for an offer.
        .ice_trans_policy = ESP_PEER_ICE_TRANS_POLICY_ALL, // Gather all types of connection candidates
        // .video_info = {
        //     // H.264 is preferred for smooth streaming over Wi-Fi.
        //     // MJPEG is an alternative if the camera only supports JPEG.
        //     .codec = ESP_PEER_VIDEO_CODEC_H264,
        //     .width = 320,  // Resolution (e.g., QVGA or VGA)
        //     .height = 240,
        //     .fps = 15,     // Frame rate (15-20 is typical for ESP32)
        //     .dir = ESP_PEER_MEDIA_DIR_SEND_ONLY, // We usually don't see the user, only send video.
        // },
        .audio_dir = ESP_PEER_MEDIA_DIR_NONE, // Not using RTP audio
        .video_dir = ESP_PEER_MEDIA_DIR_NONE, // Not using RTP video
        .no_auto_reconnect = false,
        .enable_data_channel = true,    // The video is sent over the data channel, because binary images(MJPEG) cannot be encoded over the standard Video (RTP) track.
        .manual_ch_create = false,      // The HMI (Offerer) will create the Data channel labled as TODO. The ESP32S3 will accept it
        .ctx = NULL,                    // user context needed??? NULL &peers[idx] or rtc->ice_role
        .on_state = peer_state_handler, // For connection state changes.
        .on_msg = peer_msg_handler,     // For SDP answers and ICE candidates.
        // .on_video_info = peer_video_info_handler,
        // .on_audio_info = peer_audio_info_handler,
        // .on_audio_data = peer_audio_data_handler,
        // .on_video_data = peer_video_data_handler,
        .on_channel_open = peer_channel_open_handler,
        // .on_data = peer_data_handler, // Implement if receiving data from Angular
        // .on_channel_close = peer_channel_close_handler,
    };

    // Get default implementation (required for 1.2.6)
    const esp_peer_ops_t *ops = esp_peer_get_default_impl();

    // Initialize the peer
    ESP_ERROR_CHECK(esp_peer_open(&peer_cfg, ops, &peer));
    ESP_LOGI(TAG, "Peer initialized successfully");

    xSemaphoreGive(peer_mutex); // Unlock
    ESP_LOGI(TAG, "UNLOCK");
}

// --- Callbacks ---

// 1. Outgoing Signaling: Called when the ESP32 generates an SDP Answer or ICE Candidate.
// These messages must be sent to the remote peer via the signaling server.
static int peer_msg_handler(esp_peer_msg_t *msg, void *ctx) // TODO gemini deep research
{
    ESP_LOGI(TAG, "peer_msg_handler was called");
    if (msg->type == ESP_PEER_MSG_TYPE_SDP)
    {
        ESP_LOGI(TAG, "SDP Content: %s", (char *)msg->data); // msg->data contains the SDP string

        send_signaling_json("answer", (char *)msg->data, NULL);

        // // 1. Convert raw data to std::string for safe manipulation
        // std::string sdp_str((char *)msg->data, msg->size);

        // // 2. FORENSIC FIX: Force DTLS Active Role
        // // Replace 'setup:passive' with 'setup:active' to break the handshake deadlock.
        // replace_all(sdp_str, "a=setup:passive", "a=setup:active");

        // ESP_LOGI(TAG, "Munged SDP Content (Snippet):...%s...",
        //          sdp_str.substr(sdp_str.find("a=setup"), 20).c_str());

        // char *sdp_char_pointer = const_cast<char *>(sdp_str.c_str());

        // // 4. Send to Signaling Server (WebSocket)
        // send_signaling_json("answer", sdp_char_pointer, NULL);
    }
    else if (msg->type == ESP_PEER_MSG_TYPE_CANDIDATE)
    {
        // NOTE: To match your JSON schema {candidate, sdpMid, sdpMLineIndex},
        // we might need to parse the string or check if esp_peer provides a struct.
        // For now, we send the raw string. Angular should handle parsing or we adjust schema.

        // self-defined Signaling Message Schema
        // | Msg Type  | Direction | Payload Structure                | Description                                     |
        // | --------- | --------- | -------------------------------- | ----------------------------------------------- |
        // | offer     | UI -> ESP | {"sdp": "v=0..."}                | The Session Description Protocol Offer string.  |
        // | answer    | ESP -> UI | {"sdp": "v=0..."}                | The Session Description Protocol Answer string. |
        // | candidate | Bi-direct | {"candidate": "...", "sdpMid": "0", "sdpMLineIndex": 0} | An ICE Candidate object. |

        cJSON *payload = cJSON_CreateObject();
        cJSON_AddStringToObject(payload, "candidate", (char *)msg->data);
        // Defaults if not parsed
        cJSON_AddStringToObject(payload, "sdpMid", "0");
        cJSON_AddNumberToObject(payload, "sdpMLineIndex", 0);

        send_signaling_json("candidate", NULL, payload);
    }
    return 0;
}

// 2. State Change: Handles WebRTC connection state changes.
static int peer_state_handler(esp_peer_state_t state, void *ctx)
{
    switch (state)
    {
    case ESP_PEER_STATE_CLOSED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_CLOSED)", state);
        break;
    case ESP_PEER_STATE_DISCONNECTED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_DISCONNECTED)", state);
        break;
    case ESP_PEER_STATE_NEW_CONNECTION:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_NEW_CONNECTION)", state);
        break;
    case ESP_PEER_STATE_PAIRING:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_PAIRING)", state);
        break;
    case ESP_PEER_STATE_PAIRED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_PAIRED)", state);
        break;
    case ESP_PEER_STATE_CONNECTING:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_CONNECTING)", state);
        break;
    case ESP_PEER_STATE_CONNECTED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_CONNECTED)", state);
        break;
    case ESP_PEER_STATE_CONNECT_FAILED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_CONNECT_FAILED)", state);
        break;
    case ESP_PEER_STATE_DATA_CHANNEL_CONNECTED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_DATA_CHANNEL_CONNECTED)", state);
        break;
    case ESP_PEER_STATE_DATA_CHANNEL_OPENED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_DATA_CHANNEL_OPENED)", state);
        break;
    case ESP_PEER_STATE_DATA_CHANNEL_CLOSED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_DATA_CHANNEL_CLOSED)", state);
        break;
    case ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED:
        ESP_LOGI(TAG, "Peer state changed: %d (ESP_PEER_STATE_DATA_CHANNEL_DISCONNECTED)", state);
        break;
    default:
        break;
    }

    // Stop streaming if the connection or data channel drops
    if (state != ESP_PEER_STATE_CONNECTED && state != ESP_PEER_STATE_DATA_CHANNEL_OPENED)
    {
        is_streaming = false;
        gpio_set_level(BLINK_GPIO, 0);
    }
    return 0;
}

// 3. Data Channel Open: Called when the data channel is successfully established.
// We capture the stream_id here to know where to send video data.
static int peer_channel_open_handler(esp_peer_data_channel_info_t *ch, void *ctx)
{
    ESP_LOGI(TAG, "Data Channel Opened! Label: %s, ID: %d", ch->label, ch->stream_id);
    video_stream_id = ch->stream_id;
    is_streaming = true;
    gpio_set_level(BLINK_GPIO, 1);
    return 0;
}

// Helper function to construct and send a JSON signaling message via WebSocket.
static void send_signaling_json(const char *type, const char *payload_str, cJSON *payload_obj)
{
    ESP_LOGI(TAG, "send signaling json with type: %s", type);
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "type", type);
    if (payload_str)
        cJSON_AddStringToObject(root, "payload", payload_str);
    if (payload_obj)
        cJSON_AddItemToObject(root, "payload", payload_obj);
    char *json = cJSON_PrintUnformatted(root);
    signaling_send_message(json);
    free(json);
    cJSON_Delete(root);
}

// --- Signaling Message Handler ---
// This function is called by signaling_client.c when a full JSON message is received from the server.
// It handles the "Offer" from the browser and incoming ICE "Candidates".
extern "C" void handle_signaling_message(cJSON *root)
{
    ESP_LOGI(TAG, "start handling the signaling message");
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    if (!type_item || !cJSON_IsString(type_item))
    {
        ESP_LOGW(TAG, "Received JSON has no 'type' field.");
        return;
    }

    const char *type = type_item->valuestring;
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");

    if (strcmp(type, "offer") == 0)
    {
        ESP_LOGI(TAG, "Received Offer");

        // 1. Initialize Peer configuration
        setup_webrtc_peer();

        // 2. Pass the SDP Offer to the ESP Peer stack
        if (cJSON_IsString(payload_item))
        {
            ESP_LOGI(TAG, "LOCK");
            xSemaphoreTake(peer_mutex, portMAX_DELAY); // Lock before using peer
            if (peer)
            {
                esp_peer_msg_t msg = {
                    .type = ESP_PEER_MSG_TYPE_SDP,
                    .data = (uint8_t *)payload_item->valuestring,
                    .size = (int)strlen(payload_item->valuestring) + 1}; // Include null terminator

                ESP_LOGI(TAG, "Feeding SDP to Stack (len=%d): %s", msg.size, (char *)msg.data);

                esp_err_t ret = esp_peer_send_msg(peer, &msg);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to process Offer: %s", esp_err_to_name(ret));
                }
                else
                {
                    // 3. Signal the stack to process the offer and generate an answer
                    ESP_LOGI(TAG, "Offer passed to ESP Peer stack");
                    ret = esp_peer_new_connection(peer);

                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "Failed to initiate Answer generation: %s", esp_err_to_name(ret));
                    }
                    else
                    {
                        ESP_LOGI(TAG, "Answer generation requested.");
                    }
                }
                // Do I need to call this function in esp_peer.h?
                // esp_err_t esp_peer_set_remote_description(esp_peer_handle_t handle,
                //                                           esp_peer_sdp_type_t type,
                //                                           char *sdp_text,
                //                                           int len);
            }
            xSemaphoreGive(peer_mutex); // Unlock
            ESP_LOGI(TAG, "UNLOCK");
        }
    }
    else if (strcmp(type, "candidate") == 0)
    {
        // Handle incoming ICE candidates from the browser to help establish connectivity
        ESP_LOGI(TAG, "Received Candidate");
        xSemaphoreTake(peer_mutex, portMAX_DELAY); // Lock
        if (peer == NULL)
        {
            xSemaphoreGive(peer_mutex);
            return;
        }

        // The peer stack expects a JSON object with candidate, sdpMid, and sdpMLineIndex.
        // We should pass the whole payload object as a string.
        char *candidate_payload_str = cJSON_PrintUnformatted(payload_item);
        if (candidate_payload_str)
        {
            ESP_LOGI(TAG, "Feeding Candidate to Stack (len=%d): %s", strlen(candidate_payload_str) + 1, candidate_payload_str);
            esp_peer_msg_t msg = {.type = ESP_PEER_MSG_TYPE_CANDIDATE, .data = (uint8_t *)candidate_payload_str, .size = (int)strlen(candidate_payload_str) + 1}; // Include null terminator
            esp_err_t ret = esp_peer_send_msg(peer, &msg);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to add candidate: %s", esp_err_to_name(ret));
            }
            else
            {
                ESP_LOGI(TAG, "Candidate passed to ESP Peer stack");
                // ret = esp_peer_new_connection(peer);
                // if (ret != ESP_OK)
                // {
                //     ESP_LOGE(TAG, "Failed to initiate Answer generation: %s", esp_err_to_name(ret));
                // }
                // else
                // {
                //     ESP_LOGI(TAG, "Answer generation requested.");
                // }
            }
            free(candidate_payload_str);
        }
        else
        {
            ESP_LOGE(TAG, "Failed to stringify candidate payload");
        }
        xSemaphoreGive(peer_mutex); // Unlock
    }
    else
    {
        ESP_LOGI(TAG, "Received Something else");
    }
}

// --- Video Task ---
// Captures frames from the camera and sends them via the WebRTC Data Channel.
static void video_stream_task(void *pvParameters)
{
    // We use a specific label for the video data channel.
    // Note: In standard WebRTC, the initiator (Angular) usually creates the Data Channel.
    // However, we can just send data to the first open channel or a specific one.
    // esp_peer usually handles the underlying channel management.
    ESP_LOGI(TAG, "started video_stream_task...");

    while (1)
    {
        if (is_streaming && peer && video_stream_id > 0)
        {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb)
            {
                ESP_LOGE(TAG, "Camera capture failed");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            // Send the JPEG buffer via the Data Channel
            esp_peer_data_frame_t frame = {
                .type = ESP_PEER_DATA_CHANNEL_DATA,
                .stream_id = video_stream_id,
                .data = fb->buf,
                .size = (int)fb->len};

            esp_err_t ret = esp_peer_send_data(peer, &frame);
            if (ret != ESP_OK)
            {
                ESP_LOGW(TAG, "Failed to send frame: %s", esp_err_to_name(ret));
            }

            esp_camera_fb_return(fb);

            // Frame rate control (e.g., ~15 FPS)
            vTaskDelay(pdMS_TO_TICKS(66));
        }
        else
        {
            // Idle wait if not streaming
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

// --- WebRTC Engine Task ---
// Periodically calls the main loop of the ESP Peer stack to process events and timers.
static void webrtc_main_task(void *pvParameters)
{
    ESP_LOGI(TAG, "WebRTC Engine Task Started");
    while (1)
    {
        // We only lock if we are going to do work, to avoid blocking the signaling task unnecessarily
        if (xSemaphoreTake(peer_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            if (peer)
            {
                esp_err_t ret = esp_peer_main_loop(peer);
                if (ret != ESP_OK)
                {
                    ESP_LOGW(TAG, "esp_peer_main_loop error: %s", esp_err_to_name(ret));
                }
            }
            xSemaphoreGive(peer_mutex);
        }

        // Optional: Check stack usage to ensure we aren't overflowing
        // if (uxTaskGetStackHighWaterMark(NULL) < 500) {
        //     ESP_LOGW(TAG, "WebRTC Task Stack Low: %d", uxTaskGetStackHighWaterMark(NULL));
        // }

        // Yield to let other tasks run. 10-20ms is usually a good balance for WebRTC.
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}