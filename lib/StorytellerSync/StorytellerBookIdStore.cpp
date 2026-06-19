#include "StorytellerBookIdStore.h"

#include <ArduinoJson.h>
#include <HalStorage.h>
#include <Logging.h>
#include <MD5Builder.h>

#include <cstdio>
#include <cstring>

void StorytellerBookIdStore::buildSidecarPath(const char* epubPath, char* outPath, size_t maxLen) {
  MD5Builder md5;
  md5.begin();
  md5.add(epubPath);
  md5.calculate();
  // Result: /.crosspoint/storyteller_<32hexchars>.json  (57 chars total)
  snprintf(outPath, maxLen, "/.crosspoint/storyteller_%s.json", md5.toString().c_str());
}

bool StorytellerBookIdStore::loadBookUuid(const char* epubPath, char* outUuid, size_t maxLen) {
  char sidecarPath[64];
  buildSidecarPath(epubPath, sidecarPath, sizeof(sidecarPath));

  if (!Storage.exists(sidecarPath)) {
    return false;
  }

  String json = Storage.readFile(sidecarPath);
  if (json.isEmpty()) {
    return false;
  }

  JsonDocument doc;
  if (deserializeJson(doc, json) != DeserializationError::Ok) {
    LOG_ERR("STBookIdStore", "Sidecar JSON parse error: %s", sidecarPath);
    return false;
  }

  const char* uuid = doc["book_uuid"] | "";
  if (uuid[0] == '\0') {
    return false;
  }

  strlcpy(outUuid, uuid, maxLen);
  LOG_DBG("STBookIdStore", "Loaded uuid=%s for %s", outUuid, epubPath);
  return true;
}

bool StorytellerBookIdStore::saveBookUuid(const char* epubPath, const char* uuid) {
  if (uuid == nullptr || uuid[0] == '\0') {
    LOG_ERR("STBookIdStore", "Refusing to save empty uuid for %s", epubPath);
    return false;
  }

  char sidecarPath[64];
  buildSidecarPath(epubPath, sidecarPath, sizeof(sidecarPath));
  Storage.mkdir("/.crosspoint");

  JsonDocument doc;
  doc["book_uuid"] = uuid;

  String json;
  serializeJson(doc, json);

  const bool ok = Storage.writeFile(sidecarPath, json);
  if (ok) {
    LOG_DBG("STBookIdStore", "Saved uuid=%s for %s", uuid, epubPath);
  } else {
    LOG_ERR("STBookIdStore", "Failed to save sidecar: %s", sidecarPath);
  }
  return ok;
}
