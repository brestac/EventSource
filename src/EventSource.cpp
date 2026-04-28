
#include "EventSource.h"
// #ifdef ESP32
// #include <freertos/FreeRTOS.h>
// #include <freertos/task.h>
// #endif
#if __cplusplus < 202002L
template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;
#endif
template <typename T> struct is_char_array : std::false_type {};

template <typename T, size_t N>
struct is_char_array<T[N]> : std::is_same<remove_cvref_t<T>, char> {};

// template <typename T>
// inline constexpr bool is_char_array_v = is_char_array<T>::value;

template <typename T>
constexpr bool is_string_host_v =
    std::is_same_v<remove_cvref_t<T>, char *> || is_char_array<T>::value;
template <typename T>
constexpr bool is_ip_host_v = std::is_same_v<remove_cvref_t<T>, IPAddress>;

// ---------- Event ----------
EventSource::Event::Event() {
  memset(this, 0, sizeof(Event));
  strncpy(type, "message", sizeof(type));
}

void EventSource::Event::print() {
  DEBUG_PRINTF(
      "[Event]  type: '%s', origin: '%s', data: '%s', lastEventId: '%s', "
      "error: '%s', code: %d",
      type, origin, data, lastEventId, message, code);
}

// ---------- EventSource public ----------

EventSource::EventSource(const char *url, const Options &options) {
  _init(url, options);
}

EventSource::EventSource(const char *url, const HeadersMap &headers) {
  _init(url, headers);
}

EventSource::EventSource(const char *host, const char *path, uint16_t port,
                         const Options &options) {
  _init(host, path, port, options, options.secure);
}

EventSource::EventSource(const char *host, const char *path, uint16_t port,
                         const HeadersMap &headers) {
  _init(host, path, port, headers, false);
}

EventSource::EventSource(const IPAddress &ip, const char *path, uint16_t port,
                         const Options &options) {
  _init(ip, path, port, options, options.secure);
}

EventSource::EventSource(const IPAddress &ip, const char *path, uint16_t port,
                         const HeadersMap &headers) {
  _init(ip, path, port, headers, false);
}

EventSource::~EventSource() {
  _disconnect();
  delete _client;
}

// ---------- EventSource private: init ----------

bool EventSource::_parseURL(const char *url, char *host, char *path, uint16_t& port, bool& secure) {
  char parsed_host[MAX_EVENT_ORIGIN_SIZE] = {0};
  char parsed_path[MAX_SSE_PATH_SIZE] = {0};
  uint16_t parsed_port = DEFAULT_PORT;

  int n = sscanf(url, "%*[^:]://%127[^:/]:%hu/%127s", parsed_host, &parsed_port, parsed_path);
  if (n <= 2) {
    n = sscanf(url, "%*[^:]://%127[^:/]/%127s", parsed_host, parsed_path);
    if (n < 1) {
      DEBUG_PRINTF("Failed to parse URL: %s\n", url);
      _onError(nullptr, ERR_INVALID_URL, "Failed to parse URL");
#ifdef __EXCEPTIONS
      throw std::runtime_error("[SyntaxError] Failed to parse URL");
#endif
      return false;
    }
  }

  strncpy(host, parsed_host, MAX_EVENT_ORIGIN_SIZE);
  host[MAX_EVENT_ORIGIN_SIZE - 1] = '\0';
  snprintf(path, MAX_SSE_PATH_SIZE, (parsed_path[0] != '/') ? "/%s" : "%s", parsed_path);
  path[MAX_SSE_PATH_SIZE - 1] = '\0';
  port = parsed_port;
  secure = strncmp(url, "https://", 8) == 0;

  return true;
}

bool EventSource::_setURL(const char *url) {
  char host[MAX_EVENT_ORIGIN_SIZE] = {0};
  char path[MAX_SSE_PATH_SIZE] = {0};  
  uint16_t port = DEFAULT_PORT;
  bool secure = false;

  bool parsed = _parseURL(url, host, path, port, secure);
  if (!parsed) return false;

  strncpy(_apiHost, host, sizeof(_apiHost));
  _apiHost[sizeof(_apiHost) - 1] = '\0';
  snprintf(_ssePath, sizeof(_ssePath), (path[0] != '/') ? "/%s" : "%s", path);
  _ssePath[sizeof(_ssePath) - 1] = '\0';
  _apiPort = port;
  _secure = secure;

  return true;
}

