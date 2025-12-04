// -- CONFIGURATION --
#define BLINK_GPIO GPIO_NUM_3
#define WIFI_SSID "not.a.virus.exe"
#define WIFI_PASSWORD "12345678!" // Yes I know, ...
#define BOARD_ESP32S3_WROOM 1
#define SIGNAL_SERVER_URL "wss://webrtc-handshakeserver.onrender.com"
#define STUN_SERVER_URL "stun:stun.l.google.com:19302"

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
#include "esp_peer_default.h"
#include "camera_pinout.h"
}

// --- Globals ---
static const char *TAG = "ESP32S3_WEBRTC_APP";
static EventGroupHandle_t rtc_event_group;
const int RTC_CONNECTED_BIT = BIT0;
const int WIFI_CONNECTED_BIT = BIT1;
static esp_websocket_client_handle_t ws_client;
static esp_peer_handle_t peer = NULL;
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
};
#endif

// --- Function Prototypes ---
static void init_nvs(void);
static void wifi_init_sta(void);
static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data);
static void camera_init(void);
static void print_camera_image_hex(void);
static void signaling_server_connect(void);
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void video_stream_task(void *pvParameters);
static int on_peer_state_change(esp_peer_state_t state, void *ctx);
static int on_peer_msg(esp_peer_msg_t *msg, void *ctx);
static int on_peer_data(esp_peer_data_frame_t *frame, void *ctx);

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
    // print_camera_image_hex();
    ESP_LOGI(TAG, "start connecting to the signaling server...");
    signaling_server_connect();
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

// --- Signaling Server ---
// configures and initiates a secure, auto-reconnecting WebSocket connection to the signaling server.
static void signaling_server_connect(void)
{
    ESP_LOGI(TAG, "Connecting to: %s", SIGNAL_SERVER_URL);

    esp_websocket_client_config_t ws_cfg = {};
    memset(&ws_cfg, 0, sizeof(ws_cfg)); // zero-out the structure to ensure all fields start with a known value.

    ws_cfg.uri = SIGNAL_SERVER_URL;
    // configure security for a secure WebSocket (WSS) connection.
    // This attaches a bundle of trusted root certificates to verify the server's identity.
    ws_cfg.crt_bundle_attach = esp_crt_bundle_attach;
    ws_cfg.network_timeout_ms = 30000;
    ws_cfg.buffer_size = 8192;             // Increase buffer size to 8KB to reduce fragmentation
    ws_cfg.disable_auto_reconnect = false; // enable automatic reconnection

    ESP_LOGI(TAG, "WebSocket config: timeout=%d, skip_cert=%d",
             ws_cfg.network_timeout_ms, ws_cfg.skip_cert_common_name_check);

    ws_client = esp_websocket_client_init(&ws_cfg); // `ws_client` is a global handle to the client instance.
    if (ws_client == NULL)
    {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        return;
    }

    // Register an event handler to tell the WebSocket client to call `websocket_event_handler` for any event
    // (e.g., connected, received data, disconnected).
    esp_websocket_register_events(ws_client, WEBSOCKET_EVENT_ANY, websocket_event_handler, ws_client);

    // Start the client: This begins the non-blocking connection attempt.
    // The function will return immediately, and the connection status will be reported later through the event handler.
    esp_err_t err = esp_websocket_client_start(ws_client);
    ESP_LOGI(TAG, "WebSocket start result: %d (%s)", err, esp_err_to_name(err));
}

