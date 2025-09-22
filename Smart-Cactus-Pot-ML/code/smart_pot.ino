// --- Smart Cactus Monitor with Blynk & Google Sheets Logging (Final Version) ---
// Includes pump status logging.

// ===================================================================
// --- BLYNK & WIFI CREDENTIALS ---
// ===================================================================
#define BLYNK_TEMPLATE_ID "TMPL3q2QR1WQC"
#define BLYNK_TEMPLATE_NAME "Cactus Monitor"
#define BLYNK_AUTH_TOKEN "_FQkRzlSVhnhimYlElLgBDa7MpfsTS_R"
#define BLYNK_PRINT Serial

// --- LIBRARIES ---
#include <ESP8266WiFi.h>
#include <BlynkSimpleEsp8266.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include <DHT.h>
#include <math.h>
#include <ESP8266HTTPClient.h> // For sending data to Google
#include <WiFiClientSecure.h>  // For HTTPS connection

// --- WIFI CREDENTIALS ---
char ssid[] = "DNS";
char pass[] = "@LIONASHISH@";

// ===================================================================
// --- GOOGLE SHEETS INTEGRATION ---
// ===================================================================
// Paste your Google Apps Script URL here
String GOOGLE_SCRIPT_URL = "https://script.google.com/macros/s/AKfycbzd4I0LAatSvja0xJHnQ6R6pXSeUlkjLkyO_HR_UMHfVDNVt3tceq54aPx1VWsplrZEUw/exec";
WiFiClientSecure client;
// ===================================================================

// --- PIN DEFINITIONS ---
#define RELAY_PIN D5
#define DHT_PIN   D4
#define DHTTYPE   DHT11

// --- CALIBRATED SENSOR VALUES ---
const int SOIL_DRY_VALUE  = 16850;
const int SOIL_WET_VALUE  = -530;
const int LDR_DARK_VALUE  = 6000;
const int LDR_LIGHT_VALUE = 16130;
#define SOIL_THRESHOLD 30

// --- MQ-135 CONFIGURATION ---
#define MQ_CLEAN_AIR_RAW   990.0  // Your fine-tuned value
#define RL_VALUE           10.0
#define CO2_CURVE_A        116.6020682
#define CO2_CURVE_B        -2.769034857
#define ADC_RESOLUTION     32767.0
float R0 = 0.0;

// --- INITIALIZE OBJECTS ---
Adafruit_ADS1115 ads;
DHT dht(DHT_PIN, DHTTYPE);
BlynkTimer blynkTimer;      // Timer for sending data to Blynk
BlynkTimer googleSheetTimer; // A separate timer for Google Sheets

// --- GLOBAL VARIABLES ---
bool manualPumpControl = false;
String pumpStatus = "OFF"; // Global variable to track pump status

