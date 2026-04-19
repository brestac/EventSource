
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
#include <string_view>

/*
  In Arduino ESP8266, uses ESPAsyncTCP library: https://github.com/ESP32Async/AsyncTCP/blob/main/src/AsyncTCP.h
*/
#include "ESPAsyncTCP.h"
#include "Ticker.h"

using namespace std;

typedef std::function<void(std::string_view, std::string_view)>
  ResponseCallback;
typedef std::map<std::string, std::string> HeadersMap;

#define MAX_SSE_KEY_SIZE 64
#define MAX_SSE_VALUE_SIZE 1024
#define TO_STRING(x) #x

constexpr size_t MAX_EVENT_DATA_SIZE = 1024;
constexpr size_t MAX_EVENT_ERROR_SIZE = 256;
constexpr size_t MAX_EVENT_TYPE_SIZE = 32;
constexpr size_t MAX_EVENT_ORIGIN_SIZE = 128;
constexpr size_t MAX_EVENT_HANDLER_COUNT = 8;
constexpr size_t MAX_EVENT_LINES = 20;

constexpr size_t MAX_SSE_REQUEST_SIZE = 1024;
constexpr size_t MAX_SSE_PATH_SIZE = 128;
constexpr uint32_t DEFAULT_RETRY_DELAY = 3000U;
constexpr size_t EXPONENTIAL_RETRY_LIMIT = 10;
constexpr uint16_t DEFAULT_PORT = 80;
constexpr uint32_t DEFAULT_TIMEOUT = 5000U;
constexpr const char *DEFAULT_HEADERS =
  "Accept: text/event-stream\r\n"
  "Connection: keep-alive\r\n"
  "Cache-Control: no-cache\r\n"
  "Accept-Encoding: identity\r\n";

constexpr size_t MAX_HEADER_COUNT = 8;
constexpr size_t MAX_HEADER_KEY_SIZE = 64;
constexpr size_t MAX_HEADER_VAL_SIZE = 128;
constexpr size_t MAX_DISPACH_QUEUE_SIZE = 10;

static size_t get_line_size(const char *cstr, size_t max_len);
static bool isdigits(const char *str);
static void _isResponseValidEventStream(const char *data, size_t len, bool &contentTypeOk, int &statusCode);
static constexpr bool validate_event_type(const char *str);
#ifndef ARDUINO
static uint64_t millis();
static void yield(){};
#endif

#ifndef HAVE_STRNCHR
static const char *strnchr(const char *str, char c, size_t max_len);
#endif
#ifndef ARDUINO
static char *strnstr(const char *haystack, const char *needle, size_t len);
#endif

class EventSource {

public:
  enum : uint8_t { CONNECTING = 0,
                   OPEN = 1,
                   CLOSED = 2 };

struct Event {
public:
    char type[MAX_EVENT_TYPE_SIZE] = { 0 };
    char origin[MAX_EVENT_ORIGIN_SIZE] = { 0 };

    union {
        // type == "message"
        struct {
            char data[MAX_EVENT_DATA_SIZE];
            char lastEventId[128];
        } message;

        // type == "error"
        struct {
            char message[MAX_EVENT_ERROR_SIZE];
            int code;
        } error;

        // type == "open" / "close" : rien de plus
    };

    void print() {
      DEBUG_PRINTF("[Event] data: '%s', origin: '%s', lastEventId: '%s', "
                   "message: '%s', code: %d, type: '%s'", data, origin, lastEventId, message, code, type);
    }

    const char* data() const { return message.data; }
    const char* lastEventId() const { return message.lastEventId; }
    const char* err() const { return error.message; }
    int code() { return error.code; }

    Event() {
      memset(this, 0, sizeof(Event));
      strncpy(type, "message", sizeof(type));      
    }

    Event(const Event &) = default;
    Event &operator=(const Event &) = default;

  private:
    friend class EventSource;
    bool _dispached = false;
    bool _queued = false;
    bool _hasData = false;
    // ~Event() {
    //   DEBUG_PRINTLN("[Event] Event destroyed");
    // }
  };

  struct Options {
    bool autoReconnect;
    uint32_t retryDelay;
    uint32_t timeout;
    // uint32_t keepAliveTimeout;
    // uint8_t keepAliveCount;
    HeadersMap headers;

