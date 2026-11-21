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

#include <base/warnings.hpp>
#include <game_logic/deathmatch_state.hpp>
#include <game_logic/snapshot_interpolation.hpp>

RIGEL_DISABLE_WARNINGS
#include <catch2/catch_test_macros.hpp>
RIGEL_RESTORE_WARNINGS

#include <cstdlib>
#include <vector>


using namespace rigel;
using namespace rigel::game_logic;


namespace
{

NetSprite spriteAt(const std::uint32_t key, const int x, const int y)
{
  NetSprite sprite;
  sprite.mKey = key;
  sprite.mDestRect = {{x, y}, {16, 16}};
  sprite.mImageId = 1;
  return sprite;
}


/** A snapshot with one sprite moving right by 8 pixels per frame */
NetSnapshot movingSnapshot(const std::uint32_t frame)
{
  NetSnapshot snapshot;
  snapshot.mFrame = frame;
  snapshot.mSprites = {spriteAt(42, int(frame) * 8, 100)};
  return snapshot;
}


int shownX(const SnapshotInterpolator& interpolator, const float subFrame)
{
  const auto frame = interpolator.sample(subFrame, true);
  REQUIRE(frame.mSprites.size() == 1);
  return frame.mSprites[0].mDestRect.topLeft.x;
}

} // namespace


TEST_CASE("Sprites are interpolated between snapshots")
{
  const auto from = std::vector<NetSprite>{
    spriteAt(1, 0, 0), spriteAt(2, 100, 100), spriteAt(0, 50, 50)};
  const auto to = std::vector<NetSprite>{
    spriteAt(2, 116, 92), spriteAt(1, 8, 0), spriteAt(0, 0, 0)};

  const auto result = interpolateSprites(from, to, 0.5f);

  REQUIRE(result.size() == 3);

  SECTION("Matching is by key, not by order")
  {
    CHECK(result[0].mDestRect.topLeft == base::Vec2{4, 0});
    CHECK(result[1].mDestRect.topLeft == base::Vec2{108, 96});
  }

  SECTION("Sprites without key stay where they are")
  {
    CHECK(result[2].mDestRect.topLeft == base::Vec2{50, 50});
  }

  SECTION("Big jumps aren't smoothed")
  {
    const auto teleported = interpolateSprites(
      {spriteAt(5, 0, 0)}, {spriteAt(5, 100, 0)}, 0.5f);
    CHECK(teleported[0].mDestRect.topLeft == base::Vec2{0, 0});
  }

  SECTION("Sprites that disappear stay until the next snapshot")
  {
    const auto gone = interpolateSprites({spriteAt(5, 10, 20)}, {}, 0.9f);
    REQUIRE(gone.size() == 1);
    CHECK(gone[0].mDestRect.topLeft == base::Vec2{10, 20});
  }
}


TEST_CASE("Snapshot playback on a steady connection")
{
  SnapshotInterpolator interpolator;

  for (auto frame = 1u; frame <= 60; ++frame)
  {
    interpolator.addSnapshot(movingSnapshot(frame));
    interpolator.advance();
  }

  SECTION("Delay stays small")
  {
    CHECK(interpolator.delay() <= SnapshotInterpolator::MIN_DELAY + 0.01);
  }

  SECTION("Movement is steady, also between frames")
  {
    // One frame of playback moves the sprite by 8 pixels. Within a frame, it
    // moves proportionally.
    const auto start = shownX(interpolator, 0.0f);
    const auto half = shownX(interpolator, 0.5f);
    CHECK(std::abs(half - start - 4) <= 1);

    interpolator.addSnapshot(movingSnapshot(61));
    interpolator.advance();
    const auto next = shownX(interpolator, 0.0f);
    CHECK(std::abs(next - start - 8) <= 1);
  }

  SECTION("Viewed frame is behind the newest by the delay")
  {
    const auto behind = 60 - int(interpolator.viewedFrame());
    CHECK(behind >= 1);
    CHECK(behind <= 2);
  }
}


TEST_CASE("Snapshot playback smooths out an irregular connection")
{
  SnapshotInterpolator interpolator;

  // Snapshots arrive in bursts: nothing for two frames, then three at once
  auto nextFrame = 1u;
  for (auto localFrame = 0; localFrame < 90; ++localFrame)
  {
    if (localFrame % 3 == 2)
    {
      for (auto i = 0; i < 3; ++i)
      {
        interpolator.addSnapshot(movingSnapshot(nextFrame++));
      }
    }

    interpolator.advance();
  }

  SECTION("Delay grows to cover the gaps")
  {
    CHECK(interpolator.delay() >= 3.0);
    CHECK(interpolator.delay() <= SnapshotInterpolator::MAX_DELAY);
  }

  SECTION("Shown position advances steadily, without stops or jumps")
  {
    auto previous = shownX(interpolator, 0.0f);
    for (auto localFrame = 90; localFrame < 120; ++localFrame)
    {
      if (localFrame % 3 == 2)
      {
        for (auto i = 0; i < 3; ++i)
        {
          interpolator.addSnapshot(movingSnapshot(nextFrame++));
        }
      }

      interpolator.advance();

      const auto current = shownX(interpolator, 0.0f);
      const auto step = current - previous;
      CHECK(step >= 6);
      CHECK(step <= 10);
      previous = current;
    }
  }
}


