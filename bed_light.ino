#include <ESP8266WiFi.h>
#include <ESPAsyncWebServer.h>
#include <fauxmoESP.h>
#include <WebSerial.h>
#include <credentials.h>
#include "ota.h"
#include "memory.h"

#define LED_PIN D6
#define PIR_PIN D7
#define LDR_PIN A0

enum State { steady, fadingIn, fadingOut };

fauxmoESP      fauxmo;
AsyncWebServer server(80);
const char*    ssid                     = STASSID;
const char*    password                 = STAPSK;
const char*    deviceId                 = "Jacob's bed light";
State          currentState             = steady;
int            ledStripBrightness       = 0;
int            brightnessMemoryAddress  = 0;
int            thresholdMemoryAddress   = 4;
int            falseAlertsMemoryAddress = 8;
int            maximumBrightness        = 255;
int            roomBrightness           = 0;
int            roomBrightnessThreshold  = 100;
int            timeOfLastProcessing     = millis();
int            timeOfLastTrigger        = millis();
int            timeOfSensorSwitchOff    = millis();
bool           controlledByAssistant    = false;
int            falsePositivesCount      = 0;
int            lastFalsePositivesCount  = 0;
boolean        lightOn                  = false;
int            lightOnTimeout           = 30 * 1000;

void turnOffWiFi() {
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
}

void turnOnWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid, password);
    while (WiFi.status() != WL_CONNECTED){
        delay(500);
    }
}

void setup() {
    pinMode(LED_PIN,     OUTPUT);
    pinMode(PIR_PIN,     INPUT);
    pinMode(LED_BUILTIN, OUTPUT);

    turnOnWiFi();

    // Inititate eeprom memory
    initiateMemory();

    // erase memory, only on the first upload
    // writeToMemory(brightnessMemoryAddress, 0);
    // writeToMemory(thresholdMemoryAddress, roomBrightnessThreshold);

    maximumBrightness = readFromMemory(brightnessMemoryAddress);
    roomBrightnessThreshold = readFromMemory(thresholdMemoryAddress);
    lastFalsePositivesCount = readFromMemory(falseAlertsMemoryAddress);
    writeToMemory(falseAlertsMemoryAddress, 0);

    // Initiate over the air programming
    OTA::initialize(deviceId);

    // Configure connection to home assistants
    fauxmo.createServer(false);
    fauxmo.setPort(80);
    fauxmo.addDevice(deviceId);
    fauxmo.enable(true);

    fauxmo.onSetState([](unsigned char device_id, const char * device_name, bool switchedOn, unsigned char value) {
        String statusMessage = String("[MAIN] Device ") + "device_id" + "(" + device_name + ") state: " + (switchedOn ? "ON" : "OFF") + ", value: " + value;
        WebSerial.println(statusMessage);

        maximumBrightness = value;
        writeToMemory(brightnessMemoryAddress, value);
        controlledByAssistant = switchedOn;
        if (switchedOn) {
            currentState = fadingIn;
        } else {
            currentState = fadingOut;
        }
    });

    // WebSerial is accessible at "<IP Address>/webserial" in browser
    WebSerial.begin(&server);
    WebSerial.msgCallback(handleWebSerialMessage);

    server.on("/index.html", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", (String("The room brightness is ") + String(roomBrightness)).c_str());
    });

    server.onRequestBody([](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
        if (fauxmo.process(request->client(), request->method() == HTTP_GET, request->url(), String((char *)data))) return;
        // Handle any other body request here...
    });

    server.onNotFound([](AsyncWebServerRequest *request) {
        String body = (request->hasParam("body", true)) ? request->getParam("body", true)->value() : String();
        if (fauxmo.process(request->client(), request->method() == HTTP_GET, request->url(), body)) return;
        request->send(404, "text/plain", "Sorry, not found. Did you mean to go to /webserial ?");
    });

    server.begin();
}

