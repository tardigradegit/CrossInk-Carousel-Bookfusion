#include "StorytellerTokenStore.h"

#include <HalStorage.h>
#include <Logging.h>

#include "StorytellerJsonIO.h"

StorytellerTokenStore StorytellerTokenStore::instance;

namespace {
constexpr char ST_FILE_JSON[] = "/.crosspoint/storyteller.json";
}  // namespace

bool StorytellerTokenStore::saveToFile() const {
  Storage.mkdir("/.crosspoint");
  return StorytellerJsonIO::save(*this, ST_FILE_JSON);
}

bool StorytellerTokenStore::loadFromFile() {
  if (!Storage.exists(ST_FILE_JSON)) {
    LOG_DBG("STS", "No Storyteller token file found");
    return false;
  }

  String json = Storage.readFile(ST_FILE_JSON);
  if (json.isEmpty()) {
    LOG_DBG("STS", "Storyteller token file is empty");
    return false;
  }

  return StorytellerJsonIO::load(*this, json.c_str());
}

void StorytellerTokenStore::setToken(const std::string& token) {
  accessToken = token;
  LOG_DBG("STS", "Storyteller token set (%zu chars)", token.size());
}

void StorytellerTokenStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  LOG_DBG("STS", "Storyteller server URL: %s", url.c_str());
}

void StorytellerTokenStore::clearToken() {
  accessToken.clear();
  saveToFile();
  LOG_DBG("STS", "Storyteller token cleared");
}