    // ~Options() {
    //   DEBUG_PRINTLN("[Options] Options destroyed");
    // }
  };

  typedef std::function<void(Event &)> EventHandler;

  Options defaultOptions() {
    Options options;
    options.autoReconnect = true;
    options.retryDelay = DEFAULT_RETRY_DELAY;
    options.timeout = DEFAULT_TIMEOUT;
    // options.keepAliveTimeout = 0;
    // options.keepAliveCount = 0;

    return options;
  }

  EventSource(const char *url, HeadersMap headers = {}) {
    Options options = defaultOptions();
    options.headers = std::move(headers);
    _init(url, options);
  }

  EventSource(const char *url, Options options = Options()) {
    _init(url, options);
  }

  EventSource(const char *host, const char *path, uint16_t port, HeadersMap headers = {}) {
    Options options = defaultOptions();
    options.headers = std::move(headers);
    _init(host, path, port, options);
  }

  EventSource(const char *host, const char *path, uint16_t port, Options options = Options()) {
    _init(host, path, port, options);
  }

  // ~EventSource() {
  //   delete _client;
  //   delete _sseReconnectTimer;
  //   DEBUG_PRINTLN("[SSE] EventSource destroyed");
  // }

  void addEventListener(const char *type, const EventHandler &handler);
  void close();
  template<size_t N, size_t M> void addHeader(char (&key)[N], char (&val)[M]);
  void setAutoreconnect(bool autoreconnect);
  void update();

  const char *host() const {
    return _apiHost;
  }
  const char *path() const {
    return _ssePath;
  }
  uint16_t port() const {
    return _apiPort;
  }
  const char *url() const {
    static char url[512];
    snprintf(url, sizeof(url), "http://%s:%hu%s", _apiHost, _apiPort, _ssePath);
    return url;
  }
  uint8_t readyState() {
    return _readyState;
  }

private:

  template<size_t N, typename Value>
  struct KeyValuePair {
    char key[N];
    Value value;

    // ~KeyValuePair() {
    //   DEBUG_PRINTF("[KeyValuePair] KeyValuePair destroyed: %s\n", key);
    // }
  };

  using Header = KeyValuePair<MAX_HEADER_KEY_SIZE, char[MAX_HEADER_VAL_SIZE]>;
  using EventHandlerEntry = KeyValuePair<MAX_EVENT_TYPE_SIZE, EventHandler>;

  AsyncClient *_client;

  Header _customHeaders[MAX_HEADER_COUNT];
  uint8_t _customHeaderCount;

  EventHandlerEntry _eventHandlers[MAX_EVENT_HANDLER_COUNT];
  uint8_t _eventHandlerCount = 0;

  Event _dispachQueue[MAX_DISPACH_QUEUE_SIZE] = {};
  char _lastEventId[128];
  char _ssePath[MAX_SSE_PATH_SIZE];
  char _apiHost[MAX_EVENT_ORIGIN_SIZE];

  uint16_t _apiPort;
  bool _sseAutoreconnect;
  volatile uint32_t _retryDelay;
  uint8_t _readyState;
  bool _initial_connection;
  uint64_t _lastConnectionTime;
  size_t _dispachQueueSize;
  bool _lock_queue;
  size_t _retryCount;
  size_t _retryDelayMultiplier;

  static void _onConnectStatic(void *arg, AsyncClient *client);
  static void _onDisconnectStatic(void *arg, AsyncClient *client);
  static void _onDataStatic(void *arg, AsyncClient *client, void *data, size_t len);
  static void _onErrorStatic(void *arg, AsyncClient *client, uint8_t error);
  static void _onTimeoutStatic(void *arg, AsyncClient *client, uint32_t time);

  void _init(const char *url, Options &options);
  void _init(const char *host, const char *path, uint16_t port, Options &options);
  void _onConnect(AsyncClient *client);
  void _onDisconnect(AsyncClient *client);
  void _onData(AsyncClient *client, void *data, size_t len);
  void _onError(AsyncClient *client, uint8_t error);
  void _onError(AsyncClient *client, uint8_t code, const char *error);

