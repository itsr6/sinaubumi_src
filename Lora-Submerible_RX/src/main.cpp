#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include "time.h"
#include "sys/time.h"
#include "esp_sntp.h"
#include <Firebase_ESP_Client.h>

// Add Firebase token helper
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

// WiFi and AP Configuration
#define WIFI_SSID_ADDR 0
#define WIFI_PASS_ADDR 50
const char* ap_ssid = "Wifi Login_SB";
const char* ap_password = "12345678";

// Firebase configuration
#define API_KEY "AIzaSyCTjgXOengQjinmKz5hB7IwaLN1cVylOBs"
#define DATABASE_URL "https://awrl-49c31-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "awrlsubmersible@gmail.com"
#define USER_PASSWORD "sinaubumi123"

// Firebase objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
String uid;
String databasePath;
unsigned long lastFirebaseUpdate = 0;
const unsigned long FIREBASE_UPDATE_INTERVAL = 600000; // 10 minutes

// NTP Server settings
const char* ntpServer1 = "pool.ntp.org";
const char* ntpServer2 = "time.nist.gov";
const long gmtOffset_sec = 25200;
const int daylightOffset_sec = 0;

// LoRa pins
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 2

// TFT display pins
#define TFT_CS 4
#define TFT_RST 16
#define TFT_DC 17

// Initialize objects
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
WebServer server(80);
String stored_ssid;
String stored_password;

// Display colors
#define BACKGROUND ST7735_BLACK
#define TEXT_COLOR ST7735_WHITE
#define VALUE_COLOR ST7735_GREEN
#define ERROR_COLOR ST7735_RED
#define TIME_COLOR ST7735_CYAN
#define DATE_COLOR ST7735_YELLOW
#define SYNC_COLOR ST7735_MAGENTA

// Time sync status
bool timeInitialized = false;
unsigned long lastNTPSync = 0;
const unsigned long NTP_SYNC_INTERVAL = 3600000; // 1 hour

// Latest sensor readings
struct SensorData {
    float waterDepth;     // Water depth in cm
    float temperature;    // Temperature in °C
    float turbidity;      // Turbidity in NTU
    float current;        // Submersible sensor current in mA
    float turbVoltage;    // Turbidity sensor voltage
    int rssi;             // Signal strength
    bool isValid;         // Data validity flag
} latestData;

// EEPROM helper functions
void writeString(int addr, String str) {
    int len = str.length();
    EEPROM.write(addr, len);
    for (int i = 0; i < len; i++) {
        EEPROM.write(addr + 1 + i, str[i]);
    }
}

String readString(int addr) {
    int len = EEPROM.read(addr);
    String str = "";
    for (int i = 0; i < len; i++) {
        str += char(EEPROM.read(addr + 1 + i));
    }
    return str;
}

// WiFi Configuration Web Interface
void handleRoot() {
    String html = "<html><head>";
    html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<style>";
    html += "body{font-family:Arial;margin:20px;max-width:800px;margin:auto;padding:20px}";
    html += ".status{background:#f0f0f0;padding:20px;margin:20px 0;border-radius:8px}";
    html += ".readings{background:#e8f4e8;padding:20px;margin:20px 0;border-radius:8px}";
    html += ".form-group{margin:15px 0}";
    html += "input{padding:8px;width:200px}";
    html += "label{display:inline-block;width:150px}";
    html += ".button{background-color:#4CAF50;border:none;color:white;padding:10px 20px;text-align:center;margin:10px 0;cursor:pointer;border-radius:4px}";
    html += "</style></head><body>";
    html += "<h2>Water Monitor System</h2>";
    
    html += "<div class='status'>";
    html += "<h3>System Status</h3>";
    html += "WiFi Status: " + String(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected") + "<br>";
    if (WiFi.status() == WL_CONNECTED) {
        html += "Connected to: " + WiFi.SSID() + "<br>";
        html += "IP Address: " + WiFi.localIP().toString() + "<br>";
    }
    html += "</div>";
    
    if (latestData.isValid) {
        html += "<div class='readings'>";
        html += "<h3>Latest Readings</h3>";
        html += "Current Reading: " + String(latestData.current, 2) + " mA<br>";
        html += "Water Depth: " + String(latestData.waterDepth, 1) + " cm<br>";
        html += "Temperature: " + String(latestData.temperature, 1) + " °C<br>";
        html += "Turbidity: " + String(latestData.turbidity, 1) + " NTU<br>";
        html += "Turbidity Voltage: " + String(latestData.turbVoltage, 2) + " V<br>";
        html += "Signal Strength: " + String(latestData.rssi) + " dBm<br>";
        html += "</div>";
    }
    
    html += "<form action='/configure' method='post'>";
    html += "<div class='form-group'>";
    html += "<h3>WiFi Settings</h3>";
    html += "<label>WiFi SSID:</label>";
    html += "<input type='text' name='ssid' required><br><br>";
    html += "<label>Password:</label>";
    html += "<input type='password' name='password' required><br>";
    html += "</div>";
    html += "<input type='submit' value='Save Configuration' class='button'>";
    html += "</form></body></html>";
    
    server.send(200, "text/html", html);
}

void handleWiFiConfig() {
    if (server.method() != HTTP_POST) {
        server.send(405, "text/plain", "Method Not Allowed");
        return;
    }
    
    String new_ssid = server.arg("ssid");
    String new_password = server.arg("password");
    
    writeString(WIFI_SSID_ADDR, new_ssid);
    writeString(WIFI_PASS_ADDR, new_password);
    EEPROM.commit();
    
    String response = "<html><body>";
    response += "<h2>Configuration Saved</h2>";
    response += "Attempting to connect to new network...<br>";
    response += "Device will restart in 5 seconds.";
    response += "</body></html>";
    
    server.send(200, "text/html", response);
    delay(5000);
    ESP.restart();
}

void setupWiFiConfig() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    
    server.on("/", HTTP_GET, handleRoot);
    server.on("/configure", HTTP_POST, handleWiFiConfig);
    server.begin();
    
    Serial.println("Configuration AP Started");
    Serial.print("IP Address: ");
    Serial.println(WiFi.softAPIP());
}

