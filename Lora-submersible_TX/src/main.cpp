#include <Arduino.h>
#include <SPI.h>
#include <LoRa.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>

// LoRa pins
#define LORA_SS 5
#define LORA_RST 14
#define LORA_DIO0 2

// Sensor pins
#define LEVEL_SENSOR_PIN 34
#define TEMP_SENSOR_PIN 4
#define TURBIDITY_SENSOR_PIN 32

// WiFi credentials
const char* ssid = "WaterSensor-AP";
const char* password = "12345677";

// Web server
WebServer server(80);

// EEPROM configuration
#define EEPROM_SIZE 32
#define ADDR_CURRENT_4MA 0
#define ADDR_DEPTH_RANGE 4
#define ADDR_TURB_CAL_A 8
#define ADDR_TURB_CAL_B 12
#define ADDR_CLEAR_WATER_VOLTAGE 16

// Sensor configuration constants
const float RESISTOR_VALUE = 150.0;
const int NUM_SAMPLES = 10;
const float CURRENT_4MA = 3.40;
const float DEPTH_AT_4MA = 0;
const float CURRENT_RANGE = 16.0;
const float DEPTH_RANGE = 500.0;
const float SENSOR_VCC = 3.3;
const float ESP32_VCC = 3.3;
const float CLEAR_WATER_VOLTAGE = 1.45;  // Voltage reading in clear water

// Initialize sensors
OneWire oneWire(TEMP_SENSOR_PIN);
DallasTemperature tempSensor(&oneWire);

// Global calibration values
float g_current_4ma = CURRENT_4MA;
float g_depth_range = DEPTH_RANGE;
float g_turb_cal_a = 0.0;
float g_turb_cal_b = 0.0;
float clearWaterVoltage = CLEAR_WATER_VOLTAGE;

struct TurbidityReading {
  int rawADC;
  float actualVoltage;
  float ntu;
};

// Function prototypes
float readCurrentMA();
float convertToDepth(float current_mA);
float readTemperature();
TurbidityReading readTurbidity();
void handleCalibrationServer();
void setupWiFiAP();
void handleRoot();
void handleCalibrate();
void loadCalibrationValues();
void setupCalibration();
String generateReadingsHtml();
void handleTurbidityCalibration();
void handleReadings();

// Sensor Reading Functions
float readCurrentMA() {
  long sum = 0;
  
  for(int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(LEVEL_SENSOR_PIN);
    delay(10);
  }
  
  float average = sum / (float)NUM_SAMPLES;
  float voltage = (average / 4095.0) * 3.3;
  float current = (voltage / RESISTOR_VALUE) * 1000.0;
  
  return current;
}

float convertToDepth(float current_mA) {
  float current_diff = current_mA - g_current_4ma;
  float depth = DEPTH_AT_4MA + (current_diff * (g_depth_range / CURRENT_RANGE));
  
  if (depth < 0) depth = 0;
  return depth;
}

float readTemperature() {
  tempSensor.requestTemperatures();
  float tempC = tempSensor.getTempCByIndex(0);
  
  if(tempC == DEVICE_DISCONNECTED_C) {
    Serial.println("Error: Could not read temperature data");
    return -127;
  }
  
  return tempC;
}

TurbidityReading readTurbidity() {
  long sum = 0;
  TurbidityReading result;
  
  for(int i = 0; i < NUM_SAMPLES; i++) {
    sum += analogRead(TURBIDITY_SENSOR_PIN);
    delay(10);
  }
  
  result.rawADC = sum / NUM_SAMPLES;
  result.actualVoltage = (result.rawADC / 4095.0) * 3.3;
  result.ntu = 3000 * (1 - (result.actualVoltage / clearWaterVoltage));
  
  if(result.actualVoltage > clearWaterVoltage) {
    result.ntu = 0;
  }
  
  if(result.ntu < 0) result.ntu = 0;
  if(result.ntu > 3000) result.ntu = 3000;
  
  return result;
}

String generateReadingsHtml() {
  float current = readCurrentMA();
  float depth = convertToDepth(current);
  float temperature = readTemperature();
  TurbidityReading turb = readTurbidity();
  
  String html = "<div class='reading'>";
  html += "<h3>Current Readings</h3>";
  html += "<table>";
  html += "<tr><td>Current Reading:</td><td>" + String(current, 2) + " mA</td></tr>";
  html += "<tr><td>Water Depth:</td><td>" + String(depth, 1) + " cm</td></tr>";
  html += "<tr><td>Temperature:</td><td>" + String(temperature, 1) + " °C</td></tr>";
  html += "<tr><td>Turbidity:</td><td>" + String(turb.ntu, 1) + " NTU (Voltage: " + String(turb.actualVoltage, 3) + "V)</td></tr>";
  html += "<tr><td>Clear Water Voltage:</td><td>" + String(clearWaterVoltage, 3) + "V</td></tr>";
  html += "</table>";
  html += "</div>";
  
  return html;
}