// Function to log data to Google Sheets
void logDataToGoogleSheet() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi not connected, skipping Google Sheet log.");
    return;
  }

  Serial.println("Preparing data for Google Sheets...");
  // Read all sensor values for a fresh reading
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int16_t soilRaw = ads.readADC_SingleEnded(0);
  int16_t ldrRaw = ads.readADC_SingleEnded(1);
  int16_t mqRaw = ads.readADC_SingleEnded(2);
  
  if(isnan(h) || isnan(t)) {
    Serial.println("DHT read failed, skipping log.");
    return;
  }
  
  int soilPercent = constrain(map(soilRaw, SOIL_DRY_VALUE, SOIL_WET_VALUE, 0, 100), 0, 100);
  int lightPercent = constrain(map(ldrRaw, LDR_DARK_VALUE, LDR_LIGHT_VALUE, 0, 100), 0, 100);
  float Rs = (ADC_RESOLUTION / (float)mqRaw - 1.0) * RL_VALUE;
  float ppm = CO2_CURVE_A * pow(Rs / R0, CO2_CURVE_B);

  // Construct the URL with sensor data as parameters, including the pump status
  String url = GOOGLE_SCRIPT_URL + "?temperature=" + String(t, 2) +
               "&humidity=" + String(h, 2) +
               "&soil=" + String(soilPercent) +
               "&light=" + String(lightPercent) +
               "&ppm=" + String(ppm, 2) +
               "&pump_status=" + pumpStatus; // <-- ADDED PUMP STATUS

  Serial.print("Requesting URL: ");
  Serial.println(url);

  HTTPClient http;
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  
  int httpCode = http.GET();
  if (httpCode > 0) {
    String payload = http.getString();
    Serial.print("HTTP Response code: ");
    Serial.println(httpCode);
    Serial.print("Response payload: ");
    Serial.println(payload);
  } else {
    Serial.print("HTTP GET failed, error: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }
  http.end();
}

// This function handles the manual pump switch (V5) from the app
BLYNK_WRITE(V5) {
  int switchValue = param.asInt();
  if (switchValue == 1) {
    manualPumpControl = true;
    digitalWrite(RELAY_PIN, LOW); // Turn pump ON
    Blynk.virtualWrite(V6, 1);
    pumpStatus = "ON"; // Update global status
  } else {
    manualPumpControl = false;
    digitalWrite(RELAY_PIN, HIGH); // Turn pump OFF
    Blynk.virtualWrite(V6, 0);
    pumpStatus = "OFF"; // Update global status
  }
}

// All sensor reading and logic for BLYNK is in this function
void sendSensorDataToBlynk() {
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int16_t soilRaw = ads.readADC_SingleEnded(0);
  int16_t ldrRaw = ads.readADC_SingleEnded(1);
  int16_t mqRaw = ads.readADC_SingleEnded(2);

  float Rs = (ADC_RESOLUTION / (float)mqRaw - 1.0) * RL_VALUE;
  float ppm = CO2_CURVE_A * pow(Rs / R0, CO2_CURVE_B);
  int soilPercent = constrain(map(soilRaw, SOIL_DRY_VALUE, SOIL_WET_VALUE, 0, 100), 0, 100);
  int lightPercent = constrain(map(ldrRaw, LDR_DARK_VALUE, LDR_LIGHT_VALUE, 0, 100), 0, 100);

  // Automatic pump logic
  if (!manualPumpControl) {
    if (soilPercent < SOIL_THRESHOLD) {
      digitalWrite(RELAY_PIN, LOW); // Turn pump ON
      Blynk.virtualWrite(V6, 1);
      pumpStatus = "ON"; // Update global status
    } else {
      digitalWrite(RELAY_PIN, HIGH); // Turn pump OFF
      Blynk.virtualWrite(V6, 0);
      pumpStatus = "OFF"; // Update global status
    }
  }

  // Send data to Blynk App
  if (!isnan(h) && !isnan(t)) {
    Blynk.virtualWrite(V1, t);
    Blynk.virtualWrite(V2, soilPercent);
    Blynk.virtualWrite(V3, lightPercent);
    Blynk.virtualWrite(V4, h);
    Blynk.virtualWrite(V7, ppm);
  }
}

void setup() {
  Serial.begin(9600);
  delay(1000);
  Serial.println("\nSmart Cactus Monitor with Blynk & Google Sheets - Starting...");

  client.setInsecure();

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); // Start with pump OFF

  dht.begin();
  if (!ads.begin()) {
    Serial.println("Failed to initialize ADS. Halting.");
    while (1);
  }
  ads.setGain(GAIN_TWOTHIRDS);

  R0 = (ADC_RESOLUTION / MQ_CLEAN_AIR_RAW - 1.0) * RL_VALUE;
  Serial.print("R0 calculated as: ");
  Serial.println(R0);

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blr1.blynk.cloud", 80);

  // Timer for updating Blynk app UI (every 5 seconds)
  blynkTimer.setInterval(5000L, sendSensorDataToBlynk);
  
  // Timer for logging data to Google Sheets (every 15 minutes = 900000 milliseconds)
  googleSheetTimer.setInterval(900000L, logDataToGoogleSheet);
}

void loop() {
  Blynk.run();
  blynkTimer.run();
  googleSheetTimer.run();
}