void initFirebase() {
    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.token_status_callback = tokenStatusCallback;
    
    Firebase.begin(&config, &auth);
    Firebase.reconnectWiFi(true);
    
    while ((auth.token.uid) == "") {
        Serial.print('.');
        delay(1000);
    }
    
    uid = auth.token.uid.c_str();
    Serial.print("User UID: ");
    Serial.println(uid);
    
    databasePath = String("/AWRLData/") + String(uid) + String("/Record");
}

void sendToFirebase(const SensorData& data) {
    if (!Firebase.ready()) {
        Serial.println("Firebase not ready");
        return;
    }

    Serial.println("\n--- Firebase Upload Attempt ---");
    
    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
    }
    
    char timeStringBuff[30];
    strftime(timeStringBuff, sizeof(timeStringBuff), "%Y-%m-%d_%H-%M-%S", &timeinfo);
    String timestamp = String(timeStringBuff);
    
    String readingPath = String(databasePath) + String("/") + String(timestamp);
    
    // Create a JSON object with all data exactly matching the screenshot format
    FirebaseJson json;
    json.set("depth", data.waterDepth);
    json.set("temperature", data.temperature);
    json.set("timestamp", timestamp);
    json.set("turbidity_ntu", data.turbidity);

    // Debug print before upload
    Serial.println("Uploading complete data set:");
    Serial.print("Path: "); Serial.println(readingPath);
    String jsonStr;
    json.toString(jsonStr, true);
    Serial.println(jsonStr);

    // Upload entire JSON object in one transaction
    if (Firebase.RTDB.setJSON(&fbdo, readingPath.c_str(), &json)) {
        Serial.println("Complete data upload successful!");
        tft.fillRect(tft.width() - 10, 0, 10, 10, VALUE_COLOR);
    } else {
        Serial.println("Data upload failed");
        Serial.println("Error: " + fbdo.errorReason());
        tft.fillRect(tft.width() - 10, 0, 10, 10, ERROR_COLOR);
    }
}

void timeAvailable(struct timeval *t) {
    Serial.println("NTP time sync completed!");
    timeInitialized = true;
    lastNTPSync = millis();
    
    struct tm timeinfo;
    if(getLocalTime(&timeinfo)) {
        Serial.print("Synchronized time: ");
        Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    }
}

void setupTime() {
    setenv("TZ", "GMT-7", 1);
    tzset();
    
    sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    sntp_setservername(0, (char*)ntpServer1);
    sntp_setservername(1, (char*)ntpServer2);
    sntp_set_time_sync_notification_cb(timeAvailable);
    
    sntp_init();
    
    int retry = 0;
    const int retry_count = 10;
    while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
        Serial.println("Waiting for NTP time sync...");
        delay(2000);
    }
}

void setupDisplay() {
    tft.initR(INITR_BLACKTAB);
    tft.setRotation(1);
    tft.fillScreen(BACKGROUND);
    tft.setTextSize(1);
    tft.setTextColor(TEXT_COLOR);
    tft.setTextWrap(true);
    
    // Draw title
    tft.setCursor(5, 5);
    tft.println("Water Level Monitor");
    tft.drawFastHLine(0, 15, tft.width(), TEXT_COLOR);
}

