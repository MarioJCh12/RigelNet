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

// Network deathmatch support for GameWorld.
//
// The host runs the game as usual (see game_world_deathmatch.cpp), with some
// player slots controlled over the network. For each of these, it captures
// a snapshot of what that player sees every frame. A client runs a GameWorld
// for the same level, but doesn't run any game logic. Instead, it only
// displays the snapshots it receives.
//
// Sprites are sent as a list of images and positions, which the client
// draws with its own copy of the game's graphics. The map is mostly static,
// but can change when walls are destroyed or pieces of the map fall down.
// These changes are sent as tile changes plus the state of the dynamic
// geometry entities, which both sides create from the level file in the
// same order.

#include "game_world.hpp"

#include "engine/base_components.hpp"
#include "engine/sprite_rendering_system.hpp"
#include "game_logic/deathmatch_state.hpp"
#include "game_logic/dynamic_geometry_components.hpp"
#include "game_logic/remote_view_state.hpp"
#include "game_logic/world_state.hpp"

#include <loguru.hpp>


namespace rigel::game_logic
{

using engine::components::InterpolateMotion;
using engine::components::WorldPosition;

namespace
{

std::vector<std::uint32_t> copyMapTiles(const data::map::Map& map)
{
  std::vector<std::uint32_t> tiles;
  tiles.reserve(size_t(map.width()) * size_t(map.height()) * 2);

  for (auto layer = 0; layer < 2; ++layer)
  {
    for (auto y = 0; y < map.height(); ++y)
    {
      for (auto x = 0; x < map.width(); ++x)
      {
        tiles.push_back(map.tileAt(layer, x, y));
      }
    }
  }

  return tiles;
}


std::vector<NetTileChange> diffMapTiles(
  const std::vector<std::uint32_t>& before,
  const data::map::Map& map)
{
  std::vector<NetTileChange> changes;

  auto index = 0u;
  for (auto layer = 0; layer < 2; ++layer)
  {
    for (auto y = 0; y < map.height(); ++y)
    {
      for (auto x = 0; x < map.width(); ++x)
      {
        const auto tile = map.tileAt(layer, x, y);
        if (index < before.size() && before[index] != tile)
        {
          changes.push_back(NetTileChange{layer, {x, y}, int(tile)});
        }

        ++index;
      }
    }
  }

  return changes;
}


std::vector<entityx::Entity>
  collectDynamicSections(entityx::EntityManager& entities)
{
  std::vector<entityx::Entity> result;
  entities.each<components::DynamicGeometrySection>(
    [&](entityx::Entity entity, const components::DynamicGeometrySection&) {
      result.push_back(entity);
    });
  return result;
}

} // namespace


void GameWorld::initNetworkHostState()
{
  auto& dm = *mpDeathmatch;

  dm.mInitialMapTiles = copyMapTiles(mpState->mMap);
  dm.mLastSentMapTiles = dm.mInitialMapTiles;
  dm.mLastSentMessageCounter = mMessageDisplay.messageCounter();
  dm.mDynamicSections = collectDynamicSections(mpState->mEntities);
}


std::optional<int> GameWorld::findFreeRemoteSlot() const
{
  if (!mpDeathmatch)
  {
    return std::nullopt;
  }

  const auto& participants = mpDeathmatch->mParticipants;
  for (auto i = 0; i < int(participants.size()); ++i)
  {
    if (!participants[i].mIsLocal && !participants[i].mIsActive)
    {
      return i;
    }
  }

  return std::nullopt;
}


void GameWorld::connectRemotePlayer(const int playerIndex)
{
  setDeathmatchParticipantActive(playerIndex, true);
}


void GameWorld::disconnectRemotePlayer(const int playerIndex)
{
  setDeathmatchParticipantActive(playerIndex, false);
}


void GameWorld::setRemoteViewportSize(
  const int playerIndex,
  const base::Size& size)
{
  auto& participant = mpDeathmatch->mParticipants[playerIndex];

  // Guard against nonsense from the network
  const auto clampedSize =
    base::Size{std::clamp(size.width, 8, 128), std::clamp(size.height, 8, 64)};

  if (participant.mRemoteViewportSize != clampedSize)
  {
    participant.mRemoteViewportSize = clampedSize;
    participant.mpCamera->recenter(clampedSize);
  }
}


NetSnapshot GameWorld::captureSnapshot(const int playerIndex)
{
  auto& state = *mpState;
  const auto& dm = *mpDeathmatch;
  const auto& participant = dm.mParticipants[playerIndex];
  const auto& model = *participant.mpModel;

  NetSnapshot snapshot;
  snapshot.mFrame = dm.mFrameCounter;
  snapshot.mCameraPosition = participant.mpCamera->position();
  snapshot.mScreenShakeOffsetX = state.mScreenShakeOffsetX;
  snapshot.mScreenFlashColor = state.mScreenFlashColor;

  snapshot.mIsDead = participant.mpPlayer->isDead();
  snapshot.mHealth = model.health();
  snapshot.mWeapon = static_cast<int>(model.weapon());
  snapshot.mAmmo = model.ammo();
  snapshot.mWeaponConsumesAmmo = model.currentWeaponConsumesAmmo();

  snapshot.mFragLimit = dm.mOptions.mFragLimit;
  snapshot.mRoundWinner = dm.mRoundWinner;
  for (const auto& other : dm.mParticipants)
  {
    snapshot.mScores.push_back(
      NetPlayerScore{other.mIsActive, other.mFrags, other.mDeaths});
  }

  // Collect the sprites visible to this player. This reuses the sprite
  // rendering system's own logic, which is fine since it collects sprites
  // again before drawing anything on this computer.
  state.mSpriteRenderingSystem.update(
    state.mEntities,
    participant.mRemoteViewportSize,
    snapshot.mCameraPosition,
    1.0f);

  const auto& sprites = state.mSpriteRenderingSystem.sprites();
  snapshot.mSprites.reserve(sprites.size());
  for (const auto& spec : sprites)
  {
    NetSprite sprite;
    sprite.mDestRect = spec.mDestRect;
    sprite.mImageId = spec.mImageId;
    if (spec.mIsFlashingWhite)
    {
      sprite.mFlags |= NetSprite::FlashingWhite;
    }
    if (spec.mUseCloakEffect)
    {
      sprite.mFlags |= NetSprite::CloakEffect;
    }
    if (spec.mTint)
    {
      sprite.mFlags |= NetSprite::Tinted;
      sprite.mTint = *spec.mTint;
    }

    snapshot.mSprites.push_back(sprite);
  }
  snapshot.mFirstForegroundSprite =
    state.mSpriteRenderingSystem.firstForegroundSpriteIndex();

  for (auto i = 0; i < int(dm.mDynamicSections.size()); ++i)
  {
    auto entity = dm.mDynamicSections[i];
    if (!entity.valid())
    {
      continue;
    }

    const auto& section =
      *entity.component<const components::DynamicGeometrySection>();
    snapshot.mDynamicSections.push_back(NetDynamicSection{
      i,
      *entity.component<const WorldPosition>(),
      section.mLinkedGeometrySection,
      section.mPreviousHeight});
  }

  return snapshot;
}


NetWorldEvents GameWorld::collectNetEvents()
{
  auto& dm = *mpDeathmatch;

  NetWorldEvents events;

  events.mTileChanges = diffMapTiles(dm.mLastSentMapTiles, mpState->mMap);
  for (const auto& change : events.mTileChanges)
  {
    const auto index = size_t(change.mLayer) * size_t(mpState->mMap.width()) *
        size_t(mpState->mMap.height()) +
      size_t(change.mPosition.y) * size_t(mpState->mMap.width()) +
      size_t(change.mPosition.x);
    dm.mLastSentMapTiles[index] = std::uint32_t(change.mTile);
  }

  // Only the most recent message can be seen, so there's no need to send
  // any others in case multiple messages appeared within one frame.
  if (mMessageDisplay.messageCounter() != dm.mLastSentMessageCounter)
  {
    dm.mLastSentMessageCounter = mMessageDisplay.messageCounter();
    events.mMessages.push_back(mMessageDisplay.currentMessage());
  }

  return events;
}


std::vector<NetTileChange> GameWorld::allTileChanges() const
{
  return diffMapTiles(mpDeathmatch->mInitialMapTiles, mpState->mMap);
}


void GameWorld::enableRemoteView(const int playerIndex)
{
  LOG_F(INFO, "Enabling remote view as player %d", playerIndex + 1);

  mpRemoteView = std::make_unique<RemoteViewState>();
  mpRemoteView->mPlayerIndex = playerIndex;
  mpRemoteView->mDynamicSections = collectDynamicSections(mpState->mEntities);
  mpRemoteView->mSnapshot.mCameraPosition = mpState->mCamera.position();

  // Nothing is shown until the first snapshot arrives
  mpState->mSpriteRenderingSystem.setSprites({}, 0);
}


void GameWorld::updateRemoteView()
{
  // No game logic runs on network clients. We only keep animations going
  // which are not part of what the host sends.
  auto& state = *mpState;

  mMessageDisplay.update();
  state.mMapRenderer.updateAnimatedMapTiles();

  ++state.mWaterAnimStep;
  if (state.mWaterAnimStep >= 4)
  {
    state.mWaterAnimStep = 0;
  }
}


void GameWorld::applySnapshot(const NetSnapshot& snapshot)
{
  auto& state = *mpState;
  auto& remoteView = *mpRemoteView;

  remoteView.mSnapshot = snapshot;
  state.mScreenShakeOffsetX = snapshot.mScreenShakeOffsetX;
  state.mScreenFlashColor = snapshot.mScreenFlashColor;

  std::vector<engine::SpriteDrawSpec> sprites;
  sprites.reserve(snapshot.mSprites.size());
  for (const auto& sprite : snapshot.mSprites)
  {
    engine::SpriteDrawSpec spec{
      sprite.mDestRect,
      sprite.mImageId,
      (sprite.mFlags & NetSprite::FlashingWhite) != 0,
      (sprite.mFlags & NetSprite::CloakEffect) != 0};
    if (sprite.mFlags & NetSprite::Tinted)
    {
      spec.mTint = sprite.mTint;
    }

    sprites.push_back(spec);
  }
  state.mSpriteRenderingSystem.setSprites(
    std::move(sprites), snapshot.mFirstForegroundSprite);

  // Dynamic map geometry: Update the ones still present on the host, remove
  // the others
  std::vector<bool> stillExists(remoteView.mDynamicSections.size(), false);
  for (const auto& section : snapshot.mDynamicSections)
  {
    if (
      section.mIndex < 0 ||
      section.mIndex >= int(remoteView.mDynamicSections.size()))
    {
      continue;
    }

    auto entity = remoteView.mDynamicSections[section.mIndex];
    if (!entity.valid())
    {
      continue;
    }

    stillExists[section.mIndex] = true;

    *entity.component<WorldPosition>() = section.mPosition;
    if (entity.has_component<InterpolateMotion>())
    {
      entity.component<InterpolateMotion>()->mPreviousPosition =
        section.mPosition;
    }

    auto& dynamic = *entity.component<components::DynamicGeometrySection>();
    dynamic.mLinkedGeometrySection = section.mSection;
    dynamic.mPreviousHeight = section.mPreviousHeight;
  }

  for (auto i = 0u; i < remoteView.mDynamicSections.size(); ++i)
  {
    auto entity = remoteView.mDynamicSections[i];
    if (!stillExists[i] && entity.valid())
    {
      entity.destroy();
    }
  }
}


void GameWorld::applyTileChanges(const std::vector<NetTileChange>& changes)
{
  auto& map = mpState->mMap;

  for (const auto& change : changes)
  {
    if (
      change.mLayer < 0 || change.mLayer > 1 || change.mPosition.x < 0 ||
      change.mPosition.y < 0 || change.mPosition.x >= map.width() ||
      change.mPosition.y >= map.height())
    {
      continue;
    }

    map.setTileAt(
      change.mLayer,
      change.mPosition.x,
      change.mPosition.y,
      data::map::TileIndex(change.mTile));
  }
}


void GameWorld::showRemoteMessage(const std::string& message)
{
  mMessageDisplay.setMessage(message, ui::MessagePriority::Menu);
}


base::Size GameWorld::remoteViewportSize() const
{
  return deathmatchWorldAreaSize();
}

} // namespace rigel::game_logic