// This is the central command post for WebRTC signaling. It listens for two main commands from the signaling server:
// - offer: A request from a browser to start a new WebRTC session.
//   The handler responds by creating and configuring a local esp_peer instance and feeding it the offer.
// - candidate: A suggestion for a network path to connect directly to the browser.
//   The handler passes this information to the esp_peer instance to help it establish the connection.
static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    // Static buffer to reassemble fragmented messages
    static char *reassembly_buffer = NULL;
    static int reassembly_buffer_len = 0;
    static int total_payload_len = 0;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    switch (event_id)
    {
    case WEBSOCKET_EVENT_CONNECTED: // Connection Established
        ESP_LOGI(TAG, "Signaling connected");
        break;

    case WEBSOCKET_EVENT_DATA: // Data Received
    {
        // ESP_LOGI(TAG, "data->payload_len: %d bytes.", data->payload_len);
        // ESP_LOGI(TAG, "data->data_len: %d bytes.", data->data_len);
        // ESP_LOGI(TAG, "data->payload_offset: %d bytes.", data->payload_offset);
        // ESP_LOGI(TAG, "data->data_ptr: %d bytes.", data->data_ptr);
        // Ignore empty messages, which can be sent as keep-alives.
        if (data->payload_len == 0 && data->data_len == 0)
        {
            ESP_LOGI(TAG, "Received empty message, ignoring.");
            return;
        }
        // ESP_LOGI(TAG, "Received data: %.*s", data);

        // Append the new data to the reassembly buffer
        char *temp_buffer = (char *)realloc(reassembly_buffer, reassembly_buffer_len + data->data_len + 1);
        if (!temp_buffer)
        {
            ESP_LOGE(TAG, "Failed to allocate memory for reassembly buffer");
            free(reassembly_buffer);
            reassembly_buffer = NULL;
            reassembly_buffer_len = 0;
            return;
        }
        reassembly_buffer = temp_buffer;

        memcpy(reassembly_buffer + reassembly_buffer_len, data->data_ptr, data->data_len);
        reassembly_buffer_len += data->data_len;
        reassembly_buffer[reassembly_buffer_len] = '\0'; // Null-terminate the buffer

        // A simple check to see if the message might be a complete JSON object.
        // cJSON_Parse will tell us for sure.
        char *last_char = reassembly_buffer + reassembly_buffer_len - 1;
        if (*last_char != '}')
        {
            ESP_LOGI(TAG, "JSON doesn't end with '}', assuming it's a fragment. Buffer size: %d", reassembly_buffer_len);
            return;
        }

        char *msg = reassembly_buffer;
        ESP_LOGI(TAG, "Processing complete message of %d bytes", reassembly_buffer_len);
        ESP_LOGI(TAG, "Message: %s", msg);

        // Parse the string as a JSON object
        cJSON *json = cJSON_Parse(msg);
        if (json == NULL)
        {
            ESP_LOGE(TAG, "Failed to parse received data as JSON: %s", msg);
            // If parsing fails, it might be because the message is not yet complete.
            // We will wait for the next packet, unless the message is getting too large.
            if (reassembly_buffer_len > 16384)
            { // Safety break for huge invalid messages
                free(reassembly_buffer);
                reassembly_buffer = NULL;
                reassembly_buffer_len = 0;
            }
            break;
        }

        // Check if the JSON contains an "offer" from a HMI
        if (cJSON_HasObjectItem(json, "offer"))
        {
            if (peer != NULL)
            {
                ESP_LOGW(TAG, "Got OFFER, but peer already exists. Closing old peer.");
                esp_peer_close(peer);
                peer = NULL;
            }

            ESP_LOGI(TAG, "Got OFFER, creating new peer...");
            {
                // Setup STUN server to help devices behind NATs find each other.
                esp_peer_ice_server_cfg_t ice_server = {};
                ice_server.stun_url = (char *)STUN_SERVER_URL;

                // Configure peer as answerer (controlled/server role)
                esp_peer_cfg_t cfg = {};
                cfg.server_lists = &ice_server;
                cfg.server_num = 1;
                cfg.role = ESP_PEER_ROLE_CONTROLLED; // The ESP32 is waiting for an offer.
                cfg.ice_trans_policy = ESP_PEER_ICE_TRANS_POLICY_ALL;
                cfg.enable_data_channel = true; // video should be send over the data channel
                cfg.manual_ch_create = false;   // Let the library create the data channel automatically
                cfg.ctx = NULL;
                // set up callbacks to handle peer events
                cfg.on_state = on_peer_state_change;     // For connection state changes.
                cfg.on_msg = on_peer_msg;                // For SDP answers and ICE candidates.
                cfg.on_data = on_peer_data;              // For data received on the data channel.
                cfg.video_dir = ESP_PEER_MEDIA_DIR_NONE; // Not using RTP video
                cfg.audio_dir = ESP_PEER_MEDIA_DIR_NONE; // Not using RTP audio

                // Create peer connection
                int ret = esp_peer_open(&cfg, esp_peer_get_default_impl(), &peer);
                if (ret != ESP_PEER_ERR_NONE || peer == NULL)
                {
                    ESP_LOGE(TAG, "Failed to create peer: %d", ret);
                    cJSON_Delete(json);
                    break;
                }
            }

            // Extract the offer SDP (Session Description Protocol) from the JSON
            // and send it to the newly created peer instance. The peer will use this to generate its own "answer" SDP.
            cJSON *offer_item = cJSON_GetObjectItem(json, "offer");
            if (!cJSON_IsString(offer_item))
            {
                ESP_LOGE(TAG, "Offer JSON does not contain a valid SDP string!");
                cJSON_Delete(json);
                break;
            }
            char *offer_sdp = offer_item->valuestring;
            esp_peer_msg_t offer_msg = {};
            offer_msg.type = ESP_PEER_MSG_TYPE_SDP;
            offer_msg.data = (uint8_t *)offer_sdp;
            offer_msg.size = strlen(offer_sdp);

            ESP_LOGI(TAG, "Send SDP offer to peer with size %d", offer_msg.size);
            int ret = esp_peer_send_msg(peer, &offer_msg);
            if (ret != ESP_PEER_ERR_NONE)
            {
                ESP_LOGE(TAG, "Failed to send offer: %d", ret);
            }
        }
        // An "else if" is crucial here. A message is either an offer OR a candidate, not both.
        else if (cJSON_HasObjectItem(json, "candidate"))
        {
            ESP_LOGI(TAG, "Got ICE candidate");
            // If a peer connection exists, forward the ICE candidate information to HMI.
            // The ICE candidates describe possible network paths for the direct peer-to-peer connection.
            if (peer == NULL)
            {
                ESP_LOGW(TAG, "Peer not created yet, dropping ICE candidate.");
            }
            else
            {
                cJSON *cand_item = cJSON_GetObjectItem(json, "candidate");
                // A `null` candidate signals the end of candidates from the peer.
                // An object candidate is a real candidate.
                if (cJSON_IsNull(cand_item))
                {
                    ESP_LOGI(TAG, "Received null ICE candidate, signaling end of candidates.");
                    esp_peer_msg_t cand_msg = {};
                    cand_msg.type = ESP_PEER_MSG_TYPE_CANDIDATE;
                    cand_msg.data = NULL; // A null data pointer signals the end of candidates to esp_peer
                    cand_msg.size = 0;
                    esp_peer_send_msg(peer, &cand_msg);
                }
                else if (cJSON_IsObject(cand_item))
                {
                    char *cand_str = cJSON_PrintUnformatted(cand_item);
                    esp_peer_msg_t cand_msg = {};
                    cand_msg.type = ESP_PEER_MSG_TYPE_CANDIDATE;
                    cand_msg.data = (uint8_t *)cand_str;
                    cand_msg.size = strlen(cand_str);

                    ESP_LOGI(TAG, "Sending ICE candidate to peer with size %d", cand_msg.size);
                    esp_peer_send_msg(peer, &cand_msg);
                    free(cand_str);
                }
                else
                {
                    // This handles the case where the candidate might be null, which can happen at the end of gathering.
                    ESP_LOGI(TAG, "Received a null or invalid ICE candidate, ignoring.");
                }
            }
        }
        cJSON_Delete(json);
        // Free the buffer now that the message has been processed
        free(reassembly_buffer);
        reassembly_buffer = NULL;
        reassembly_buffer_len = 0;
        total_payload_len = 0;
        break;
    }

    case WEBSOCKET_EVENT_DISCONNECTED: // Connection Lost
        ESP_LOGW(TAG, "Signaling disconnected");
        break;

    case WEBSOCKET_EVENT_CLOSED:
        ESP_LOGI(TAG, "WebSocket connection closed. Cleaning up reassembly buffer.");
        if (reassembly_buffer)
        {
            free(reassembly_buffer);
            reassembly_buffer = NULL;
        }
        break;
    case WEBSOCKET_EVENT_ERROR: // An Error Occurred
        ESP_LOGE(TAG, "Signaling error");
        break;
    }
}

