// esp32_camera.ino
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include "esp_camera.h"

// Wi-Fi Credentials
const char* ssid = "animal_shelter";
const char* password = "";

// Camera ID (must match server configuration)
String CAMERA_ID = "01";

// ==== CAMERA PIN DEFINITIONS ====
#define PWDN_GPIO_NUM 32
#define RESET_GPIO_NUM -1
#define XCLK_GPIO_NUM 0
#define SIOD_GPIO_NUM 26
#define SIOC_GPIO_NUM 27
#define Y9_GPIO_NUM 35
#define Y8_GPIO_NUM 34
#define Y7_GPIO_NUM 39
#define Y6_GPIO_NUM 36
#define Y5_GPIO_NUM 21
#define Y4_GPIO_NUM 19
#define Y3_GPIO_NUM 18
#define Y2_GPIO_NUM 5
#define VSYNC_GPIO_NUM 25
#define HREF_GPIO_NUM 23
#define PCLK_GPIO_NUM 22

// ==== MONITORING SETTINGS - 5 PICTURES EACH ====
const int PHOTOS_PER_TRIGGER = 5;
const int PHOTO_INTERVAL = 30000;  // 30 seconds between photos

// Flag to track if trigger received
volatile bool capture_trigger = false;
volatile uint8_t category_code = 0;

// Callback when data is received via ESP-NOW
void on_receive(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
    Serial.println("=============================");
    Serial.println("TRIGGER RECEIVED!");
    Serial.print("Data value: ");
    Serial.println(data[0]);
    Serial.println("=============================");
    category_code = data[0];
    capture_trigger = true;
}

void setup() {
    Serial.begin(115200);
    delay(1000);
    
    Serial.println("\n\nESP32-CAM Initializing...");
    Serial.println("=============================");
    Serial.println("MONITORING MODE: 5 photos per trigger");
    Serial.println("=============================");
    
    // Camera config
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM;
    config.pin_d1 = Y3_GPIO_NUM;
    config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM;
    config.pin_d4 = Y6_GPIO_NUM;
    config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM;
    config.pin_d7 = Y9_GPIO_NUM;
    config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM;
    config.pin_vsync = VSYNC_GPIO_NUM;
    config.pin_href = HREF_GPIO_NUM;
    config.pin_sscb_sda = SIOD_GPIO_NUM;
    config.pin_sscb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM;
    config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
    
    esp_err_t err = esp_camera_init(&config);
    if (err != ESP_OK) {
        Serial.printf("Camera init failed: 0x%x\n", err);
        return;
    }
    Serial.println("Camera initialized successfully!");
    
    // Connect WiFi
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("ESP32-CAM MAC Address: ");
    Serial.println(WiFi.macAddress());
    
    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ERROR: ESP-NOW init failed!");
        return;
    }
    
    Serial.println("ESP-NOW initialized successfully!");
    esp_now_register_recv_cb(on_receive);
    
    Serial.println("\nReady! Waiting for triggers...");
    Serial.println("Will capture 5 photos per trigger (30s interval)");
    Serial.println("=======================\n");
}

