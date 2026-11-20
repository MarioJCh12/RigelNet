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
#include "base/warnings.hpp"
#include "data/map.hpp"
#include "data/player_model.hpp"
#include "game_logic/camera.hpp"
#include "game_logic/player.hpp"
#include "game_logic/player/damage_system.hpp"
#include "game_logic/player/interaction_system.hpp"
#include "game_logic_common/deathmatch_options.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>


namespace rigel::game_logic
{

/** Rows (in tiles) below the split-screen views used for the scoreboard */
constexpr auto DEATHMATCH_STATUS_ROWS = 4;


/** Everything belonging to one player in a deathmatch.
 *
 * Player 1 re-uses the Player, Camera, PersistentPlayerState and player
 * systems that already exist in WorldState/GameWorld for single player.
 * Players 2 and up get their own copies of these, owned here. The raw
 * pointers always point to the right objects regardless of the player.
 */
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
