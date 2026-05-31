#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace shared {

struct CubeSpec {
  uint8_t palette[6]
                 [4];   // Per-face RGBA (back, front, left, right, bottom, top)
  uint8_t emissive[4];  // Emissive RGBA
};

inline constexpr CubeSpec CUBE_RAINBOW = {
    .palette = {{204, 51, 51, 255},
                {51, 204, 51, 255},
                {51, 51, 204, 255},
                {204, 204, 51, 255},
                {51, 204, 204, 255},
                {204, 51, 204, 255}},
    .emissive = {0, 0, 0, 255},
};

inline constexpr CubeSpec CUBE_WHITE_EMISSIVE = {
    .palette = {{255, 255, 255, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 255},
                {255, 255, 255, 255}},
    .emissive = {255, 255, 255, 255},
};

// Tangram pieces — bright fall palette (matte).
// Match tangram_puzzle_data.h swan reference colors.
inline constexpr CubeSpec CUBE_TANGRAM_1 = {
    .palette = {{235, 145, 55, 255}, {235, 145, 55, 255}, {235, 145, 55, 255},
                {235, 145, 55, 255}, {235, 145, 55, 255}, {235, 145, 55, 255}},
    .emissive = {0, 0, 0, 255},
};
inline constexpr CubeSpec CUBE_TANGRAM_2 = {
    .palette = {{58, 118, 195, 255}, {58, 118, 195, 255}, {58, 118, 195, 255},
                {58, 118, 195, 255}, {58, 118, 195, 255}, {58, 118, 195, 255}},
    .emissive = {0, 0, 0, 255},
};
inline constexpr CubeSpec CUBE_TANGRAM_3 = {
    .palette = {{235, 130, 155, 255}, {235, 130, 155, 255}, {235, 130, 155, 255},
                {235, 130, 155, 255}, {235, 130, 155, 255}, {235, 130, 155, 255}},
    .emissive = {0, 0, 0, 255},
};
inline constexpr CubeSpec CUBE_TANGRAM_4 = {
    .palette = {{195, 65, 75, 255},  {195, 65, 75, 255},  {195, 65, 75, 255},
                {195, 65, 75, 255},  {195, 65, 75, 255},  {195, 65, 75, 255}},
    .emissive = {0, 0, 0, 255},
};
inline constexpr CubeSpec CUBE_TANGRAM_5 = {
    .palette = {{145, 95, 185, 255}, {145, 95, 185, 255}, {145, 95, 185, 255},
                {145, 95, 185, 255}, {145, 95, 185, 255}, {145, 95, 185, 255}},
    .emissive = {0, 0, 0, 255},
};
inline constexpr CubeSpec CUBE_TANGRAM_6 = {
    .palette = {{75, 165, 95, 255},  {75, 165, 95, 255},  {75, 165, 95, 255},
                {75, 165, 95, 255},  {75, 165, 95, 255},  {75, 165, 95, 255}},
    .emissive = {0, 0, 0, 255},
};
inline constexpr CubeSpec CUBE_TANGRAM_7 = {
    .palette = {{238, 205, 55, 255}, {238, 205, 55, 255}, {238, 205, 55, 255},
                {238, 205, 55, 255}, {238, 205, 55, 255}, {238, 205, 55, 255}},
    .emissive = {0, 0, 0, 255},
};

inline constexpr CubeSpec CUBE_START_MARKER = {
    .palette = {{40, 210, 90, 255},
                {40, 210, 90, 255},
                {40, 210, 90, 255},
                {40, 210, 90, 255},
                {40, 210, 90, 255},
                {40, 210, 90, 255}},
    .emissive = {40, 120, 60, 255},
};

inline constexpr CubeSpec CUBE_GOAL_MARKER = {
    .palette = {{255, 110, 40, 255},
                {255, 110, 40, 255},
                {255, 110, 40, 255},
                {255, 110, 40, 255},
                {255, 110, 40, 255},
                {255, 110, 40, 255}},
    .emissive = {140, 50, 20, 255},
};

struct AssetInfo {
  std::string_view name;
  std::string_view filename;
  float qw, qx, qy, qz;
  const CubeSpec* cubeSpec;
};

inline constexpr AssetInfo ASSETS[] = {
    {.name = "cube",
     .filename = "",
     .qw = 1.0f,
     .qx = 0.0f,
     .qy = 0.0f,
     .qz = 0.0f,
     .cubeSpec = &CUBE_RAINBOW},
    {.name = "light_cube",
     .filename = "",
     .qw = 1.0f,
     .qx = 0.0f,
     .qy = 0.0f,
     .qz = 0.0f,
     .cubeSpec = &CUBE_WHITE_EMISSIVE},
    {.name = "start_cube",
     .filename = "",
     .qw = 1.0f,
     .qx = 0.0f,
     .qy = 0.0f,
     .qz = 0.0f,
     .cubeSpec = &CUBE_START_MARKER},
    {.name = "goal_cube",
     .filename = "",
     .qw = 1.0f,
     .qx = 0.0f,
     .qy = 0.0f,
     .qz = 0.0f,
     .cubeSpec = &CUBE_GOAL_MARKER},
    {.name = "tangram_1",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_1},
    {.name = "tangram_2",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_2},
    {.name = "tangram_3",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_3},
    {.name = "tangram_4",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_4},
    {.name = "tangram_5",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_5},
    {.name = "tangram_6",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_6},
    {.name = "tangram_7",
     .filename = "",
     .qw = 1.0f, .qx = 0.0f, .qy = 0.0f, .qz = 0.0f,
     .cubeSpec = &CUBE_TANGRAM_7},
    {.name = "bear",
     .filename = "assets/bear/bear_full.obj",
     .qw = 0.0f,
     .qx = 0.0f,
     .qy = 0.70710678f,
     .qz = 0.70710678f,
     .cubeSpec = nullptr},
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
     .dirX = 0.3f,
     .dirY = 1.0f,
     .dirZ = -0.4f,
     .ambientR = 0.2f,
     .ambientG = 0.2f,
     .ambientB = 0.2f,
     .diffuseR = 0.8f,
     .diffuseG = 0.8f,
     .diffuseB = 0.8f,
     .specularR = 1.0f,
     .specularG = 1.0f,
     .specularB = 1.0f},
    {.name = "night",
     .skyboxDirectory = "assets/skybox-2",
     .dirX = 0.0f,
     .dirY = 0.0f,
     .dirZ = -1.0f,
     .ambientR = 0.02f,
     .ambientG = 0.02f,
     .ambientB = 0.05f,
     .diffuseR = 0.0f,
     .diffuseG = 0.0f,
     .diffuseB = 0.0f,
     .specularR = 0.0f,
     .specularG = 0.0f,
     .specularB = 0.0f},
    {.name = "overcast",
     .skyboxDirectory = "assets/skybox-3",
     .dirX = 0.0f,
     .dirY = -1.0f,
     .dirZ = -0.3f,
     .ambientR = 0.3f,
     .ambientG = 0.3f,
     .ambientB = 0.35f,
     .diffuseR = 0.35f,
     .diffuseG = 0.35f,
     .diffuseB = 0.4f,
     .specularR = 0.1f,
     .specularG = 0.1f,
     .specularB = 0.1f},
};

inline constexpr std::size_t SCENE_COUNT = sizeof(SCENES) / sizeof(SCENES[0]);

inline const SceneInfo* findScene(std::string_view name) {
  for (const auto& s : SCENES) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

}  // namespace shared
