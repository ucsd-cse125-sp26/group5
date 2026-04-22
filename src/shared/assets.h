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

struct SceneInfo {
  std::string_view name;
  std::string_view skyboxDirectory;
  float dirX, dirY, dirZ;
  float ambientR, ambientG, ambientB;
  float diffuseR, diffuseG, diffuseB;
  float specularR, specularG, specularB;
};

inline constexpr SceneInfo SCENES[] = {
    {.name = "sunny",
     .skyboxDirectory = "assets/skybox-1",
     .dirX = 0.3f, .dirY = 1.0f, .dirZ = 0.4f,
     .ambientR = 0.2f, .ambientG = 0.2f, .ambientB = 0.2f,
     .diffuseR = 0.8f, .diffuseG = 0.8f, .diffuseB = 0.8f,
     .specularR = 1.0f, .specularG = 1.0f, .specularB = 1.0f},
    {.name = "overcast",
     .skyboxDirectory = "assets/skybox-2",
     .dirX = 0.0f, .dirY = -1.0f, .dirZ = -0.3f,
     .ambientR = 0.3f, .ambientG = 0.3f, .ambientB = 0.35f,
     .diffuseR = 0.35f, .diffuseG = 0.35f, .diffuseB = 0.4f,
     .specularR = 0.1f, .specularG = 0.1f, .specularB = 0.1f},
    {.name = "night",
     .skyboxDirectory = "assets/skybox-3",
     .dirX = 0.0f, .dirY = 0.0f, .dirZ = -1.0f,
     .ambientR = 0.02f, .ambientG = 0.02f, .ambientB = 0.05f,
     .diffuseR = 0.0f, .diffuseG = 0.0f, .diffuseB = 0.0f,
     .specularR = 0.0f, .specularG = 0.0f, .specularB = 0.0f},
};

inline constexpr std::size_t SCENE_COUNT =
    sizeof(SCENES) / sizeof(SCENES[0]);

inline const SceneInfo* findScene(std::string_view name) {
  for (const auto& s : SCENES) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

}  // namespace shared
