#include "shared/log.h"

#include <cstdlib>

namespace shared::log {

bool debugEnabled = false;

void setDebugEnabled(bool enabled) { debugEnabled = enabled; }

void initFromEnvironment() {
  const char* v = std::getenv("CSE125_DEBUG_LOG");
  if (v && v[0] == '1' && v[1] == '\0') debugEnabled = true;
}

}  // namespace shared::log