template <typename Opts>
void EventSource::_init(const char *url, Opts options) {
  char host[MAX_EVENT_ORIGIN_SIZE] = {0};
  char path[MAX_SSE_PATH_SIZE] = {0};  
  uint16_t port = DEFAULT_PORT;
  bool secure = false;

  bool parsed = _parseURL(url, host, path, port, secure);
  DEBUG_PRINTF("[SSE] Parsed URL: %s:%hu%s secure=%d\n", host, port, path, secure);

  if (parsed) {
    _init(host, path, port, options, secure);
  } 
}

template <typename Host, typename Opts>
void EventSource::_init(Host host, const char *path, uint16_t port,
                        const Opts &options, bool secure) {

  if constexpr (is_string_host_v<Host>) {
    strncpy(_apiHost, host, sizeof(_apiHost));
  } else if constexpr (is_ip_host_v<Host>) {
    strncpy(_apiHost, host.toString().c_str(), sizeof(_apiHost));
  } else {
    DEBUG_PRINTLN("Invalid host type");
    return;
  }
  _apiHost[sizeof(_apiHost) - 1] = '\0';

  _customHeaderCount = 0;

  if constexpr (std::is_same_v<remove_cvref_t<Opts>, Options>) {
    _addHeaders(options.headers);
  } else if constexpr (std::is_same_v<remove_cvref_t<Opts>, HeadersMap>) {
    _addHeaders(options);
  } else {
    DEBUG_PRINTLN("Invalid options type");
    return;
  }
  
  snprintf(_ssePath, sizeof(_ssePath), (path[0] != '/') ? "/%s" : "%s", path);
  _ssePath[sizeof(_ssePath) - 1] = '\0';

  _client = new AsyncClient;
  _client->onConnect(_onConnectStatic, this);
  _client->onDisconnect(_onDisconnectStatic, this);
  _client->onError(_onErrorStatic, this);
  _client->onData(_onDataStatic, this);

  _retryDelay = DEFAULT_RETRY_DELAY;
  _secure = secure;
  _apiPort = port;
  _readyState = CONNECTING;
  _retryDelayMultiplier = 1;
  _retryCount = 0;
  _lastEventId[0] = '\0';
  _lastConnectionTime = 0;
  _dispachQueueSize = 0;
  _lock_queue = false;
  _eventHandlerCount = 0;
  _timeout = DEFAULT_TIMEOUT * 1000;
  _force_connect = true;
  _force_disconnect = false;

  _client->setRxTimeout(_timeout);

  // #ifdef ESP32
  //   // Start a vTask containing update() function
  //   xTaskCreate([](void *arg) {
  //     EventSource *source = static_cast<EventSource *>(arg);
  //     while (true) {
  //        DEBUG_PRINTF("[SSE] EventSourceTask running\n");
  //       source->_update();
  //       vTaskDelay(10 / portTICK_PERIOD_MS);
  //     }
  //   }, "EventSourceTask", 4096, this, 1, nullptr);
  // #endif

  DEBUG_PRINTF("[SSE] EventSource url init %s:%hu%s retry:%u\n", _apiHost,
               _apiPort, _ssePath, _retryDelay);
}

void EventSource::_addHeaders(const HeadersMap &headers) {
  for (const auto &header : headers) {
    _addHeader(header.first.c_str(), header.first.length(), header.second);
  }
}
// ---------- update (main loop) ----------
// #ifndef ESP32
void EventSource::update() { _update(); }
// #endif

