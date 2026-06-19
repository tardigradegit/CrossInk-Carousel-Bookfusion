#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

struct StorytellerPosition {
  std::string href;              // EPUB spine-item href, e.g. "OEBPS/chapter3.xhtml"
  float totalProgression = 0.0f; // 0.0–1.0 whole-book position
  uint64_t timestamp = 0;        // milliseconds since epoch (0 = use current time)
};

struct StorytellerDeviceCodeResponse {
  char deviceCode[256];
  char userCode[32];
  char verificationUri[256];
  char verificationUriComplete[512];
  int interval;    // seconds between polls
  int expiresIn;   // seconds until code expires
};

static constexpr int STORYTELLER_MAX_BOOKS = 20;

struct StorytellerBook {
  char uuid[64];
  char title[128];
};

struct StorytellerBookList {
  StorytellerBook books[STORYTELLER_MAX_BOOKS];
  int count = 0;
};

class StorytellerSyncClient {
 public:
  enum Error {
    OK,
    NO_TOKEN,
    NO_SERVER_URL,
    NETWORK_ERROR,
    AUTH_FAILED,
    SERVER_ERROR,
    JSON_ERROR,
    NOT_FOUND,
    CONFLICT,    // 409: server position is newer
    PENDING,
    SLOW_DOWN,
    EXPIRED,
    DENIED,
  };

  // OAuth device-code flow
  static Error requestDeviceCode(StorytellerDeviceCodeResponse& out);
  static Error pollForToken(const char* deviceCode, char* outToken, size_t tokenMaxLen);

  // Reading progress
  static Error getProgress(const char* bookUuid, StorytellerPosition& out);
  static Error setProgress(const char* bookUuid, const StorytellerPosition& pos);

  // Library (used for book UUID resolution)
  static Error getBooks(StorytellerBookList& out);

  static const char* errorString(Error error);

 private:
  static void buildUrl(char* buf, size_t maxLen, const char* path);
};
