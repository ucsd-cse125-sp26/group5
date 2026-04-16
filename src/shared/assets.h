#pragma once

#include <cstddef>
#include <string_view>

namespace shared {

struct AssetInfo {
  std::string_view name;
  std::string_view filename;
  float qw, qx, qy, qz;
};

inline constexpr AssetInfo ASSETS[] = {
    {"cube", "", 1.0f, 0.0f, 0.0f, 0.0f},
    {"bear", "assets/bear/bear_full.obj", 0.0f, 0.0f, 0.70710678f, 0.70710678f},
};

inline constexpr std::size_t ASSET_COUNT = sizeof(ASSETS) / sizeof(ASSETS[0]);

inline const AssetInfo* findAsset(std::string_view name) {
  for (std::size_t i = 0; i < ASSET_COUNT; ++i) {
    if (ASSETS[i].name == name) return &ASSETS[i];
  }
  return nullptr;
}

}  // namespace shared