void EventSource::_update() {
  static uint64_t lastQueueUpdate = 0;
#ifdef DEBUG_EVENTSOURCE
  static uint8_t prevReadyState = CONNECTING;
#endif
  if (millis() - lastQueueUpdate > QUEUE_PROCESSING_INTERVAL) {
    _processQueue();
    lastQueueUpdate = millis();
#ifdef ARDUINO
    system_soft_wdt_feed();
#endif
  }

  if (_readyState != CONNECTING)
    return;
#ifdef DEBUG_EVENTSOURCE
  if (prevReadyState != _readyState) {
    DEBUG_PRINTF("[SSE] Ready state changed to %hhu\n", _readyState);
    prevReadyState = _readyState;
  }
#endif
  if (_client->connected()) {
    if (_force_disconnect || (millis() - _lastConnectionTime) > _timeout) {
      std::printf("[SSE] Timeout");
      _lastConnectionTime = millis();
      _force_disconnect = false;
      _client->close();
    }
  } else if (_force_connect || (millis() - _lastConnectionTime) >
                                 _retryDelay * _retryDelayMultiplier) {
    DEBUG_PRINTF("[SSE] Reconnecting after %zu ms",
                 millis() - _lastConnectionTime);
    _force_connect = false;
    _connect();
  }
}

// ---------- header management ----------

void EventSource::_addHeader(const char *key, size_t key_len,
                             const CustomHeaderValue &value) {
  if (_customHeaderCount >= MAX_HEADER_COUNT)
    return;
  if (key == nullptr || key_len == 0 || key_len > MAX_HEADER_KEY_SIZE)
    return;

  if (strcmp(key, "Last-Event-ID") == 0 || strcmp(key, "Content-Type") == 0 ||
      strcmp(key, "Content-Length") == 0 ||
      strcmp(key, "Transfer-Encoding") == 0 || strcmp(key, "Connection") == 0 ||
      strcmp(key, "Keep-Alive") == 0 || strcmp(key, "Accept") == 0 ||
      strcmp(key, "Cache-Control") == 0 ||
      strcmp(key, "Accept-Encoding") == 0 || strcmp(key, "Host") == 0) {
    DEBUG_PRINTF("[SSE] Header %s is not allowed\n", key);
    return;
  }

  if (!_contains(_customHeaders, key)) {
    strncpy(_customHeaders[_customHeaderCount].key, key, MAX_HEADER_KEY_SIZE);
    _customHeaders[_customHeaderCount].key[MAX_HEADER_KEY_SIZE - 1] = '\0';

    std::visit(
        [&](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::string>) {
            strncpy(_customHeaders[_customHeaderCount].value, arg.c_str(),
                    MAX_HEADER_VALUE_SIZE);
          } else if constexpr (std::is_convertible_v<T, int>) {
            snprintf(_customHeaders[_customHeaderCount].value,
                     MAX_HEADER_VALUE_SIZE, "%d", (int)arg);
          } else if constexpr (std::is_convertible_v<T, float>) {
            snprintf(_customHeaders[_customHeaderCount].value,
                     MAX_HEADER_VALUE_SIZE, "%f", (float)arg);
          }
        },
        value);

    _customHeaders[_customHeaderCount].value[MAX_HEADER_VALUE_SIZE - 1] = '\0';
    _customHeaderCount++;
  }
}

// ---------- static callbacks ----------

void EventSource::_onConnectStatic(void *arg, AsyncClient *client) {
  static_cast<EventSource *>(arg)->_onConnect(client);
}

void EventSource::_onDisconnectStatic(void *arg, AsyncClient *client) {
  static_cast<EventSource *>(arg)->_onDisconnect(client);
}

void EventSource::_onDataStatic(void *arg, AsyncClient *client, void *data,
                                size_t len) {
  static_cast<EventSource *>(arg)->_onData(client, data, len);
}

void EventSource::_onErrorStatic(void *arg, AsyncClient *client, int error) {
  static_cast<EventSource *>(arg)->_onError(client, error);
}

void EventSource::_onTimeoutStatic(void *arg, AsyncClient *client,
                                   uint32_t /*time*/) {
  DEBUG_PRINTLN("[SSE] Timeout");
}

// ---------- connection lifecycle ----------

void EventSource::_onConnect(AsyncClient *client) {
  DEBUG_PRINTLN("[SSE] Connexion établie");
  _sendRequest(client);
}

