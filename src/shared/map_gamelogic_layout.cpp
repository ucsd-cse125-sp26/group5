#include "shared/map_gamelogic_layout.h"

#include <assimp/scene.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "shared/log.h"
#include "shared/map_format.h"
#include "shared/mesh_loader.h"
#include "shared/puzzles/tangram/puzzle_data.h"
#include "shared/puzzles/tangram/slot_layout.h"
#include "shared/puzzles/tangram/slot_validate.h"

namespace shared::map_gamelogic_layout {
namespace {

bool nodeTransform(const ParsedModel& parsed, const char* name, float& x,
                   float& y, float& z, float& sx, float& sy, float& sz) {
  const aiMatrix4x4* world = parsed.worldTransform(name);
  if (!world) return false;

  aiVector3D t;
  aiVector3D s;
  aiQuaternion r;
  world->Decompose(s, r, t);
  x = t.x;
  y = t.y;
  z = t.z;
  sx = s.x;
  sy = s.y;
  sz = s.z;
  return true;
}

bool nodePosition(const ParsedModel& parsed, const char* name, float& x,
                  float& y, float& z) {
  float sx = 0.0f;
  float sy = 0.0f;
  float sz = 0.0f;
  return nodeTransform(parsed, name, x, y, z, sx, sy, sz);
}

bool nodeYaw(const ParsedModel& parsed, const char* name, float& yaw) {
  const aiMatrix4x4* world = parsed.worldTransform(name);
  if (!world) return false;

  aiVector3D t;
  aiVector3D s;
  aiQuaternion r;
  world->Decompose(s, r, t);
  yaw = std::atan2(2.0f * (r.w * r.z + r.x * r.y),
                   1.0f - 2.0f * (r.y * r.y + r.z * r.z));
  return true;
}

bool nodeTransformAny(const ParsedModel& parsed,
                      std::initializer_list<const char*> names,
                      const char*& matchedName, float& x, float& y, float& z,
                      float& sx, float& sy, float& sz) {
  for (const char* name : names) {
    if (nodeTransform(parsed, name, x, y, z, sx, sy, sz)) {
      matchedName = name;
      return true;
    }
  }
  matchedName = nullptr;
  return false;
}

}  // namespace

bool tryApplyMazeLayoutFromMap(const ParsedModel& parsed,
                               maze_layout::Config& layout) {
  bool foundTrigger = false;
  bool foundBoard = false;
  bool foundSpawn = false;

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  if (nodePosition(parsed, kMazeTriggerNode, x, y, z)) {
    layout.triggerCenterX = x;
    layout.triggerCenterY = y;
    layout.triggerCenterZ = z;
    foundTrigger = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f)\n", kMazeTriggerNode, x, y,
           z);
  } else {
    LOG_DEBUG("[MapGamelogic] node \"%s\" not found; using layout default\n",
           kMazeTriggerNode);
  }

  if (nodePosition(parsed, kMazeBoardNode, x, y, z)) {
    layout.boardCenterX = x;
    layout.boardCenterY = y;
    layout.boardCenterZ = z;
    layout.autoBoardFromTrigger = false;
    foundBoard = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f)\n", kMazeBoardNode, x, y,
           z);
  } else {
    LOG_DEBUG("[MapGamelogic] node \"%s\" not found; board derived from trigger\n",
           kMazeBoardNode);
  }

  if (nodePosition(parsed, kPlayerStartNode, x, y, z)) {
    layout.spawnBaseX = x;
    layout.spawnBaseY = y;
    layout.spawnHeightZ = z;
    foundSpawn = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f)\n", kPlayerStartNode, x, y,
           z);
  } else {
    LOG_DEBUG("[MapGamelogic] node \"%s\" not found; using layout default\n",
           kPlayerStartNode);
  }

  // Rebecca's player_start is often below walkable mesh; use maze_trigger
  // height when the empty sits low (manual bump until Blender export is fixed).
  if (foundSpawn && foundTrigger &&
      layout.spawnHeightZ < layout.triggerCenterZ + 0.25f) {
    const float adjusted = layout.triggerCenterZ + 0.5f;
    LOG_DEBUG("[MapGamelogic] %s Z raised %.3f -> %.3f (maze_trigger floor)\n",
           kPlayerStartNode, layout.spawnHeightZ, adjusted);
    layout.spawnHeightZ = adjusted;
  }

  if (layout.autoBoardFromTrigger) {
    layout.syncBoardFromTrigger();
  }

  layout.resolveBoardPlacement();

  if (foundTrigger || foundBoard || foundSpawn) {
    LOG_DEBUG(
        "[MapGamelogic] maze layout: trigger (%.3f, %.3f) board (%.3f, %.3f, "
        "%.3f) spawn (%.3f, %.3f, %.3f)\n",
        layout.triggerCenterX, layout.triggerCenterY, layout.boardCenterX,
        layout.boardCenterY, layout.boardCenterZ, layout.spawnBaseX,
        layout.spawnBaseY, layout.spawnHeightZ);
  }

  return foundTrigger;
}

