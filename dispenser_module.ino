// dispenser_module.ino
#include "HX711.h"
#include <ESP32Servo.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <esp_now.h>
#include <ArduinoJson.h>

// Wi-Fi Credentials
const char* ssid = "animal_shelter";
const char* password = "";

// Hardcoded device no. which serves as the module ID
String MODULE_ID = "MODULE01";

// ESP32-CAM's MAC Address
uint8_t camera_address[] = {0xEC, 0xE3, 0x34, 0xB4, 0x70, 0x40};

// HX711 objects
HX711 scale1;
HX711 scale2;

// Load cell pins
const int LOADCELL1_DOUT = 26;
const int LOADCELL1_SCK = 27;
const int LOADCELL2_DOUT = 25;
const int LOADCELL2_SCK = 33;

// Calibration factors
float calibration_factor1 = -110.000;
float calibration_factor2 = -115.600;

// Servo
Servo servo1, servo2;

unsigned long last_button_press = 0;
const unsigned long DEBOUNCE_DELAY = 50;

unsigned long last_schedule_check = 0;
unsigned long last_weight_alert = 0;
unsigned long dispense_time = 0;
bool waiting_for_camera = false;

// Camera timing variables - EXACT TIMING
bool during_feeding_captured = false;
const unsigned long DURING_FEEDING_DELAY = 15000;  // 15 seconds
const unsigned long AFTER_FEEDING_DELAY = 180000;  // 3 minutes (180 seconds)

const unsigned long SCHEDULE_CHECK_INTERVAL = 30000;  // 30 seconds

// Servo pins
const int SERVO1_PIN = 14;
const int SERVO2_PIN = 18;

// Manual Button Pin
const int MANUAL_BUTTON = 32;

// Reset Button Pin
const int RESET_BUTTON = 22;

// Buzzer Pin
const int BUZZER_PIN = 15;

// weight threshold in grams, before the alarm turns on
const int weight_threshold = 100;

// Buzzer state tracking
unsigned long last_buzzer_toggle = 0;
bool buzzer_on = false;
bool buzzer_active = false;

// ESP-NOW callback
void on_sent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    Serial.print("Camera trigger status: ");
    Serial.println(status == ESP_NOW_SEND_SUCCESS ? "Success" : "Failed");
}

void setup() {
    Serial.begin(115200);
    Serial.println("\n\nESP32 Dispenser Module Initializing...");
    Serial.println("===============================");

    // Connect to WiFi
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println("\nWiFi connected!");
    Serial.print("Hostname: ");
    Serial.println(WiFi.getHostname());
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    Serial.print("WiFi Channel: ");
    Serial.println(WiFi.channel());

    // Initialize ESP-NOW
    if (esp_now_init() != ESP_OK) {
        Serial.println("ESP-NOW init failed");
    } else {
        Serial.println("ESP-NOW initialized successfully!");
        esp_now_register_send_cb(on_sent);
        
        esp_now_peer_info_t peer_info = {};
        memcpy(peer_info.peer_addr, camera_address, 6);
        peer_info.channel = 0;
        peer_info.encrypt = false;
        
        if (esp_now_add_peer(&peer_info) != ESP_OK) {
            Serial.println("Failed to add ESP32-CAM peer");
        } else {
            Serial.println("ESP32-CAM peer added successfully!");
        }
    }

    // Initialize load cells
    scale1.begin(LOADCELL1_DOUT, LOADCELL1_SCK);
    scale2.begin(LOADCELL2_DOUT, LOADCELL2_SCK);

    // Initialize servo motors
    servo1.attach(SERVO1_PIN);
    servo2.attach(SERVO2_PIN);

    pinMode(MANUAL_BUTTON, INPUT_PULLUP);
    pinMode(BUZZER_PIN, OUTPUT);
    digitalWrite(BUZZER_PIN, LOW);
    pinMode(RESET_BUTTON, INPUT_PULLUP);

    Serial.println("\nTaring scales...");
    scale1.tare();
    scale2.tare();
    scale1.set_scale(calibration_factor1);
    scale2.set_scale(calibration_factor2);
    delay(1000);

    Serial.println("\n=================================");
    Serial.println("System ready! Waiting for schedule...");
    Serial.println("=================================");
    delay(1000);
}