void EventSource::_onDisconnect(AsyncClient *client) {
  // reestablish the connection in case of disconnect
  if (_readyState == OPEN) {
    DEBUG_PRINTLN("[SSE] Déconnecté");
    _readyState = CONNECTING;
  }
}

bool EventSource::_getStatusCode(const char *data, size_t len, int &statusCode) {
  int code = -1;
  size_t n = sscanf(data, "HTTP/%*f %d", &code);
  if (n == 0) return false;
  
  statusCode = code;
  
  DEBUG_PRINTF("[SSE] statusCode=%d\n", statusCode);
  return true;
}

bool EventSource::_hasHeader(const char *data, size_t len, const char *header_name, const char *header_value) {
  char parsedValue[MAX_HEADER_VALUE_SIZE] = {0};
  bool found = _getHeaderValue(data, len, header_name, parsedValue);
  return found && strcmp(parsedValue, header_value) == 0;
}

void EventSource::_onData(AsyncClient *client, void *data, size_t len) {
  DEBUG_PRINTF("[SSE] Received %zu bytes\n", len);
  _client->ack(len);

  char *body_start = reinterpret_cast<char *>(data);
  size_t body_len = len;

  if (_readyState == CONNECTING) {
    // Handle redirection
    int statusCode = -1;
    bool hasStatusCode = _getStatusCode(body_start, len, statusCode);

    if (!hasStatusCode) {
      DEBUG_PRINTF("[SSE] HTTP status code not 200: %d\n", statusCode);
      _onError(client, statusCode, "Invalid HTTP status code");
      return;
    }
    
    if (_is_redirection(statusCode)) {

      bool ok = _handleRedirection(body_start, len, statusCode);

      if (!ok) {
        DEBUG_PRINTLN("[SSE] Location header not found");
        // DO WE CLOSE THE EVENTSOURCE CONNECTION HERE OR NOT?
        _client->close();
        _onError(
            client, ERR_REDIRECT_LOCATION,
            "Invalid Location header");
      }

      return;
    }
    // https://html.spec.whatwg.org/multipage/server-sent-events.html#sse-processing-model
    // Otherwise, if res's status is not 200, or if res's `Content-Type` is not
    // `text/event-stream`, then fail the connection.
    else if (statusCode == 200) {
      DEBUG_PRINTLN("[SSE] Checking response headers");
      bool valid = _hasHeader(body_start, len, "Content-Type", "text/event-stream");

      DEBUG_PRINTF(
          "[SSE] Response headers checked: contentTypeOk=%d, statusCode=%d\n",
          valid, statusCode);

      if (!valid) {
        DEBUG_PRINTLN("[SSE] Content-Type: text/event-stream not found");
        _onError(client, ERR_SERVER_INVALID_CONTENT_TYPE,
                 "Content-Type: text/event-stream not found");
        return;
      }
    } else {
      DEBUG_PRINTF("[SSE] HTTP status code not 200: %d\n", statusCode);
      _onError(client, statusCode, "HTTP status code not 200");
      return;
    }

    DEBUG_PRINTLN("[SSE] Response headers OK");
    _readyState = OPEN;
    _queueConnectionEvent();
    _retryCount = 0;
    _retryDelayMultiplier = 1;

    body_start = strnstr((char *)data, "\r\n\r\n", len);

    if (body_start != nullptr) {
      DEBUG_PRINTLN("[SSE] Body found");
      body_start += 4;
      body_len -= (body_start - (char *)data);
    }
  }

  if (_readyState == OPEN && body_start != nullptr && body_len > 0) {
    _parse_event_stream(body_start, body_len);
  }
}

bool EventSource::_is_redirection(int statusCode) {
  return statusCode >= 300 && statusCode < 400;
}