  void _addHeader(const char *key, const char *val, size_t ley_len, size_t value_len);
  void _sendRequest(AsyncClient *c);
  void _connect();
  void _disconnect();
  void _connect_client();
  bool _connected_client();
  void _setLastEventId(const char *lastEventId);
  void _setRetryDelay(uint32_t retryDelay);
  void _dispachEvent(Event &event);
  void _addToQueue(Event &event);
  void _processQueue();
  void _queueConnectionEvent(const char *type);
  void _parse_event_stream(const char *cstr, size_t len);
  bool _process_line(const char *cstr, size_t len, Event &event);
  void _process_field(const char *name, const char *value, Event &event);
  Event _newMessageEvent();
  Event _get_current_event(Event &event);

  template<size_t N, typename T>
  bool _contains(const T (&array)[N], const char *key);
};

void EventSource::_init(const char *url, Options &options) {
  char host[MAX_EVENT_ORIGIN_SIZE] = { 0 };
  char path[MAX_SSE_PATH_SIZE] = { 0 };
  uint16_t port = DEFAULT_PORT;

  // TODO: handle case where path is a/b/c
  // TODO: handle case where path is empty
  // Try parsing with port first, then without
  int n = sscanf(url, "%*[^:]://%127[^:/]:%hu/%127s", host, &port, path);

  if (n <= 2) {
    n = sscanf(url, "%*[^:]://%127[^:/]/%127s", host, path);
    if (n < 1) {
      DEBUG_PRINTF("Failed to parse URL: %s\n", url);
      _onError(nullptr, 0, "Failed to parse URL");
#ifdef __EXCEPTIONS
      throw std::runtime_error("[SyntaxError] Failed to parse URL");
#endif
      _sseAutoreconnect = false;
      return;
    }
  }

  _init(host, path, port, options);
}

void EventSource::_init(const char *host, const char *path, uint16_t port, Options &options) {

  strncpy(_apiHost, host, sizeof(_apiHost));
  _apiHost[sizeof(_apiHost) - 1] = '\0';

  snprintf(_ssePath, sizeof(_ssePath), "/%s", path);
  _ssePath[sizeof(_ssePath) - 1] = '\0';

  _client = new AsyncClient;
  _apiPort = port;
  _readyState = CLOSED;
  _sseAutoreconnect = options.autoReconnect;
  _retryDelay = options.retryDelay;
  _retryDelayMultiplier = 1;
  _retryCount = 0;
  _lastEventId[0] = '\0';
  _initial_connection = true;
  _lastConnectionTime = 0;
  _dispachQueueSize = 0;
  _lock_queue = false;

  _client->setRxTimeout(options.retryDelay - 100);
  _client->setAckTimeout(options.retryDelay - 100);
  // _client->setKeepAlive(options.keepAliveTimeout, options.keepAliveCount);
  DEBUG_PRINTF(
    "[SSE] EventSource url parsed %s:%hu%s retry:%u autoreconnect: %d\n",
    _apiHost, _apiPort, _ssePath, _retryDelay, _sseAutoreconnect);

  _customHeaderCount = 0;
  for (const auto &header : options.headers) {
    _addHeader(header.first.c_str(), header.second.c_str(), header.first.length(), header.second.length());
  }
}

bool EventSource::_connected_client() {
  return _client->connected();
}

void EventSource::update() {

  static uint64_t lastQueueUpdate = 0;

  if (millis() - lastQueueUpdate > 100) {
    lastQueueUpdate = millis();
    _processQueue();
    yield();
  }

  if (_initial_connection) {
    DEBUG_PRINTLN("[SSE] Initial connection");
    _initial_connection = false;
    _connect();
    yield();
  } else if (_readyState == CLOSED) {
    if (_client->connected()) {
      if ((millis() - _lastConnectionTime) > DEFAULT_TIMEOUT) {
        DEBUG_PRINTLN("[SSE] Timeout");
        _onError(_client, 0, "Timeout");
      }
    } else if (_sseAutoreconnect && (millis() - _lastConnectionTime) > _retryDelay * _retryDelayMultiplier) {
      DEBUG_PRINTF("[SSE] Reconnecting after %zu ms", millis() - _lastConnectionTime);
      _lastConnectionTime = millis();
      _retryCount++;
      _connect_client();

      // Optionally, wait some more. In particular, if the previous attempt failed, then user agents might introduce an exponential backoff delay to avoid overloading a potentially already overloaded server.
      if (_retryCount > EXPONENTIAL_RETRY_LIMIT) {
        _retryCount = 0;
        _retryDelayMultiplier++;
        DEBUG_PRINTF("[SSE] Too many reconnection attempts, increasing retry delay to %zu seconds\n", _retryDelay * _retryDelayMultiplier);
      }
    }
  }
}