void captureMultiplePhotos() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected! Cannot upload images.");
        return;
    }
    
    String category_str = (category_code == 1) ? "during" : "after";
    String category_name = (category_code == 1) ? "DURING FEEDING" : "AFTER FEEDING";
    
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.printf("║ %s - 5 PHOTOS ║\n", category_name.c_str());
    Serial.println("╚════════════════════════════════════════╝\n");
    
    int successful_uploads = 0;
    int failed_uploads = 0;
    
    // CAPTURE AND UPLOAD 5 PHOTOS WITH INTERVAL
    for (int photo_num = 1; photo_num <= PHOTOS_PER_TRIGGER; photo_num++) {
        Serial.printf("\n╔════════════════════════════════════════╗\n");
        Serial.printf("║ Photo %d/%d (%s) ║\n", photo_num, PHOTOS_PER_TRIGGER, category_str.c_str());
        Serial.println("╚════════════════════════════════════════╝\n");
        
        // Capture photo
        camera_fb_t* fb = esp_camera_fb_get();
        if (!fb) {
            Serial.printf("✗ Photo %d: Camera capture FAILED!\n", photo_num);
            failed_uploads++;
            Serial.println();
            delay(PHOTO_INTERVAL);
            continue;
        }
        
        Serial.printf("✓ Photo %d captured! Size: %d bytes\n", photo_num, fb->len);
        Serial.println("Uploading to server...");
        
        // Upload to server
        bool upload_success = uploadPhoto(fb, category_str);
        
        if (upload_success) {
            Serial.printf("✓ Photo %d uploaded successfully!\n", photo_num);
            successful_uploads++;
        } else {
            Serial.printf("✗ Photo %d upload FAILED!\n", photo_num);
            failed_uploads++;
        }
        
        esp_camera_fb_return(fb);
        Serial.println();
        
        // Wait interval before next photo (except after last photo)
        if (photo_num < PHOTOS_PER_TRIGGER) {
            Serial.printf("⏳ Waiting %d seconds before next photo...\n\n", PHOTO_INTERVAL/1000);
            delay(PHOTO_INTERVAL);
        }
    }
    
    // Summary
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.printf("║ CAPTURE COMPLETE: %d/%d uploaded ║\n", successful_uploads, PHOTOS_PER_TRIGGER);
    if (failed_uploads > 0) {
        Serial.printf("║ Failed: %d ║\n", failed_uploads);
    }
    Serial.println("╚════════════════════════════════════════╝\n");
}

bool uploadPhoto(camera_fb_t* fb, String category_str) {
    HTTPClient http;
    http.begin("http://raspberrypi.local:8080/upload_image");
    http.setTimeout(15000);  // 15 second timeout
    
    // Create multipart form data
    String boundary = "---WebKitFormBoundary7MA4YWxkTrZuOgW";
    String contentType = "multipart/form-data; boundary=" + boundary;
    http.addHeader("Content-Type", contentType);
    
    // Build multipart payload
    String payloadStart = "--" + boundary + "\r\n";
    payloadStart += "Content-Disposition: form-data; name=\"camera_id\"\r\n\r\n";
    payloadStart += CAMERA_ID + "\r\n";
    payloadStart += "--" + boundary + "\r\n";
    payloadStart += "Content-Disposition: form-data; name=\"category\"\r\n\r\n";
    payloadStart += category_str + "\r\n";
    payloadStart += "--" + boundary + "\r\n";
    payloadStart += "Content-Disposition: form-data; name=\"image\"; filename=\"capture.jpg\"\r\n";
    payloadStart += "Content-Type: image/jpeg\r\n\r\n";
    
    String payloadEnd = "\r\n--" + boundary + "--\r\n";
    
    int totalLen = payloadStart.length() + fb->len + payloadEnd.length();
    
    uint8_t* fullPayload = (uint8_t*)malloc(totalLen);
    if (!fullPayload) {
        Serial.println("ERROR: Memory allocation failed!");
        http.end();
        return false;
    }
    
    memcpy(fullPayload, payloadStart.c_str(), payloadStart.length());
    memcpy(fullPayload + payloadStart.length(), fb->buf, fb->len);
    memcpy(fullPayload + payloadStart.length() + fb->len, payloadEnd.c_str(), payloadEnd.length());
    
    int res = http.POST(fullPayload, totalLen);
    
    bool success = false;
    if (res == 200) {
        Serial.print("Server response: ");
        String response = http.getString();
        Serial.println(response);
        success = true;
    } else if (res > 0) {
        Serial.printf("HTTP Error Code: %d\n", res);
        Serial.println(http.getString());
    } else {
        Serial.print("Connection error: ");
        Serial.println(http.errorToString(res));
    }
    
    free(fullPayload);
    http.end();
    
    return success;
}

void loop() {
    if (capture_trigger) {
        capture_trigger = false;
        captureMultiplePhotos();
    }
    delay(100);
}