TEST_CASE("Snapshot playback recovers from a long gap")
{
  SnapshotInterpolator interpolator;

  auto frame = 1u;
  for (; frame <= 30; ++frame)
  {
    interpolator.addSnapshot(movingSnapshot(frame));
    interpolator.advance();
  }

  // Nothing arrives for a second, then the host's current state
  for (auto i = 0; i < 15; ++i)
  {
    interpolator.advance();
  }

  SECTION("Holds the last known state while waiting")
  {
    // Playback slows down when data is late, then stops at the newest
    const auto x = shownX(interpolator, 0.5f);
    CHECK(x >= 29 * 8);
    CHECK(x <= 30 * 8);
  }

  SECTION("Jumps ahead once data arrives again")
  {
    frame += 15;
    interpolator.addSnapshot(movingSnapshot(frame));
    interpolator.advance();

    const auto shown = double(shownX(interpolator, 0.0f)) / 8.0;
    CHECK(shown >= double(frame) - SnapshotInterpolator::MAX_DELAY - 1.0);
  }
}


TEST_CASE("Late snapshots are ignored")
{
  SnapshotInterpolator interpolator;
  interpolator.addSnapshot(movingSnapshot(10));
  interpolator.addSnapshot(movingSnapshot(9));
  interpolator.addSnapshot(movingSnapshot(10));
  interpolator.advance();

  CHECK(shownX(interpolator, 0.0f) == 80);
  CHECK(interpolator.viewedFrame() <= 10);
  CHECK(interpolator.viewedFrame() >= 8);
}


TEST_CASE("Without motion smoothing, only whole frames are shown")
{
  SnapshotInterpolator interpolator;
  for (auto frame = 1u; frame <= 20; ++frame)
  {
    interpolator.addSnapshot(movingSnapshot(frame));
    interpolator.advance();
  }

  for (const auto subFrame : {0.0f, 0.3f, 0.7f})
  {
    const auto x = interpolator.sample(subFrame, false).mSprites[0].mDestRect.topLeft.x;
    CHECK(x % 8 == 0);
  }
}


TEST_CASE("Lag compensation")
{
  SECTION("Rewind amount")
  {
    CHECK(lagCompensationFrames(100, 95) == 5);
    CHECK(lagCompensationFrames(100, 100) == 0);
    CHECK(lagCompensationFrames(100, 0) == 0);
    CHECK(lagCompensationFrames(100, 120) == 0);
    CHECK(lagCompensationFrames(100, 50) == MAX_LAG_COMPENSATION_FRAMES);
  }

  SECTION("Hit box history")
  {
    HitBoxHistory history;
    const auto box = base::Rect<int>{{10, 20}, {3, 5}};

    history.record(7, box);
    history.record(8, std::nullopt);

    CHECK(history.at(7) == box);
    CHECK(!history.at(8));
    CHECK(!history.at(6));

    // Old entries get replaced
    history.record(7 + HitBoxHistory::SIZE, base::Rect<int>{{1, 1}, {1, 1}});
    CHECK(!history.at(7));
  }
}


TEST_CASE("Prediction corrections slide instead of jumping")
{
  CorrectionSmoothing smoothing;

  SECTION("Player is first drawn where it was, then slides into place")
  {
    // Player was moved 2 tiles to the right
    smoothing.addCorrection({2, 0});
    CHECK(smoothing.offset(0.0f, false) == base::Vec2{-16, 0});

    auto previous = 16;
    for (auto frame = 0; frame < 10; ++frame)
    {
      smoothing.advance();
      const auto distance = -smoothing.offset(0.0f, false).x;
      CHECK(distance <= previous);
      previous = distance;
    }

    CHECK(smoothing.offset(0.0f, false) == base::Vec2{});
  }

  SECTION("Gone within a few frames")
  {
    smoothing.addCorrection({0, -3});
    for (auto frame = 0; frame < 6; ++frame)
    {
      smoothing.advance();
    }
    CHECK(smoothing.offset(0.0f, false) == base::Vec2{});
  }

  SECTION("With motion smoothing, also slides between frames")
  {
    smoothing.addCorrection({1, 0});
    smoothing.advance();
    CHECK(smoothing.offset(0.0f, true) == base::Vec2{-8, 0});
    CHECK(smoothing.offset(0.5f, true) == base::Vec2{-6, 0});
    CHECK(smoothing.offset(1.0f, true) == base::Vec2{-4, 0});
  }

  SECTION("Corrections add up")
  {
    smoothing.addCorrection({1, 0});
    smoothing.addCorrection({1, 1});
    CHECK(smoothing.offset(0.0f, false) == base::Vec2{-16, -8});
  }

  SECTION("Very big corrections just jump")
  {
    smoothing.addCorrection({20, 0});
    CHECK(smoothing.offset(0.0f, false) == base::Vec2{});
  }
}
