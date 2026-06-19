#include "StorytellerSyncClient.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <Logging.h>
#include <WiFiClientSecure.h>

#include <cstdio>
#include <cstring>

#include "StorytellerTokenStore.h"

namespace {

void addAuthHeader(HTTPClient& http) {
  const std::string bearer = "Bearer " + ST_TOKEN_STORE.getToken();
  http.addHeader("Authorization", bearer.c_str());
}

// millis() wraps after ~49 days; fine as a relative timestamp.
uint64_t nowMs() { return static_cast<uint64_t>(millis()); }

}  // namespace

void StorytellerSyncClient::buildUrl(char* buf, size_t maxLen, const char* path) {
  std::string base = ST_TOKEN_STORE.getServerUrl();
  while (!base.empty() && base.back() == '/') base.pop_back();
  snprintf(buf, maxLen, "%s%s", base.c_str(), path);
}

// --- OAuth device-code flow ---

StorytellerSyncClient::Error StorytellerSyncClient::requestDeviceCode(StorytellerDeviceCodeResponse& out) {
  if (!ST_TOKEN_STORE.hasServerUrl()) return NO_SERVER_URL;

  char url[512];
  buildUrl(url, sizeof(url), "/api/v2/device/start");
  LOG_DBG("STS", "requestDeviceCode: %s", url);

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  const int httpCode = http.POST("{}");
  String responseBody = http.getString();
  http.end();

  LOG_DBG("STS", "requestDeviceCode response: %d", httpCode);
  if (httpCode < 0) return NETWORK_ERROR;
  if (httpCode != 200) return SERVER_ERROR;

  JsonDocument doc;
  if (deserializeJson(doc, responseBody) != DeserializationError::Ok) {
    LOG_ERR("STS", "requestDeviceCode JSON parse error");
    return JSON_ERROR;
  }

  strlcpy(out.deviceCode, doc["device_code"] | "", sizeof(out.deviceCode));
  strlcpy(out.userCode, doc["user_code"] | "", sizeof(out.userCode));
  strlcpy(out.verificationUri, doc["verification_uri"] | "", sizeof(out.verificationUri));
  // verification_uri_complete already has the code embedded; fall back to bare URI
  const char* uriComplete = doc["verification_uri_complete"] | out.verificationUri;
  strlcpy(out.verificationUriComplete, uriComplete, sizeof(out.verificationUriComplete));
  out.interval = doc["interval"] | 5;
  out.expiresIn = doc["expires_in"] | 600;

  LOG_DBG("STS", "Device code: user_code=%s interval=%ds", out.userCode, out.interval);
  return OK;
}

StorytellerSyncClient::Error StorytellerSyncClient::pollForToken(const char* deviceCode, char* outToken,
                                                                 size_t tokenMaxLen) {
  if (!ST_TOKEN_STORE.hasServerUrl()) return NO_SERVER_URL;

  char url[512];
  buildUrl(url, sizeof(url), "/api/v2/device/token");

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, url);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  JsonDocument body;
  body["device_code"] = deviceCode;
  String bodyStr;
  serializeJson(body, bodyStr);

  const int httpCode = http.POST(bodyStr);
  String responseBody = http.getString();
  http.end();

  LOG_DBG("STS", "pollForToken response: %d", httpCode);
  if (httpCode < 0) return NETWORK_ERROR;

  JsonDocument doc;
  if (deserializeJson(doc, responseBody) != DeserializationError::Ok) {
    LOG_ERR("STS", "pollForToken JSON parse error");
    return JSON_ERROR;
  }

  if (httpCode == 200) {
    const char* token = doc["access_token"] | "";
    if (token[0] == '\0') return JSON_ERROR;
    strlcpy(outToken, token, tokenMaxLen);
    LOG_DBG("STS", "Storyteller token received");
    return OK;
  }

  const char* errCode = doc["error"] | "";
  LOG_DBG("STS", "pollForToken error: %s", errCode);

  if (strcmp(errCode, "authorization_pending") == 0) return PENDING;
  if (strcmp(errCode, "slow_down") == 0) return SLOW_DOWN;
  if (strcmp(errCode, "expired_token") == 0) return EXPIRED;
  if (strcmp(errCode, "access_denied") == 0) return DENIED;

  return SERVER_ERROR;
}

// --- Reading progress ---

StorytellerSyncClient::Error StorytellerSyncClient::getProgress(const char* bookUuid, StorytellerPosition& out) {
  if (!ST_TOKEN_STORE.hasToken()) return NO_TOKEN;
  if (!ST_TOKEN_STORE.hasServerUrl()) return NO_SERVER_URL;

  char path[192];
  snprintf(path, sizeof(path), "/api/v2/books/%s/positions", bookUuid);
  char url[512];
  buildUrl(url, sizeof(url), path);
  LOG_DBG("STS", "getProgress: %s", url);

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, url);
  addAuthHeader(http);
  http.addHeader("Accept", "application/json");

  const int httpCode = http.GET();

  if (httpCode == 200) {
    String responseBody = http.getString();
    http.end();

    JsonDocument doc;
    if (deserializeJson(doc, responseBody) != DeserializationError::Ok) {
      LOG_ERR("STS", "getProgress JSON parse error");
      return JSON_ERROR;
    }

    out.href = (doc["locator"]["href"] | "");
    out.totalProgression = doc["locator"]["locations"]["totalProgression"] | 0.0f;
    out.timestamp = doc["timestamp"] | static_cast<uint64_t>(0);

    LOG_DBG("STS", "Remote progress: href=%s prog=%.4f", out.href.c_str(), out.totalProgression);
    return OK;
  }

  http.end();
  LOG_DBG("STS", "getProgress response: %d", httpCode);
  if (httpCode == 404) return NOT_FOUND;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode < 0) return NETWORK_ERROR;
  return SERVER_ERROR;
}

