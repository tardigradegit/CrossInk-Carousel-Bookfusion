#pragma once

class StorytellerTokenStore;

namespace StorytellerJsonIO {
bool save(const StorytellerTokenStore& store, const char* path);
bool load(StorytellerTokenStore& store, const char* json);
}  // namespace StorytellerJsonIO