void loop() {
    unsigned long currentMillis = millis();

    // Check reset button ALWAYS (independent of alert state)
    if (digitalRead(RESET_BUTTON) == LOW) {
        delay(50);
        if (digitalRead(RESET_BUTTON) == LOW) {
            digitalWrite(BUZZER_PIN, LOW);
            buzzer_active = false;
            buzzer_on = false;
            Serial.println("✓ Alert acknowledged by button press");
            delay(500);
        }
    }

    // ============================================================
    // Check if it's time to trigger "During Feeding" camera
    if (waiting_for_camera && !during_feeding_captured && 
        (currentMillis - dispense_time >= DURING_FEEDING_DELAY)) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║ 15 SECONDS ELAPSED - DURING FEEDING ║");
        Serial.println("║ TRIGGERING CAMERA NOW ║");
        Serial.println("╚════════════════════════════════════════╝");
        send_camera_trigger();
        during_feeding_captured = true;
        Serial.println("✓ During Feeding photo captured\n");
    }

    // Check if it's time to trigger "After Feeding" camera
    if (waiting_for_camera && during_feeding_captured && 
        (currentMillis - dispense_time >= AFTER_FEEDING_DELAY)) {
        Serial.println("\n╔════════════════════════════════════════╗");
        Serial.println("║ 3 MINUTES ELAPSED - AFTER FEEDING ║");
        Serial.println("║ TRIGGERING CAMERA NOW ║");
        Serial.println("╚════════════════════════════════════════╝");
        send_camera_trigger();
        waiting_for_camera = false;
        during_feeding_captured = false;
        Serial.println("✓ After Feeding photo captured\n");
    }

    // Check for scheduled feeding every 30 seconds
    if (currentMillis - last_schedule_check >= SCHEDULE_CHECK_INTERVAL) {
        Serial.println("=== Schedule check ===");
        last_schedule_check = currentMillis;
        
        int feed_amount = 0;
        int schedule_id = 0;
        
        if (check_schedule(feed_amount, schedule_id)) {
            Serial.println("\n╔════════════════════════════════════════╗");
            Serial.println("║ DISPENSE TRIGGERED! ║");
            Serial.println("╚════════════════════════════════════════╝");
            
            bool dispense_success = dispense(feed_amount);
            
            if (dispense_success) {
                complete_schedule(schedule_id);
                Serial.println("✓ Schedule completed");
                
                weight_update();
                
                dispense_time = millis();
                waiting_for_camera = true;
                during_feeding_captured = false;
                
                Serial.println("\n⏱️ CAMERA TIMING STARTED:");
                Serial.println(" - During Feeding in 15 seconds");
                Serial.println(" - After Feeding in 180 seconds (3 minutes)");
                Serial.println("");
            } else {
                Serial.println("✗ Dispensing failed - schedule NOT marked complete");
            }
            Serial.println(">>> Dispense cycle complete <<<\n");
        }
    }

    // Manual dispense (always available)
    if (digitalRead(MANUAL_BUTTON) == LOW) {
        manual_dispense();
    }

    delay(10);
}

void send_camera_trigger() {
    Serial.println("Sending trigger to ESP32-CAM...");
    uint8_t trigger = 1;
    esp_err_t result = esp_now_send(camera_address, &trigger, sizeof(trigger));
    
    if (result == ESP_OK) {
        Serial.println("✓ Camera trigger sent successfully!");
    } else {
        Serial.println("✗ Camera trigger send failed");
    }
}

bool check_schedule(int& amount, int& schedule_id) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected!");
        return false;
    }
    
    HTTPClient http;
    http.begin("http://raspberrypi.local:8080/check_schedule");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    String payload = "module_id=" + MODULE_ID;
    int code = http.POST(payload);
    
    if (code == 200) {
        String response = http.getString();
        Serial.print("Server response: ");
        Serial.println(response);
        http.end();
        
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, response);
        
        if (error) {
            Serial.print("JSON parsing failed: ");
            Serial.println(error.c_str());
            amount = 0;
            schedule_id = 0;
            return false;
        }
        
        bool dispense = doc["dispense"] | false;
        
        if (dispense) {
            amount = int(doc["amount"]) | 0;
            schedule_id = doc["schedule_id"] | 0;
            const char* scheduled_time = doc["scheduled_time"] | "unknown";
            
            low_weight_alert();
            
            Serial.println("Dispensing scheduled!");
            Serial.print("Schedule ID: ");
            Serial.println(schedule_id);
            Serial.print("Scheduled time: ");
            Serial.println(scheduled_time);
            Serial.print("Amount: ");
            Serial.println(amount);
            
            return true;
        } else {
            Serial.println("No pending schedule");
            amount = 0;
            schedule_id = 0;
            return false;
        }
    } else if (code == 404) {
        String response = http.getString();
        Serial.println("Error: Invalid or inactive module_id");
        Serial.println(response);
        http.end();
        amount = 0;
        schedule_id = 0;
        return false;
    } else if (code == 400) {
        String response = http.getString();
        Serial.println("Error: Missing module_id parameter");
        Serial.println(response);
        http.end();
        amount = 0;
        schedule_id = 0;
        return false;
    } else if (code > 0) {
        String response = http.getString();
        Serial.println("Failed to check schedule. HTTP code: ");
        Serial.println(code);
        Serial.println(response);
        http.end();
        amount = 0;
        schedule_id = 0;
        return false;
    } else {
        Serial.println("Connection error: ");
        Serial.println(http.errorToString(code));
        http.end();
        amount = 0;
        schedule_id = 0;
        return false;
    }
}

