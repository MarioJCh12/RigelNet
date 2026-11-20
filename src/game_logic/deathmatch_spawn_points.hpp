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

#pragma once

#include "base/spatial_types.hpp"
#include "data/map.hpp"

#include <vector>


namespace rigel::game_logic
{

/** Which player positions can be reached from a starting point
 *
 * A position is the bottom-left tile of the player's bounding box, like
 * the player's WorldPosition. Reachability is determined with a flood fill
 * over all positions where the player's bounding box doesn't overlap any
 * walls. This is more generous than what the player can actually do (it
 * ignores gravity and jump height), but it reliably tells apart the inside
 * of a level from empty space outside of the level's walls.
 *
 * Doors, force fields etc. are entities, not map tiles, so they don't
 * block the flood fill. Some obstacles are made of map tiles though, like
 * shootable walls, or doors and floors that open/collapse. These are given
 * via openableAreas, and are treated as passable. Areas only reachable via
 * teleporter are not considered reachable.
 */
class ReachabilityMap
{
public:
  ReachabilityMap(
    const data::map::Map& map,
    const base::Vec2& start,
    const std::vector<base::Rect<int>>& openableAreas = {});

  bool isReachable(const base::Vec2& position) const;
  int numReachablePositions() const { return mNumReachable; }

private:
  std::vector<bool> mReachable;
  int mWidth;
  int mHeight;
  int mNumReachable = 0;
};


/** True if the player fits at the given position and stands on ground */
bool isValidSpawnSpot(const data::map::Map& map, const base::Vec2& position);


/** Determine where players can (re-)spawn in deathmatch
 *
 * Uses the player start and the positions of items in the level as
 * candidates, since level designers put those in places players can get
 * to. Candidates are moved slightly if needed to make the player fit.
 * If that doesn't give enough spawn points, more are found by scanning the
 * map. All spawn points are reachable from the player start (see
 * ReachabilityMap), and are spaced out from each other.
 *
 * Never returns an empty list: In the worst case, it contains just the
 * player start.
 */
std::vector<base::Vec2> findDeathmatchSpawnPoints(
  const data::map::Map& map,
  const base::Vec2& playerStart,
  const std::vector<base::Vec2>& itemPositions,
  const std::vector<base::Rect<int>>& openableAreas);

} // namespace rigel::game_logic
