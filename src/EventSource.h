// Class EventSource
// A class to handle Server-Sent Events (SSE) from a server
// Using AsyncTCP library for ESP8266
// The API reflects the API of the EventSource class in JavaScript
// https://developer.mozilla.org/en-US/docs/Web/API/EventSource

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
#include <sys/_stdint.h>
#include "user_interface.h"
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

constexpr size_t MAX_EVENT_NAME_SIZE = 64U;
constexpr size_t MAX_EVENT_VALUE_SIZE = 1024U;
constexpr size_t MAX_EVENT_DATA_SIZE    = 1024U;
constexpr size_t MAX_EVENT_ERROR_SIZE   = 256U;
constexpr size_t MAX_EVENT_TYPE_SIZE    = 32U;
constexpr size_t MAX_EVENT_ORIGIN_SIZE  = 128U;
constexpr size_t MAX_EVENT_HANDLER_COUNT = 8U;
constexpr size_t MAX_EVENT_LINES        = 20U;

constexpr size_t   MAX_SSE_REQUEST_SIZE   = 1024U;
constexpr size_t   MAX_SSE_PATH_SIZE      = 128U;
constexpr uint32_t DEFAULT_RETRY_DELAY    = 3000U;
constexpr size_t   EXPONENTIAL_RETRY_LIMIT = 10U;
constexpr uint16_t DEFAULT_PORT           = 80U;
constexpr uint32_t DEFAULT_TIMEOUT        = 20U;
constexpr const char *DEFAULT_HEADERS =
  "Accept: text/event-stream\r\n"
  "Connection: keep-alive\r\n"
  "Cache-Control: no-cache\r\n"
  "Accept-Encoding: identity\r\n";

constexpr size_t MAX_HEADER_COUNT      = 8U;
constexpr size_t MAX_HEADER_KEY_SIZE   = 64U;
constexpr size_t MAX_HEADER_VALUE_SIZE   = 128U;
constexpr size_t MAX_DISPACH_QUEUE_SIZE = 10U;
constexpr uint32_t QUEUE_PROCESSING_INTERVAL = 100U;

// ---------- free-function declarations ----------

inline size_t get_line_size(const char *cstr, size_t max_len);
inline bool isdigits(const char *str);
inline void _isResponseValidEventStream(const char *data, size_t len, bool &contentTypeOk, int &statusCode);
static constexpr bool validate_event_type(const char *str);

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
  using CustomHeaderValue = std::variant<std::string, int, uint32_t, float, double>;
  typedef std::map<std::string, CustomHeaderValue> HeadersMap;

  enum : uint8_t { CONNECTING = 0, OPEN = 1, CLOSED = 2 };

  struct Event {
  public:
    char type[MAX_EVENT_TYPE_SIZE]     = { 0 };
    char origin[MAX_EVENT_ORIGIN_SIZE] = { 0 };

    union {
      struct {
        char data[MAX_EVENT_DATA_SIZE];
        char lastEventId[128];
      } message;

      struct {
        char message[MAX_EVENT_ERROR_SIZE];
        int  code;
      } error;
    };

    void print();

    const char *data()        const { return message.data; }
    const char *lastEventId() const { return message.lastEventId; }
    const char *err()         const { return error.message; }
    int         code()        const { return error.code; }

    Event();
    Event(const Event &)            = default;
    Event &operator=(const Event &) = default;

  private:
    friend class EventSource;
    bool _dispached = false;
    bool _queued    = false;
    bool _hasData   = false;
  };

  struct Options {
    bool       secure;
    HeadersMap headers;
  };

  typedef std::function<void(Event &)> EventHandler;

  EventSource(const char *url, const Options &options);
  EventSource(const char *host, const char *path, uint16_t port, const Options &options);
  EventSource(const IPAddress &host, const char *path, uint16_t port, const Options &options);

  EventSource(const char *url, const HeadersMap &headers = HeadersMap());
  EventSource(const char *host, const char *path, uint16_t port, const HeadersMap &headers = HeadersMap());
  EventSource(const IPAddress &host, const char *path, uint16_t port, const HeadersMap &headers = HeadersMap());
  
  ~EventSource();
  
  void addEventListener(const char *type, const EventHandler &handler);
  void close();
  template<size_t N, size_t M> void addHeader(char (&key)[N], char (&val)[M]);
  void setAutoreconnect(bool autoreconnect);
  void setRetryDelay(uint32_t retryDelay);
  void setTimeout(uint32_t timeout);
  void update();

  const char *host()  const { return _apiHost; }
  const char *path()  const { return _ssePath; }
  uint16_t    port()  const { return _apiPort;  }

  uint8_t     readyState()  { return _readyState; }
  bool        secure() const { return _secure; }
  bool        autoreconnect() const { return _sseAutoreconnect; }
  uint32_t    retryDelay() const { return _retryDelay; }
  uint32_t    timeout() const { return _timeout; }

  const char *url() const {
    static char url[512];
    snprintf(url, sizeof(url), "http://%s:%hu%s", _apiHost, _apiPort, _ssePath);
    return url;
  }

