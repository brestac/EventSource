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
#include "credentials.h"
#include "EventSource.h"

EventSource source("https://192.168.1.2:4001/events", {{"X-Device", "1234567890"}, {"User-Agent", "EventSource/1.0"}});

void setup() {

  Serial.begin(115200);
  delay(500);
  Serial.printf("\n\nSETUP\n");

  connectWifi();

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
}

void loop() {
    source.update();
    ESP.wdtFeed();  // nourrit explicitement le HW WDT
    yield();
    delay(100);
}

void connectWifi() {
  DEBUG_PRINTLN("Connecting as wifi client...");

  // WIFI
  uint8_t mac[6] = {170, 0, 0, 0, 0, 11};
  WiFi.mode(WIFI_STA);
  wifi_set_macaddr(STATION_IF, mac);

  WiFi.onStationModeConnected([](const WiFiEventStationModeConnected &event) {
    DEBUG_PRINTLN("Station connected");
  });

  WiFi.onStationModeDisconnected([](const WiFiEventStationModeDisconnected &event) {
    DEBUG_PRINTLN("Station disconnected");
    connectWifi();
  });

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  /*
  IPAddress ip(192, 168, 1, 10);
  IPAddress gateway(192, 168, 1, 1); // set gateway to match your network
  IPAddress subnet(255, 255, 255, 0); // set subnet mask to match your network
  WiFi.config(ip, ip, gateway, subnet);
*/
  uint8_t numberOftry = 0;
  while (WiFi.status() != WL_CONNECTED && numberOftry < 10) {
    yield();
    DEBUG_PRINTF("... WiFi connecting status:%d\n", WiFi.status());
    delay(500);
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  if (++numberOftry >= 10) {
    DEBUG_PRINTF("... WiFi timeout status:%d\n", WiFi.status());
  } else {
    DEBUG_PRINTF("WiFi connected status:%d\n", WiFi.status());
    WiFi.setAutoReconnect(true);
    WiFi.persistent(true);
  }
}
