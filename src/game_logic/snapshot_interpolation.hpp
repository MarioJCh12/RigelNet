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
#include "game_logic_common/network_world.hpp"

#include <cstdint>
#include <deque>
#include <vector>


namespace rigel::game_logic
{

/** Smooth display of what the host sends, for network clients.
 *
 * Snapshots don't arrive at a steady pace over the internet: Sometimes two
 * come in at once, sometimes none for a while. Showing each one as soon as
 * it arrives makes other players and enemies move in jerks.
 *
 * Instead, we keep a few snapshots around and show the game slightly in the
 * past, at a steady pace. When it's time to show a moment between two
 * snapshots, sprite positions are interpolated between them. The delay
 * adapts to how irregular the connection is: as small as possible, but big
 * enough that there's usually a newer snapshot to move towards.
 *
 * Our own player isn't affected by this, it's simulated locally
 * (prediction).
 */
class SnapshotInterpolator
{
public:
  struct Frame
  {
    std::vector<NetSprite> mSprites;
    int mFirstForegroundSprite = 0;
    base::Vec2 mCameraPosition;
  };

  /** Most snapshots kept, about two seconds' worth */
  static constexpr auto MAX_BUFFERED = 32u;

  /** Bounds of the delay, in game frames */
  static constexpr auto MIN_DELAY = 1.5;
  static constexpr auto MAX_DELAY = 8.0;

  /** Sprites moving further than this per frame (in pixels) jump instead
   * of sliding, e.g. when teleporting or respawning */
  static constexpr auto MAX_DISTANCE_PER_FRAME = 48;

  void addSnapshot(const NetSnapshot& snapshot);

  /** Call once per game logic frame, after adding new snapshots */
  void advance();

  /** What to show right now.
   *
   * @param subFrame progress towards the next game logic frame, 0..1
   * @param smooth interpolate between frames. Without, positions only
   *   change at game frame boundaries, like the original game.
   */
  Frame sample(float subFrame, bool smooth) const;

  bool hasData() const { return !mBuffer.empty(); }

  /** Host frame currently shown, for lag compensation */
  std::uint32_t viewedFrame() const;

  /** How smooth things were, for the log. A stall is a frame where the
   * shown moment didn't move forward, a skip is one where it moved forward
   * by two or more frames at once. "Received" is what showing snapshots right
   * away (without interpolation) would have looked like. */
  struct Stats
  {
    int mFrames = 0;
    int mReceivedStalls = 0;
    int mReceivedSkips = 0;
    int mShownStalls = 0;
    int mShownSkips = 0;
  };

  /** Stats since the last call */
  Stats takeStats();

  double delay() const { return mDelay; }
  double playbackFrame() const { return mPlaybackFrame; }

  void reset();

private:
  struct Entry
  {
    std::uint32_t mFrame = 0;
    Frame mData;
  };

  std::deque<Entry> mBuffer;
  std::deque<std::int64_t> mArrivalHistory;
  std::int64_t mLocalFrame = 0;
  double mPlaybackFrame = 0.0;
  double mDelay = MIN_DELAY + 0.5;
  double mRate = 1.0;
  bool mIsStarted = false;

  Stats mStats;
  std::uint32_t mLastNewestFrame = 0;
};


/** Positions of the sprites in from, moved towards matching sprites in to.
 * Sprites moving further than maxDistance pixels stay in place. */
std::vector<NetSprite> interpolateSprites(
  const std::vector<NetSprite>& from,
  const std::vector<NetSprite>& to,
  float factor,
  int maxDistance = SnapshotInterpolator::MAX_DISTANCE_PER_FRAME);

} // namespace rigel::game_logic