private:

  template<size_t N, typename Value>
  struct KeyValuePair {
    char  key[N];
    Value value;
  };

  using Header = KeyValuePair<MAX_HEADER_KEY_SIZE, char[MAX_HEADER_VALUE_SIZE]>;
  using EventHandlerEntry = KeyValuePair<MAX_EVENT_TYPE_SIZE, EventHandler>;
  
  AsyncClient *_client;

  Header           _customHeaders[MAX_HEADER_COUNT];
  uint8_t          _customHeaderCount;

  EventHandlerEntry _eventHandlers[MAX_EVENT_HANDLER_COUNT];
  uint8_t           _eventHandlerCount = 0;

  Event   _dispachQueue[MAX_DISPACH_QUEUE_SIZE];
  char    _lastEventId[128];
  char    _ssePath[MAX_SSE_PATH_SIZE];
  char    _apiHost[MAX_EVENT_ORIGIN_SIZE];

  uint16_t         _apiPort;
  bool             _secure;
  bool             _sseAutoreconnect;
  uint32_t         _retryDelay;
  uint8_t          _readyState;
  bool             _initial_connection;
  uint64_t         _lastConnectionTime;
  size_t           _dispachQueueSize;
  bool             _lock_queue;
  size_t           _retryCount;
  size_t           _retryDelayMultiplier;
  uint32_t         _timeout;

  // Static callbacks
  static void _onConnectStatic   (void *arg, AsyncClient *client);
  static void _onDisconnectStatic(void *arg, AsyncClient *client);
  static void _onDataStatic      (void *arg, AsyncClient *client, void *data, size_t len);
  static void _onErrorStatic     (void *arg, AsyncClient *client, uint8_t error);
  static void _onTimeoutStatic   (void *arg, AsyncClient *client, uint32_t time);

  // Internal helpers
  template<typename Opts>
  void _init(const char *url, Opts options);
  template<typename Host, typename Opts>
  void _init(Host host, const char *path, uint16_t port, const Opts &options, bool secure = false);

  void _onConnect   (AsyncClient *client);
  void _onDisconnect(AsyncClient *client);
  void _onData      (AsyncClient *client, void *data, size_t len);
  void _onError     (AsyncClient *client, uint8_t error);
  void _onError     (AsyncClient *client, uint8_t code, const char *error);

  void _addHeaders(const HeadersMap& headers);
  void _addHeader(const char *key, size_t key_len, const CustomHeaderValue &value);
  void _sendRequest(AsyncClient *c);
  void _connect();
  void _disconnect();
  void _setLastEventId(const char *lastEventId);
  void _dispachEvent(Event &event);
  void _addToQueue(Event &event);
  void _processQueue();
  void _queueConnectionEvent();
  void _parse_event_stream(const char *cstr, size_t len);
  bool _process_line(const char *cstr, size_t len, Event &event);
  void _process_field(const char *name, const char *value, Event &event);
  Event _newMessageEvent();
  Event _get_current_event(Event &event);

  template<size_t N, typename T>
  bool _contains(const T (&array)[N], const char *key);
};

// ---------- template implementations (must stay in header) ----------

template <size_t N, size_t M>
void EventSource::addHeader(char (&key)[N], char (&val)[M]) {
  _addHeader(key, N - 1, CustomHeaderValue{std::string(val, M - 1)});
}

template<size_t N, typename T>
bool EventSource::_contains(const T (&array)[N], const char *key) {
  for (size_t i = 0; i < N; ++i) {
    if (strcmp(array[i].key, key) == 0)
      return true;
  }
  return false;
}

// ---------- free-function definitions (inline, header-only) ----------

static constexpr bool validate_event_type(const char *str) {
  if (str == nullptr || strlen(str) == 0)
    return false;
  while (*str != '\0') {
    if (*str == '\r' || *str == '\n' || *str == '\0')
      return false;
    str++;
  }
  return true;
}

inline size_t get_line_size(const char *cstr, size_t max_len) {
  size_t pos = 0;
  while (pos < max_len) {
    if (cstr[pos] == '\r' || cstr[pos] == '\n')
      break;
    pos++;
  }
  return pos;
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
  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}
#endif

inline void _isResponseValidEventStream(const char *data, size_t len,
                                        bool &contentTypeOk, int &statusCode) {
  const char *statusCodePtr = strnstr(data, "HTTP/1.1 ", len);
  if (statusCodePtr == nullptr) {
    statusCode = -1;
  } else {
    statusCodePtr += 9;
    statusCode = atoi(statusCodePtr);
    if (statusCode == 0) statusCode = -1;
  }

  const char *contentType    = "Content-Type: text/event-stream\r\n";
  size_t      contentTypeLen = strlen(contentType);

  if (statusCodePtr == nullptr || len - (statusCodePtr - data) < contentTypeLen) {
    contentTypeOk = false;
    return;
  }

  const char *contentTypePtr = strnstr(statusCodePtr, contentType,
                                       len - (statusCodePtr - data));
  contentTypeOk = (contentTypePtr != nullptr);
}