void handleWebSerialMessage(uint8_t *data, size_t len){
    // Process message into command:value pair  
    String command = "";
    String value   = "";
    boolean beforeColon = true;
    for(int i=0; i < len; i++){
        if (char(data[i]) == ':'){
            beforeColon = false;
        } else if (beforeColon) {
            command += char(data[i]);
        } else {
            value += char(data[i]);
        }
    }

    if(command.equals("setBrightnessThreshold")) {
        WebSerial.println(String("Setting brightness threshold to ") + value.toInt());
        roomBrightnessThreshold = value.toInt();
        writeToMemory(thresholdMemoryAddress, roomBrightnessThreshold);
    } else
    if(command.equals("getBrightnessThreshold")) {
        WebSerial.println(String("Brightness threshold is ") + roomBrightnessThreshold);
    } else 
    if(command.equals("getBrightness")) {
        WebSerial.println(String("Brightness is ") + readRoomBrightness());
    } else 
    if(command.equals("sleepWiFi")) {
        WebSerial.println("Putting WiFi to sleep. To enable it again, you have to restart the device.");
        turnOffWiFi();
    } else
    if(command.equals("getFalsePositives")) {
        WebSerial.println(String("Number of false positives since the start: ") + falsePositivesCount);
    } else
    if(command.equals("getPreviousFalsePositives")) {
        WebSerial.println(String("Number of false positives in the last run: ") + lastFalsePositivesCount);
    } else
    if(command.equals("help")) {
        WebSerial.println("Available commands:\n");
        WebSerial.println("getBrightnessThreshold");
        WebSerial.println("setBrightnessThreshold:value");
        WebSerial.println("getBrightness");
        WebSerial.println("sleepWiFi");
        WebSerial.println("getFalsePositives");
        WebSerial.println("getPreviousFalsePositives");
    } else {
        WebSerial.println(String("Unknown command '") + command + "' with value '" + value +"'" + value);
    }
}

int readRoomBrightness () {
    // analogRead (namely the ADC) sometimes conflicts with the WiFi and Server request processing
    // we can't use it on every loop iteration, but only sometimes, on demand!
    roomBrightness = analogRead(LDR_PIN);

    return roomBrightness;
}

void handleLightChangeEvents () {
    int currentTime = millis();
    if (currentTime - timeOfLastProcessing < 2) {
        return;
    }
    timeOfLastProcessing = currentTime;
    
    switch (currentState) {
        case steady:
            analogWrite(LED_PIN, ledStripBrightness);
            break;
        case fadingIn:
            if (ledStripBrightness < maximumBrightness) {
                analogWrite(LED_PIN, ++ledStripBrightness);
            } else 
            if (ledStripBrightness > maximumBrightness) {
                analogWrite(LED_PIN, --ledStripBrightness);
            } else {
                currentState = steady;
            }
            break;
        case fadingOut:
            if (ledStripBrightness > 0) {
                analogWrite(LED_PIN, --ledStripBrightness);
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

    // Sensor changes the state from not triggered to triggered - switch on action
    if(!lightOn && digitalRead(PIR_PIN) == HIGH) {
        lightOn = true;
        readRoomBrightness();
        timeOfLastTrigger = millis();
        digitalWrite(LED_BUILTIN, LOW);  
        if (!controlledByAssistant && roomBrightness < roomBrightnessThreshold) {
            currentState = fadingIn;
        }
    }

    // Sensor changes the state from triggered to not triggered - switch off action
    if(lightOn && digitalRead(PIR_PIN) == LOW) {
        timeOfSensorSwitchOff = millis();
        digitalWrite(LED_BUILTIN, HIGH);
        float sensorOnTime = (float)(timeOfSensorSwitchOff - timeOfLastTrigger)/1000;
        boolean isFalseAlarm = sensorOnTime < (float)6.32;
        if(isFalseAlarm) {
            // turn off the light immediately if we know it's a false alarm
            lightOn = false;
            currentState = fadingOut;
            writeToMemory(falseAlertsMemoryAddress, ++falsePositivesCount);
        }
        WebSerial.println(String("[MAIN] Sensor switched off after ") + sensorOnTime + " seconds");
    }

    // Light is on some time even after the sensor switched off, here we turn the light off eventually
    if(lightOn && !controlledByAssistant && millis() - timeOfSensorSwitchOff > lightOnTimeout) {
        lightOn = false;
        currentState = fadingOut;
    }
}