static void video_stream_task(void *pvParameters)
{
    ESP_LOGI(TAG, "Video task started. Waiting for connection...");
    xEventGroupWaitBits(rtc_event_group, RTC_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
    ESP_LOGI(TAG, "Streaming started!");

    while (1)
    {
        if (xEventGroupGetBits(rtc_event_group) & RTC_CONNECTED_BIT)
        {
            camera_fb_t *fb = esp_camera_fb_get();
            if (!fb)
            {
                ESP_LOGE(TAG, "Camera capture failed");
                vTaskDelay(pdMS_TO_TICKS(100));
                continue;
            }

            // Send video frame via data channel
            esp_peer_data_frame_t frame = {
                .type = ESP_PEER_DATA_CHANNEL_DATA, // Use DATA type for binary JPEG data
                .stream_id = 0,
                .data = fb->buf,
                .size = (int)fb->len};

            int ret = esp_peer_send_data(peer, &frame);
            if (ret != ESP_PEER_ERR_NONE)
            {
                ESP_LOGW(TAG, "Send failed: %d", ret);
            }

            esp_camera_fb_return(fb);
            vTaskDelay(pdMS_TO_TICKS(33)); // ~30fps
        }
        else
        {
            ESP_LOGW(TAG, "Disconnected, waiting...");
            xEventGroupWaitBits(rtc_event_group, RTC_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);
        }
    }
}

// --- ESP Peer Callbacks ---
static int on_peer_state_change(esp_peer_state_t state, void *ctx)
{
    ESP_LOGI(TAG, "Peer state: %d", state);

    const char *state_str;
    switch (state)
    {
    case ESP_PEER_STATE_NEW_CONNECTION:
        state_str = "NEW";
        break;
    case ESP_PEER_STATE_CONNECTING:
        state_str = "CONNECTING";
        break;
    case ESP_PEER_STATE_CONNECTED:
        state_str = "CONNECTED";
        break;
    case ESP_PEER_STATE_DISCONNECTED:
        state_str = "DISCONNECTED";
        break;
    case ESP_PEER_STATE_CONNECT_FAILED:
        state_str = "FAILED";
        break;
    case ESP_PEER_STATE_CLOSED:
        state_str = "CLOSED";
        break;
    case ESP_PEER_STATE_DATA_CHANNEL_OPENED:
        state_str = "DATA_CHANNEL_OPENED (Success!)";
        break;
    default:
        state_str = "UNKNOWN";
        break;
    }
    ESP_LOGI(TAG, ">> PEER STATE CHANGE: %s", state_str);

    switch (state)
    {
    case ESP_PEER_STATE_DATA_CHANNEL_OPENED:
        ESP_LOGI(TAG, "Data channel OPENED - Starting video!");
        xEventGroupSetBits(rtc_event_group, RTC_CONNECTED_BIT);
        // Start video streaming
        xTaskCreate(video_stream_task, "video", 8192, NULL, 5, NULL);
        break;

    case ESP_PEER_STATE_DISCONNECTED:
    case ESP_PEER_STATE_CLOSED:
        ESP_LOGW(TAG, "Disconnected");
        xEventGroupClearBits(rtc_event_group, RTC_CONNECTED_BIT);
        if (peer)
        {
            esp_peer_close(peer);
            peer = NULL;
        }
        break;

    default:
        break;
    }

    return 0;
}

// 8.a: Send SDP Answer to the Signalling Server
static int on_peer_msg(esp_peer_msg_t *msg, void *ctx)
{
    if (!msg || !msg->data)
    {
        ESP_LOGI(TAG, "Failed to send SDP answer or ICE candidate to signaling server");
        return -1;
    }

    ESP_LOGI(TAG, "Peer message type: %d", msg->type);

    if (msg->type == ESP_PEER_MSG_TYPE_SDP)
    {
        // This is the SDP answer generated by esp_peer
        ESP_LOGI(TAG, "Sending SDP answer to signaling server");

        cJSON *answer_json = cJSON_CreateObject();
        cJSON_AddStringToObject(answer_json, "answer", (const char *)msg->data);
        char *answer_str = cJSON_PrintUnformatted(answer_json);
        ESP_LOGI(TAG, "SDP answer: %s", answer_str);

        esp_websocket_client_send_text(ws_client, answer_str, strlen(answer_str), portMAX_DELAY);

        cJSON_Delete(answer_json);
        free(answer_str);
    }
    else if (msg->type == ESP_PEER_MSG_TYPE_CANDIDATE)
    {
        ESP_LOGI(TAG, "Sending local ICE candidate");

        cJSON *msg_json = cJSON_CreateObject();
        cJSON *cand = cJSON_Parse((const char *)msg->data);
        cJSON_AddItemToObject(msg_json, "candidate", cand);
        ESP_LOGI(TAG, "local ESP ICE candidate: %s", cand);

        char *msg_str = cJSON_PrintUnformatted(msg_json);
        esp_websocket_client_send_text(ws_client, msg_str, strlen(msg_str), portMAX_DELAY);

        cJSON_Delete(msg_json);
        free(msg_str);
    }

    return 0;
}

static int on_peer_data(esp_peer_data_frame_t *frame, void *ctx)
{
    if (!frame || !frame->data)
    {
        return -1;
    }

    ESP_LOGI(TAG, "Received data: %.*s", frame->size, frame->data);
    return 0;
}