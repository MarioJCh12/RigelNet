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

#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace rigel::game_logic
{

// Network deathmatch works with an authoritative host: The host runs the
// game, clients only send their input and display what the host tells them.
// The types here describe what's sent from host to client.


/** One sprite to draw, in world pixel coordinates */
struct NetSprite
{
  enum Flags : std::uint8_t
  {
    FlashingWhite = 1 << 0,
    CloakEffect = 1 << 1,
    Tinted = 1 << 2,
  };

  base::Rect<int> mDestRect;
  int mImageId = 0;
  std::uint8_t mFlags = 0;
  base::Color mTint;
};


/** State of a falling/shootable piece of map geometry */
struct NetDynamicSection
{
  int mIndex = 0;
  base::Vec2 mPosition;
  base::Rect<int> mSection;
  int mPreviousHeight = 0;
};


struct NetPlayerScore
{
  bool mIsActive = false;
  int mFrags = 0;
  int mDeaths = 0;
};


/** Everything a client needs to draw one frame */
struct NetSnapshot
{
  std::uint32_t mFrame = 0;

  base::Vec2 mCameraPosition;
  int mScreenShakeOffsetX = 0;
  std::optional<base::Color> mScreenFlashColor;

  // The client's own player
  bool mIsDead = false;
  int mHealth = 0;
  int mWeapon = 0;
  int mAmmo = 0;
  bool mWeaponConsumesAmmo = false;

  int mFragLimit = 0;
  std::optional<int> mRoundWinner;
  std::vector<NetPlayerScore> mScores;

  std::vector<NetSprite> mSprites;
  // Sprites from this index on are drawn on top of the map foreground
  int mFirstForegroundSprite = 0;

  // All pieces of dynamic geometry that still exist. Pieces not in the
  // list have been destroyed.
  std::vector<NetDynamicSection> mDynamicSections;
};


struct NetTileChange
{
  int mLayer = 0;
  base::Vec2 mPosition;
  int mTile = 0;
};


/** Things that happened on the host and need to reach clients reliably */
struct NetWorldEvents
{
  std::vector<NetTileChange> mTileChanges;
  std::vector<std::string> mMessages;
};


/** Network-related functionality of a game world
 *
 * Implemented by the modern GameWorld. The frontend uses this to connect
 * the game world to the network code.
 */
class INetworkWorld
{
public:
  virtual ~INetworkWorld() = default;

  // Host side

  /** Find an empty slot for a network player, or nothing if the game is
   * full. Returns the player index. */
  virtual std::optional<int> findFreeRemoteSlot() const = 0;
  virtual void connectRemotePlayer(int playerIndex) = 0;
  virtual void disconnectRemotePlayer(int playerIndex) = 0;
  virtual void
    setRemoteViewportSize(int playerIndex, const base::Size& size) = 0;

  virtual NetSnapshot captureSnapshot(int playerIndex) = 0;

  /** Changes since the previous call */
  virtual NetWorldEvents collectNetEvents() = 0;

  /** All map changes since level start, for players joining mid-game */
  virtual std::vector<NetTileChange> allTileChanges() const = 0;

  // Client side

  /** Turn this world into a passive view of a game hosted elsewhere */
  virtual void enableRemoteView(int playerIndex) = 0;
  virtual void applySnapshot(const NetSnapshot& snapshot) = 0;
  virtual void applyTileChanges(const std::vector<NetTileChange>& changes) = 0;
  virtual void showRemoteMessage(const std::string& message) = 0;

  /** Size of this computer's game view, which the host needs to know */
  virtual base::Size remoteViewportSize() const = 0;
};

} // namespace rigel::game_logic
