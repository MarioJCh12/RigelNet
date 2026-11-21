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

#include "base/color.hpp"
#include "base/spatial_types.hpp"
#include "base/warnings.hpp"
#include "data/map.hpp"
#include "data/player_model.hpp"
#include "game_logic/camera.hpp"
#include "game_logic/player.hpp"
#include "game_logic/player/damage_system.hpp"
#include "game_logic/player/interaction_system.hpp"
#include "game_logic_common/deathmatch_options.hpp"
#include "game_logic_common/network_world.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>


namespace rigel::game_logic
{

/** Rows (in tiles) below the split-screen views used for the scoreboard */
constexpr auto DEATHMATCH_STATUS_ROWS = 4;


/** Color tint for each player, so that they can be told apart.
 *
 * Player 1 keeps Duke's original colors (no tint). */
inline std::optional<base::Color> deathmatchPlayerTint(const int playerIndex)
{
  switch (playerIndex)
  {
    case 1:
      return base::Color{130, 170, 255, 255};
    case 2:
      return base::Color{140, 255, 140, 255};
    case 3:
      return base::Color{255, 220, 110, 255};
    default:
      return std::nullopt;
  }
}


/** Everything belonging to one player in a deathmatch.
 *
 * Player 1 re-uses the Player, Camera, PersistentPlayerState and player
 * systems that already exist in WorldState/GameWorld for single player.
 * Players 2 and up get their own copies of these, owned here. The raw
 * pointers always point to the right objects regardless of the player.
 */
/** Most frames that lag compensation looks into the past, 0.8
 * seconds. Players with more latency than this have to lead their shots. */
constexpr auto MAX_LAG_COMPENSATION_FRAMES = 12;


/** How many frames to rewind other players when checking hits from a shot
 * fired on host frame hostFrame by a player who was seeing viewedFrame. */
inline int lagCompensationFrames(
  const std::uint32_t hostFrame,
  const std::uint32_t viewedFrame)
{
  if (viewedFrame == 0 || viewedFrame >= hostFrame)
  {
    return 0;
  }

  return int(std::min<std::uint32_t>(
    hostFrame - viewedFrame, std::uint32_t(MAX_LAG_COMPENSATION_FRAMES)));
}


/** Where a player's hit box was during recent frames, for lag compensation */
class HitBoxHistory
{
public:
  static constexpr auto SIZE = 32u;

  /** Record the hit box after the given frame. Nothing means the player
   * couldn't be hit (dead or not in the game). */
  void record(
    const std::uint32_t frame,
    const std::optional<base::Rect<int>>& hitBox)
  {
    auto& entry = mEntries[frame % SIZE];
    entry.mFrame = frame;
    entry.mHitBox = hitBox;
    entry.mIsValid = true;
  }

  /** The hit box after the given frame. Nothing if the player couldn't be
   * hit back then, or if we don't know anymore. */
  std::optional<base::Rect<int>> at(const std::uint32_t frame) const
  {
    const auto& entry = mEntries[frame % SIZE];
    if (!entry.mIsValid || entry.mFrame != frame)
    {
      return std::nullopt;
    }

    return entry.mHitBox;
  }

  void clear() { mEntries = {}; }

private:
  struct Entry
  {
    std::uint32_t mFrame = 0;
    std::optional<base::Rect<int>> mHitBox;
    bool mIsValid = false;
  };

  std::array<Entry, SIZE> mEntries;
};


struct DeathmatchParticipant
{
  std::unique_ptr<data::PersistentPlayerState> mpOwnedModel;
  std::unique_ptr<Player> mpOwnedPlayer;
  std::unique_ptr<Camera> mpOwnedCamera;
  std::unique_ptr<PlayerInteractionSystem> mpOwnedInteractionSystem;
  std::unique_ptr<player::DamageSystem> mpOwnedDamageSystem;

  data::PersistentPlayerState* mpModel = nullptr;
  Player* mpPlayer = nullptr;
  Camera* mpCamera = nullptr;
  PlayerInteractionSystem* mpInteractionSystem = nullptr;
  player::DamageSystem* mpDamageSystem = nullptr;

  base::Vec2 mPreviousCameraPosition;

  /** Inactive players are empty network slots: Not in the game world. */
  bool mIsActive = true;

  /** Local players are shown on this computer's screen. Remote players are
   * controlled over the network, their view size is reported by the client.
   */
  bool mIsLocal = true;
  base::Size mRemoteViewportSize{32, 20};

  // Network players: Inputs to apply in the next frame (usually one), and
  // the sequence number of the last input that was applied (for client-side
  // prediction)
  std::vector<RemotePlayerFrame> mPendingRemoteFrames;
  std::uint32_t mLastInputSequence = 0;

  // Lag compensation: How far in the past this player currently sees the
  // others (network players only), and where this player was recently
  int mRewindFrames = 0;
  HitBoxHistory mHitBoxHistory;

  int mFrags = 0;
  int mDeaths = 0;

  /** Index of the player who last hurt this player, for kill credit */
  std::optional<int> mLastAttacker;

  bool mDeathCounted = false;
  int mFramesDead = 0;
};


/** A level item (or item box) which comes back after being taken */
struct ItemSpawner
{
  data::map::LevelData::Actor mActor;
  entityx::Entity mEntity;
  int mFramesUntilRespawn = -1;
};


struct DeathmatchState
{
  DeathmatchOptions mOptions;
  std::vector<DeathmatchParticipant> mParticipants;
  std::vector<ItemSpawner> mItemSpawners;
  std::vector<base::Vec2> mSpawnPoints;

  /** Set while the end-of-round screen is showing */
  std::optional<int> mRoundWinner;
  int mFramesSinceRoundEnd = 0;

  // Network hosting: map state for sending changes to clients
  std::vector<std::uint32_t> mInitialMapTiles;
  std::vector<std::uint32_t> mLastSentMapTiles;
  std::uint32_t mLastSentMessageCounter = 0;
  std::uint32_t mFrameCounter = 0;

  // Pieces of dynamic map geometry, in a fixed order that's the same on host
  // and clients
  std::vector<entityx::Entity> mDynamicSections;

  /** Own random sequence, so that we don't disturb the game's RNG */
  std::uint32_t mRandomState = 0x2545F491u;

  int nextRandom(const int upperBound)
  {
    // xorshift32
    mRandomState ^= mRandomState << 13;
    mRandomState ^= mRandomState >> 17;
    mRandomState ^= mRandomState << 5;
    return static_cast<int>(mRandomState % std::uint32_t(upperBound));
  }
};

} // namespace rigel::game_logic