// TODO: Handle relative URL in Location header
bool EventSource::_handleRedirection(char *data, size_t len, int statusCode) {
  DEBUG_PRINTLN("[SSE] Redirection");
  _readyState = CLOSED; // prevent automatic reconnection
  
  char new_url[MAX_EVENT_ORIGIN_SIZE + MAX_SSE_PATH_SIZE] = {0};
  bool found = _getHeaderValue(data, len, "Location", new_url);

  if (!found) return false;

  if (new_url[0] == '/') {
    strncpy(_ssePath, new_url, sizeof(_ssePath));
    _ssePath[sizeof(_ssePath) - 1] = '\0';
  } else {
    char host[MAX_EVENT_ORIGIN_SIZE] = {0};
    char path[MAX_SSE_PATH_SIZE] = {0};
    uint16_t port = DEFAULT_PORT;
    bool secure = false;

    bool parsed = _parseURL(new_url, host, path, port, secure);
    if (!parsed) return false;
    _setURL(new_url);
  }

  _force_disconnect = true;
  _force_connect = true;
  _readyState = CONNECTING;

  return true;
}
/*
enum err_enum_t {
  ERR_OK = 0,
  ERR_MEM = -1,
  ERR_BUF = -2,
  ERR_TIMEOUT = -3,
  ERR_RTE = -4,
  ERR_INPROGRESS = -5,
  ERR_VAL = -6,
  ERR_WOULDBLOCK = -7,
  ERR_USE = -8,
  ERR_ALREADY = -9,
  ERR_ISCONN = -10,
  ERR_CONN = -11,
  ERR_IF = -12,
  ERR_ABRT = -13,
  ERR_RST = -14,
  ERR_CLSD = -15,
  ERR_ARG = -16
}
*/
bool EventSource::_is_abort_error(int code) {
  // ERR_MEM, ERR_BUF, ERR_VAL, ERR_WOULDBLOCK, ERR_IF, ERR_ABRT, ERR_ARG
  return code == ERR_MEM || code == ERR_BUF || code == ERR_VAL || code == ERR_WOULDBLOCK || code == ERR_IF || code == ERR_ABRT || code == ERR_ARG;
}
/*
If res is an aborted network error, then fail the connection.
Otherwise, if res is a network error, then reestablish the connection, unless
the user agent knows that to be futile, in which case the user agent may fail
the connection.
*/
void EventSource::_onError(AsyncClient *client, int error) {
  if (_is_abort_error(error)) {
    _onError(client, error, client->errorToString(error));
  }
}

void EventSource::_onError(AsyncClient *client, int code, const char *error) {
  DEBUG_PRINTF("[SSE] Error: %s\n", error);

  _readyState = CLOSED;
  _retryCount = 0;
  _retryDelayMultiplier = 1;

  _queueErrorEvent(code, error);
}

void EventSource::_queueConnectionEvent() {
  Event event;
  strncpy(event.type, "open", sizeof(event.type));
  _addToQueue(event);
}

void EventSource::_queueErrorEvent(int code, const char *error) {
  Event event;
  event.code = code;
  strncpy(event.type, "error", sizeof(event.type));
  strncpy(event.message, error, sizeof(event.message));
  event.message[sizeof(event.message) - 1] = '\0';

  _addToQueue(event);
}

// ---------- event dispatch / queue ----------

void EventSource::_dispachEvent(Event &event) {
  // Strip trailing newline from data
  size_t dlen = strlen(event.data);
  if (dlen > 0 && event.data[dlen - 1] == '\n')
    event.data[dlen - 1] = '\0';

  for (uint8_t i = 0; i < _eventHandlerCount; i++) {
    if (strcmp(_eventHandlers[i].key, event.type) == 0 && _eventHandlers[i].value) {
      _eventHandlers[i].value(event);
      event._dispached = true;
      break;
    }
  }
}

void EventSource::_addToQueue(Event &event) {
  if (_lock_queue || event._queued)
    return;
  if (_dispachQueueSize >= MAX_DISPACH_QUEUE_SIZE)
    return;

  for (size_t i = 0; i < MAX_DISPACH_QUEUE_SIZE; i++) {
    if (&_dispachQueue[i] == &event) {
      event._queued = true;
      if (i == _dispachQueueSize)
        _dispachQueueSize++;
      return;
    }
  }

  _dispachQueue[_dispachQueueSize++] = event;
  event._queued = true;
}

