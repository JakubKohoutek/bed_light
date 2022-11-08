#include <ESP8266WiFi.h>
#include <fauxmoESP.h>
#include <credentials.h>
#include "ota.h"

#define LED_PIN D6
#define PIR_PIN D7

const char* ssid     = STASSID;
const char* password = STAPSK;
const char* deviceId = "Jacob's bed light";

fauxmoESP fauxmo;
int currentBrightness = 0;
int maximumBrightness = 1023;

void setup() {
  Serial.begin(115200);
  Serial.println("Booting");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Initiate over the air programming
  OTA::initialize(deviceId);

  // Configure connection with home assistants
  fauxmo.createServer(true);
  fauxmo.setPort(80);
  fauxmo.addDevice(deviceId);
  fauxmo.enable(true);

  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool state, unsigned char value) {
    Serial.printf("[MAIN] Device #%d (%s) state: %s value: %d\n", device_id, device_name, state ? "ON" : "OFF", value);
  });
    
  pinMode(LED_PIN,     OUTPUT);
  pinMode(PIR_PIN,     INPUT);
  pinMode(LED_BUILTIN, OUTPUT);
}

void handleNetworkRequests () {
  fauxmo.handle();
  OTA::handle();
}

void saveDelay (int milliseconds) {
  handleNetworkRequests();
  delay(milliseconds);
}

void fadeIn() {
  while (currentBrightness < maximumBrightness) {
    analogWrite(LED_PIN, ++currentBrightness);
    saveDelay(1);
  };
}

void fadeOut() {
  while (currentBrightness > 0) {
    analogWrite(LED_PIN, --currentBrightness);
    saveDelay(1);
  };
}

void loop() {
  handleNetworkRequests();
  if (digitalRead(PIR_PIN) == HIGH) {
    digitalWrite(LED_BUILTIN, LOW);
    fadeIn();
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    fadeOut();
  }
}
