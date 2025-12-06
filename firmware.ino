/*
 * SafeSocket Project - Hackathon Firmware
 * Components: ESP32 + ACS712 (20A) + Relay + Buzzer + LED + Button
 * Features: RMS Current Calculation, Safety Timer, WiFi, HTTP API Sync
 */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h> // REQUIRED: Install via Library Manager

// ================= USER CONFIGURATION =================

// 1. WiFi Credentials (Target Network)
const char* ssid     = "BootLoaderTRJN_.exe";      // <--- CHANGE THIS to your Friend's WiFi Name
const char* password = "At210107";  // <--- CHANGE THIS to your Friend's WiFi Pass

// 2. Python Server IP (Your Friend's Laptop)
// Note: Ensure Firewall is OFF on the laptop for Port 5000
const char* serverUrl = "http://10.66.160.231:5000/update_sensor"; 

// 3. Safety Thresholds (Demo Settings)
const float CURRENT_THRESHOLD = 0.20;       // 0.20 Amps (Sensitivity for 100W Bulb)
const unsigned long SAFETY_LIMIT = 20000;   // 20 Seconds Max Run Time (Demo)
const unsigned long WARNING_TIME = 10000;   // 10 Seconds Warning Phase

// ================= PIN DEFINITIONS =================
#define PIN_SENSOR 34   // ACS712 Output (Analog)
#define PIN_RELAY  26   // Relay Signal (Digital Out)
#define PIN_BUZZER 27   // Buzzer Positive
#define PIN_LED    14   // Red LED
#define PIN_BUTTON 33   // Reset Button (Input Pullup)

// Global Variables
unsigned long timerStart = 0;
bool powerCutoff = false;
unsigned long lastSendTime = 0;
String statusMsg = "SAFE";

// Calibration (Adjust if sensor reads incorrectly at 0A)
int zeroPoint = 1880; 

void setup() {
  Serial.begin(115200);

  // Pin Configuration
  pinMode(PIN_RELAY, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_LED, OUTPUT);
  pinMode(PIN_SENSOR, INPUT);
  pinMode(PIN_BUTTON, INPUT_PULLUP);

  // Default State: Relay ON (Power Flowing)
  digitalWrite(PIN_RELAY, HIGH); 
  digitalWrite(PIN_BUZZER, LOW);
  digitalWrite(PIN_LED, LOW);

  // WiFi Connection
  Serial.println("\n------------------------------");
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi Connected!");
    Serial.print("Device IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nWiFi Failed! Running in Offline Mode.");
  }
}

void loop() {
  // 1. MEASURE CURRENT (RMS)
  float amps = readCurrentRMS();

  // 2. CHECK RESET BUTTON
  // Button connects Pin 33 to GND. LOW = Pressed.
  if (digitalRead(PIN_BUTTON) == LOW) {
    resetSystem();
  }

  // 3. SAFETY LOGIC ENGINE
  if (!powerCutoff) {
    if (amps > CURRENT_THRESHOLD) {
      // --- LOAD DETECTED ---
      if (timerStart == 0) timerStart = millis(); // Start Timer
      
      unsigned long elapsed = millis() - timerStart;
      
      if (elapsed < SAFETY_LIMIT) {
        // Normal Operation (Counting down)
        statusMsg = "ACTIVE (" + String((SAFETY_LIMIT - elapsed)/1000) + "s)";
        digitalWrite(PIN_LED, LOW);
      } 
      else if (elapsed < (SAFETY_LIMIT + WARNING_TIME)) {
        // Warning Phase (Grace Period)
        statusMsg = "WARNING! PRESS BUTTON";
        // Blink LED and Beep
        bool blink = (millis() / 250) % 2; // Fast blink
        digitalWrite(PIN_LED, blink);
        digitalWrite(PIN_BUZZER, blink);
      } 
      else {
        // Time Up -> Kill Power
        triggerCutoff();
      }
    } else {
      // --- IDLE / SAFE ---
      timerStart = 0;
      statusMsg = "IDLE";
      digitalWrite(PIN_LED, LOW);
      digitalWrite(PIN_BUZZER, LOW);
    }
  } else {
    // --- SYSTEM LOCKED ---
    statusMsg = "CUTOFF TRIGGERED";
  }

  // 4. SEND DATA TO PYTHON (Once per second)
  if (millis() - lastSendTime > 1000) {
    sendData(amps, statusMsg);
    lastSendTime = millis();
  }
}

// === HELPER FUNCTIONS ===

void triggerCutoff() {
  Serial.println(">>> CUTOFF TRIGGERED <<<");
  digitalWrite(PIN_RELAY, LOW);  // Turn Relay OFF
  digitalWrite(PIN_BUZZER, LOW); // Silence Buzzer
  digitalWrite(PIN_LED, HIGH);   // Solid Red LED
  powerCutoff = true;
}

void resetSystem() {
  Serial.println(">>> SYSTEM RESET <<<");
  digitalWrite(PIN_RELAY, HIGH); // Turn Relay ON
  digitalWrite(PIN_LED, LOW);
  powerCutoff = false;
  timerStart = 0;
  delay(500); // Debounce
}

float readCurrentRMS() {
  int readValue;             
  int maxValue = 0;          
  int minValue = 4095;       
  
  uint32_t start_time = millis();
  // Sample for 20ms (One complete 50Hz AC Cycle)
  while((millis()-start_time) < 20) {
     readValue = analogRead(PIN_SENSOR);
     if (readValue > maxValue) maxValue = readValue;
     if (readValue < minValue) minValue = readValue;
  }
  
  // ESP32 ADC: 0-4095 maps to 0-3.3V
  float voltagePeak = (maxValue - minValue) * (3.3 / 4095.0);
  
  // ACS712 Sensitivity: 100mV/A for 20A module (0.100 V/A)
  // RMS Voltage = Peak * 0.707
  float amps = (voltagePeak * 0.707) / 0.100; 
  
  if (amps < 0.10) amps = 0.0; // Filter noise floor
  return amps;
}

void sendData(float current, String status) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(serverUrl);
    http.addHeader("Content-Type", "application/json");

    // Create JSON using ArduinoJson
    StaticJsonDocument<200> doc;
    doc["current"] = current;
    doc["status"] = status;
    
    String requestBody;
    serializeJson(doc, requestBody);

    // Send POST Request
    int httpResponseCode = http.POST(requestBody);
    
    if (httpResponseCode > 0) {
       // Success
    } else {
      Serial.print("Error sending data: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  }
}