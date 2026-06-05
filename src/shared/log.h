#pragma once

#include <cstdio>

namespace shared::log {

extern bool debugEnabled;
void setDebugEnabled(bool enabled);
void initFromEnvironment();

}  // namespace shared::log

#define LOG_DEBUG(...)                                       \
  do {                                                       \
    if (::shared::log::debugEnabled) std::printf(__VA_ARGS__); \
  } while (0)