bool complete_schedule(int schedule_id) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected! Cannot complete schedule.");
        return false;
    }
    
    if (schedule_id == 0) {
        Serial.println("Invalid schedule_id");
        return false;
    }
    
    Serial.print("Marking schedule as complete: ");
    Serial.println(schedule_id);
    
    HTTPClient http;
    http.begin("http://raspberrypi.local:8080/complete_schedule");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String payload = "schedule_id=" + String(schedule_id) + "&module_id=" + MODULE_ID;
    int code = http.POST(payload);
    
    Serial.print("Complete schedule response code: ");
    Serial.println(code);
    
    if (code == 200) {
        String response = http.getString();
        Serial.println("✓ Schedule completed successfully!");
        Serial.print("Server response: ");
        Serial.println(response);
        http.end();
        return true;
    } else {
        String response = http.getString();
        Serial.println("✗ Failed to complete schedule. HTTP code: ");
        Serial.println(code);
        Serial.println(response);
        http.end();
        return false;
    }
}

void weight_update() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi disconnected! Cannot update weight.");
        return;
    }
    
    Serial.println("Updating weight on server...");
    
    float weight = get_current_weight();
    
    Serial.print("Current weight: ");
    Serial.print(weight, 2);
    Serial.println("g");
    
    if (weight < 0 || weight > 10000) {
        Serial.println("✗ Invalid weight value, skipping update");
        return;
    }
    
    HTTPClient http;
    http.begin("http://raspberrypi.local:8080/weight_update");
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    
    String payload = "module_id=" + MODULE_ID + "&weight=" + String(weight, 2);
    
    int httpResponseCode = http.POST(payload);
    
    Serial.print("Weight update response code: ");
    Serial.println(httpResponseCode);
    
    if (httpResponseCode == 200) {
        String response = http.getString();
        Serial.println("Weight updated successfully!");
        Serial.print("Server response: ");
        Serial.println(response);
    } else if (httpResponseCode == 403) {
        String response = http.getString();
        Serial.println("✗ Module not registered on server!");
        Serial.println(response);
        Serial.println("Please register this module first.");
    } else {
        Serial.println("✗ Weight update failed");
    }
    http.end();
    low_weight_alert();
}

void low_weight_alert() {
    float weight = get_current_weight();
    if (weight < weight_threshold) {
        unsigned long currentMillis = millis();
        buzzer_active = true;
        if (currentMillis - last_buzzer_toggle >= 300) {
            buzzer_on = !buzzer_on;
            digitalWrite(BUZZER_PIN, buzzer_on ? HIGH : LOW);
            last_buzzer_toggle = currentMillis;
        }
    } else {
        digitalWrite(BUZZER_PIN, LOW);
        buzzer_active = false;
        buzzer_on = false;
    }
}

float get_current_weight() {
    float weight1 = scale1.get_units(5);
    float weight2 = scale2.get_units(5);
    return (weight1 + weight2) / 2.0;
}

void rotate_motors() {
    servo1.write(0);
    servo2.write(0);
    delay(1000);
    servo1.write(180);
    servo2.write(180);
    delay(500);
    servo1.write(0);
    servo2.write(0);
}

void stop_motors() {
    servo1.write(0);
    servo2.write(0);
}

bool dispense(int feed_amount) {
    Serial.print("Dispensing ");
    Serial.print(feed_amount);
    Serial.println("g...");
    
    float target_weight = get_current_weight() - feed_amount;
    float current_weight = get_current_weight();
    float initial_weight = current_weight;
    
    Serial.print("Initial weight: ");
    Serial.print(initial_weight, 1);
    Serial.println(" g");
    Serial.print("Target weight: ");
    Serial.print(target_weight, 1);
    Serial.println(" g");
    
    unsigned long start_time = millis();
    const unsigned long MAX_DISPENSE_TIME = 30000;
    
    Serial.println("Dispensing...");
    rotate_motors();
    
    while (current_weight > target_weight) {
        if (millis() - start_time > MAX_DISPENSE_TIME) {
            stop_motors();
            Serial.println("✗ Dispense timeout! Stopping motors.");
            return false;
        }
        delay(10);
        current_weight = get_current_weight();
    }
    
    stop_motors();
    
    float actual_dispensed = initial_weight - current_weight;
    
    Serial.println("\n=== Dispense Complete ===");
    Serial.print("Final weight: ");
    Serial.print(current_weight, 1);
    Serial.println(" g");
    Serial.print("Dispensed amount: ");
    Serial.print(actual_dispensed, 1);
    Serial.println(" g");
    
    const float TOLERANCE = 15.0;
    float difference = abs(actual_dispensed - feed_amount);
    Serial.print("Difference from target: ");
    Serial.print(difference, 1);
    Serial.println(" g");
    
    if (difference <= TOLERANCE) {
        Serial.println("✓ Dispense successful!");
        return true;
    } else {
        Serial.println("⚠ Dispense completed but outside tolerance");
        if (actual_dispensed >= feed_amount * 0.5) {
            Serial.println("✓ Acceptable amount dispensed (>50% of target)");
            return true;
        } else {
            Serial.println("✗ Insufficient food dispensed");
            return false;
        }
    }
}

void manual_dispense() {
    Serial.println("Manual dispense triggered");
    // Add manual dispense logic here
    while (digitalRead(MANUAL_BUTTON) == LOW) {
        delay(10);
    }
}