void displayDateTime() {
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)) {
        tft.fillRect(0, 105, tft.width(), 25, BACKGROUND);
        tft.setTextColor(ERROR_COLOR);
        tft.setCursor(5, 115);
        tft.print("Time Error");
        return;
    }
    
    tft.fillRect(0, 105, tft.width(), 25, BACKGROUND);
    
    char dateTimeStr[30];
    strftime(dateTimeStr, sizeof(dateTimeStr), "%d/%m/%Y | %H:%M:%S", &timeinfo);
    tft.setTextColor(DATE_COLOR);
    tft.setCursor(5, 105);
    tft.print(dateTimeStr);
    
    tft.setCursor(5, 115);
    if (timeInitialized && (millis() - lastNTPSync < NTP_SYNC_INTERVAL)) {
        tft.setTextColor(SYNC_COLOR);
        tft.print("@ ");
    }
    tft.setTextColor(TIME_COLOR);
    tft.print("sinaubumi.org");
}

void updateDisplay(const SensorData& data) {
    tft.fillRect(0, 16, tft.width(), 89, BACKGROUND);
    
    // Display Water Depth with Current in parentheses
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(5, 20);
    tft.print("Depth: ");
    tft.setTextColor(VALUE_COLOR);
    tft.print(data.waterDepth, 1);
    tft.print(" cm (");
    tft.print(data.current, 2);
    tft.println(" mA)");
    
    // Display Temperature
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(5, 35);
    tft.print("Temp: ");
    if(data.temperature != -127) {
        tft.setTextColor(VALUE_COLOR);
        tft.print(data.temperature, 1);
        tft.println(" C");
    } else {
        tft.setTextColor(ERROR_COLOR);
        tft.println("Error");
    }
    
    // Display Turbidity
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(5, 50);
    tft.print("Turbidity: ");
    tft.setTextColor(VALUE_COLOR);
    tft.print(data.turbidity, 1);
    tft.println(" NTU");
    
    // Display Signal Strength
    tft.setTextColor(TEXT_COLOR);
    tft.setCursor(5, 65);
    tft.print("Signal: ");
    
    if (data.rssi >= -50) {
        tft.setTextColor(ST7735_GREEN);
        tft.println("Excellent");
    } else if (data.rssi >= -70) {
        tft.setTextColor(ST7735_GREEN);
        tft.println("Good");
    } else if (data.rssi >= -90) {
        tft.setTextColor(ST7735_YELLOW);
        tft.println("Fair");
    } else {
        tft.setTextColor(ST7735_RED);
        tft.println("Poor");
    }
}

void setup() {
    Serial.begin(115200);
    while (!Serial);
    Serial.println("Water Monitor System");

    EEPROM.begin(512);
    setupDisplay();
    
    // Try to connect with stored credentials
    stored_ssid = readString(WIFI_SSID_ADDR);
    stored_password = readString(WIFI_PASS_ADDR);
    
    if (stored_ssid.length() > 0) {
        WiFi.mode(WIFI_STA);
        WiFi.begin(stored_ssid.c_str(), stored_password.c_str());
        
        int attempts = 0;
        while (WiFi.status() != WL_CONNECTED && attempts < 20) {
            delay(500);
            attempts++;
            Serial.print(".");
        }
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        setupWiFiConfig();
    } else {
        setupTime();
        initFirebase();
    }
    
    // Initialize LoRa
    LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
    while (!LoRa.begin(915E6)) {
        Serial.print(".");
        delay(500);
    }
    
    // Configure LoRa parameters - using different settings to avoid interference
    LoRa.setTxPower(17);                // Reduced power from 20 to 17
    LoRa.setSignalBandwidth(250E3);     // Changed bandwidth from 500kHz to 250kHz
    LoRa.setSpreadingFactor(8);         // Increased spreading factor from 7 to 8
    LoRa.setCodingRate4(6);             // Changed coding rate from 4/5 to 4/6
    LoRa.setSyncWord(0xA5);             // Changed sync word from 0xF0 to 0xA5 for submersible parameter
    LoRa.enableCrc();
    
    Serial.println("Setup Complete!");
    
    // Initialize latestData
    latestData.isValid = false;
    latestData.current = 0;
    latestData.waterDepth = 0;
    latestData.temperature = 0;
    latestData.turbidity = 0;
    latestData.turbVoltage = 0;
    latestData.rssi = -120;
}

