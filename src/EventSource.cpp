
#include "EventSource.h"

template <class T>
using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

template <typename T> struct is_char_array : std::false_type {};

template <typename T, size_t N>
struct is_char_array<T[N]> : std::is_same<remove_cvref_t<T>, char> {};

// template <typename T>
// inline constexpr bool is_char_array_v = is_char_array<T>::value;

template <typename T>
constexpr bool is_string_host_v = std::is_same_v<remove_cvref_t<T>, char*> || is_char_array<T>::value;
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
      type, origin, message.data, message.lastEventId, error.message,
      error.code);
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

template <typename Opts>
void EventSource::_init(const char *url, Opts options) {
  char host[MAX_EVENT_ORIGIN_SIZE] = {0};
  char path[MAX_SSE_PATH_SIZE] = {0};
  uint16_t port = DEFAULT_PORT;

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

  bool secure = strncmp(url, "https://", 8) == 0;

  _init(host, path, port, options, secure);
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
    _onError(nullptr, 0, "Invalid host type");
    _sseAutoreconnect = false;
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
    _onError(nullptr, 0, "Invalid options type");
    _sseAutoreconnect = false;
    return;
  }

  snprintf(_ssePath, sizeof(_ssePath), (path[0] != '/') ? "/%s" : "%s", path);
  _ssePath[sizeof(_ssePath) - 1] = '\0';

  _sseAutoreconnect = true;
  _retryDelay = DEFAULT_RETRY_DELAY;
  _client = new AsyncClient;
  _secure = secure;
  _apiPort = port;
  _readyState = CLOSED;
  _retryDelayMultiplier = 1;
  _retryCount = 0;
  _lastEventId[0] = '\0';
  _initial_connection = true;
  _lastConnectionTime = 0;
  _dispachQueueSize = 0;
  _lock_queue = false;
  _eventHandlerCount = 0;

  _client->setRxTimeout(DEFAULT_TIMEOUT);

  DEBUG_PRINTF(
      "[SSE] EventSource url parsed %s:%hu%s retry:%u autoreconnect: %d\n",
      _apiHost, _apiPort, _ssePath, _retryDelay, _sseAutoreconnect);
}

void EventSource::_addHeaders(const HeadersMap &headers) {
  for (const auto &header : headers) {
    _addHeader(header.first.c_str(), header.first.length(), header.second);
  }
}
// ---------- update (main loop) ----------

