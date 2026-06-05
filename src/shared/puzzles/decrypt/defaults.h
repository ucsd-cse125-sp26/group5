#pragma once

#include <algorithm>
#include <cctype>
#include <string>

namespace shared::decrypt {

inline constexpr const char* kExpectedAnswer =
    "so far to go, but so far you've gone";

inline std::string normalizeAnswer(std::string s) {
  auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
  while (!s.empty() && isSpace(static_cast<unsigned char>(s.front()))) {
    s.erase(s.begin());
  }
  while (!s.empty() && isSpace(static_cast<unsigned char>(s.back()))) {
    s.pop_back();
  }
  std::string out;
  out.reserve(s.size());
  bool prevSpace = false;
  for (size_t i = 0; i < s.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
        (static_cast<unsigned char>(s[i + 2]) == 0x99 ||
         static_cast<unsigned char>(s[i + 2]) == 0x98)) {
      out.push_back('\'');
      prevSpace = false;
      i += 2;
      continue;
    }
    if (isSpace(c)) {
      if (!prevSpace && !out.empty()) {
        out.push_back(' ');
        prevSpace = true;
      }
      continue;
    }
    out.push_back(static_cast<char>(std::tolower(c)));
    prevSpace = false;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  return out;
}

inline bool answersMatch(const std::string& submitted) {
  return normalizeAnswer(submitted) == normalizeAnswer(kExpectedAnswer);
}

}  // namespace shared::decrypt