void handleReadings() {
  server.send(200, "text/html", generateReadingsHtml());
}

void setupWiFiAP() {
  WiFi.softAP(ssid, password);
  Serial.println("Access Point Started");
  Serial.print("IP Address: ");
  Serial.println(WiFi.softAPIP());
}

void handleRoot() {
  String html = "<html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<script>";
  html += "function updateReadings() {";
  html += "  fetch('/readings')";
  html += "    .then(response => response.text())";
  html += "    .then(data => {";
  html += "      document.getElementById('readings').innerHTML = data;";
  html += "    });";
  html += "}";
  html += "setInterval(updateReadings, 2000);";
  html += "</script>";
  html += "<style>";
  html += "body{font-family:Arial;margin:20px;max-width:800px;margin:auto;padding:20px}";
  html += ".reading{background:#f0f0f0;padding:20px;margin:20px 0;border-radius:8px}";
  html += ".config{background:#e0e0e0;padding:20px;margin:20px 0;border-radius:8px}";
  html += "table{width:100%;border-collapse:collapse;margin:10px 0}";
  html += "td,th{padding:8px;text-align:left;border:1px solid #ddd}";
  html += ".button{background-color:#4CAF50;border:none;color:white;padding:10px 20px;text-align:center;margin:10px 0;cursor:pointer;border-radius:4px}";
  html += ".status{padding:10px;margin:10px 0;border-radius:4px}";
  html += ".success{background-color:#dff0d8;color:#3c763d;border:1px solid #d6e9c6}";
  html += "</style></head><body>";
  html += "<h2>Water Level Monitoring System</h2>";
  
  html += "<div id='readings'>";
  html += generateReadingsHtml();
  html += "</div>";
  
  html += "<div class='config'>";
  html += "<h3>Depth Sensor Calibration</h3>";
  html += "<form action='/calibrate' method='post'>";
  html += "<table>";
  html += "<tr><td>Known Depth (cm):</td><td><input type='number' step='0.1' name='known_depth' required></td></tr>";
  html += "<tr><td>Current at Depth (mA):</td><td><input type='number' step='0.01' name='known_current' required></td></tr>";
  html += "</table>";
  html += "<input type='submit' value='Update Depth Calibration' class='button'>";
  html += "</form></div>";
  
  html += "<div class='config'>";
  html += "<h3>Turbidity Calibration</h3>";
  html += "<p>Current Voltage Reading: <span id='currentVoltage'>" + String(readTurbidity().actualVoltage, 3) + "V</span></p>";
  html += "<div style='margin:20px 0'>";
  html += "<h4>Automatic Calibration</h4>";
  html += "<form action='/calibrate_turbidity' method='post'>";
  html += "<input type='submit' value='Set Current as Clear Water' class='button'>";
  html += "</form></div>";
  html += "<div style='margin:20px 0'>";
  html += "<h4>Manual Calibration</h4>";
  html += "<form action='/calibrate_turbidity_manual' method='post'>";
  html += "<table>";
  html += "<tr><td>Clear Water Voltage (V):</td><td><input type='number' step='0.001' name='clear_water_voltage' value='" + String(clearWaterVoltage, 3) + "' required></td></tr>";
  html += "</table>";
  html += "<input type='submit' value='Set Manual Voltage' class='button'>";
  html += "</form></div>";
  html += "</div>";
  
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleCalibrate() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  float known_depth = server.arg("known_depth").toFloat();
  float known_current = server.arg("known_current").toFloat();
  
  // Calculate new calibration values
  g_current_4ma = known_current - ((known_depth / DEPTH_RANGE) * CURRENT_RANGE);
  g_depth_range = (known_depth / (known_current - g_current_4ma)) * CURRENT_RANGE;
  
  // Save to EEPROM
  EEPROM.writeFloat(ADDR_CURRENT_4MA, g_current_4ma);
  EEPROM.writeFloat(ADDR_DEPTH_RANGE, g_depth_range);
  EEPROM.commit();
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleTurbidityCalibration() {
  TurbidityReading currentReading = readTurbidity();
  clearWaterVoltage = currentReading.actualVoltage;
  EEPROM.writeFloat(ADDR_CLEAR_WATER_VOLTAGE, clearWaterVoltage);
  EEPROM.commit();
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void loadCalibrationValues() {
  g_current_4ma = EEPROM.readFloat(ADDR_CURRENT_4MA);
  g_depth_range = EEPROM.readFloat(ADDR_DEPTH_RANGE);
  float savedClearWaterVoltage = EEPROM.readFloat(ADDR_CLEAR_WATER_VOLTAGE);
  
  if (isnan(g_current_4ma) || g_current_4ma < 0 || g_current_4ma > 20) {
    g_current_4ma = CURRENT_4MA;
  }
  if (isnan(g_depth_range) || g_depth_range <= 0 || g_depth_range > 1000) {
    g_depth_range = DEPTH_RANGE;
  }
  if (!isnan(savedClearWaterVoltage) && savedClearWaterVoltage > 0) {
    clearWaterVoltage = savedClearWaterVoltage;
  }
}

//trubid manual calibration
void handleTurbidityCalibrationManual() {
  if (server.method() != HTTP_POST) {
    server.send(405, "text/plain", "Method Not Allowed");
    return;
  }
  
  float newVoltage = server.arg("clear_water_voltage").toFloat();
  
  // Validate the input
  if (newVoltage > 0 && newVoltage <= 3.3) {  // 3.3V is max for ESP32
    clearWaterVoltage = newVoltage;
    EEPROM.writeFloat(ADDR_CLEAR_WATER_VOLTAGE, clearWaterVoltage);
    EEPROM.commit();
  }
  
  server.sendHeader("Location", "/");
  server.send(303);
}

void setupCalibration() {
  EEPROM.begin(EEPROM_SIZE);
  loadCalibrationValues();
  setupWiFiAP();
  
  server.on("/", handleRoot);
  server.on("/readings", handleReadings);
  server.on("/calibrate", handleCalibrate);
  server.on("/calibrate_turbidity", HTTP_POST, handleTurbidityCalibration);
  server.on("/calibrate_turbidity_manual", HTTP_POST, handleTurbidityCalibrationManual);  // Add this line
  server.begin();
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Serial.println("LoRa Water Monitoring System - Sender");
  
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  
  tempSensor.begin();
  
  LoRa.setPins(LORA_SS, LORA_RST, LORA_DIO0);
  
  while (!LoRa.begin(915E6)) {
    Serial.println(".");
    delay(500);
  }
  
  LoRa.setTxPower(20);
  LoRa.setSignalBandwidth(500E3);
  LoRa.setSpreadingFactor(7);
  LoRa.setCodingRate4(5);
  LoRa.setSyncWord(0xF0);
  LoRa.enableCrc();
  
  setupCalibration();
  
  Serial.println("LoRa Initializing OK!");
}

// In the loop() function, replace handleCalibrationServer() with server.handleClient()

void loop() {
  float current = readCurrentMA();
  float depth = convertToDepth(current);
  float temperature = readTemperature();
  TurbidityReading turbidity = readTurbidity();
  
  String dataPacket = "{\"current\":";
  dataPacket += String(current, 2);
  dataPacket += ",\"depth\":";
  dataPacket += String(depth, 1);
  dataPacket += ",\"temp\":";
  dataPacket += String(temperature, 1);
  dataPacket += ",\"turb_v\":";
  dataPacket += String(turbidity.actualVoltage, 2);
  dataPacket += ",\"turb_ntu\":";
  dataPacket += String(turbidity.ntu, 1);
  dataPacket += ",\"id\":1}";

  Serial.print("Packet size: ");
  Serial.print(dataPacket.length());
  Serial.println(" bytes");

  Serial.println("Attempting to send packet...");
  
  LoRa.beginPacket();
  LoRa.print(dataPacket);
  bool transmitted = LoRa.endPacket();
  delay(50);
  
  if (transmitted) {
    Serial.println("✓ Packet transmitted successfully");
  } else {
    Serial.println("✗ Transmission failed!");
  }

  Serial.println("\n--- Sensor Readings ---");
  Serial.print("Water Level - Current: ");
  Serial.print(current, 2);
  Serial.print(" mA, Depth: ");
  Serial.print(depth, 1);
  Serial.println(" cm");
  
  Serial.print("Temperature: ");
  if(temperature != -127) {
    Serial.print(temperature, 1);
    Serial.println(" °C");
  } else {
    Serial.println("Error reading temperature");
  }
  
  Serial.print("Turbidity - Raw ADC: ");
  Serial.print(turbidity.rawADC);
  Serial.print(", Actual Voltage: ");
  Serial.print(turbidity.actualVoltage, 2);
  Serial.print("V, NTU: ");
  Serial.println(turbidity.ntu, 1);
  
  Serial.print("Packet sent: ");
  Serial.println(dataPacket);
  Serial.println("--------------------");

  static unsigned long lastParamPrint = 0;
  if (millis() - lastParamPrint > 10000) {
    Serial.println("\n--- LoRa Status ---");
    Serial.print("RSSI: ");
    Serial.print(LoRa.packetRssi());
    Serial.println(" dBm");
    Serial.println("--------------------\n");
    lastParamPrint = millis();
  }

  server.handleClient(); // Changed this line from handleCalibrationServer()
  delay(2000);
}