template<size_t N, typename T>
bool EventSource::_contains(const T (&array)[N], const char *key) {
  for (size_t i = 0; i < N; ++i) {
    if (strcmp(array[i].key, key) == 0) {
      return true;
    }
  }

  return false;
}

template<size_t N, size_t M>
void EventSource::addHeader(char (&key)[N], char (&val)[M]) {
  _addHeader(key, val, N, M);
}

void EventSource::_addHeader(const char *key, const char *val, size_t ley_len, size_t value_len) {
  if (_customHeaderCount >= MAX_HEADER_COUNT) return;
  if (key == nullptr || val == nullptr || ley_len == 0 || value_len == 0 || ley_len > MAX_HEADER_KEY_SIZE || value_len > MAX_HEADER_VAL_SIZE) return;

  if (strcmp(key, "Last-Event-ID") == 0 || strcmp(key, "Content-Type") == 0 || strcmp(key, "Content-Length") == 0 || strcmp(key, "Transfer-Encoding") == 0 || strcmp(key, "Connection") == 0 || strcmp(key, "Keep-Alive") == 0 || strcmp(key, "Accept") == 0 || strcmp(key, "Cache-Control") == 0 || strcmp(key, "Accept-Encoding") == 0 || strcmp(key, "Host") == 0) {
    DEBUG_PRINTF("[SSE] Header %s is not allowed\n", key);
    return;
  }

  if (!_contains(_customHeaders, key)) {
    strncpy(_customHeaders[_customHeaderCount].key, key, MAX_HEADER_KEY_SIZE - 1);
    strncpy(_customHeaders[_customHeaderCount].value, val, MAX_HEADER_VAL_SIZE - 1);
    _customHeaderCount++;
  }
}

void EventSource::_onConnectStatic(void *arg, AsyncClient *client) {
  EventSource *self = static_cast<EventSource *>(arg);
  self->_onConnect(client);
}

void EventSource::_onDisconnectStatic(void *arg, AsyncClient *client) {
  EventSource *self = static_cast<EventSource *>(arg);
  self->_onDisconnect(client);
}

void EventSource::_onDataStatic(void *arg, AsyncClient *client, void *data,
                                size_t len) {
  EventSource *self = static_cast<EventSource *>(arg);
  self->_onData(client, data, len);
}

void EventSource::_onErrorStatic(void *arg, AsyncClient *client,
                                 uint8_t error) {
  EventSource *self = static_cast<EventSource *>(arg);
  self->_onError(client, error);
}

void EventSource::_onTimeoutStatic(void *arg, AsyncClient *client, uint32_t time) {
  EventSource *self = static_cast<EventSource *>(arg);
  self->_onError(client, 0, "Timeout");
}

void EventSource::_onConnect(AsyncClient *client) {
  DEBUG_PRINTLN("[SSE] Connexion établie");
  _sendRequest(client);
}

void EventSource::_onDisconnect(AsyncClient *client) {
  bool disconnected = _readyState != CLOSED;
  _readyState = CLOSED;

  DEBUG_PRINTF("onDisconnect _readyState=%hhu disconnected=%d\n", _readyState, disconnected);

  if (disconnected) {
    DEBUG_PRINTLN("[SSE] Calling user disconnect handler");
    _queueConnectionEvent("close");
  }
}

