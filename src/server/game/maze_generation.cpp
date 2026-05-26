#include "server/game/maze_generation.h"

#include <algorithm>
#include <array>
#include <stdexcept>
#include <utility>

namespace maze {
namespace {

struct Direction {
  int dx = 0;
  int dy = 0;
  MazeWall wall;
  MazeWall opposite;
};

constexpr std::array<Direction, 4> kDirections{{
    {.dx = 0, .dy = 1, .wall = kWallNorth, .opposite = kWallSouth},
    {.dx = 1, .dy = 0, .wall = kWallEast, .opposite = kWallWest},
    {.dx = 0, .dy = -1, .wall = kWallSouth, .opposite = kWallNorth},
    {.dx = -1, .dy = 0, .wall = kWallWest, .opposite = kWallEast},
}};

uint32_t NextRandom(uint32_t& state) {
  // Numerical Recipes LCG: small, deterministic, and enough for layout
  // shuffling without pulling gameplay code into std::random_device behavior.
  state = state * 1664525u + 1013904223u;
  return state;
}

}  // namespace

bool MazeLayout::InBounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < width && y < height;
}

int MazeLayout::Index(int x, int y) const { return y * width + x; }

const MazeCell& MazeLayout::Cell(int x, int y) const {
  return cells[static_cast<size_t>(Index(x, y))];
}

MazeCell& MazeLayout::Cell(int x, int y) {
  return cells[static_cast<size_t>(Index(x, y))];
}

bool MazeLayout::HasWall(int x, int y, MazeWall wall) const {
  return (Cell(x, y).walls & wall) != 0;
}

bool MazeTileGrid::InBounds(int x, int y) const {
  return x >= 0 && y >= 0 && x < width && y < height;
}

int MazeTileGrid::Index(int x, int y) const { return y * width + x; }

MazeTile MazeTileGrid::Tile(int x, int y) const {
  return tiles[static_cast<size_t>(Index(x, y))];
}

MazeLayout GenerateMazeLayout(int width, int height, uint32_t seed) {
  if (width <= 0 || height <= 0) {
    throw std::invalid_argument("maze dimensions must be positive");
  }

  MazeLayout layout;
  layout.width = width;
  layout.height = height;
  layout.cells.assign(static_cast<size_t>(width * height), MazeCell{});
  layout.goalX = width - 1;
  layout.goalY = height - 1;

  std::vector<bool> visited(static_cast<size_t>(width * height), false);
  std::vector<std::pair<int, int>> stack;
  stack.emplace_back(layout.startX, layout.startY);
  visited[static_cast<size_t>(layout.Index(layout.startX, layout.startY))] =
      true;

  uint32_t randomState = seed == 0 ? 1u : seed;
  while (!stack.empty()) {
    const auto [cx, cy] = stack.back();
    std::array<Direction, 4> dirs = kDirections;
    for (int i = static_cast<int>(dirs.size()) - 1; i > 0; --i) {
      const int j = static_cast<int>(NextRandom(randomState) %
                                     static_cast<uint32_t>(i + 1));
      std::swap(dirs[static_cast<size_t>(i)], dirs[static_cast<size_t>(j)]);
    }

    bool advanced = false;
    for (const auto& dir : dirs) {
      const int nx = cx + dir.dx;
      const int ny = cy + dir.dy;
      if (!layout.InBounds(nx, ny)) continue;

      const int neighborIndex = layout.Index(nx, ny);
      if (visited[static_cast<size_t>(neighborIndex)]) continue;

      layout.Cell(cx, cy).walls &= static_cast<uint8_t>(~dir.wall);
      layout.Cell(nx, ny).walls &= static_cast<uint8_t>(~dir.opposite);
      visited[static_cast<size_t>(neighborIndex)] = true;
      stack.emplace_back(nx, ny);
      advanced = true;
      break;
    }

    if (!advanced) stack.pop_back();
  }

  return layout;
}

MazeTileGrid ConvertToTileGrid(const MazeLayout& layout) {
  if (layout.width <= 0 || layout.height <= 0 ||
      layout.cells.size() !=
          static_cast<size_t>(layout.width * layout.height)) {
    throw std::invalid_argument("invalid maze layout");
  }

  MazeTileGrid grid;
  grid.width = layout.width * 2 + 1;
  grid.height = layout.height * 2 + 1;
  grid.tiles.assign(static_cast<size_t>(grid.width * grid.height),
                    MazeTile::Wall);

  auto carve = [&grid](int x, int y) {
    grid.tiles[static_cast<size_t>(grid.Index(x, y))] = MazeTile::Floor;
  };

  for (int y = 0; y < layout.height; ++y) {
    for (int x = 0; x < layout.width; ++x) {
      const int tx = x * 2 + 1;
      const int ty = y * 2 + 1;
      carve(tx, ty);
      if (!layout.HasWall(x, y, kWallNorth)) carve(tx, ty + 1);
      if (!layout.HasWall(x, y, kWallEast)) carve(tx + 1, ty);
      if (!layout.HasWall(x, y, kWallSouth)) carve(tx, ty - 1);
      if (!layout.HasWall(x, y, kWallWest)) carve(tx - 1, ty);
    }
  }

  return grid;
}

}  // namespace maze
