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

#include "data/unit_conversions.hpp"
#include "engine/base_components.hpp"
#include "engine/sprite_rendering_system.hpp"
#include "engine/visual_components.hpp"
#include "frontend/game_service_provider.hpp"
#include "game_logic/deathmatch_spawn_points.hpp"
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

/** Extra tiles around a network player's view to include sprites from */
constexpr auto SNAPSHOT_SPRITE_MARGIN = 8;

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


/** Passes everything through, except sounds.
 *
 * A network client's locally simulated player would otherwise play sounds
 * twice: Once when simulated locally, and again when the host tells the
 * client about the sounds that happened in the actual game.
 */
class MutedServiceProvider : public IGameServiceProvider
{
public:
  explicit MutedServiceProvider(IGameServiceProvider* pTarget)
    : mpTarget(pTarget)
  {
  }

  void fadeOutScreen() override { mpTarget->fadeOutScreen(); }
  void fadeInScreen() override { mpTarget->fadeInScreen(); }
  void playSound(data::SoundId) override { }
  void stopSound(data::SoundId) override { }
  void stopAllSounds() override { mpTarget->stopAllSounds(); }
  void playMusic(const std::string& name) override
  {
    mpTarget->playMusic(name);
  }
  void stopMusic() override { mpTarget->stopMusic(); }
  void scheduleGameQuit() override { mpTarget->scheduleGameQuit(); }
  void switchGamePath(const std::filesystem::path& newGamePath) override
  {
    mpTarget->switchGamePath(newGamePath);
  }
  void markCurrentFrameAsWidescreen() override
  {
    mpTarget->markCurrentFrameAsWidescreen();
  }
  bool isSharewareVersion() const override
  {
    return mpTarget->isSharewareVersion();
  }
  const CommandLineOptions& commandLineOptions() const override
  {
    return mpTarget->commandLineOptions();
  }
  const GameControllerInfo& gameControllerInfo() const override
  {
    return mpTarget->gameControllerInfo();
  }

private:
  IGameServiceProvider* mpTarget;
};


void setHealth(data::PersistentPlayerState& model, const int health)
{
  model.giveHealth(data::MAX_HEALTH);
  const auto difference = model.health() - health;
  if (difference > 0)
  {
    model.takeDamage(difference);
  }
}