bool tryApplyTangramArenaFromMap(const ParsedModel& parsed,
                                 tangram::ArenaLayout& layout) {
  bool foundTrigger = false;
  bool foundZone = false;
  bool foundSpawn = false;

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  if (nodePosition(parsed, kSpringTriggerNode, x, y, z)) {
    layout.triggerCenterX = x;
    layout.triggerCenterY = y;
    layout.platformCenterX = x;
    layout.platformCenterY = y;
    const float topZ = z + 0.5f;
    layout.platformCenterZ = topZ - layout.platformScaleZ * 0.5f;
    layout.spawnHeightZ = topZ + 0.5f;
    foundTrigger = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f)\n", kSpringTriggerNode, x,
           y, z);
  } else {
    LOG_DEBUG(
        "[MapGamelogic] node \"%s\" not found; tangram trigger uses "
        "tangram_defaults fallback\n",
        kSpringTriggerNode);
  }

  if (nodePosition(parsed, kSpringTangramZoneNode, x, y, z)) {
    layout.boardCenterX = x;
    layout.boardCenterY = y;
    layout.boardCenterZ = z;
    foundZone = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f)\n", kSpringTangramZoneNode,
           x, y, z);
  } else {
    LOG_DEBUG(
        "[MapGamelogic] node \"%s\" not found; tangram board derived from "
        "trigger\n",
        kSpringTangramZoneNode);
  }

  if (nodePosition(parsed, kSpringPlayerStartNode, x, y, z)) {
    layout.spawnBaseX = x;
    layout.spawnBaseY = y;
    if (foundTrigger) {
      const float topZ = layout.platformTopZ();
      if (z < topZ - 0.25f) {
        LOG_DEBUG(
            "[MapGamelogic] %s Z raised %.3f -> %.3f (spring_trigger "
            "floor)\n",
            kSpringPlayerStartNode, z, topZ + 0.5f);
        layout.spawnHeightZ = topZ + 0.5f;
      } else {
        layout.spawnHeightZ = z + 0.5f;
      }
    } else {
      layout.spawnHeightZ = z + 0.5f;
    }
    foundSpawn = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f)\n", kSpringPlayerStartNode,
           x, y, z);
  }

  if (foundTrigger && !foundSpawn) {
    layout.spawnBaseX = layout.triggerCenterX;
    layout.spawnBaseY = layout.triggerCenterY - layout.halfExtent - 2.0f;
  }

  if (!foundZone && foundTrigger) {
    layout.syncBoardFromTrigger();
  }

  if (foundTrigger || foundZone || foundSpawn) {
    LOG_DEBUG(
        "[MapGamelogic] tangram layout: trigger (%.3f, %.3f) board (%.3f, "
        "%.3f, %.3f) pad spawn (%.3f, %.3f, %.3f)\n",
        layout.triggerCenterX, layout.triggerCenterY, layout.boardCenterX,
        layout.boardCenterY, layout.boardCenterZ, layout.spawnBaseX,
        layout.spawnBaseY, layout.spawnHeightZ);
  }

  return foundTrigger;
}

bool tryApplyTangramArenaFromMapFile(const std::string& path,
                                     tangram::ArenaLayout& layout) {
  ParsedModel parsed;
  if (!parsed.load(path, MAP_LOAD_FLAGS)) {
    LOG_DEBUG("[MapGamelogic] failed to load \"%s\": %s\n", path.c_str(),
           parsed.lastError().c_str());
    return false;
  }
  return tryApplyTangramArenaFromMap(parsed, layout);
}