StorytellerSyncClient::Error StorytellerSyncClient::setProgress(const char* bookUuid,
                                                                const StorytellerPosition& pos) {
  if (!ST_TOKEN_STORE.hasToken()) return NO_TOKEN;
  if (!ST_TOKEN_STORE.hasServerUrl()) return NO_SERVER_URL;

  char path[192];
  snprintf(path, sizeof(path), "/api/v2/books/%s/positions", bookUuid);
  char url[512];
  buildUrl(url, sizeof(url), path);
  LOG_DBG("STS", "setProgress: %s (prog=%.4f)", url, pos.totalProgression);

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  http.begin(secureClient, url);
  addAuthHeader(http);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");

  JsonDocument body;
  body["locator"]["href"] = pos.href;
  body["locator"]["type"] = "application/xhtml+xml";
  body["locator"]["locations"]["totalProgression"] = pos.totalProgression;
  body["timestamp"] = static_cast<long long>(pos.timestamp > 0 ? pos.timestamp : nowMs());
  String bodyStr;
  serializeJson(body, bodyStr);

  const int httpCode = http.POST(bodyStr);
  http.end();

  LOG_DBG("STS", "setProgress response: %d", httpCode);
  if (httpCode == 200 || httpCode == 201) return OK;
  if (httpCode == 409) return CONFLICT;
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode < 0) return NETWORK_ERROR;
  return SERVER_ERROR;
}

// --- Library ---

StorytellerSyncClient::Error StorytellerSyncClient::getBooks(StorytellerBookList& out) {
  if (!ST_TOKEN_STORE.hasToken()) return NO_TOKEN;
  if (!ST_TOKEN_STORE.hasServerUrl()) return NO_SERVER_URL;

  char url[512];
  buildUrl(url, sizeof(url), "/api/v2/books");
  LOG_DBG("STS", "getBooks: %s", url);

  WiFiClientSecure secureClient;
  secureClient.setInsecure();
  HTTPClient http;
  // HTTP/1.0 lets ArduinoJson stream-parse to avoid buffering the full list.
  http.useHTTP10(true);
  http.begin(secureClient, url);
  addAuthHeader(http);
  http.addHeader("Accept", "application/json");

  const int httpCode = http.GET();
  LOG_DBG("STS", "getBooks response: %d", httpCode);

  if (httpCode < 0) { http.end(); return NETWORK_ERROR; }
  if (httpCode == 401) { http.end(); return AUTH_FAILED; }
  if (httpCode != 200) { http.end(); return SERVER_ERROR; }

  JsonDocument filter;
  filter[0]["uuid"] = true;
  filter[0]["title"] = true;

  JsonDocument doc;
  const auto parseErr = deserializeJson(doc, secureClient, DeserializationOption::Filter(filter));
  http.end();

  if (parseErr != DeserializationError::Ok) {
    LOG_ERR("STS", "getBooks JSON parse error: %s", parseErr.c_str());
    return JSON_ERROR;
  }
  if (!doc.is<JsonArray>()) {
    LOG_ERR("STS", "getBooks: expected array");
    return JSON_ERROR;
  }

  out.count = 0;
  for (JsonObject book : doc.as<JsonArray>()) {
    if (out.count >= STORYTELLER_MAX_BOOKS) break;
    StorytellerBook& b = out.books[out.count];
    strlcpy(b.uuid, book["uuid"] | "", sizeof(b.uuid));
    strlcpy(b.title, book["title"] | "", sizeof(b.title));
    if (b.uuid[0] != '\0') out.count++;
  }

  LOG_DBG("STS", "getBooks: %d books found", out.count);
  return OK;
}

const char* StorytellerSyncClient::errorString(Error error) {
  switch (error) {
    case OK: return "Success";
    case NO_TOKEN: return "Not logged in to Storyteller";
    case NO_SERVER_URL: return "Storyteller server URL not set";
    case NETWORK_ERROR: return "Network error";
    case AUTH_FAILED: return "Authentication failed";
    case SERVER_ERROR: return "Server error (try again later)";
    case JSON_ERROR: return "JSON parse error";
    case NOT_FOUND: return "No progress found";
    case CONFLICT: return "Server position is newer";
    case PENDING: return "Authorization pending";
    case SLOW_DOWN: return "Slow down polling";
    case EXPIRED: return "Device code expired";
    case DENIED: return "Authorization denied";
    default: return "Unknown error";
  }
}
