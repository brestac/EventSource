/**
 * BasicEventSource.ino
 *
 * Minimal example: connect to an SSE endpoint and print each message
 * received to the Serial monitor.
 *
 * Required libraries:
 *   - EventSource  (this library)
 *   - ESPAsyncTCP  https://github.com/me-no-dev/ESPAsyncTCP
 */

// Uncomment to enable library debug output
// #define DEBUG_EVENTSOURCE 1

#include <ESP8266WiFi.h>
#include <EventSource.h>

#define WIFI_SSID "YOUR_SSID"
#define WIFI_PASSWORD "YOUR_PASSWORD"

// Change host and port to match your server setup.
EventSource source("http://192.168.1.2:4001/events", {{"X-Device", ESP.getChipId()}, {"User-Agent", "EventSource/1.0"}});

void setup() {

  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\nSETUP\n");

  source.addEventListener("open", [](EventSource::Event& event) {
    Serial.printf("Connected to server\n");
  });

  source.addEventListener("message", [](EventSource::Event& event) {
    Serial.printf("Received message: %s\n", event.data);
  });

  source.addEventListener("myevent", [](EventSource::Event& event) {
    Serial.printf("Received myevent message: %s\n", event.data);
  });

  source.addEventListener("error", [](EventSource::Event& event) {
    Serial.printf("Error %d: %s\n", event.code, event.message);
  });

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  if (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.printf("WiFi Failed!\n");
    return;
  }
  Serial.printf("WiFi Connected! IP:");
  Serial.println(WiFi.localIP());
}

void loop() {
    if (WiFi.status == WL_CONNECTED) {
      source.update();
    }

    delay(100);
}