void setPlayerSpriteVisible(Player& player, const bool visible)
{
  auto entity = player.entity();
  entity.component<engine::components::Sprite>()->mShow = visible;
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


void GameWorld::setRemotePlayerFrame(
  const int playerIndex,
  const RemotePlayerFrame& frame)
{
  if (
    !mpDeathmatch || playerIndex < 0 ||
    playerIndex >= int(mpDeathmatch->mParticipants.size()))
  {
    return;
  }

  mpDeathmatch->mParticipants[playerIndex].mPendingRemoteFrames.push_back(
    frame);
}


void GameWorld::applyPredictedPosition(
  const int playerIndex,
  const RemotePlayerFrame& frame)
{
  // The host is authoritative: The client's own simulation of its player
  // normally arrives at the same position as ours, since both apply the
  // same inputs in the same order. When it doesn't, that's because of
  // something only the host simulates (e.g. being pushed by a fan, or a
  // door that's closed). Our position is then the correct one, and the
  // client corrects itself when it learns about it via the snapshot.
  // We never take over the client's position, so that players can't move
  // through walls by sending fake positions.
  auto& participant = mpDeathmatch->mParticipants[playerIndex];
  participant.mLastInputSequence = frame.mSequence;

  if (
    frame.mPredictedPosition &&
    *frame.mPredictedPosition != participant.mpPlayer->position())
  {
    LOG_F(
      1,
      "Player %d prediction differs: client %d,%d host %d,%d",
      playerIndex + 1,
      frame.mPredictedPosition->x,
      frame.mPredictedPosition->y,
      participant.mpPlayer->position().x,
      participant.mpPlayer->position().y);
  }
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
  snapshot.mLastInputSequence = participant.mLastInputSequence;
  snapshot.mPlayerPosition = participant.mpPlayer->position();
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
  //
  // The client draws its own player while it's alive, since it simulates it
  // locally. So we leave it out here.
  auto playerEntity = participant.mpPlayer->entity();
  auto& playerSprite = *playerEntity.component<engine::components::Sprite>();
  const auto playerSpriteWasShown = playerSprite.mShow;
  if (!snapshot.mIsDead)
  {
    playerSprite.mShow = false;
  }

  //
  // The client's camera is usually a bit ahead of ours, since it follows the
  // client's own simulation of its player. So we include sprites a bit
  // outside of our view of this player as well, to avoid them popping in
  // late at the edges of the client's screen.
  const auto margin =
    base::Vec2{SNAPSHOT_SPRITE_MARGIN, SNAPSHOT_SPRITE_MARGIN};
  const auto collectionStart = snapshot.mCameraPosition - margin;
  const auto collectionSize = base::Size{
    participant.mRemoteViewportSize.width + 2 * SNAPSHOT_SPRITE_MARGIN,
    participant.mRemoteViewportSize.height + 2 * SNAPSHOT_SPRITE_MARGIN};

  state.mSpriteRenderingSystem.update(
    state.mEntities, collectionSize, collectionStart, 1.0f);

  playerSprite.mShow = playerSpriteWasShown;

  // The sprite rendering system gives us positions relative to the given
  // camera position. We send world positions instead, since the client
  // draws them relative to its own camera.
  const auto worldOffset = data::tilesToPixels(collectionStart);

  const auto& sprites = state.mSpriteRenderingSystem.sprites();
  snapshot.mSprites.reserve(sprites.size());
  for (const auto& spec : sprites)
  {
    NetSprite sprite;
    sprite.mDestRect = spec.mDestRect;
    sprite.mDestRect.topLeft += worldOffset;
    sprite.mImageId = spec.mImageId;
    sprite.mKey = spec.mKey;
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

  auto& state = *mpState;

  mpRemoteView = std::make_unique<RemoteViewState>();
  mpRemoteView->mPlayerIndex = playerIndex;
  mpRemoteView->mDynamicSections = collectDynamicSections(state.mEntities);
  mpRemoteView->mSnapshot.mCameraPosition = state.mCamera.position();

  // Our own player is simulated locally for responsiveness (prediction),
  // the rest comes from the host. We re-use the single player's Player
  // object for this, but silence it.
  mpRemoteView->mpMutedServiceProvider =
    std::make_unique<MutedServiceProvider>(mpServiceProvider);
  state.mPlayer.setServiceProvider(mpRemoteView->mpMutedServiceProvider.get());

  // The host tells us which sprites to draw. Remove all others, so that the
  // only sprite left in our own world is our player.
  const auto playerEntity = state.mPlayer.entity();
  std::vector<entityx::Entity> toRemove;
  state.mEntities.each<engine::components::Sprite>(
    [&](entityx::Entity entity, const engine::components::Sprite&)
    {
      if (entity != playerEntity)
      {
        toRemove.push_back(entity);
      }
    });
  for (auto entity : toRemove)
  {
    entity.destroy();
  }

  if (const auto tint = deathmatchPlayerTint(playerIndex))
  {
    auto entity = state.mPlayer.entity();
    entity.assign<engine::components::TintColor>(*tint);
  }

  // Wait for the first snapshot to know where we are
  setPlayerSpriteVisible(state.mPlayer, false);
  state.mSpriteRenderingSystem.setSprites({}, 0);
}


void GameWorld::updateRemoteView(const PlayerInput& input)
{
  // The game runs on the host. We only simulate our own player's movement
  // (prediction), and keep animations going which are not part of what the
  // host sends.
  auto& state = *mpState;
  auto& remoteView = *mpRemoteView;

  mMessageDisplay.update();
  state.mMapRenderer.updateAnimatedMapTiles();

  ++state.mWaterAnimStep;
  if (state.mWaterAnimStep >= 4)
  {
    state.mWaterAnimStep = 0;
  }

  remoteView.mInterpolator.advance();

  // Every 5 seconds, log how smooth the connection is
  if (++remoteView.mFramesSinceStatsLog >= 75)
  {
    remoteView.mFramesSinceStatsLog = 0;
    const auto stats = remoteView.mInterpolator.takeStats();
    LOG_F(
      INFO,
      "Network smoothness over %d frames: received %d stalls/%d skips, "
      "shown %d stalls/%d skips, delay %.1f frames",
      stats.mFrames,
      stats.mReceivedStalls,
      stats.mReceivedSkips,
      stats.mShownStalls,
      stats.mShownSkips,
      remoteView.mInterpolator.delay());
  }

  if (!remoteView.mIsPredicting)
  {
    return;
  }

  // Health is decided by the host. Our player never dies locally: The host
  // tells us when that happens.
  //
  // Weapon and ammo are simulated locally like movement, since they affect
  // it: The flame thrower works as a jetpack until it runs out of ammo.
  // Copying them from the host would make us use outdated values, since the
  // host's state is a little behind ours. They're corrected like the
  // position, see correctWeapon().
  const auto& snapshot = remoteView.mSnapshot;
  setHealth(*mpPersistentPlayerState, std::max(1, snapshot.mHealth));

  // Resets per-frame sprite effects like flashing white. Only our player is
  // left in our world at this point, so this doesn't affect anything else.
  engine::updateAnimatedSprites(state.mEntities);

  state.mPlayer.update(input);

  // Shooting creates projectiles and effects in our world, but the real
  // ones come from the host
  const auto playerEntity = state.mPlayer.entity();
  std::vector<entityx::Entity> toRemove;
  state.mEntities.each<engine::components::Sprite>(
    [&](entityx::Entity entity, const engine::components::Sprite&)
    {
      if (entity != playerEntity)
      {
        toRemove.push_back(entity);
      }
    });
  for (auto entity : toRemove)
  {
    entity.destroy();
  }

  state.mPreviousCameraPosition = state.mCamera.position();
  state.mCamera.update(input, remoteViewportSize());
}


base::Vec2 GameWorld::updateRemoteSprites(const float interpolationFactor)
{
  auto& state = *mpState;
  auto& remoteView = *mpRemoteView;

  std::vector<engine::SpriteDrawSpec> localSprites;
  if (remoteView.mIsPredicting)
  {
    state.mSpriteRenderingSystem.update(
      state.mEntities,
      remoteViewportSize(),
      state.mCamera.position(),
      interpolationFactor);
    localSprites = state.mSpriteRenderingSystem.sprites();
  }

  // Everything except our own player is shown slightly in the past, moving
  // smoothly between the snapshots we received
  const auto smooth = mpOptions->mMotionSmoothing;
  const auto hostFrame =
    remoteView.mInterpolator.sample(smooth ? interpolationFactor : 1.0f, smooth);

  // The host sends world positions. Draw them relative to our camera, which
  // may be ahead of the host's camera due to prediction. This keeps static
  // things like items exactly in place while our view moves.
  const auto cameraPosition = remoteView.mIsPredicting
    ? state.mCamera.position()
    : (remoteView.mInterpolator.hasData() ? hostFrame.mCameraPosition
                                          : remoteView.mSnapshot.mCameraPosition);
  const auto cameraOffset = data::tilesToPixels(cameraPosition);

  std::vector<engine::SpriteDrawSpec> hostSprites;
  hostSprites.reserve(hostFrame.mSprites.size());
  for (const auto& sprite : hostFrame.mSprites)
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
    spec.mDestRect.topLeft -= cameraOffset;

    hostSprites.push_back(spec);
  }

  // Our player goes on top of the host's regular sprites, but below the
  // ones that are drawn in front of the map's foreground
  const auto firstForeground = std::clamp(
    hostFrame.mFirstForegroundSprite, 0, int(hostSprites.size()));

  std::vector<engine::SpriteDrawSpec> sprites;
  sprites.reserve(hostSprites.size() + localSprites.size());
  sprites.insert(
    sprites.end(), hostSprites.begin(), hostSprites.begin() + firstForeground);
  sprites.insert(sprites.end(), localSprites.begin(), localSprites.end());
  sprites.insert(
    sprites.end(), hostSprites.begin() + firstForeground, hostSprites.end());

  state.mSpriteRenderingSystem.setSprites(
    std::move(sprites), firstForeground + int(localSprites.size()));

  return cameraPosition;
}


std::optional<base::Vec2> GameWorld::predictedPlayerPosition() const
{
  if (!mpRemoteView || !mpRemoteView->mIsPredicting)
  {
    return std::nullopt;
  }

  return mpState->mPlayer.position();
}


std::optional<PredictedPlayerState> GameWorld::predictedPlayerState() const
{
  if (!mpRemoteView || !mpRemoteView->mIsPredicting)
  {
    return std::nullopt;
  }

  return PredictedPlayerState{
    mpState->mPlayer.position(),
    static_cast<int>(mpPersistentPlayerState->weapon()),
    mpPersistentPlayerState->ammo()};
}


std::uint32_t GameWorld::viewedFrame() const
{
  return mpRemoteView ? mpRemoteView->mInterpolator.viewedFrame() : 0;
}


base::Vec2 GameWorld::correctPrediction(const base::Vec2& offset)
{
  if (!mpRemoteView || !mpRemoteView->mIsPredicting)
  {
    return {};
  }

  auto& position = mpState->mPlayer.position();
  const auto box = mpState->mPlayer.collisionBox();
  const auto isFree = [&](const base::Vec2& candidate) {
    return isAreaFree(mpState->mMap, engine::toWorldSpace(box, candidate));
  };

  // Shifting by the difference keeps the movement we simulated since then.
  // But if the difference is big, that movement happened somewhere else,
  // and the result could be inside a wall (e.g. we kept flying while the
  // host had us fall and land). Then we move as far as walls allow. The
  // rest of the difference shows up again in the next snapshot, and gets
  // corrected then.
  const auto start = position;
  if (isFree(start + offset))
  {
    position = start + offset;
  }
  else
  {
    auto stepTowards = [&](const int amount, const base::Vec2& step) {
      for (auto i = 0; i < std::abs(amount); ++i)
      {
        if (!isFree(position + step))
        {
          break;
        }

        position += step;
      }
    };

    stepTowards(offset.y, base::Vec2{0, offset.y < 0 ? -1 : 1});
    stepTowards(offset.x, base::Vec2{offset.x < 0 ? -1 : 1, 0});
  }

  return position - start;
}


void GameWorld::correctWeapon(const int weapon, const int ammo)
{
  auto& model = *mpPersistentPlayerState;

  const auto type = static_cast<data::WeaponType>(
    std::clamp(weapon, 0, static_cast<int>(data::WeaponType::FlameThrower)));
  if (model.weapon() != type)
  {
    model.switchToWeapon(type);
  }

  if (model.currentWeaponConsumesAmmo() && ammo <= 0)
  {
    model.switchToWeapon(data::WeaponType::Normal);
    return;
  }

  model.setAmmo(std::clamp(ammo, 0, model.currentMaxAmmo()));
}


void GameWorld::applySnapshot(const NetSnapshot& snapshot)
{
  auto& state = *mpState;
  auto& remoteView = *mpRemoteView;

  remoteView.mSnapshot = snapshot;
  state.mScreenShakeOffsetX = snapshot.mScreenShakeOffsetX;
  state.mScreenFlashColor = snapshot.mScreenFlashColor;

  remoteView.mInterpolator.addSnapshot(snapshot);

  // Our own player: Death and respawning are decided by the host
  auto& player = state.mPlayer;
  if (snapshot.mIsDead)
  {
    if (remoteView.mIsPredicting)
    {
      // The host now shows our death animation
      remoteView.mIsPredicting = false;
      setPlayerSpriteVisible(player, false);
    }
  }
  else if (!remoteView.mIsPredicting)
  {
    // First snapshot, or respawned: Start simulating from the host's state
    player.reSpawnAt(snapshot.mPlayerPosition);
    correctWeapon(snapshot.mWeapon, snapshot.mAmmo);
    setPlayerSpriteVisible(player, true);
    remoteView.mIsPredicting = true;
    remoteView.mLastHealth = snapshot.mHealth;

    state.mCamera.recenter(remoteViewportSize());
    state.mCamera.centerViewOnPlayer();
    state.mPreviousCameraPosition = state.mCamera.position();
  }
  else
  {
    // Getting hurt: Show the blinking effect on our player as well
    if (snapshot.mHealth < remoteView.mLastHealth)
    {
      setHealth(*mpPersistentPlayerState, data::MAX_HEALTH);
      player.takeDamage(1);
    }
    remoteView.mLastHealth = snapshot.mHealth;
  }

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
