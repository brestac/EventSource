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
EventSource source("https://192.168.1.2:4001/events", {{"X-Device", ESP.getChipId()}, {"User-Agent", "EventSource/1.0"}});

void setup() {

  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\nSETUP\n");

  source.addEventListener("open", [](EventSource::Event& event) {
    Serial.printf("Connected to server\n");
  });

  source.addEventListener("message", [](EventSource::Event& event) {
    Serial.printf("Received message: %s\n", event.message.data);
  });

  source.addEventListener("myevent", [](EventSource::Event& event) {
    Serial.printf("Received myevent message: %s\n", event.message.data);
  });

  source.addEventListener("close", [](EventSource::Event& event) {
    Serial.printf("Connection closed\n");
  });

  source.addEventListener("error", [](EventSource::Event& event) {
    Serial.printf("Error: %s\n", event.error.message);
  });

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  uint8_t numberOftry = 0;
  while (WiFi.status() != WL_CONNECTED && numberOftry < 10) {
    yield();
    delay(200);
  }

  Serial.printf("WiFi %s connected\n", WiFi.status() == WL_CONNECTED ? "is" : "is not");
}

void loop() {
    source.update();
    delay(100);
}