bool tryApplyFallLayoutFromMap(const ParsedModel& parsed, FallLayout& layout) {
  bool foundTrigger = false;
  bool foundPlay = false;

  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;
  float sx = 1.0f;
  float sy = 1.0f;
  float sz = 1.0f;
  const char* matchedName = nullptr;

  if (nodeTransformAny(parsed, {kFallTriggerNode, kAutumnTriggerNode},
                       matchedName, x, y, z, sx, sy, sz)) {
    layout.triggerCenterX = x;
    layout.triggerCenterY = y;
    layout.triggerCenterZ = z;
    if (std::abs(sx) > 1.25f) layout.triggerHalfX = std::abs(sx);
    if (std::abs(sy) > 1.25f) layout.triggerHalfY = std::abs(sy);
    foundTrigger = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f) scale (%.3f, %.3f, %.3f)\n",
           matchedName, x, y, z, sx, sy, sz);
  } else {
    LOG_DEBUG(
        "[MapGamelogic] nodes \"%s\"/\"%s\" not found; fall trigger uses "
        "default\n",
        kFallTriggerNode, kAutumnTriggerNode);
  }

  if (nodeTransformAny(parsed, {kFallPlayZoneNode, kAutumnZoneNode},
                       matchedName, x, y, z, sx, sy, sz)) {
    layout.playCenterX = x;
    layout.playCenterY = y;
    layout.playCenterZ = z;
    if (std::abs(sx) > 1.25f) layout.playHalfX = std::abs(sx);
    if (std::abs(sy) > 1.25f) layout.playHalfY = std::abs(sy);
    foundPlay = true;
    LOG_DEBUG("[MapGamelogic] %s -> (%.3f, %.3f, %.3f) scale (%.3f, %.3f, %.3f)\n",
           matchedName, x, y, z, sx, sy, sz);
  } else {
    LOG_DEBUG(
        "[MapGamelogic] nodes \"%s\"/\"%s\" not found; fall play zone uses "
        "default\n",
        kFallPlayZoneNode, kAutumnZoneNode);
  }

  // Blender empties in this map have been inconsistent about Z. Keep both
  // regions on the same effective floor so the arena marker does not spawn
  // underground while still preserving the map-authored X/Y placement.
  if (foundTrigger || foundPlay) {
    const float floorZ = std::max(layout.triggerCenterZ, layout.playCenterZ);
    if (layout.triggerCenterZ < floorZ - 0.25f) {
      LOG_DEBUG("[MapGamelogic] fall trigger Z raised %.3f -> %.3f\n",
             layout.triggerCenterZ, floorZ);
      layout.triggerCenterZ = floorZ;
    }
    if (layout.playCenterZ < floorZ - 0.25f) {
      LOG_DEBUG("[MapGamelogic] fall play Z raised %.3f -> %.3f\n",
             layout.playCenterZ, floorZ);
      layout.playCenterZ = floorZ;
    }
  }

  layout.spawnHeight = layout.playCenterZ + 20.0f;

  if (foundTrigger || foundPlay) {
    LOG_DEBUG(
        "[MapGamelogic] fall layout: trigger (%.3f, %.3f, %.3f) half (%.3f, "
        "%.3f) play (%.3f, %.3f, %.3f) half (%.3f, %.3f) spawnHeight %.3f\n",
        layout.triggerCenterX, layout.triggerCenterY, layout.triggerCenterZ,
        layout.triggerHalfX, layout.triggerHalfY, layout.playCenterX,
        layout.playCenterY, layout.playCenterZ, layout.playHalfX,
        layout.playHalfY, layout.spawnHeight);
  }

  return foundTrigger || foundPlay;
}

bool tryApplyFallLayoutFromMapFile(const std::string& path,
                                   FallLayout& layout) {
  ParsedModel parsed;
  if (!parsed.load(path, MAP_LOAD_FLAGS)) {
    LOG_DEBUG("[MapGamelogic] failed to load \"%s\": %s\n", path.c_str(),
           parsed.lastError().c_str());
    return false;
  }
  return tryApplyFallLayoutFromMap(parsed, layout);
}

