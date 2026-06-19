#pragma once

#include <string>

// Forward-declare the I/O helpers so friend declarations below compile
// without pulling in StorytellerJsonIO.h (which would create a circular
// include between TokenStore ↔ JsonIO).
namespace StorytellerJsonIO {
bool save(const class StorytellerTokenStore&, const char*);
bool load(class StorytellerTokenStore&, const char*);
}  // namespace StorytellerJsonIO

class StorytellerTokenStore {
 public:
  static StorytellerTokenStore& getInstance() { return instance; }

  StorytellerTokenStore(const StorytellerTokenStore&) = delete;
  StorytellerTokenStore& operator=(const StorytellerTokenStore&) = delete;

  bool saveToFile() const;
  bool loadFromFile();

  void setToken(const std::string& token);
  void setServerUrl(const std::string& url);
  void clearToken();

  const std::string& getToken() const { return accessToken; }
  const std::string& getServerUrl() const { return serverUrl; }
  bool hasToken() const { return !accessToken.empty(); }
  bool hasServerUrl() const { return !serverUrl.empty(); }

 private:
  StorytellerTokenStore() = default;
  static StorytellerTokenStore instance;

  std::string accessToken;
  std::string serverUrl;

  friend bool StorytellerJsonIO::save(const StorytellerTokenStore&, const char*);
  friend bool StorytellerJsonIO::load(StorytellerTokenStore&, const char*);
};

#define ST_TOKEN_STORE StorytellerTokenStore::getInstance()