/*
The user agent must then process the response as follows:

If res is an aborted network error, then fail the connection.

Otherwise, if res is a network error, then reestablish the connection, unless the user agent knows that to be futile, in which case the user agent may fail the connection.

Otherwise, if res's status is not 200, or if res's `Content-Type` is not `text/event-stream`, then fail the connection.

Otherwise, announce the connection and interpret res's body line by line.
*/
void EventSource::_onData(AsyncClient *client, void *data, size_t len) {
  _client->ack(len);

  char *body_start = reinterpret_cast<char *>(data);
  size_t body_len = len;

  if (_readyState != OPEN) {
    // Parse header and search for Content-Type: text/event-stream
    // If not found, close connection
    bool contentTypeOk = false;
    int statusCode = -1;
    _isResponseValidEventStream((char *)data, len, contentTypeOk, statusCode);

    if (contentTypeOk == false || statusCode != 200) {
      char error[MAX_EVENT_ERROR_SIZE];
      snprintf(error, sizeof(error), "Invalid response: status code %d, Content-Type header is %s", statusCode, contentTypeOk ? "text/event-stream" : "not found");
      _onError(client, 0, error);  // or close connection ?
      return;
    }

    DEBUG_PRINTLN("[SSE] Content-Type found");
    _readyState = OPEN;
    DEBUG_PRINTLN("[SSE] Calling user connect handler");
    _queueConnectionEvent("open");
    _retryCount = 0;
    _retryDelayMultiplier = 1;

    // Now search body for event-stream
    body_start = strnstr((char *)data, "\r\n\r\n", len);

    if (body_start != nullptr) {
      DEBUG_PRINTLN("[SSE] Body found");
      body_start += 4;
      body_len -= (body_start - (char *)data);
    }
  }

  if (body_start != nullptr && body_len > 0) {
    _parse_event_stream((char *)body_start, body_len);
    // DEBUG_PRINTF("'.*%s'\n", (int)20, body_start);
  }

  DEBUG_PRINTLN("[SSE] onData ended.");
}

void EventSource::_onError(AsyncClient *client, uint8_t error) {
  _onError(client, error, client->errorToString(error));
}

void EventSource::_onError(AsyncClient *client, uint8_t code, const char *error) {
  DEBUG_PRINTF("[SSE] Error: %s\n", error);
  _readyState = CLOSED;

  Event event;
  event.error.code = code;
  strncpy(event.type, "error", sizeof(event.type));
  strncpy(event.error.message, error, sizeof(event.error.message));
  _addToQueue(event);
}

void EventSource::_queueConnectionEvent(const char *type) {
  Event event;
  strncpy(event.type, type, sizeof(event.type));
  _addToQueue(event);
}

void EventSource::_dispachEvent(Event &event) {

  // remove trailing \n from event.data
  if (event.message.data[strlen(event.message.data) - 1] == '\n') {
    event.message.data[strlen(event.message.data) - 1] = '\0';
  }

  for (uint8_t i = 0; i < _eventHandlerCount; i++) {
    if (strcmp(_eventHandlers[i].key, event.type) == 0 && _eventHandlers[i].value) {
      _eventHandlers[i].value(event);
      event._dispached = true;
      break;  // Il n'y a qu'un gestionnaire par type d'événement
    }
  }
}

void EventSource::_addToQueue(Event &event) {
    if (_lock_queue || event._queued) return;
    if (_dispachQueueSize >= MAX_DISPACH_QUEUE_SIZE) return;

    // Si event est déjà dans _dispachQueue (par pointeur), juste incrémenter
    for (size_t i = 0; i < MAX_DISPACH_QUEUE_SIZE; i++) {
        if (&_dispachQueue[i] == &event) {
            event._queued = true;
            if (i == _dispachQueueSize) _dispachQueueSize++;
            return;
        }
    }

    // Sinon copie (cas error/open/close qui viennent de la stack)
    _dispachQueue[_dispachQueueSize++] = event;
    event._queued = true;
}

void EventSource::_processQueue() {
  if (_dispachQueueSize == 0) return;
  _lock_queue = true;

  size_t toProcess = _dispachQueueSize;  // snapshot : on ne traite que ce qui est déjà en queue

  for (size_t j = 0; j < toProcess; j++) {
    Event event = _dispachQueue[0];
    for (size_t i = 0; i < _dispachQueueSize - 1; i++) {
      _dispachQueue[i] = _dispachQueue[i + 1];
    }
    _dispachQueueSize--;
    _lock_queue = false;  // déverrouiller AVANT le dispatch
    _dispachEvent(event);
    event._queued = false;
    _lock_queue = true;
  }

  _lock_queue = false;
}

void EventSource::_connect() {

  _client->onConnect(_onConnectStatic, this);

  _client->onDisconnect(_onDisconnectStatic, this);

  _client->onError(_onErrorStatic, this);

  _client->onData(_onDataStatic, this);

  _connect_client();
}

