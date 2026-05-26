#include <gtest/gtest.h>

#include <array>
#include <queue>
#include <stdexcept>
#include <utility>
#include <vector>

#include "server/game/maze_generation.h"

namespace {

struct Neighbor {
  int dx = 0;
  int dy = 0;
  maze::MazeWall wall;
  maze::MazeWall opposite;
};

constexpr std::array<Neighbor, 4> kNeighbors{{
    {.dx = 0, .dy = 1, .wall = maze::kWallNorth, .opposite = maze::kWallSouth},
    {.dx = 1, .dy = 0, .wall = maze::kWallEast, .opposite = maze::kWallWest},
    {.dx = 0, .dy = -1, .wall = maze::kWallSouth, .opposite = maze::kWallNorth},
    {.dx = -1, .dy = 0, .wall = maze::kWallWest, .opposite = maze::kWallEast},
}};

std::vector<std::pair<int, int>> ConnectedNeighbors(const maze::MazeLayout& m,
                                                    int x, int y) {
  std::vector<std::pair<int, int>> result;
  for (const auto& n : kNeighbors) {
    const int nx = x + n.dx;
    const int ny = y + n.dy;
    if (!m.InBounds(nx, ny)) continue;
    if (!m.HasWall(x, y, n.wall)) result.emplace_back(nx, ny);
  }
  return result;
}

int CountReachableCells(const maze::MazeLayout& m) {
  std::vector<bool> visited(static_cast<size_t>(m.width * m.height), false);
  std::queue<std::pair<int, int>> frontier;
  frontier.emplace(m.startX, m.startY);
  visited[static_cast<size_t>(m.Index(m.startX, m.startY))] = true;

  int count = 0;
  while (!frontier.empty()) {
    const auto [x, y] = frontier.front();
    frontier.pop();
    count++;

    for (const auto& [nx, ny] : ConnectedNeighbors(m, x, y)) {
      const int index = m.Index(nx, ny);
      if (visited[static_cast<size_t>(index)]) continue;
      visited[static_cast<size_t>(index)] = true;
      frontier.emplace(nx, ny);
    }
  }

  return count;
}

}  // namespace

TEST(MazeGeneration, SameSeedProducesSameWalls) {
  const maze::MazeLayout a = maze::GenerateMazeLayout(8, 8, 12505);
  const maze::MazeLayout b = maze::GenerateMazeLayout(8, 8, 12505);

  ASSERT_EQ(a.cells.size(), b.cells.size());
  for (size_t i = 0; i < a.cells.size(); ++i) {
    EXPECT_EQ(a.cells[i].walls, b.cells[i].walls);
  }
}

TEST(MazeGeneration, GeneratedMazeIsFullyConnected) {
  const maze::MazeLayout layout = maze::GenerateMazeLayout(8, 8, 7);

  EXPECT_EQ(CountReachableCells(layout), layout.width * layout.height);
  EXPECT_EQ(layout.startX, 0);
  EXPECT_EQ(layout.startY, 0);
  EXPECT_EQ(layout.goalX, layout.width - 1);
  EXPECT_EQ(layout.goalY, layout.height - 1);
}

TEST(MazeGeneration, OpenWallsAreReciprocal) {
  const maze::MazeLayout layout = maze::GenerateMazeLayout(6, 5, 99);

  for (int y = 0; y < layout.height; ++y) {
    for (int x = 0; x < layout.width; ++x) {
      for (const auto& n : kNeighbors) {
        const int nx = x + n.dx;
        const int ny = y + n.dy;
        if (!layout.InBounds(nx, ny)) continue;

        const bool wallHere = layout.HasWall(x, y, n.wall);
        const bool wallThere = layout.HasWall(nx, ny, n.opposite);
        EXPECT_EQ(wallHere, wallThere) << "between (" << x << ", " << y
                                       << ") and (" << nx << ", " << ny << ")";
      }
    }
  }
}

TEST(MazeGeneration, TileGridContainsFloorsForOpenPassages) {
  const maze::MazeLayout layout = maze::GenerateMazeLayout(4, 3, 42);
  const maze::MazeTileGrid grid = maze::ConvertToTileGrid(layout);

  EXPECT_EQ(grid.width, layout.width * 2 + 1);
  EXPECT_EQ(grid.height, layout.height * 2 + 1);

  for (int y = 0; y < layout.height; ++y) {
    for (int x = 0; x < layout.width; ++x) {
      const int tx = x * 2 + 1;
      const int ty = y * 2 + 1;
      EXPECT_EQ(grid.Tile(tx, ty), maze::MazeTile::Floor);

      if (!layout.HasWall(x, y, maze::kWallNorth))
        EXPECT_EQ(grid.Tile(tx, ty + 1), maze::MazeTile::Floor);
      if (!layout.HasWall(x, y, maze::kWallEast))
        EXPECT_EQ(grid.Tile(tx + 1, ty), maze::MazeTile::Floor);
      if (!layout.HasWall(x, y, maze::kWallSouth))
        EXPECT_EQ(grid.Tile(tx, ty - 1), maze::MazeTile::Floor);
      if (!layout.HasWall(x, y, maze::kWallWest))
        EXPECT_EQ(grid.Tile(tx - 1, ty), maze::MazeTile::Floor);
    }
  }
}

TEST(MazeGeneration, RejectsInvalidDimensions) {
  EXPECT_THROW(
      {
        const auto layout = maze::GenerateMazeLayout(0, 5, 1);
        (void)layout;
      },
      std::invalid_argument);
  EXPECT_THROW(
      {
        const auto layout = maze::GenerateMazeLayout(5, -1, 1);
        (void)layout;
      },
      std::invalid_argument);
}