void EventSource::_processQueue() {
  if (_dispachQueueSize == 0)
    return;
  _lock_queue = true;

  size_t toProcess = _dispachQueueSize;
  for (size_t j = 0; j < toProcess; j++) {
    Event event = _dispachQueue[0];
    for (size_t i = 0; i < _dispachQueueSize - 1; i++)
      _dispachQueue[i] = _dispachQueue[i + 1];
    _dispachQueueSize--;
    _lock_queue = false;
    _dispachEvent(event);
    event._queued = false;
    _lock_queue = true;
  }

  _lock_queue = false;
}

// ---------- connection ----------
void EventSource::_connect() {
  _connect(_apiHost, _ssePath, _apiPort, _secure);
}

void EventSource::_connect(const char * host, const char * path, uint16_t port, bool secure) {
  DEBUG_PRINTF("[SSE] Connexion à %s:%hu%s ssl=%d\n", host, port, path, secure);

  if (_readyState == OPEN) {
    DEBUG_PRINTLN("[SSE] Déjà connecté");
    return;
  }

#if ASYNC_TCP_SSL_ENABLED
  _client->connect(host, port, secure);
#else
  _client->connect(host, port);
#endif

  _lastConnectionTime = millis();
  _retryCount++;

  if (_retryCount > EXPONENTIAL_RETRY_LIMIT) {
    _retryCount = 0;
    _retryDelayMultiplier *= 2;
    DEBUG_PRINTF("[SSE] Too many reconnection attempts, increasing retry "
                 "delay to %zu seconds\n",
                 _retryDelay * _retryDelayMultiplier);
  }
}

void EventSource::_sendRequest(AsyncClient *client) {
  char reqBuf[MAX_SSE_REQUEST_SIZE];
  size_t len = 0;

  len += snprintf(reqBuf, sizeof(reqBuf), "GET %s HTTP/1.1\r\n", _ssePath);
  len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "Host: %s\r\n", _apiHost);
  len += snprintf(reqBuf + len, sizeof(reqBuf) - len, DEFAULT_HEADERS);

  for (uint8_t i = 0; i < _customHeaderCount; i++) {
    len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "%s: %s\r\n",
                    _customHeaders[i].key, _customHeaders[i].value);
  }

  if (_lastEventId[0] != '\0') {
    len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "Last-Event-ID: %s\r\n",
                    _lastEventId);
  }

  len += snprintf(reqBuf + len, sizeof(reqBuf) - len, "\r\n");

  DEBUG_PRINTF("[SSE] Requête qui sera envoyée:\n%.*s\n", (int)len, reqBuf);
  client->write(reqBuf, len);
}

// ---------- public API ----------

void EventSource::addEventListener(const char *type,
                                   const EventHandler &handler) {
  if (handler == nullptr || !validate_event_type(type)) {
    DEBUG_PRINTLN("[SSE] Gestionnaire ou type invalide");
#ifdef __EXCEPTIONS
    throw std::runtime_error("[SyntaxError] Invalid handler or event type");
#endif
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
    DEBUG_PRINTF("[SSE] Added handler for '%s', count: %d\n", type,
                 _eventHandlerCount);
  }
}

void EventSource::_disconnect() { _client->close(); }

void EventSource::close() {
  _readyState = CLOSED;
  _client->close();
}

void EventSource::reconnect() {
  _force_disconnect = true;
  _readyState = CONNECTING;
}

void EventSource::setRetryDelay(uint32_t retryDelay) {
  if (retryDelay != _retryDelay) {
    _retryDelay = retryDelay;
    DEBUG_PRINTF("[SSE] Retry delay updated to: %u\n", _retryDelay);
  }
}

void EventSource::setTimeout(uint32_t timeout) { _timeout = timeout; }

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

// ---------- SSE parsing ----------

EventSource::Event EventSource::_newMessageEvent() {
  DEBUG_PRINTLN("Creating new event");
  Event event;
  strncpy(event.origin, _apiHost, sizeof(event.origin));
  event.origin[sizeof(event.origin) - 1] = '\0';
  return event;
}

