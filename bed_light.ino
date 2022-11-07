#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>

#include <credentials.h>

#define LED_PIN  D6
#define PIR_PIN D7

const char* ssid = STASSID;
const char* password = STAPSK;

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

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
   ArduinoOTA.setHostname("Jacob's bed light");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  pinMode(LED_PIN, OUTPUT);
  pinMode(PIR_PIN, INPUT);
}

void fadeIn() {
  while (currentBrightness < maximumBrightness) {
    analogWrite(LED_PIN, ++currentBrightness);
    delay(1);
  };
}

void fadeOut() {
  while (currentBrightness > 0) {
    analogWrite(LED_PIN, --currentBrightness);
    delay(1);
  };
}

void loop() {
  ArduinoOTA.handle();
  if (digitalRead(PIR_PIN) == HIGH) {
    fadeIn();
  } else {
    fadeOut();
  }
    
  // int interval = 2048;
  // int intervalProgress = millis() % interval;
  // int modificationCoefficient = interval / 2 / 255;
  // analogWrite(
  //   LED_PIN,
  //   intervalProgress > interval/2 
  //     ? (interval - intervalProgress) / modificationCoefficient
  //     : intervalProgress / modificationCoefficient
  // );
}