void EventSource::_connect_client() {
  DEBUG_PRINTF("[SSE] Connexion à %s:%hu%s retry:%u\n", _apiHost, _apiPort,
               _ssePath, _retryDelay);

  if (_readyState == OPEN) {
    DEBUG_PRINTLN("[SSE] Déjà connecté");
    return;
  }

  _readyState = CONNECTING;
  _client->connect(_apiHost, _apiPort);
}

void EventSource::_sendRequest(AsyncClient *client) {
  // Construction dynamique de la requête
  // La requête complète sera de la forme :
  // GET /path HTTP/1.1\r\n
  // Host: host\r\n
  // Accept: text/event-stream\r\n
  // [Headers personnalisés]\r\n
  // [Last-Event-ID si présent]\r\n
  // \r\n

  char reqBuf[MAX_SSE_REQUEST_SIZE];
  size_t len = 0;
  // 1. Ajout de la ligne de requête GET /path HTTP/1.1\r\n
  len += snprintf(reqBuf, sizeof(reqBuf), "GET %s HTTP/1.1\r\n", _ssePath);

  // Host ajouté séparément car dépend de _apiHost (connu à la construction)
  len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "Host: %s\r\n", _apiHost);

  // 2. Ajout des headers fixes
  len += snprintf(reqBuf + len, sizeof(reqBuf) - len, DEFAULT_HEADERS);

  // 3. Ajout des headers personnalisés à la requête
  for (uint8_t i = 0; i < _customHeaderCount; i++) {
    len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "%s: %s\r\n", _customHeaders[i].key, _customHeaders[i].value);
  }

  // 4. Ajout conditionnel du header Last-Event-ID
  if (_lastEventId[0] != '\0') {
    // Serial.printf( "Last-Event-ID: %s\n", _lastEventId);
    len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "Last-Event-ID: %s\r\n",
                    _lastEventId);
  }

  // 5. Fin des headers
  len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "\r\n");

  DEBUG_PRINTF("[SSE] Requête qui sera envoyée:\n%.*s\n", (int)len, reqBuf);
  // 6.Envoi
  client->write(reqBuf, len);
}

void EventSource::addEventListener(const char *type,
                                   const EventHandler &handler) {

  // Ajoute un gestionnaire d'événements pour un type spécifique

  if (handler == nullptr || validate_event_type(type) == false) {
    DEBUG_PRINTLN("[SSE] Gestionnaire ou type invalide");
    return;
  }

  if (_eventHandlerCount >= MAX_EVENT_HANDLER_COUNT) {
    DEBUG_PRINTLN("[SSE] Max event handler count reached");
    return;
  }

  if (!_contains(_eventHandlers, type)) {
    strncpy(_eventHandlers[_eventHandlerCount].key, type, MAX_EVENT_TYPE_SIZE - 1);
    _eventHandlers[_eventHandlerCount].key[MAX_EVENT_TYPE_SIZE - 1] = '\0';
    _eventHandlers[_eventHandlerCount].value = handler;
    _eventHandlerCount++;

    DEBUG_PRINTF("[SSE] Added handler for '%s', count: %d\n", type, _eventHandlerCount);
  }
}

void EventSource::_disconnect() {
  _client->close();
}

void EventSource::close() {
  _sseAutoreconnect = false;
  _client->close();
}

void EventSource::setAutoreconnect(bool autoreconnect) {
  _sseAutoreconnect = autoreconnect;
}

void EventSource::_setRetryDelay(uint32_t retryDelay) {
  if (retryDelay != _retryDelay) {
    _retryDelay = retryDelay;
    DEBUG_PRINTF("[SSE] Retry delay updated to: %u\n", _retryDelay);
  }
}

void EventSource::_setLastEventId(const char *lastEventId) {
  if (lastEventId == nullptr) {
    DEBUG_PRINTLN("lastEventId is null");
    _lastEventId[0] = '\0';
    return;
  }

  if (strcmp(lastEventId, _lastEventId) != 0) {
    strncpy(_lastEventId, lastEventId, sizeof(_lastEventId));
    _lastEventId[sizeof(_lastEventId) - 1] = '\0';
    DEBUG_PRINTF("Last event ID updated to: %s\n", _lastEventId);
  }
}

EventSource::Event EventSource::_newMessageEvent() {
  DEBUG_PRINTLN("Creating new event");
  Event event;
  strncpy(event.origin, _apiHost, sizeof(event.origin));
  return event;
}

