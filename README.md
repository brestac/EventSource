# EventSource — Arduino library for ESP8266

An [SSE (Server-Sent Events)](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) client for the ESP8266, built on top of [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP).

The public API mirrors the [W3C EventSource interface](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) familiar to JavaScript developers.

---

## Features
- Conforms to the [SSE (Server-Sent Events)](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) specification
- Multiple event-type handlers (`message`, `open`, `close`, `error`, `custom_event`)
- Support for id, retry, comment fields in event stream
## Additional Features
- Custom HTTP headers
- Set the default retry delay
- Force a reconnection in error listener
- Non-blocking — runs entirely on ESPAsyncTCP callbacks

---

## Dependencies

| Library | URL |
|---------|-----|
| ESPAsyncTCP | https://github.com/me-no-dev/ESPAsyncTCP |

---

## Installation

### Manual
Download the ZIP, then in the Arduino IDE: *Sketch → Include Library → Add .ZIP Library…*

---

## Quick start

```cpp
#include <ESP8266WiFi.h>
#include "EventSource.h"

EventSource events("http://192.168.1.2:4001/events", {{"X-Device-Id", ESP.getChipId()}, {"User-Agent":"ESP8266/1.0"}});

void setup() {
    // … connect to Wi-Fi …

    events.addEventListener("open", [](EventSource::Event &event) {
        Serial.println("Connected!");
    });

    events.addEventListener("message", [](EventSource::Event &event) {
        Serial.println(event.data);
    });

    events.addEventListener("error", [](EventSource::Event &event) {
        Serial.printf("Error %d: %s\n", event.code, event.message);
    });
}

void loop() {
  events.update();
  delay(10);
}
```

---

## API reference

### Constructor

```cpp
EventSource(const char *url);
EventSource(const char *url, Options& options);
EventSource(const char * host, const char *path, uint_16_t port, Options& options = Options());
EventSource(IPAdress& ip, const char *path, uint_16_t port, Options& options = Options());
```

### Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `secure` | `bool` | `false` | Connect using SSL (https) |
| `headers` | `HeadersMap` | `{}` | Extra HTTP request headers |

### Methods

| Method | Description | Default | Comment |
|--------|-------------|---------|---------|
| `addEventListener(const char *type, const EventHandler& handler)` | Register a callback for an event type |  | handler type: `typedef std::function<void(Event &)> EventHandler`; |
| `close()` | Close the connection permanently |  |  |
| `readyState()` | Returns `CONNECTING`(0), `OPEN`(1) or `CLOSED`(2) |  |  |
| `addHeader(const char *name, const HeaderValue& value)` | Add a custom HTTP header |  | HeaderValue type can be a const char*, a std::string or any type convertible to int or float |
| `reconnect()` | Reconnect |  | Can be used in an error event listener for forcing a reconnection |
| `setRetryDelay(uint32)` | Change default reconnect delay at runtime. | 3000ms | Unit: milliseconds. May be overriden by stream event retry field |
| `setTimeout(uint32)` | The timeout for the TCP connection. | 20s | Unit: seconds |
| `setURL(const char *)` | Sets the connection URL |  | If the url is invalid, throws an error Event and closes the connection |
| `update()` | Processes event queue and automatic reconnection |  | Required in the loop() function. |

### Event structure

```cpp
struct Event {
    char type[32];         // event type
    char origin[128];      // server hostname
    char target[128];      // server path
    char lastEventId[128]; // id field from the stream (data events only)
    char data[1024];       // event payload (data events only)
    char message[256];     // error message (error events only)
    int  code;             // error code    (error events only, see below)
};
```

### Error codes

| Code | Name | Description |
|--------|-------------|---------|
|-1|ERR_MEM|Out of memory error.|
|-2|ERR_BUF |Buffer error.|
|-6|ERR_VAL |Illegal value.|
|-7|ERR_WOULDBLOCK|Operation would block.|
|-12|ERR_IF|Low-level netif error.|
|-13|ERR_ABRT|Connection aborted.|
|-16|ERR_ARG|Illegal argument.|
|100|ERR_INVALID_URL|Invalid URL.|
|101|ERR_SERVER_TIMEOUT|Server did not respond in time.|
|102|ERR_REDIRECT_LOCATION|Location header is not present.|
|103|ERR_SERVER_INVALID_RESPONSE|Invalid response from server.|
|104|ERR_SERVER_INVALID_CONTENT_TYPE|Content-Type is not text/event-stream.|
|201-599|INVALID_STATUS|Invalid HTTP status code response (not 200 and not 3XX).|

Note: These errors will cause a permanent deconnection (readyState == CLOSED)
You can force a reconnection with the non-normative reconnect() function.


---

## Server example (Node.js)

See [`examples/BasicEventSource/server/`](examples/BasicEventSource/server/) for an Express SSE server used for testing.

Install:

```bash
cd examples/BasicEventSource/server/
npm install
```

Run:

```bash
npm run start
```
---

## License

ISC — see [LICENSE](LICENSE).