void EventSource::update() {
  static uint64_t lastQueueUpdate = 0;

  if (millis() - lastQueueUpdate > 100) {
    lastQueueUpdate = millis();
    _processQueue();
#ifdef ARDUINO
    system_soft_wdt_feed();
#endif
  }

  if (_initial_connection) {
    DEBUG_PRINTLN("[SSE] Initial connection");
    _initial_connection = false;
    _connect();
#ifdef ARDUINO
    system_soft_wdt_feed();
#endif
  } else if (_readyState == CLOSED) {
    if (_client->connected()) {
      if ((millis() - _lastConnectionTime) > DEFAULT_TIMEOUT) {
        DEBUG_PRINTLN("[SSE] Timeout");
        _onError(_client, 0, "Timeout");
      }
    } else if (_sseAutoreconnect && (millis() - _lastConnectionTime) >
                                        _retryDelay * _retryDelayMultiplier) {
      DEBUG_PRINTF("[SSE] Reconnecting after %zu ms",
                   millis() - _lastConnectionTime);
      _lastConnectionTime = millis();
      _retryCount++;
      _connect_client();

      if (_retryCount > EXPONENTIAL_RETRY_LIMIT) {
        _retryCount = 0;
        _retryDelayMultiplier++;
        DEBUG_PRINTF("[SSE] Too many reconnection attempts, increasing retry "
                     "delay to %zu seconds\n",
                     _retryDelay * _retryDelayMultiplier);
      }
    }
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
    strncpy(_customHeaders[_customHeaderCount].key, key,
            MAX_HEADER_KEY_SIZE - 1);
    _customHeaders[_customHeaderCount].key[MAX_HEADER_KEY_SIZE - 1] = '\0';

    std::visit(
        [&](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::string>) {
            strncpy(_customHeaders[_customHeaderCount].value, arg.c_str(),
                    MAX_HEADER_VALUE_SIZE - 1);
          } else if constexpr (std::is_convertible_v<T, int>) {
            snprintf(_customHeaders[_customHeaderCount].value,
                     MAX_HEADER_VALUE_SIZE, "%d", (int)arg);
          } else if constexpr (std::is_convertible_v<T, float>) {
            snprintf(_customHeaders[_customHeaderCount].value,
                     MAX_HEADER_VALUE_SIZE, "%f", (float)arg);
          }

          _customHeaders[_customHeaderCount].value[MAX_HEADER_VALUE_SIZE - 1] =
              '\0';
        },
        value);

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

void EventSource::_onErrorStatic(void *arg, AsyncClient *client,
                                 uint8_t error) {
  static_cast<EventSource *>(arg)->_onError(client, error);
}

void EventSource::_onTimeoutStatic(void *arg, AsyncClient *client,
                                   uint32_t /*time*/) {
  static_cast<EventSource *>(arg)->_onError(client, 0, "Timeout");
}

// ---------- connection lifecycle ----------

void EventSource::_onConnect(AsyncClient *client) {
  DEBUG_PRINTLN("[SSE] Connexion établie");
  _sendRequest(client);
}

void EventSource::_onDisconnect(AsyncClient *client) {
  bool disconnected = (_readyState != CLOSED);
  _readyState = CLOSED;

  DEBUG_PRINTF("onDisconnect _readyState=%hhu disconnected=%d\n", _readyState,
               disconnected);

  if (disconnected) {
    DEBUG_PRINTLN("[SSE] Calling user disconnect handler");
    _queueConnectionEvent("close");
  }
}

void EventSource::_onData(AsyncClient *client, void *data, size_t len) {
  _client->ack(len);

  char *body_start = reinterpret_cast<char *>(data);
  size_t body_len = len;

  if (_readyState != OPEN) {
    bool contentTypeOk = false;
    int statusCode = -1;
    _isResponseValidEventStream((char *)data, len, contentTypeOk, statusCode);

    if (!contentTypeOk || statusCode != 200) {
      char error[MAX_EVENT_ERROR_SIZE];
      snprintf(error, sizeof(error),
               "Invalid response: status code %d, Content-Type header is %s",
               statusCode, contentTypeOk ? "text/event-stream" : "not found");
      _onError(client, 0, error);
      return;
    }

    DEBUG_PRINTLN("[SSE] Content-Type found");
    _readyState = OPEN;
    DEBUG_PRINTLN("[SSE] Calling user connect handler");
    _queueConnectionEvent("open");
    _retryCount = 0;
    _retryDelayMultiplier = 1;

    body_start = strnstr((char *)data, "\r\n\r\n", len);
    if (body_start != nullptr) {
      DEBUG_PRINTLN("[SSE] Body found");
      body_start += 4;
      body_len -= (body_start - (char *)data);
    }
  }

  if (body_start != nullptr && body_len > 0)
    _parse_event_stream(body_start, body_len);

  DEBUG_PRINTLN("[SSE] onData ended.");
}

void EventSource::_onError(AsyncClient *client, uint8_t error) {
  _onError(client, error, client->errorToString(error));
}

void EventSource::_onError(AsyncClient *client, uint8_t code,
                           const char *error) {
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

// ---------- event dispatch / queue ----------

void EventSource::_dispachEvent(Event &event) {
  // Strip trailing newline from data
  size_t dlen = strlen(event.message.data);
  if (dlen > 0 && event.message.data[dlen - 1] == '\n')
    event.message.data[dlen - 1] = '\0';

  for (uint8_t i = 0; i < _eventHandlerCount; i++) {
    if (strcmp(_eventHandlers[i].key, event.type) == 0 &&
        _eventHandlers[i].value) {
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
#if ASYNC_TCP_SSL_ENABLED
  _client->connect(_apiHost, _apiPort, _secure);
#else
  _client->connect(_apiHost, _apiPort);
#endif
}

bool EventSource::_connected_client() { return _client->connected(); }

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
    strncpy(_eventHandlers[_eventHandlerCount].key, type,
            MAX_EVENT_TYPE_SIZE - 1);
    _eventHandlers[_eventHandlerCount].key[MAX_EVENT_TYPE_SIZE - 1] = '\0';
    _eventHandlers[_eventHandlerCount].value = handler;
    _eventHandlerCount++;
    DEBUG_PRINTF("[SSE] Added handler for '%s', count: %d\n", type,
                 _eventHandlerCount);
  }
}

void EventSource::_disconnect() { _client->close(); }

void EventSource::close() {
  _sseAutoreconnect = false;
  _client->close();
}

void EventSource::setAutoreconnect(bool autoreconnect) {
  _sseAutoreconnect = autoreconnect;
}

void EventSource::setRetryDelay(uint32_t retryDelay) {
  if (retryDelay != _retryDelay) {
    _retryDelay = retryDelay;
    DEBUG_PRINTF("[SSE] Retry delay updated to: %u\n", _retryDelay);
  }
}

void EventSource::setTimeout(uint32_t timeout) {
  _client->setRxTimeout(timeout);
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

// ---------- SSE parsing ----------

EventSource::Event EventSource::_newMessageEvent() {
  DEBUG_PRINTLN("Creating new event");
  Event event;
  strncpy(event.origin, _apiHost, sizeof(event.origin));
  return event;
}

EventSource::Event EventSource::_get_current_event(Event &event) {
  if (event._queued)
    return _newMessageEvent();
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
    size_t line_len = get_line_size(pos, end - pos);
    bool dispatched = _process_line(pos, line_len, *event);

    if (dispatched) {
      queueSlot = _dispachQueueSize;
      if (queueSlot < MAX_DISPACH_QUEUE_SIZE) {
        _dispachQueue[queueSlot] = _newMessageEvent();
        event = &_dispachQueue[queueSlot];
      } else {
        break;
      }
    }

    pos += line_len;
    if (pos < end - 1 && pos[0] == '\r' && pos[1] == '\n')
      pos += 2;
    else if (pos < end && (pos[0] == '\r' || pos[0] == '\n'))
      pos += 1;

    lines_count++;
  }
}

bool EventSource::_process_line(const char *cstr, size_t len, Event &event) {
  DEBUG_PRINTF("Process line: '%.*s' len=%zu\n", (int)len, cstr, len);

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

  if (len < 4) {
    DEBUG_PRINTLN("Invalid line");
    return false;
  }

  if (cstr[0] == ':') {
    DEBUG_PRINTLN("Ignoring comment line");
    return false;
  }

  const char *colon_pos = strnchr(cstr, ':', len);
  if (colon_pos != nullptr) {
    char field[MAX_EVENT_NAME_SIZE];
    char value[MAX_EVENT_VALUE_SIZE];

    strncpy(field, cstr, colon_pos - cstr);
    field[colon_pos - cstr] = '\0';

    const char *value_start = colon_pos + 1;
    if (*value_start == ' ')
      value_start++;

    size_t value_len = len - (value_start - cstr);
    if (value_len >= MAX_EVENT_VALUE_SIZE)
      value_len = MAX_EVENT_VALUE_SIZE - 1;

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
    strncat(event.message.data, value,
            sizeof(event.message.data) - strlen(event.message.data) - 1);
    strncat(event.message.data, "\n",
            sizeof(event.message.data) - strlen(event.message.data) - 1);
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
    if (strnchr(value, '\0', strlen(value)) == nullptr)
      _setLastEventId(value);

    strncpy(event.message.lastEventId, value,
            sizeof(event.message.lastEventId));
    event.message.lastEventId[sizeof(event.message.lastEventId) - 1] = '\0';

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