void loop() {
    static unsigned long lastTimeUpdate = 0;
    static unsigned long lastWiFiCheck = 0;
    static unsigned long lastFirebaseRetry = 0;
    static bool firstDataSent = false;
    unsigned long currentMillis = millis();

    // Constants for timing
    const unsigned long TIME_UPDATE_INTERVAL = 1000;      // 1 second
    const unsigned long WIFI_CHECK_INTERVAL = 30000;      // 30 seconds
    const unsigned long FIREBASE_RETRY_INTERVAL = 5000;   // 5 seconds

    // Handle WiFi configuration if not connected
    if (WiFi.status() != WL_CONNECTED) {
        server.handleClient();
        tft.fillRect(tft.width() - 10, 0, 10, 10, ERROR_COLOR);
        
        // Attempt to reconnect to WiFi
        if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
            Serial.println("Attempting to reconnect to WiFi...");
            WiFi.reconnect();
            lastWiFiCheck = currentMillis;
        }
        return;
    }

    // Update time display every second
    if (currentMillis - lastTimeUpdate >= TIME_UPDATE_INTERVAL) {
        displayDateTime();
        lastTimeUpdate = currentMillis;
    }
    
    // Check connections and retry if needed
    if (currentMillis - lastWiFiCheck >= WIFI_CHECK_INTERVAL) {
        if (WiFi.status() != WL_CONNECTED) {
            Serial.println("WiFi disconnected, attempting to reconnect...");
            WiFi.reconnect();
        }
        if (!Firebase.ready()) {
            Serial.println("Firebase not ready, attempting to reconnect...");
            Firebase.begin(&config, &auth);
        }
        lastWiFiCheck = currentMillis;
    }

    // Firebase reconnection attempt
    if (!Firebase.ready() && (currentMillis - lastFirebaseRetry >= FIREBASE_RETRY_INTERVAL)) {
        Serial.println("Attempting to reconnect to Firebase...");
        Firebase.begin(&config, &auth);
        lastFirebaseRetry = currentMillis;
    }

    // Handle LoRa packets
    int packetSize = LoRa.parsePacket();
    if (packetSize) {
        String message = "";
        while (LoRa.available()) {
            message += (char)LoRa.read();
        }

        Serial.println("\n--- Received LoRa Packet ---");
        Serial.print("Raw message: ");
        Serial.println(message);

        DynamicJsonDocument doc(200);
        DeserializationError error = deserializeJson(doc, message);

        if (!error) {
            // Update latest data with all fields from the submersible sensor packet
            latestData.current = doc["current"].as<float>();  // Current in mA
            latestData.waterDepth = doc["depth"].as<float>(); // Depth in cm
            latestData.temperature = doc["temp"].as<float>(); // Temperature in °C
            latestData.turbVoltage = doc["turb_v"].as<float>(); // Turbidity voltage
            latestData.turbidity = doc["turb_ntu"].as<float>(); // Turbidity in NTU
            latestData.rssi = LoRa.packetRssi();
            latestData.isValid = true;
            
            // Debug output
            Serial.println("Parsed Data:");
            Serial.print("Current: ");
            Serial.print(latestData.current);
            Serial.println(" mA");
            Serial.print("Water Depth: ");
            Serial.print(latestData.waterDepth);
            Serial.println(" cm");
            Serial.print("Temperature: ");
            Serial.print(latestData.temperature);
            Serial.println(" °C");
            Serial.print("Turbidity Voltage: ");
            Serial.print(latestData.turbVoltage);
            Serial.println(" V");
            Serial.print("Turbidity: ");
            Serial.print(latestData.turbidity);
            Serial.println(" NTU");
            Serial.print("RSSI: ");
            Serial.print(latestData.rssi);
            Serial.println(" dBm");
            
            // Update display
            updateDisplay(latestData);
            
            // Send to Firebase if it's time or first data
            if (!firstDataSent || (currentMillis - lastFirebaseUpdate >= FIREBASE_UPDATE_INTERVAL)) {
                if (Firebase.ready() && WiFi.status() == WL_CONNECTED) {
                    Serial.println("Sending data to Firebase...");
                    sendToFirebase(latestData);
                    lastFirebaseUpdate = currentMillis;
                    firstDataSent = true;
                } else {
                    Serial.println("Skipping Firebase upload - connection not ready");
                    tft.fillRect(tft.width() - 10, 0, 10, 10, ERROR_COLOR);
                }
            }
        } else {
            Serial.println("JSON parsing failed");
            Serial.print("Error: ");
            Serial.println(error.c_str());
            Serial.print("Message: ");
            Serial.println(message);
        }
    }

    // Visual connection status indicator
    if (WiFi.status() == WL_CONNECTED && Firebase.ready()) {
        if (latestData.isValid && (currentMillis - lastFirebaseUpdate < FIREBASE_UPDATE_INTERVAL + 5000)) {
            tft.fillRect(tft.width() - 10, 0, 10, 10, VALUE_COLOR); // Green when all good
        } else {
            tft.fillRect(tft.width() - 10, 0, 10, 10, SYNC_COLOR); // Magenta when connected but waiting
        }
    } else {
        tft.fillRect(tft.width() - 10, 0, 10, 10, ERROR_COLOR); // Red when there's a connection issue
    }
}