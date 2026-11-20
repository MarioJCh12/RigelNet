/* Copyright (C) 2026, RigelEngine deathmatch contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "deathmatch_spawn_points.hpp"

#include "engine/physical_components.hpp"
#include "game_logic/player/components.hpp"

#include <algorithm>
#include <optional>
#include <queue>


namespace rigel::game_logic
{

namespace
{

using data::map::SolidEdge;

constexpr auto MIN_SPAWN_POINTS_FROM_ITEMS = 8;
constexpr auto MIN_SPAWN_POINT_DISTANCE = 4;


base::Rect<int> playerBoxAt(const base::Vec2& position)
{
  return engine::toWorldSpace(DEFAULT_PLAYER_BOUNDS, position);
}


bool isInsideMap(const data::map::Map& map, const base::Rect<int>& box)
{
  return box.left() >= 0 && box.top() >= 0 && box.right() < map.width() &&
    box.bottom() < map.height();
}


/** A tile the player can't move through.
 *
 * One-way platforms are only solid on top, and can be passed from below,
 * so they don't count.
 */
bool isBlocking(const data::map::Map& map, const int x, const int y)
{
  const auto& collision = map.collisionData(x, y);
  return collision.isSolidOn(SolidEdge::left()) ||
    collision.isSolidOn(SolidEdge::right()) ||
    collision.isSolidOn(SolidEdge::bottom());
}


bool isInOpenableArea(
  const std::vector<base::Rect<int>>& openableAreas,
  const int x,
  const int y)
{
  return std::any_of(
    openableAreas.begin(),
    openableAreas.end(),
    [&](const base::Rect<int>& area) {
      return area.containsPoint({x, y});
    });
}


bool isPassable(
  const data::map::Map& map,
  const std::vector<base::Rect<int>>& openableAreas,
  const base::Vec2& position)
{
  const auto box = playerBoxAt(position);
  if (!isInsideMap(map, box))
  {
    return false;
  }

  for (auto y = box.top(); y <= box.bottom(); ++y)
  {
    for (auto x = box.left(); x <= box.right(); ++x)
    {
      if (isBlocking(map, x, y) && !isInOpenableArea(openableAreas, x, y))
      {
        return false;
      }
    }
  }

  return true;
}


std::optional<base::Vec2> findSpawnSpotNear(
  const data::map::Map& map,
  const ReachabilityMap& reachability,
  const base::Vec2& position)
{
  static constexpr int Y_OFFSETS[] = {0, 1, -1, 2, -2, 3, -3, 4, 5, 6};
  static constexpr int X_OFFSETS[] = {0, -1, -2, 1};

  for (const auto dy : Y_OFFSETS)
  {
    for (const auto dx : X_OFFSETS)
    {
      const auto candidate = position + base::Vec2{dx, dy};
      if (
        reachability.isReachable(candidate) &&
        isValidSpawnSpot(map, candidate))
      {
        return candidate;
      }
    }
  }

  return std::nullopt;
}


int distanceSquared(const base::Vec2& a, const base::Vec2& b)
{
  const auto d = a - b;
  return d.x * d.x + d.y * d.y;
}

} // namespace


ReachabilityMap::ReachabilityMap(
  const data::map::Map& map,
  const base::Vec2& start,
  const std::vector<base::Rect<int>>& openableAreas)
  : mReachable(size_t(map.width()) * size_t(map.height()), false)
  , mWidth(map.width())
  , mHeight(map.height())
{
  if (!isPassable(map, openableAreas, start))
  {
    return;
  }

  std::queue<base::Vec2> toVisit;
  auto visit = [&](const base::Vec2& position) {
    const auto index = size_t(position.y) * size_t(mWidth) + size_t(position.x);
    if (mReachable[index] || !isPassable(map, openableAreas, position))
    {
      return;
    }

    mReachable[index] = true;
    ++mNumReachable;
    toVisit.push(position);
  };

  visit(start);

  while (!toVisit.empty())
  {
    const auto position = toVisit.front();
    toVisit.pop();

    visit(position + base::Vec2{1, 0});
    visit(position + base::Vec2{-1, 0});
    visit(position + base::Vec2{0, 1});
    visit(position + base::Vec2{0, -1});
  }
}


bool isPlayerPositionFree(const data::map::Map& map, const base::Vec2& position)
{
  return isPassable(map, {}, position);
}


bool ReachabilityMap::isReachable(const base::Vec2& position) const
{
  if (
    position.x < 0 || position.y < 0 || position.x >= mWidth ||
    position.y >= mHeight)
  {
    return false;
  }

  return mReachable[size_t(position.y) * size_t(mWidth) + size_t(position.x)];
}


bool isValidSpawnSpot(const data::map::Map& map, const base::Vec2& position)
{
  const auto box = playerBoxAt(position);

  // Need one more row below the player for the ground
  if (!isInsideMap(map, box) || box.bottom() + 1 >= map.height())
  {
    return false;
  }

  for (auto y = box.top(); y <= box.bottom(); ++y)
  {
    for (auto x = box.left(); x <= box.right(); ++x)
    {
      if (map.collisionData(x, y).isSolidOn(SolidEdge::any()))
      {
        return false;
      }
    }
  }

  for (auto x = box.left(); x <= box.right(); ++x)
  {
    if (map.collisionData(x, box.bottom() + 1).isSolidOn(SolidEdge::top()))
    {
      return true;
    }
  }

  return false;
}


std::vector<base::Vec2> findDeathmatchSpawnPoints(
  const data::map::Map& map,
  const base::Vec2& playerStart,
  const std::vector<base::Vec2>& itemPositions,
  const std::vector<base::Rect<int>>& openableAreas)
{
  const auto reachability = ReachabilityMap{map, playerStart, openableAreas};

  std::vector<base::Vec2> result;

  auto addIfFarEnough = [&](const base::Vec2& spot) {
    const auto tooClose =
      std::any_of(result.begin(), result.end(), [&](const base::Vec2& other) {
        return distanceSquared(other, spot) <
          MIN_SPAWN_POINT_DISTANCE * MIN_SPAWN_POINT_DISTANCE;
      });

    if (!tooClose)
    {
      result.push_back(spot);
    }
  };

  auto addNear = [&](const base::Vec2& position) {
    if (const auto spot = findSpawnSpotNear(map, reachability, position))
    {
      addIfFarEnough(*spot);
    }
  };

  addNear(playerStart);
  for (const auto& position : itemPositions)
  {
    addNear(position);
  }

  if (result.size() < MIN_SPAWN_POINTS_FROM_ITEMS)
  {
    for (auto y = 0; y < map.height(); ++y)
    {
      for (auto x = 0; x < map.width(); x += 5)
      {
        const auto candidate = base::Vec2{x, y};
        if (
          reachability.isReachable(candidate) &&
          isValidSpawnSpot(map, candidate))
        {
          addIfFarEnough(candidate);
        }
      }
    }
  }

  if (result.empty())
  {
    result.push_back(playerStart);
  }

  return result;
}

} // namespace rigel::game_logic
