#include "StorytellerJsonIO.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <ObfuscationUtils.h>

#include "StorytellerTokenStore.h"

namespace StorytellerJsonIO {

bool save(const StorytellerTokenStore& store, const char* path) {
  JsonDocument doc;
  doc["token_obf"] = obfuscation::obfuscateToBase64(store.accessToken);
  doc["server_url"] = store.serverUrl;

  String json;
  serializeJson(doc, json);
  return Storage.writeFile(path, json);
}

bool load(StorytellerTokenStore& store, const char* json) {
  JsonDocument doc;
  auto error = deserializeJson(doc, json);
  if (error) {
    LOG_ERR("STJsonIO", "JSON parse error: %s", error.c_str());
    return false;
  }

  bool ok = false;
  store.accessToken = obfuscation::deobfuscateFromBase64(doc["token_obf"] | "", &ok);
  if (!ok) {
    store.accessToken.clear();
    return false;
  }

  store.serverUrl = doc["server_url"] | "";
  LOG_DBG("STJsonIO", "Loaded Storyteller token");
  return true;
}

}  // namespace StorytellerJsonIO
