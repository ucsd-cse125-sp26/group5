#include "util.h"

#include <cstdio>
#include <cstdlib>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

std::filesystem::path exeDir() {
#if defined(_WIN32)
  wchar_t buf[MAX_PATH];
  DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
  if (len == 0 || len == MAX_PATH) {
    fprintf(stderr, "Failed to get executable path\n");
    exit(EXIT_FAILURE);
  }
  return std::filesystem::path(buf, buf + len).parent_path();
#elif defined(__APPLE__)
  char buf[4096];
  uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) {
    fprintf(stderr, "Failed to get executable path\n");
    exit(EXIT_FAILURE);
  }
  return std::filesystem::canonical(buf).parent_path();
#else
  return std::filesystem::canonical("/proc/self/exe").parent_path();
#endif
}