EventSource::Event EventSource::_get_current_event(Event &event) {
  if (event._queued == true) {
    return _newMessageEvent();
  }

  return event;
}

void EventSource::_parse_event_stream(const char *cstr, size_t len) {
  if (len == 0) return;

  const char *pos = cstr;
  const char *end = cstr + len;
  size_t lines_count = 0;

  // Allouer dans la queue directement
  size_t queueSlot = _dispachQueueSize;
  if (queueSlot >= MAX_DISPACH_QUEUE_SIZE) return;

  _dispachQueue[queueSlot] = _newMessageEvent();
  Event *event = &_dispachQueue[queueSlot];

  while (pos < end && lines_count <= MAX_EVENT_LINES) {

    size_t line_len = get_line_size(pos, end - pos);
    bool dispatched = _process_line(pos, line_len, *event);

    if (dispatched) {
      // L'event a été enqueued dans _process_line → prendre le slot suivant
      queueSlot = _dispachQueueSize;
      if (queueSlot < MAX_DISPACH_QUEUE_SIZE) {
        _dispachQueue[queueSlot] = _newMessageEvent();
        event = &_dispachQueue[queueSlot];
      } else {
        break;
      }
    }

    pos += line_len;
    if (pos < end - 1 && pos[0] == '\r' && pos[1] == '\n') {
      pos += 2;
    } else if (pos < end && (pos[0] == '\r' || pos[0] == '\n')) {
      pos += 1;
    }

    lines_count++;
  }
}

// Parse an event stream line according to the SSE specification
// https://html.spec.whatwg.org/multipage/server-sent-events.html#event-stream-interpretation
bool EventSource::_process_line(const char *cstr, size_t len, Event &event) {
  DEBUG_PRINTF("Process line: '%.*s' len=%zu\n", (int)len, cstr, len);

  /*
    If the line is empty (a blank line)
    Dispatch the event, as defined below.
  */
  if (len == 0) {
    DEBUG_PRINT("Empty line, ");

    if (event._hasData) {
      DEBUG_PRINTLN("Enqueuing data event");
      _addToQueue(event);
      return true;
    }
    DEBUG_PRINTLN("no data to enqueue");
    return false;
  }

  if (len < 4) {  // id:1
    DEBUG_PRINTLN("Invalid line");
    return false;
  }
  /*
      If the line starts with a U+003A COLON character (:)
      Ignore the line.
    */
  const char *start = cstr;

  if (cstr[0] == ':') {
    DEBUG_PRINTLN("Ignoring comment line");
    return false;
  }
  /*
        Collect the characters on the line before the first U+003A COLON character (:), and let field be that string.
        Collect the characters on the line after the first U+003A COLON character (:), and let value be that string. If value starts with a U+0020 SPACE character, remove it from value.
      */
  const char *colon_pos = strnchr(cstr, ':', len);

  if (colon_pos != nullptr) {
    char field[MAX_SSE_KEY_SIZE];
    char value[MAX_SSE_VALUE_SIZE];

    strncpy(field, start, colon_pos - start);
    field[colon_pos - start] = '\0';

    const char *value_start = colon_pos + 1;
    if (*value_start == ' ') {
      value_start++;
    }

    size_t value_len = len - (value_start - start);

    // Clamp explicite pour éviter tout débordement
    if (value_len >= MAX_SSE_VALUE_SIZE) {
      value_len = MAX_SSE_VALUE_SIZE - 1;
    }

    strncpy(value, value_start, value_len);
    value[value_len] = '\0';

    DEBUG_PRINTF("Field: '%s', Value: '%s'\n", field, value);
    _process_field(field, value, event);
  }

  return false;
}


