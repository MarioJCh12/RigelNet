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

#include "snapshot_interpolation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <unordered_map>
#include <utility>


namespace rigel::game_logic
{

namespace
{

/** How many game frames of arrival times to look at for judging the
 * connection's regularity, two seconds' worth */
constexpr auto ARRIVAL_HISTORY_SIZE = 30u;

/** If the shown moment is further off than this from where it should be,
 * jump there instead of catching up gradually */
constexpr auto MAX_DRIFT = 4.0;

/** Max change of playback speed while catching up or slowing down */
constexpr auto MAX_RATE_ADJUSTMENT = 0.1;

/** How quickly the delay shrinks again after the connection got better, in
 * frames per frame. About 8 seconds from the max to the min. */
constexpr auto DELAY_DECREASE_RATE = 0.05;

} // namespace


std::vector<NetSprite> interpolateSprites(
  const std::vector<NetSprite>& from,
  const std::vector<NetSprite>& to,
  const float factor,
  const int maxDistance)
{
  std::unordered_map<std::uint32_t, const NetSprite*> spritesByKey;
  spritesByKey.reserve(to.size());
  for (const auto& sprite : to)
  {
    if (sprite.mKey != 0)
    {
      spritesByKey.emplace(sprite.mKey, &sprite);
    }
  }

  auto result = from;
  for (auto& sprite : result)
  {
    if (sprite.mKey == 0)
    {
      continue;
    }

    const auto iTarget = spritesByKey.find(sprite.mKey);
    if (iTarget == spritesByKey.end())
    {
      continue;
    }

    const auto& start = sprite.mDestRect.topLeft;
    const auto& end = iTarget->second->mDestRect.topLeft;
    if (
      std::abs(end.x - start.x) > maxDistance ||
      std::abs(end.y - start.y) > maxDistance)
    {
      continue;
    }

    sprite.mDestRect.topLeft = base::Vec2{
      start.x + int(std::lround(float(end.x - start.x) * factor)),
      start.y + int(std::lround(float(end.y - start.y) * factor))};
  }

  return result;
}


void SnapshotInterpolator::addSnapshot(const NetSnapshot& snapshot)
{
  if (!mBuffer.empty() && snapshot.mFrame <= mBuffer.back().mFrame)
  {
    // Late or duplicate, we've moved on already
    return;
  }

  mBuffer.push_back(Entry{
    snapshot.mFrame,
    Frame{
      snapshot.mSprites,
      snapshot.mFirstForegroundSprite,
      snapshot.mCameraPosition}});

  while (mBuffer.size() > MAX_BUFFERED)
  {
    mBuffer.pop_front();
  }
}


void SnapshotInterpolator::advance()
{
  ++mLocalFrame;

  if (mBuffer.empty())
  {
    return;
  }

  const auto newest = double(mBuffer.back().mFrame);
  const auto previousPlayback = mPlaybackFrame;

  // How far ahead of our own clock the newest snapshot is. On a perfect
  // connection, this stays the same. The more it varies, the more delay we
  // need so that there's still something to show when snapshots are late.
  mArrivalHistory.push_back(std::int64_t(mBuffer.back().mFrame) - mLocalFrame);
  while (mArrivalHistory.size() > ARRIVAL_HISTORY_SIZE)
  {
    mArrivalHistory.pop_front();
  }

  const auto [iMin, iMax] =
    std::minmax_element(mArrivalHistory.begin(), mArrivalHistory.end());
  const auto jitter = double(*iMax - *iMin);
  const auto targetDelay = std::clamp(jitter + 1.0, MIN_DELAY, MAX_DELAY);

  // Grow quickly when the connection gets worse, shrink slowly when it gets
  // better, so a single hiccup doesn't cause stutter right after
  if (targetDelay > mDelay)
  {
    mDelay = targetDelay;
  }
  else
  {
    mDelay = std::max(targetDelay, mDelay - DELAY_DECREASE_RATE);
  }

  if (!mIsStarted)
  {
    mPlaybackFrame = newest - mDelay;
    mLastNewestFrame = mBuffer.back().mFrame;
    mIsStarted = true;
    return;
  }

  // Play at normal speed, but slightly faster or slower when we drifted
  // away from where we want to be
  const auto drift = (newest - mPlaybackFrame) - mDelay;
  if (std::abs(drift) > MAX_DRIFT)
  {
    mPlaybackFrame = newest - mDelay;
    mRate = 1.0;
  }
  else
  {
    mRate = 1.0 +
      std::clamp(drift * 0.05, -MAX_RATE_ADJUSTMENT, MAX_RATE_ADJUSTMENT);
    mPlaybackFrame = std::min(mPlaybackFrame + mRate, newest);
  }

  ++mStats.mFrames;
  const auto newestAdvance =
    std::int64_t(mBuffer.back().mFrame) - std::int64_t(mLastNewestFrame);
  mLastNewestFrame = mBuffer.back().mFrame;
  if (newestAdvance <= 0)
  {
    ++mStats.mReceivedStalls;
  }
  else if (newestAdvance >= 2)
  {
    ++mStats.mReceivedSkips;
  }

  const auto playbackAdvance = mPlaybackFrame - previousPlayback;
  if (playbackAdvance < 0.5)
  {
    ++mStats.mShownStalls;
  }
  else if (playbackAdvance >= 1.5)
  {
    ++mStats.mShownSkips;
  }

  // Keep one snapshot before the shown moment, so there's always something
  // to interpolate from
  while (mBuffer.size() > 2 && double(mBuffer[1].mFrame) <= mPlaybackFrame)
  {
    mBuffer.pop_front();
  }
}


auto SnapshotInterpolator::sample(const float subFrame, const bool smooth) const
  -> Frame
{
  if (mBuffer.empty())
  {
    return {};
  }

  auto time = mPlaybackFrame;
  if (smooth)
  {
    time += double(std::clamp(subFrame, 0.0f, 1.0f)) * mRate;
  }
  else
  {
    time = std::floor(time + 0.5);
  }

  if (time <= double(mBuffer.front().mFrame))
  {
    return mBuffer.front().mData;
  }

  for (auto i = 0u; i + 1 < mBuffer.size(); ++i)
  {
    const auto& from = mBuffer[i];
    const auto& to = mBuffer[i + 1];
    if (time < double(to.mFrame))
    {
      const auto factor =
        float((time - double(from.mFrame)) / double(to.mFrame - from.mFrame));

      // Snapshots lost on the way leave bigger gaps, where things move
      // further. Limited so that teleports aren't smoothed.
      const auto framesApart = int(std::min(to.mFrame - from.mFrame, 4u));

      Frame result;
      result.mSprites = interpolateSprites(
        from.mData.mSprites,
        to.mData.mSprites,
        factor,
        MAX_DISTANCE_PER_FRAME * framesApart);
      result.mFirstForegroundSprite = from.mData.mFirstForegroundSprite;
      result.mCameraPosition = from.mData.mCameraPosition;
      return result;
    }
  }

  // Ran out of snapshots: Hold the newest one until more arrive
  return mBuffer.back().mData;
}


std::uint32_t SnapshotInterpolator::viewedFrame() const
{
  if (mBuffer.empty())
  {
    return 0;
  }

  const auto frame = std::max(0.0, std::floor(mPlaybackFrame + 0.5));
  return std::min(std::uint32_t(frame), mBuffer.back().mFrame);
}


auto SnapshotInterpolator::takeStats() -> Stats
{
  return std::exchange(mStats, Stats{});
}


void SnapshotInterpolator::reset()
{
  *this = SnapshotInterpolator{};
}

} // namespace rigel::game_logic