bool tryApplyTangramSlotsFromMap(const ParsedModel& parsed, float boardCenterX,
                                 float boardCenterY,
                                 tangram_slot::Config& layout) {
  int found = 0;
  for (int i = 0; i < 7; ++i) {
    const char* name = nullptr;
    switch (i) {
      case 0:
        name = "spring_tangram_slot_1";
        break;
      case 1:
        name = "spring_tangram_slot_2";
        break;
      case 2:
        name = "spring_tangram_slot_3";
        break;
      case 3:
        name = "spring_tangram_slot_4";
        break;
      case 4:
        name = "spring_tangram_slot_5";
        break;
      case 5:
        name = "spring_tangram_slot_6";
        break;
      case 6:
        name = "spring_tangram_slot_7";
        break;
      default:
        break;
    }
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float yaw = 0.0f;
    if (!nodePosition(parsed, name, x, y, z) || !nodeYaw(parsed, name, yaw)) {
      continue;
    }
    layout.slots[i].relX = x - boardCenterX;
    layout.slots[i].relY = y - boardCenterY;
    layout.slots[i].rotRad =
        shared::tangram_puzzle::quantizeYawToRotateStep(yaw);
    layout.slots[i].valid = true;
    ++found;
    LOG_DEBUG("[MapGamelogic] %s -> rel (%.3f, %.3f) yaw %.3f\n", name,
           layout.slots[i].relX, layout.slots[i].relY, layout.slots[i].rotRad);
  }
  if (found > 0 && found < 7) {
    LOG_DEBUG(
        "[MapGamelogic] tangram slots: only %d/7 empties — ignoring map "
        "(need all spring_tangram_slot_1..7 or none)\n",
        found);
    for (auto& slot : layout.slots) {
      slot = {};
    }
    layout.anyFromMap = false;
    return false;
  }

  layout.anyFromMap = found == 7;
  std::array<shared::tangram_slot::SlotPose, 7> loaded{};
  for (int i = 0; i < 7; ++i) {
    loaded[static_cast<size_t>(i)] = layout.slots[i];
  }
  if (layout.anyFromMap &&
      shared::tangram_slot_validate::slotPosesOverlap(loaded)) {
    LOG_DEBUG(
        "[MapGamelogic] tangram slots overlap in map — ignoring map layout "
        "(fix empty positions or remove spring_tangram_slot_*)\n");
    for (auto& slot : layout.slots) {
      slot = {};
    }
    layout.anyFromMap = false;
    return false;
  }

  if (layout.anyFromMap) {
    LOG_DEBUG("[MapGamelogic] tangram slots from map: 7/7 (no overlap)\n");
  }
  return layout.anyFromMap;
}

bool tryApplyMazeLayoutFromMapFile(const std::string& path,
                                   maze_layout::Config& layout) {
  ParsedModel parsed;
  if (!parsed.load(path, MAP_LOAD_FLAGS)) {
    LOG_DEBUG("[MapGamelogic] failed to load \"%s\": %s\n", path.c_str(),
           parsed.lastError().c_str());
    return false;
  }
  return tryApplyMazeLayoutFromMap(parsed, layout);
}

bool tryApplyFallenHouseRegionFromMap(const ParsedModel& parsed,
                                      FallenHouseRegion& region) {
  region.valid = false;
  const aiScene* scene = parsed.scene();
  if (!scene || !scene->mRootNode) return false;

  const aiNode* node = scene->mRootNode->FindNode(aiString(kFallenHouseNode));
  const aiMatrix4x4* world = parsed.worldTransform(kFallenHouseNode);
  if (!node || !world) {
    LOG_DEBUG("[MapGamelogic] node \"%s\" not found; credits trigger disabled\n",
           kFallenHouseNode);
    return false;
  }

  // flattenNodeGeometry returns mesh-local positions; lift them to world space
  // (the same space player Positions live in) before bounding.
  std::vector<aiVector3D> verts;
  std::vector<uint32_t> indices;
  flattenNodeGeometry(*node, *scene, verts, indices);
  if (verts.empty()) {
    LOG_DEBUG("[MapGamelogic] node \"%s\" has no geometry; credits trigger off\n",
           kFallenHouseNode);
    return false;
  }

  float minX = FLT_MAX, minY = FLT_MAX, minZ = FLT_MAX;
  float maxX = -FLT_MAX, maxY = -FLT_MAX, maxZ = -FLT_MAX;
  for (const aiVector3D& local : verts) {
    const aiVector3D p = (*world) * local;
    minX = std::min(minX, p.x);
    maxX = std::max(maxX, p.x);
    minY = std::min(minY, p.y);
    maxY = std::max(maxY, p.y);
    minZ = std::min(minZ, p.z);
    maxZ = std::max(maxZ, p.z);
  }

  // Pad so players standing in the doorway/threshold still count as inside.
  constexpr float kPad = 1.5f;
  region.minX = minX - kPad;
  region.maxX = maxX + kPad;
  region.minY = minY - kPad;
  region.maxY = maxY + kPad;
  region.minZ = minZ - kPad;
  region.maxZ = maxZ + kPad;
  region.valid = true;
  LOG_DEBUG(
      "[MapGamelogic] Fallen house region -> X[%.2f,%.2f] Y[%.2f,%.2f] "
      "Z[%.2f,%.2f]\n",
      region.minX, region.maxX, region.minY, region.maxY, region.minZ,
      region.maxZ);
  return true;
}

}  // namespace shared::map_gamelogic_layout
