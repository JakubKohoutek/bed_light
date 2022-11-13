#include <ESP8266WiFi.h>
#include <fauxmoESP.h>
#include <credentials.h>
#include "ota.h"
#include "memory.h"

#define LED_PIN D6
#define PIR_PIN D7

enum State { steady, fadingIn, fadingOut };

fauxmoESP   fauxmo;
const char* ssid                    = STASSID;
const char* password                = STAPSK;
const char* deviceId                = "Jacob's bed light";
State       currentState            = steady;
int         currentBrightness       = 0;
int         brightnessMemoryAddress = 0;
int         maximumBrightness       = 255;
int         timeOfLastProcessing    = millis();
bool        controlledByAssistant   = false;

void setup() {
  pinMode(LED_PIN,     OUTPUT);
  pinMode(PIR_PIN,     INPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  Serial.begin(115200);
  Serial.println("\nBooting");

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  // Inititate eeprom memory
  initiateMemory();
  //  writeToMemory(brightnessMemoryAddress, 0); // erase memory, only on the first upload
  maximumBrightness = readFromMemory(brightnessMemoryAddress);

  // Initiate over the air programming
  OTA::initialize(deviceId);

  // Configure connection to home assistants
  fauxmo.createServer(true);
  fauxmo.setPort(80);
  fauxmo.addDevice(deviceId);
  fauxmo.enable(true);

  fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool switchedOn, unsigned char value) {
    Serial.printf(
      "[MAIN] Device #%d (%s) state: %s value: %d\n",
      device_id, device_name, switchedOn ? "ON" : "OFF", value
    );
    maximumBrightness = value;
    writeToMemory(brightnessMemoryAddress, value);
    controlledByAssistant = switchedOn;
    if (switchedOn) {
      currentState = fadingIn;
    } else {
      currentState = fadingOut;
    }
  });
}

void handleLightChangeEvents () {
  int currentTime = millis();
  if (currentTime - timeOfLastProcessing < 2) {
    return;
  }
  timeOfLastProcessing = currentTime;
  
  switch (currentState) {
    case steady:
      analogWrite(LED_PIN, currentBrightness);
      break;
    case fadingIn:
      if (currentBrightness < maximumBrightness) {
        analogWrite(LED_PIN, ++currentBrightness);
      } else 
      if (currentBrightness > maximumBrightness) {
        analogWrite(LED_PIN, --currentBrightness);
      } else {
        currentState = steady;
      }
      break;
    case fadingOut:
      if (currentBrightness > 0) {
        analogWrite(LED_PIN, --currentBrightness);
      } else {
        currentState = steady;
      }
      break;
    default:
      currentState = steady;
  }
}

void loop() {
  fauxmo.handle();
  OTA::handle();
  handleLightChangeEvents();

  if (digitalRead(PIR_PIN) == HIGH) {
    digitalWrite(LED_BUILTIN, LOW);
    if (!controlledByAssistant) {
      currentState = fadingIn;
    }
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
    if (!controlledByAssistant) {
      currentState = fadingOut;
    }
  }
}
