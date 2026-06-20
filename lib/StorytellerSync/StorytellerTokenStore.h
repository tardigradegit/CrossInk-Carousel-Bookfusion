#pragma once

#include <string>

// Forward-declare at global scope first so the namespace block below can
// reference ::StorytellerTokenStore without creating a nested class.
class StorytellerTokenStore;

namespace StorytellerJsonIO {
bool save(const StorytellerTokenStore&, const char*);
bool load(StorytellerTokenStore&, const char*);
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
