# EventSource — Arduino library for ESP8266

An [SSE (Server-Sent Events)](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) client for the ESP8266, built on top of [ESPAsyncTCP](https://github.com/me-no-dev/ESPAsyncTCP).

The public API mirrors the [W3C EventSource interface](https://developer.mozilla.org/en-US/docs/Web/API/EventSource) familiar to JavaScript developers.

---

## Features

- Auto-reconnect with configurable delay
- Custom HTTP headers
- `Last-Event-ID` resumption
- Multiple event-type handlers (`message`, `open`, `close`, `error`, …)
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

EventSource events("http://192.168.1.2:4001/events", {{"X-Device", "esp8266"}});

void setup() {
    // … connect to Wi-Fi …

    events.addEventListener("open", [](EventSource::Event &event) {
        Serial.println("Connected!");
    });

    events.addEventListener("message", [](EventSource::Event &event) {
        Serial.println(event.message.data);
    });

    events.addEventListener("error", [](EventSource::Event &event) {
        Serial.printf("Error %d: %s\n", event.error.code, event.error.message);
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
EventSource(const char *url, Options options = Options());
EventSource(const char *url, HeadersMap headers);
```

### Options

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `autoReconnect` | `bool` | `true` | Reconnect automatically after disconnection |
| `retryDelay` | `uint32_t` | `3000` | Delay before reconnect (ms) |
| `timeout` | `uint32_t` | `5000` | RX / ACK timeout (ms) |
| `headers` | `HeadersMap` | `{}` | Extra HTTP request headers |

### Methods

| Method | Description |
|--------|-------------|
| `addEventListener(type, handler)` | Register a callback for an event type |
| `addHeader(name, value)` | Add a custom HTTP header |
| `setAutoreconnect(bool)` | Enable / disable auto-reconnect |
| `setRetryDelay(ms)` | Change reconnect delay at runtime |
| `close()` | Close the connection (disables auto-reconnect) |
| `readyState()` | Returns `CONNECTING`, `OPEN` or `CLOSED` |

### Event structure

```cpp
struct Event {
    char type[32];         // event type
    char origin[128];      // server hostname
    char target[128];      // SSE path
    char lastEventId[128]; // id field from the stream
    char data[1024];       // event payload
    char message[256];     // error message (error events only)
    int  code;             // error code   (error events only)
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
