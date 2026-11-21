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
#include "game_logic_common/input.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>


namespace rigel::game_logic
{

// Network deathmatch works with an authoritative host: The host runs the
// game, clients only send their input and display what the host tells them.
// The types here describe what's sent from host to client.


/** One sprite to draw. The destination is in world pixel coordinates, not
 * relative to a camera. */
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

  /** Identifies the same sprite across snapshots, so that clients can
   * interpolate its movement. 0 means unknown. */
  std::uint32_t mKey = 0;
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

  // For client-side prediction: The client's player position after the host
  // processed the client's input with the given sequence number
  std::uint32_t mLastInputSequence = 0;
  base::Vec2 mPlayerPosition;

  base::Vec2 mCameraPosition;
  int mScreenShakeOffsetX = 0;
  std::optional<base::Color> mScreenFlashColor;

  // The client's own player. Sprites don't include the client's own player
  // while it's alive, since the client draws it itself (prediction).
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


/** What a client's own simulation (prediction) says about its player */
struct PredictedPlayerState
{
  base::Vec2 mPosition;
  int mWeapon = 0;
  int mAmmo = 0;
};


/** One frame of input from a network player, see InputMessage */
struct RemotePlayerFrame
{
  PlayerInput mInput;
  std::uint32_t mSequence = 0;
  std::optional<base::Vec2> mPredictedPosition;

  /** Host frame the client was showing at the time (lag compensation) */
  std::uint32_t mViewedFrame = 0;
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

  /** Input for a network player's next update. Can be called more than
   * once per frame, each input is then applied in turn. Network players
   * without input for a frame don't move during that frame. */
  virtual void
    setRemotePlayerFrame(int playerIndex, const RemotePlayerFrame& frame) = 0;

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

  /** Position of the locally simulated (predicted) player, if alive */
  virtual std::optional<base::Vec2> predictedPlayerPosition() const = 0;

  /** State of the locally simulated (predicted) player, if alive */
  virtual std::optional<PredictedPlayerState> predictedPlayerState() const = 0;

  /** Move the predicted player, to fix a difference to the host's game.
   * Doesn't move it into walls, so the result can be less than asked for.
   * Returns how far the player was actually moved. */
  virtual base::Vec2 correctPrediction(const base::Vec2& offset) = 0;

  /** Stop simulating our player for a moment, and show it where the host
   * says it is instead. For when the difference is too big to fix by moving
   * it (see correctPrediction). Prediction starts again once a snapshot
   * includes the input with the given sequence number. Until then, the
   * host only gets neutral input, so that both games end up in the same
   * state. */
  virtual void beginResync(std::uint32_t untilSequence) = 0;

  /** Fix the predicted player's weapon and ammo */
  virtual void correctWeapon(int weapon, int ammo) = 0;

  /** Host frame currently shown for everything except our own player.
   * Remote things are shown slightly in the past (interpolation), the host
   * uses this to judge our shots fairly (lag compensation). */
  virtual std::uint32_t viewedFrame() const = 0;
};

} // namespace rigel::game_logic