void EventSource::_parse_event_stream(const char *cstr, size_t len) {
  if (len == 0)
    return;

  const char *pos = cstr;
  const char *end = cstr + len;
  size_t lines_count = 0;

  size_t queueSlot = _dispachQueueSize;
  if (queueSlot >= MAX_DISPACH_QUEUE_SIZE)
    return;

  _dispachQueue[queueSlot] = _newMessageEvent();
  Event *event = &_dispachQueue[queueSlot];

  while (pos < end && lines_count <= MAX_EVENT_LINES) {
    size_t line_len = linelen(pos, end);
    bool should_dispach = _process_line(pos, line_len, *event);

    if (should_dispach) {
      queueSlot = _dispachQueueSize;
      if (queueSlot < MAX_DISPACH_QUEUE_SIZE) {
        _dispachQueue[queueSlot] = _newMessageEvent();
        event = &_dispachQueue[queueSlot];
      } else {
        break;
      }
    }

    skip_eol(pos, end);
    lines_count++;
  }
}

bool EventSource::_process_line(const char *cstr, size_t len, Event &event) {
  DEBUG_PRINTF("Process line: '%.*s' len=%zu\n", (int)len, cstr, len);
  static bool is_empty_line = false;
  
  if (len == 0) {
    DEBUG_PRINT("Empty line, ");
    if (!is_empty_line && event._hasData) {
      DEBUG_PRINTLN("Enqueuing data event");
      _addToQueue(event);
      is_empty_line = true;
      return true;
    }
    DEBUG_PRINTLN("no data to enqueue or already empty line");
    return false;
  }

  is_empty_line = false;

  if (cstr[0] == ':') {
    DEBUG_PRINTLN("Ignoring comment line");
    return false;
  } else if (len < 4) {
    DEBUG_PRINTLN("Invalid line");
    return false;
  }

  const char *colon_pos = strnchr(cstr, ':', len);

  if (colon_pos != nullptr) {
    char field[MAX_EVENT_NAME_SIZE];
    char value[MAX_EVENT_VALUE_SIZE];

    size_t field_len = std::min((size_t)(colon_pos - cstr), MAX_EVENT_NAME_SIZE - 1);

    strncpy(field, cstr, field_len);
    field[field_len] = '\0';

    const char *value_start = colon_pos + 1;

    if (*value_start == ' ')
      value_start++;

    size_t value_len = std::min(len - (value_start - cstr), MAX_EVENT_VALUE_SIZE - 1);

    strncpy(value, value_start, value_len);
    value[value_len] = '\0';

    DEBUG_PRINTF("Field: '%s', Value: '%s'\n", field, value);
    _process_field(field, value, event);
  }

  return false;
}

void EventSource::_process_field(const char *name, const char *value,
                                 Event &event) {
  if (strcmp(name, "data") == 0) {
    strncat(event.data, value, sizeof(event.data) - strlen(event.data) - 1);
    strncat(event.data, "\n", sizeof(event.data) - strlen(event.data) - 1);
    event._hasData = true;

  } else if (strcmp(name, "event") == 0) {
    if (strcmp(value, "ping") == 0) {
      DEBUG_PRINTLN("Ping received, sending pong");
      _client->write("pong\r\n", 5);
    } else if (strlen(value) > 0) {
      strncpy(event.type, value, sizeof(event.type));
      event.type[sizeof(event.type) - 1] = '\0';
    }
  } else if (strcmp(name, "id") == 0) {
    if (strnchr(value, '\0', strlen(value)) == nullptr) {
      _setLastEventId(value);
      strncpy(event.lastEventId, value, sizeof(event.lastEventId));
      event.lastEventId[sizeof(event.lastEventId) - 1] = '\0';
    }
  } else if (strcmp(name, "retry") == 0) {
    if (isdigits(value) && strlen(value) > 0 && strlen(value) < 10) {
      long retry = strtol(value, nullptr, 10);
      if (retry > 0 && (unsigned long)retry <= UINT32_MAX)
        _retryDelay = (uint32_t)retry;
    }
  } else {
    DEBUG_PRINTF("Ignoring field: %s\n", name);
  }
}