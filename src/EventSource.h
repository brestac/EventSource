/*
  Class EventSource
  A class to handle Server-Sent Events (SSE) from a server
  Using AsyncTCP library for ESP8266
  The API reflects the API of the EventSource class in JavaScript
  https://developer.mozilla.org/en-US/docs/Web/API/EventSource

  Copyright (c) 2026 Romain Brestac. All rights reserved.

  This library is free software; you can redistribute it and/or
  modify it under the terms of the GNU Lesser General Public
  License as published by the Free Software Foundation; either
  version 2.1 of the License, or (at your option) any later version.

  This library is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this library; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/
#pragma once

#if DEBUG_EVENTSOURCE == 1
#ifdef ARDUINO
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_PRINTF(x...) Serial.printf(x)
#define DEBUG_PRINT(x) Serial.print(x)
#elif defined(__linux__) || defined(__APPLE__)
#define DEBUG_PRINTLN(x) std::printf("%s\n", x)
#define DEBUG_PRINTF(x...) std::printf(x)
#define DEBUG_PRINT(x) std::printf(x)
#endif
#else
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(x...)
#define DEBUG_PRINT(x)
#endif

#ifdef ARDUINO
#include "user_interface.h"
#include <sys/_stdint.h>
#else
#include <chrono>
#endif

#ifdef __EXCEPTIONS
#include <stdexcept>
#endif

#include <cstring>
#include <functional>
#include <map>
#include <stdint.h>
#include <string>
#include <variant>

#include "ESPAsyncTCP.h"
#include "IPAddress.h"

using namespace std;

constexpr size_t MAX_SSE_REQUEST_SIZE = 1024U;
constexpr size_t MAX_SSE_PATH_SIZE = 128U;
constexpr uint32_t DEFAULT_RETRY_DELAY = 3000U;
constexpr size_t EXPONENTIAL_RETRY_LIMIT = 10U;
constexpr uint16_t DEFAULT_PORT = 80U;
constexpr uint32_t DEFAULT_TIMEOUT = 20U;
constexpr size_t MAX_EVENT_NAME_SIZE = 32U;

constexpr size_t MAX_EVENT_VALUE_SIZE = 1024U;
constexpr size_t MAX_EVENT_DATA_SIZE = 1024U;
constexpr size_t MAX_EVENT_ERROR_SIZE = 256U;
constexpr size_t MAX_EVENT_TYPE_SIZE = 32U;
constexpr size_t MAX_EVENT_ORIGIN_SIZE = 128U;
constexpr uint8_t MAX_EVENT_HANDLER_COUNT = 8U;
constexpr size_t MAX_EVENT_LINES = 20U;

constexpr size_t MAX_RESPONSE_LINES = 20U;
constexpr uint8_t MAX_HEADER_COUNT = 8U;
constexpr size_t MAX_HEADER_KEY_SIZE = 64U;
constexpr size_t MAX_HEADER_VALUE_SIZE = 128U;

constexpr size_t MAX_DISPACH_QUEUE_SIZE = 10U;
constexpr uint32_t QUEUE_PROCESSING_INTERVAL = 100U;
constexpr const char *DEFAULT_HEADERS = "Accept: text/event-stream\r\n"
                                        "Connection: keep-alive\r\n"
                                        "Cache-Control: no-cache\r\n"
                                        "Accept-Encoding: identity\r\n";

// ---------- free-function declarations ----------

#ifndef ARDUINO
inline uint64_t millis();
inline void yield() {}
#endif

#ifndef HAVE_STRNCHR
inline const char *strnchr(const char *str, char c, size_t max_len);
#endif

#ifndef ARDUINO
inline char *strnstr(const char *haystack, const char *needle, size_t len);
#endif

// ---------- EventSource class ----------

class EventSource {

public:
  using CustomHeaderValue =
      std::variant<std::string, int, uint32_t, float, double>;
  typedef std::map<std::string, CustomHeaderValue> HeadersMap;

  enum : uint8_t { CONNECTING = 0, OPEN = 1, CLOSED = 2 };

  enum EventError : int {
    ERR_INVALID_URL = 100, // Invalid URL
    ERR_SERVER_TIMEOUT = 101, // Server did not respond in time
    ERR_REDIRECT_LOCATION = 102, // Location header is not present
    ERR_SERVER_INVALID_RESPONSE = 103,// Invalid response from server
    ERR_SERVER_INVALID_CONTENT_TYPE = 104 // Content-Type is not text/event-stream
  };

  struct Event {
  public:
    char type[MAX_EVENT_TYPE_SIZE] = {0};
    char origin[MAX_EVENT_ORIGIN_SIZE] = {0};

    char data[MAX_EVENT_DATA_SIZE];
    char lastEventId[128];

    char message[MAX_EVENT_ERROR_SIZE];
    int code;

    void print();

    Event();
    Event(const Event &) = default;
    Event &operator=(const Event &) = default;

  private:
    friend class EventSource;
    bool _dispached = false;
    bool _queued = false;
    bool _hasData = false;
  };

  struct Options {
    bool secure;
    HeadersMap headers;
  };

  typedef std::function<void(Event &)> EventHandler;

  EventSource(const char *url, const Options &options);
  EventSource(const char *host, const char *path, uint16_t port,
              const Options &options);
  EventSource(const IPAddress &host, const char *path, uint16_t port,
              const Options &options);

  EventSource(const char *url, const HeadersMap &headers = HeadersMap());
  EventSource(const char *host, const char *path, uint16_t port,
              const HeadersMap &headers = HeadersMap());
  EventSource(const IPAddress &host, const char *path, uint16_t port,
              const HeadersMap &headers = HeadersMap());

  ~EventSource();

  template <size_t N> void addEventListener(char const (&type)[N], const EventHandler &handler);
  template <size_t N, size_t M> void addHeader(char (&key)[N], char (&val)[M]);
  void close();
  void reconnect();
  void setRetryDelay(uint32_t retryDelay);
  void setTimeout(uint32_t timeout);
  // #ifndef ESP32
  void update();
  // #endif
  const char *host() const { return _apiHost; }
  const char *path() const { return _ssePath; }
  uint16_t port() const { return _apiPort; }

  uint8_t readyState() { return _readyState; }
  bool secure() const { return _secure; }
  uint32_t retryDelay() const { return _retryDelay; }
  uint32_t timeout() const { return _timeout; }

  const char *url() const {
    static char url[512];
    snprintf(url, sizeof(url), "http://%s:%hu%s", _apiHost, _apiPort, _ssePath);
    return url;
  }

  AsyncClient *client() const { return _client; }

private:
  template <size_t N, typename Value> struct KeyValuePair {
    char key[N];
    Value value;
  };

  using Header = KeyValuePair<MAX_HEADER_KEY_SIZE, char[MAX_HEADER_VALUE_SIZE]>;
  using EventHandlerEntry = KeyValuePair<MAX_EVENT_TYPE_SIZE, EventHandler>;

  AsyncClient *_client;

  Header _customHeaders[MAX_HEADER_COUNT];
  uint8_t _customHeaderCount;

  EventHandlerEntry _eventHandlers[MAX_EVENT_HANDLER_COUNT];
  uint8_t _eventHandlerCount = 0;

  Event _dispachQueue[MAX_DISPACH_QUEUE_SIZE];
  char _lastEventId[128];
  char _ssePath[MAX_SSE_PATH_SIZE];
  char _apiHost[MAX_EVENT_ORIGIN_SIZE];

  uint16_t _apiPort;
  bool _secure;
  uint32_t _retryDelay;
  uint8_t _readyState;
  uint64_t _connectionTimer;
  size_t _dispachQueueSize;
  bool _lock_queue;
  size_t _retryCount;
  size_t _retryDelayMultiplier;
  uint32_t _timeout;
  bool _force_connect;
  bool _force_disconnect;
  bool _headers_received;

  // Static callbacks
  static void _onConnectStatic(void *arg, AsyncClient *client);
  static void _onDisconnectStatic(void *arg, AsyncClient *client);
  static void _onDataStatic(void *arg, AsyncClient *client, void *data,
                            size_t len);
  static void _onErrorStatic(void *arg, AsyncClient *client, int error);
  static void _onTimeoutStatic(void *arg, AsyncClient *client, uint32_t time);

  // Internal helpers
  template <typename Opts> void _init(const char *url, Opts options);
  template <typename Host, typename Opts>
  void _init(Host host, const char *path, uint16_t port, const Opts &options,
             bool secure = false);
  bool _setURL(const char *url);
  bool _parseURL(const char *url, char *host, char *path, uint16_t& port, bool& secure);

  void _onConnect(AsyncClient *client);
  void _onDisconnect(AsyncClient *client);
  void _onData(AsyncClient *client, void *data, size_t len);
  void _onError(AsyncClient *client, int error);
  void _onError(AsyncClient *client, int code, const char *error);

  void _addHeaders(const HeadersMap &headers);
  void _addHeader(const char *key, size_t key_len,
                  const CustomHeaderValue &value);
  void _sendRequest(AsyncClient *c);
  void _connect();
  void _connect(const char *host, const char *path, uint16_t port, bool secure);
  void _disconnect();
  bool _is_abort_error(int code);
  bool _handleRedirection(char *data, size_t len, int statusCode);
  bool _is_redirection(int statusCode);
  bool _hasHeader(const char *data, size_t len, const char *header_name, const char *header_value);
  bool _getStatusCode(const char *data, size_t len, int &statusCode);
  void _setLastEventId(const char *lastEventId);
  void _dispachEvent(Event &event);
  void _update();
  void _addToQueue(Event &event);
  void _processQueue();
  void _queueConnectionEvent();
  void _queueErrorEvent(int code, const char *error);
  void _parse_event_stream(const char *cstr, size_t len);
  bool _process_line(const char *cstr, size_t len, Event &event);
  void _process_field(const char *name, const char *value, Event &event);
  Event _newMessageEvent();

  template <size_t N, typename T>
  bool _contains(const T (&array)[N], const char *key);
};

// ---------- template implementations (must stay in header) ----------

template <size_t N, size_t M>
void EventSource::addHeader(char (&key)[N], char (&val)[M]) {
  _addHeader(key, N - 1, CustomHeaderValue{std::string(val, M - 1)});
}

template <size_t N, typename T>
bool EventSource::_contains(const T (&array)[N], const char *key) {
  for (size_t i = 0; i < N; ++i) {
    if (strcmp(array[i].key, key) == 0)
      return true;
  }
  return false;
}

template <size_t N>
static bool validate_event_type(const char (&str)[N]) {
  if (N == 0 || (str)[0] == '\0')
    return false;

  size_t pos = 0;

  while (pos < N) {
    if (str[pos] == '\r' || str[pos] == '\n') {
      return false;
    }

    pos++;
  }

  return true;
}

template <size_t N>
void EventSource::addEventListener(char const (&type)[N], const EventHandler &handler) {

  if (!validate_event_type(type)) {
    DEBUG_PRINTF("[SSE] Event listener type invalide: '%.*s'\n", (int)N, type);
    return;
  }

  if (_eventHandlerCount >= MAX_EVENT_HANDLER_COUNT) {
    DEBUG_PRINTLN("[SSE] Max event handler count reached");
    return;
  }

  if (!_contains(_eventHandlers, type)) {
    strncpy(_eventHandlers[_eventHandlerCount].key, type, MAX_EVENT_TYPE_SIZE);
    _eventHandlers[_eventHandlerCount].key[MAX_EVENT_TYPE_SIZE - 1] = '\0';
    _eventHandlers[_eventHandlerCount].value = handler;
    _eventHandlerCount++;
    DEBUG_PRINTF("[SSE] Added handler for '%s', count: %d\n", type, _eventHandlerCount);
  } else {
    DEBUG_PRINTF("[SSE] Handler for '%s' already exists\n", type);
  }
}

inline bool isdigits(const char *str) {
  while (*str) {
    if (!isdigit(*str))
      return false;
    str++;
  }
  return true;
}

#ifndef HAVE_STRNCHR
inline const char *strnchr(const char *s, char c, size_t n) {
  for (size_t i = 0; i < n && s[i] != '\0'; ++i) {
    if (static_cast<unsigned char>(s[i]) == static_cast<unsigned char>(c))
      return s + i;
  }
  return nullptr;
}
#endif

#ifndef ARDUINO
inline char *strnstr(const char *haystack, const char *needle, size_t len) {
  size_t needle_len = strlen(needle);
  if (needle_len == 0)
    return (char *)haystack;
  for (size_t i = 0; i <= len - needle_len; ++i) {
    if (strncmp(haystack + i, needle, needle_len) == 0)
      return (char *)(haystack + i);
    if (haystack[i] == '\0')
      break;
  }
  return nullptr;
}

inline uint64_t millis() {
  static auto start = std::chrono::high_resolution_clock::now();
  auto now = std::chrono::high_resolution_clock::now();
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start)
      .count();
}
#endif

// ---------- Parsing ----------

inline size_t linelen(const char *cstr, const char *end) {
  size_t pos = 0;
  size_t max_len = end - cstr;

  while (pos < max_len) {
    if (cstr[pos] == '\r' || cstr[pos] == '\n')
      break;
      pos++;
  }
  return pos;
}

inline bool scan_char(const char *pos, char chr, const char *end) {
  return (pos != nullptr && pos < end && *pos == chr);
}

inline void skip_eol(const char *&pos, const char *end) {
  if (scan_char(pos, '\r', end)) {
    pos++;
    if (scan_char(pos, '\n', end)) {
        pos++;
    }
  } else if (scan_char(pos, '\n', end)) {
    pos++;
  }
}

template <size_t N>
inline bool _extractHeaderValue(const char *line_start, size_t line_len,
                                const char *header_name,
                                char (&header_value)[N]) {
  const char *pos = line_start;
  size_t header_len = strlen(header_name);

  if (pos == nullptr || header_len == 0 || header_len >= line_len)
    return false;

  if (strncmp(pos, header_name, header_len) == 0) {
    pos += header_len;

    // Skip the colon
    if (*pos != ':')
      return false;
    pos++;
    // Skip any whitespace after the colon
    while (pos < line_start + line_len) {
      if (*pos == ' ')
        pos++;
      else
        break;
    }

    size_t remaining = line_len - (pos - line_start);
    strncpy(header_value, pos, remaining);
    header_value[sizeof(header_value) - 1] = '\0';

    return true;
  }

  return false;
}

template <size_t N>
inline bool _getHeaderValue(const char *data, size_t data_len,
                            const char *header_name, char (&header_value)[N]) {
  // Find the start of the headers
  const char *headers_start = strstr(data, "\r\n");
  if (headers_start == nullptr) {
    DEBUG_PRINTLN("[SSE] Headers not found");
    return false;
  }
  headers_start += 2;

  // Find the end of the headers
  const char *headers_end = strstr(headers_start, "\r\n\r\n");
  if (headers_end == nullptr) {
    DEBUG_PRINTLN("[SSE] Headers end not found");
    return false;
  }

  const char *pos = data;

  size_t it = 0;
  while (pos < headers_end && it < MAX_RESPONSE_LINES) {
    size_t line_len = linelen(pos, headers_end);
    
    DEBUG_PRINTF("[EventSource] Line %zu: '%.*s'\n", it, (int)line_len, pos);
    if (_extractHeaderValue(pos, line_len, header_name, header_value)) {
      DEBUG_PRINTF("[EventSource] Found header '%s' with value '%s'\n",
                   header_name, header_value);
      return true;
    }
    
    pos += line_len;
    skip_eol(pos, headers_end);
    it++;
  }

  return false;
}

// template<size_t N>
// inline void _strncpy(char (&dest)[N], const char *src, size_t dest_size) {
//   if (dest_size == 0) {
//     return;
//   }

//   strncpy(dest, src, dest_size);
//   dest[N - 1] = '\0';
// }
