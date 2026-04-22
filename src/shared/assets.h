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
    {.name = "cube",
     .filename = "",
     .qw = 1.0f,
     .qx = 0.0f,
     .qy = 0.0f,
     .qz = 0.0f},
    {.name = "bear",
     .filename = "assets/bear/bear_full.obj",
     .qw = 0.0f,
     .qx = 0.0f,
     .qy = 0.70710678f,
     .qz = 0.70710678f},
};

inline constexpr std::size_t ASSET_COUNT = sizeof(ASSETS) / sizeof(ASSETS[0]);

inline const AssetInfo* findAsset(std::string_view name) {
  for (const auto& i : ASSETS) {
    if (i.name == name) return &i;
  }
  return nullptr;
}

struct SkyboxInfo {
  std::string_view name;
  std::string_view directory;  // contains px.png, nx.png, py.png, ny.png, pz.png, nz.png
};

inline constexpr SkyboxInfo SKYBOXES[] = {
    {.name = "default", .directory = "assets/skybox-1"},
};

inline constexpr std::size_t SKYBOX_COUNT =
    sizeof(SKYBOXES) / sizeof(SKYBOXES[0]);

inline const SkyboxInfo* findSkybox(std::string_view name) {
  for (const auto& s : SKYBOXES) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

}  // namespace shared
