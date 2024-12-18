#include <WiFiManager.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <FastLED.h>
#include <WiFiUdp.h>
#include <NTPClient.h>

#define NUM_LEDS 49
#define DATA_PIN 23
#define TRAIL_LENGTH 5

const char *api_url = "http://liftie.dejong.cc:7070/resort/lesarcs";

const long utcOffsetInSeconds = 3600;
const unsigned long updateInterval = 65000;  // 65 seconds

// Globals
CRGB leds[NUM_LEDS];
WiFiManager wifiManager;
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", utcOffsetInSeconds);
StaticJsonDocument<8192> jsonDoc;

unsigned long lastUpdate = 0;

const char *liftNames[] = {
  "Millerette", "Funiculaire", "Tommelet", "Combettes", "Vezaille",
  "Cachette", "Mont Blanc", "Clocheret", "Snowpark", "Arpette",
  "Carreley", "Vagere", "Charmettoger", "Transarc 1", "Villards",
  "Jardin Alpin", "Grizzly", "Lonzagne", "Vanoise Express Montchavin", "Parchey",
  "Peisey", "Vallandry", "Combe", "Cabri", "2300", "1Er Virages", "Derby",
  "Transarc 2", "Plan Vert", "Plagnettes", "Grand Col", "Arcabulle",
  "Eterlou", "Bois De L'ours", "Marmottes", "Varet", "Aiguille Rouge",
  "St Jacques", "Lanchettes", "Lac Des Combes", "Cabriolet", "Pre St Esprit",
  "Comborciere", "Rhonaz", "Droset", "Plan Des Violettes", "Replat"
};

const int LiftCount = sizeof(liftNames) / sizeof(liftNames[0]);

// Function prototypes
void configModeCallback(WiFiManager *myWiFiManager);
String fetchLiftData();
void parseAndDisplayLiftData(const String &payload);
void updateLEDStatus(const char *status, int index);

void setup() {
  Serial.begin(115200);

  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(60);
  FastLED.clear(true);
  FastLED.show();

  // Set callback for entering configuration mode
  wifiManager.setAPCallback(configModeCallback);

  // Set custom hostname
  wifiManager.setHostname("LiftStatus-ESP32");

  // Uncomment to reset saved settings
  // wifiManager.resetSettings();

  // Configure timeout
  wifiManager.setConfigPortalTimeout(180);  // 3 minutes timeout

  // Create a unique device name
  String deviceName = "LiftStatus-" + String((uint32_t)ESP.getEfuseMac(), HEX);

  // Attempt to connect or start config portal
  if (!wifiManager.autoConnect(deviceName.c_str())) {
    Serial.println("Failed to connect and hit timeout");
    delay(3000);
    ESP.restart();
  }

  Serial.println("Connected to WiFi!");
  timeClient.begin();
}

void loop() {
  leds[0] = CRGB::Black;
  FastLED.show();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi connection lost. Waiting for reconnect...");
    delay(1000);
    return;
  }

  unsigned long currentMillis = millis();
  if (lastUpdate == 0 || currentMillis - lastUpdate >= updateInterval) {
    lastUpdate = currentMillis;

    timeClient.update();
    int currentHour = timeClient.getHours();

    if (currentHour >= 8 && currentHour <= 17) {  // From 8 until 18
      String payload = fetchLiftData();
      if (!payload.isEmpty()) {
        parseAndDisplayLiftData(payload);
      }
    } else {
      Serial.println("Nighttime detected. Turning off LEDs.");
      FastLED.clear();
      FastLED.show();
    }
  }
  delay(1000);
}

// Callback for when entering configuration mode
void configModeCallback(WiFiManager *myWiFiManager) {
  Serial.println("Entered config mode");
  Serial.println(WiFi.softAPIP());
  Serial.println(myWiFiManager->getConfigPortalSSID());

  // Visual indicator for config mode - blue blinking LED
  for (int i = 0; i < NUM_LEDS + TRAIL_LENGTH; i++) {
    // Turn on the LEDs in the trail
    for (int j = 0; j < TRAIL_LENGTH; j++) {
      int index = i - j;  // Calculate the index of the trailing LED
      if (index >= 0 && index < NUM_LEDS) {
        leds[index] = CRGB::Blue;  // Set the LED to blue
      }
    }

    FastLED.show();
    delay(50);  // Adjust the delay to control the speed of the trail

    // Turn off the LEDs outside the trail
    for (int j = 0; j < TRAIL_LENGTH; j++) {
      int index = i - j - 1;  // Calculate the index of the LED to turn off
      if (index >= 0 && index < NUM_LEDS) {
        leds[index] = CRGB::Black;  // Turn off the LED
      }
    }
  }
}

// Rest of the functions remain the same
String fetchLiftData() {
  HTTPClient http;
  String payload;

  Serial.println("[HTTP] Connecting to API...");

  if (!WiFi.isConnected()) {
    Serial.println("[HTTP] WiFi is not connected!");
    return "";
  }

  Serial.println("[HTTP] Beginning connection...");
  if (http.begin(api_url)) {
    Serial.println("[HTTP] Sending GET request...");
    int httpCode = http.GET();
    Serial.printf("[HTTP] GET response code: %d\n", httpCode);

    if (httpCode == HTTP_CODE_OK) {
      payload = http.getString();
      Serial.println("[HTTP] Received payload successfully");
    } else {
      Serial.printf("[HTTP] GET failed, error: %s\n", http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.println("[HTTP] Unable to begin connection to API");
  }

  return payload;
}

void parseAndDisplayLiftData(const String &payload) {
  DeserializationError error = deserializeJson(jsonDoc, payload);
  if (error) {
    Serial.printf("JSON parsing failed: %s\n", error.c_str());
    return;
  }

  JsonObject lifts = jsonDoc["lifts"];
  if (!lifts.isNull()) {
    for (JsonPair lift : lifts) {
      String liftName = lift.key().c_str();
      const char *status = lift.value();

      if (!status) {
        Serial.printf("WARNING: No status found for lift %s\n", liftName.c_str());
        continue;
      }
      updateLEDStatus(status, liftName);
    }

    FastLED.show();
  } else {
    Serial.println("ERROR: No lift data available in the JSON response");
  }
}

void updateLEDStatus(const char *status, String liftName) {
  if (!status) {
    Serial.println("WARNING: Null status received");
  }

  int ledIndex = -1;

  for (int i = 0; i < LiftCount; i++) {
    if (strcmp(liftNames[i], liftName.c_str()) == 0) {
      ledIndex = i + 1;
      break;
    }
  }

  CRGB newColor;
  const char *colorName;

  if (!status) {
    newColor = CRGB::Black;
    colorName = "Black";
  } else if (strcmp(status, "open") == 0) {
    newColor = CRGB::Green;
    colorName = "Green";
  } else if (strcmp(status, "closed") == 0) {
    newColor = CRGB::Red;
    colorName = "Red";
  } else if (strcmp(status, "hold") == 0 || strcmp(status, "scheduled") == 0) {
    newColor = CRGB::Yellow;
    colorName = "Yellow";
  } else {
    newColor = CRGB::Black;
    colorName = "Black";
  }

  Serial.printf("Updated LED for lift '%s' with LED index '%d' to color '%s' for status '%s'\n", liftName.c_str(), ledIndex, colorName, status);

  if (ledIndex > 0 && ledIndex <= NUM_LEDS) {
    leds[ledIndex] = newColor;
  }
}