#pragma once

#include <cstdint>
#include <vector>

namespace maze {

enum MazeWall : uint8_t {
  kWallNorth = 1 << 0,
  kWallEast = 1 << 1,
  kWallSouth = 1 << 2,
  kWallWest = 1 << 3,
};

constexpr uint8_t kAllWalls = kWallNorth | kWallEast | kWallSouth | kWallWest;

struct MazeCell {
  uint8_t walls = kAllWalls;
};

struct MazeLayout {
  int width = 0;
  int height = 0;
  std::vector<MazeCell> cells;
  int startX = 0;
  int startY = 0;
  int goalX = 0;
  int goalY = 0;

  [[nodiscard]] bool InBounds(int x, int y) const;
  [[nodiscard]] int Index(int x, int y) const;
  [[nodiscard]] const MazeCell& Cell(int x, int y) const;
  [[nodiscard]] MazeCell& Cell(int x, int y);
  [[nodiscard]] bool HasWall(int x, int y, MazeWall wall) const;
};

enum class MazeTile : uint8_t {
  Wall = 0,
  Floor = 1,
};

struct MazeTileGrid {
  int width = 0;
  int height = 0;
  std::vector<MazeTile> tiles;

  [[nodiscard]] bool InBounds(int x, int y) const;
  [[nodiscard]] int Index(int x, int y) const;
  [[nodiscard]] MazeTile Tile(int x, int y) const;
};

// Generates a perfect maze: every cell is reachable, with exactly one simple
// path between any two cells. The same seed always produces the same layout.
[[nodiscard]] MazeLayout GenerateMazeLayout(int width, int height,
                                            uint32_t seed);

// Expands cell-wall data into a conventional tile grid:
// MazeTile::Wall = blocked, MazeTile::Floor = walkable.
[[nodiscard]] MazeTileGrid ConvertToTileGrid(const MazeLayout& layout);

}  // namespace maze
