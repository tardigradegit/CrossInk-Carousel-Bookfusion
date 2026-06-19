#pragma once

#include <cstddef>

class StorytellerBookIdStore {
 public:
  // Load the Storyteller UUID for the given epub path into outUuid.
  // Returns true on success, false if the book has no linked UUID.
  static bool loadBookUuid(const char* epubPath, char* outUuid, size_t maxLen);

  // Persist a UUID for an epub path. Returns false on I/O error or empty uuid.
  static bool saveBookUuid(const char* epubPath, const char* uuid);

 private:
  static void buildSidecarPath(const char* epubPath, char* outPath, size_t maxLen);
};