// Process a field-value pair from an event stream line
// https://html.spec.whatwg.org/multipage/server-sent-events.html#processField
void EventSource::_process_field(const char *name, const char *value, Event &event) {
  if (strcmp(name, "data") == 0) {
    // Append the value to the data buffer
    // then append a single U+000A LINE FEED (LF) character to the data buffer.
    strncat(event.message.data, value, sizeof(event.message.data) - strlen(event.message.data) - 1);
    strncat(event.message.data, "\n", sizeof(event.message.data) - strlen(event.message.data) - 1);
    event._hasData = true;
  } else if (strcmp(name, "event") == 0) {
    // Set the event type buffer to the value.
    // Handle the unofficial "ping" event type
    if (strcmp(value, "ping") == 0) {
      DEBUG_PRINTLN("Ping received, sending pong");
      _client->write("pong\r\n", 5);
    } else {
      if (strlen(value) > 0) {
        strncpy(event.type, value, sizeof(event.type));
        event.type[sizeof(event.type) - 1] = '\0';
      }
    }
  } else if (strcmp(name, "id") == 0) {

    // Set the last event ID buffer to the value.
    // If the value is the empty string, set the last event ID buffer to it.
    // If the field value does not contain U+0000 NULL, then set the last event ID buffer to the field value. Otherwise, ignore the field.

    if (strnchr(value, '\0', strlen(value)) == nullptr) {
      _setLastEventId(value);
    }

    strncpy(event.message.lastEventId, value, sizeof(event.message.lastEventId));
    event.message.lastEventId[sizeof(event.message.lastEventId) - 1] = '\0';
  } else if (strcmp(name, "retry") == 0) {
    // If the value consists of only ASCII digits, then interpret the value as an integer in base ten, and set the event stream's reconnection time to that integer. Otherwise, ignore the field.
    if (isdigits(value) && strlen(value) > 0 && strlen(value) < 10) {
      long retry = strtol(value, nullptr, 10);
      if (retry > 0 && retry <= UINT32_MAX) {
        _setRetryDelay(static_cast<uint32_t>(retry));
      }
    }
  } else {
    // Ignore the field.
    DEBUG_PRINTF("Ignoring field: %s\n", name);
    return;
  }
}

/*
  UTILITIES
*/
// Check if event type added via addEventListener is valid
// It should not contain any of the following characters: \r\n\0
constexpr bool validate_event_type(const char *str) {
  if (str == nullptr || strlen(str) == 0) {
    return false;
  }

  while (*str != '\0') {
    if (*str == '\r' || *str == '\n' || *str == '\0') {
      return false;
    }
    str++;
  }

  return true;
}

void _isResponseValidEventStream(const char *data, size_t len, bool &contentTypeOk, int &statusCode) {
  // Get response status code and check if it is 200
  const char *statusCodePtr = strnstr(data, "HTTP/1.1 ", len);
  if (statusCodePtr == nullptr) {
    statusCode = -1;
  }

  statusCodePtr += 9;
  statusCode = atoi(statusCodePtr);
  if (statusCode == 0) statusCode = -1;

  const char *contentType = "Content-Type: text/event-stream\r\n";
  size_t contentTypeLen = strlen(contentType);

  if (len - (statusCodePtr - data) < contentTypeLen) {
    contentTypeOk = false;
  }

  const char *contentTypePtr = strnstr(statusCodePtr, contentType, len - (statusCodePtr - data));
  contentTypeOk = contentTypePtr != nullptr;
}

size_t get_line_size(const char *cstr, size_t max_len) {
  size_t pos = 0;

  while (pos < max_len) {
    if (cstr[pos] == '\r' || cstr[pos] == '\n') {
      break;
    }

    pos++;
  }

  return pos;
}

#ifndef HAVE_STRNCHR

const char *strnchr(const char *s, char c, size_t n) {
  for (size_t i = 0; i < n && s[i] != '\0'; ++i) {
    if (static_cast<unsigned char>(s[i]) == static_cast<unsigned char>(c))
      return s + i;
  }
  return nullptr;
}

#endif

#ifndef ARDUINO
// strnstr n'est pas disponible
char *strnstr(const char *haystack, const char *needle, size_t len) {
  size_t needle_len = strlen(needle);
  if (needle_len == 0) {
    return (char *)haystack;
  }

  for (size_t i = 0; i <= len - needle_len; ++i) {
    if (strncmp(haystack + i, needle, needle_len) == 0) {
      return (char *)(haystack + i);
    }
    if (haystack[i] == '\0') {
      break;
    }
  }

  return nullptr;
}
#endif

bool isdigits(const char *str) {
  while (*str) {
    if (!isdigit(*str)) {
      return false;
    }
    str++;
  }

  return true;
}

#ifndef ARDUINO
// millis() is the amount of milliseconds since the program started
uint64_t millis() {
  static auto start = std::chrono::high_resolution_clock::now();
  auto now = std::chrono::high_resolution_clock::now();

  return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
}
#endif