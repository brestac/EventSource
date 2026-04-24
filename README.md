# EventSource — Arduino library for ESP8266

An [SSE (Server-Sent Events)](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) client for the ESP8266, built on top of [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP).

The public API mirrors the [W3C EventSource interface](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) familiar to JavaScript developers.

---

## Features
- Conforms to the [SSE (Server-Sent Events)](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) specification
- Auto-reconnect with configurable delay
- Custom HTTP headers
- `Last-Event-ID` resumption
- Multiple event-type handlers (`message`, `open`, `close`, `error`, `custom_event`)
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
EventSource(const char *url, Options options);
EventSource(const char *url, HeadersMap headers);
EventSource(const char * host, const char *path, uint_16_t port, Options options = Options());
EventSource(IPAdress& ip, const char *path, uint_16_t port, Options options = Options());
```

### Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `secure` | `bool` | `false` | Connect using SSL (https) |
| `headers` | `HeadersMap` | `{}` | Extra HTTP request headers |

### Methods

| Method | Description | Default |
|--------|-------------|---------|
| `addEventListener(type, handler)` | Register a callback for an event type | - |
| `addHeader(name, value)` | Add a custom HTTP header | - |
| `reconnect()` | Reconnect when the connection is closed, for example after a server error response | - |
| `setRetryDelay(ms)` | Change default reconnect delay in ms at runtime. May be overriden by sse retry field | 3000ms |
| `setTimeout(s)` | The timeout for the connection in seconds | 20s |
| `close()` | Close the connection permanently | - |
| `readyState()` | Returns `CONNECTING`(0), `OPEN`(1) or `CLOSED`(2) | - |

### Event structure

```cpp
struct Event {
    char type[32];         // event type
    char origin[128];      // server hostname
    char target[128];      // server path
    char lastEventId[128]; // id field from the stream (data events only)
    char data[1024];       // event payload (data events only)
    char message[256];     // error message (error events only)
    int  code;             // error code    (error events only)
};
```

---

## Debug output

Define `DEBUG_EVENTSOURCE 1` **before** including the header to enable verbose logging to `Serial`:

```cpp
#define DEBUG_EVENTSOURCE 1
#include "EventSource.h"
```

---

## Server example (Node.js)

See [`examples/BasicEventSource/server/`](examples/BasicEventSource/server/) for a minimal Express SSE server used for testing.

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
