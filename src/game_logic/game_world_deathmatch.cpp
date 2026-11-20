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

// Local split-screen deathmatch mode.
//
// This builds on top of the regular single-player GameWorld: The level is
// loaded as usual, then enableDeathmatch() adds additional players, removes
// enemies, and from then on updateDeathmatchLogic() and renderDeathmatch()
// replace the single-player update and render code.

#include "game_world.hpp"

#include "assets/level_loader.hpp"
#include "data/game_options.hpp"
#include "data/game_traits.hpp"
#include "data/sound_ids.hpp"
#include "data/unit_conversions.hpp"
#include "engine/base_components.hpp"
#include "engine/entity_activation_system.hpp"
#include "engine/entity_tools.hpp"
#include "engine/visual_components.hpp"
#include "frontend/game_service_provider.hpp"
#include "game_logic/actor_tag.hpp"
#include "game_logic/collectable_components.hpp"
#include "game_logic/deathmatch_state.hpp"
#include "game_logic/interactive/item_container.hpp"
#include "game_logic/player/components.hpp"
#include "game_logic/world_state.hpp"
#include "renderer/upscaling.hpp"

#include <loguru.hpp>

#include <algorithm>
#include <limits>
#include <string>


namespace rigel::game_logic
{

using engine::components::BoundingBox;
using engine::components::WorldPosition;

namespace
{

constexpr auto FRAMES_PER_SECOND = 15;
constexpr auto ROUND_END_DURATION_FRAMES = 6 * FRAMES_PER_SECOND;

// Safety net: Some ways of dying (e.g. getting sucked into space) might
// not reach the normal end of the death sequence. Respawn after this long
// regardless.
constexpr auto MAX_FRAMES_DEAD = 5 * FRAMES_PER_SECOND;

// Players 2 to 4 are tinted so that they can be told apart.
// Player 1 keeps Duke's original colors.
const base::Color PLAYER_TINTS[] = {
  {255, 255, 255, 255},
  {130, 170, 255, 255},
  {140, 255, 140, 255},
  {255, 220, 110, 255},
};


int pvpDamageForProjectile(const components::PlayerProjectile::Type type)
{
  using T = components::PlayerProjectile::Type;

  // The original damage values are tuned against enemies. Against other
  // players (9 health points), rockets would be instant kills, so we use
  // our own values here.
  switch (type)
  {
    case T::Normal:
      return 1;

    case T::Laser:
      return 2;

    case T::Rocket:
      return 4;

    case T::Flame:
      return 1;

    case T::ShipLaser:
      return 2;

    case T::ReactorDebris:
    default:
      return 0;
  }
}


bool projectilePassesThroughPlayers(
  const components::PlayerProjectile::Type type)
{
  using T = components::PlayerProjectile::Type;
  return type == T::Laser || type == T::ShipLaser;
}


const char* shortWeaponName(const data::WeaponType type)
{
  switch (type)
  {
    case data::WeaponType::Normal:
      return "GUN";

    case data::WeaponType::Laser:
      return "LASER";

    case data::WeaponType::Rocket:
      return "ROCKET";

    case data::WeaponType::FlameThrower:
      return "FLAME";
  }

  return "";
}


bool isFreeSpawnSpot(
  const data::map::Map& map,
  const engine::CollisionChecker& collisionChecker,
  const base::Vec2& position)
{
  const auto bbox = engine::toWorldSpace(DEFAULT_PLAYER_BOUNDS, position);

  if (
    bbox.left() < 0 || bbox.top() < 1 || bbox.right() >= map.width() ||
    bbox.bottom() + 1 >= map.height())
  {
    return false;
  }

  for (auto y = bbox.top(); y <= bbox.bottom(); ++y)
  {
    for (auto x = bbox.left(); x <= bbox.right(); ++x)
    {
      if (map.collisionData(x, y).isSolidOn(data::map::SolidEdge::any()))
      {
        return false;
      }
    }
  }

  return collisionChecker.isOnSolidGround(bbox);
}


std::optional<base::Vec2> findFreeSpawnSpotNear(
  const data::map::Map& map,
  const engine::CollisionChecker& collisionChecker,
  const base::Vec2& position)
{
  static constexpr int Y_OFFSETS[] = {0, 1, -1, 2, -2, 3, -3, 4, 5, 6};
  static constexpr int X_OFFSETS[] = {0, -1, -2, 1};

  for (const auto dy : Y_OFFSETS)
  {
    for (const auto dx : X_OFFSETS)
    {
      const auto candidate = position + base::Vec2{dx, dy};
      if (isFreeSpawnSpot(map, collisionChecker, candidate))
      {
        return candidate;
      }
    }
  }

  return std::nullopt;
}


int distanceSquared(const base::Vec2& a, const base::Vec2& b)
{
  const auto d = a - b;
  return d.x * d.x + d.y * d.y;
}

} // namespace


base::Size GameWorld::deathmatchWorldAreaSize() const
{
  if (widescreenModeOn())
  {
    const auto info = renderer::determineWidescreenViewport(mpRenderer);
    return {info.mWidthTiles, data::GameTraits::mapViewportSize.height};
  }

  return {
    data::pixelsToTiles(data::GameTraits::inGameViewportSize.width),
    data::GameTraits::mapViewportSize.height};
}


auto GameWorld::deathmatchViewports() const -> std::vector<DeathmatchViewport>
{
  const auto area = deathmatchWorldAreaSize();
  const auto numPlayers = int(mpDeathmatch->mParticipants.size());

  // Leave one tile of space between the views
  const auto leftWidth = (area.width - 1) / 2;
  const auto rightX = leftWidth + 1;
  const auto rightWidth = area.width - rightX;

  if (numPlayers <= 2)
  {
    return {
      {{0, 0}, {leftWidth, area.height}},
      {{rightX, 0}, {rightWidth, area.height}}};
  }

  const auto topHeight = (area.height - 1) / 2;
  const auto bottomY = topHeight + 1;
  const auto bottomHeight = area.height - bottomY;

  std::vector<DeathmatchViewport> result{
    {{0, 0}, {leftWidth, topHeight}},
    {{rightX, 0}, {rightWidth, topHeight}},
    {{0, bottomY}, {leftWidth, bottomHeight}},
    {{rightX, bottomY}, {rightWidth, bottomHeight}}};
  result.resize(numPlayers);
  return result;
}


void GameWorld::enableDeathmatch(const DeathmatchOptions& options)
{
  if (mpDeathmatch)
  {
    return;
  }

  LOG_SCOPE_FUNCTION(INFO);

  auto& state = *mpState;

  mpDeathmatch = std::make_unique<DeathmatchState>();
  auto& dm = *mpDeathmatch;
  dm.mOptions = options;
  dm.mOptions.mNumPlayers = std::clamp(
    options.mNumPlayers, MIN_DEATHMATCH_PLAYERS, MAX_DEATHMATCH_PLAYERS);
  dm.mOptions.mFragLimit = std::max(1, options.mFragLimit);
  dm.mOptions.mItemRespawnSeconds = std::max(1, options.mItemRespawnSeconds);

  mpQuickSave.reset();
  state.mActivatedCheckpoint.reset();

  // Remove enemies. Everything that shows up on the radar counts as enemy.
  if (!dm.mOptions.mKeepEnemies)
  {
    std::vector<entityx::Entity> toRemove;
    state.mEntities.each<components::AppearsOnRadar>(
      [&](entityx::Entity entity, const components::AppearsOnRadar&)
      { toRemove.push_back(entity); });

    for (auto entity : toRemove)
    {
      entity.destroy();
    }

    state.mActiveBossEntity = {};
    LOG_F(INFO, "Removed %d enemies", int(toRemove.size()));
  }

  // Set up item respawning. To re-create an item, we need its description
  // from the level file, so we load the level's actor list once more and
  // match it up with the item entities currently in the world (which are
  // still at their initial positions at this point).
  {
    auto levelData = assets::loadLevel(
      assets::levelFileName(mSessionId.mEpisode, mSessionId.mLevel),
      *mpResources,
      mSessionId.mDifficulty);

    std::vector<bool> actorUsed(levelData.mActors.size(), false);

    auto registerSpawner = [&](entityx::Entity entity)
    {
      const auto& position = *entity.component<const WorldPosition>();
      for (auto i = 0u; i < levelData.mActors.size(); ++i)
      {
        const auto& actor = levelData.mActors[i];
        if (
          !actorUsed[i] && !actor.mAssignedArea && actor.mPosition == position)
        {
          actorUsed[i] = true;
          dm.mItemSpawners.push_back(ItemSpawner{actor, entity});
          return;
        }
      }
    };

    state.mEntities.each<components::CollectableItem, WorldPosition>(
      [&](
        entityx::Entity entity,
        const components::CollectableItem&,
        const WorldPosition&) { registerSpawner(entity); });
    state.mEntities.each<components::ItemContainer, WorldPosition>(
      [&](
        entityx::Entity entity,
        const components::ItemContainer&,
        const WorldPosition&) { registerSpawner(entity); });

    LOG_F(INFO, "Registered %d item spawners", int(dm.mItemSpawners.size()));
  }

  // Collect spawn points. Places where the level designer put items are
  // usually reachable, so we use those plus the original player start.
  {
    auto addSpawnPoint = [&](const base::Vec2& position)
    {
      if (
        auto spot =
          findFreeSpawnSpotNear(state.mMap, state.mCollisionChecker, position))
      {
        const auto tooClose = std::any_of(
          dm.mSpawnPoints.begin(),
          dm.mSpawnPoints.end(),
          [&](const base::Vec2& existing)
          { return distanceSquared(existing, *spot) < 4 * 4; });

        if (!tooClose)
        {
          dm.mSpawnPoints.push_back(*spot);
        }
      }
    };

    addSpawnPoint(state.mPlayer.position());
    for (const auto& spawner : dm.mItemSpawners)
    {
      addSpawnPoint(spawner.mActor.mPosition);
    }

    // Not enough items in this level? Scan the map for open spots.
    if (dm.mSpawnPoints.size() < 8)
    {
      for (auto y = 1; y < state.mMap.height() - 1; y += 3)
      {
        for (auto x = 0; x < state.mMap.width() - 3; x += 5)
        {
          if (isFreeSpawnSpot(state.mMap, state.mCollisionChecker, {x, y}))
          {
            addSpawnPoint({x, y});
          }
        }
      }
    }

    if (dm.mSpawnPoints.empty())
    {
      dm.mSpawnPoints.push_back(state.mPlayer.position());
    }

    LOG_F(INFO, "Found %d spawn points", int(dm.mSpawnPoints.size()));
  }

  // Player 1: Re-use the existing single-player objects
  {
    DeathmatchParticipant participant;
    participant.mpModel = mpPersistentPlayerState;
    participant.mpPlayer = &state.mPlayer;
    participant.mpCamera = &state.mCamera;
    participant.mpInteractionSystem = &state.mPlayerInteractionSystem;
    participant.mpDamageSystem = &state.mPlayerDamageSystem;
    dm.mParticipants.push_back(std::move(participant));
  }

  // Players 2 and up
  for (auto i = 1; i < dm.mOptions.mNumPlayers; ++i)
  {
    DeathmatchParticipant participant;
    participant.mpOwnedModel = std::make_unique<data::PersistentPlayerState>();
    participant.mpOwnedModel->tutorialMessages() =
      mpPersistentPlayerState->tutorialMessages();

    auto playerEntity = state.mEntityFactory.spawnActor(
      data::ActorID::Duke_LEFT, state.mPlayer.position());
    assignPlayerComponents(
      playerEntity, engine::components::Orientation::Right);
    playerEntity.assign<engine::components::TintColor>(PLAYER_TINTS[i]);

    participant.mpOwnedPlayer = std::make_unique<Player>(
      playerEntity,
      mSessionId.mDifficulty,
      participant.mpOwnedModel.get(),
      mpServiceProvider,
      mpOptions,
      &state.mCollisionChecker,
      &state.mMap,
      &state.mEntityFactory,
      &state.mEventManager,
      &state.mRandomGenerator);
    participant.mpOwnedCamera = std::make_unique<Camera>(
      participant.mpOwnedPlayer.get(), state.mMap, state.mEventManager);
    participant.mpOwnedInteractionSystem =
      std::make_unique<PlayerInteractionSystem>(
        mSessionId,
        participant.mpOwnedPlayer.get(),
        participant.mpOwnedModel.get(),
        mpServiceProvider,
        &state.mEntityFactory,
        &state.mEventManager,
        *mpResources);
    participant.mpOwnedDamageSystem =
      std::make_unique<player::DamageSystem>(participant.mpOwnedPlayer.get());

    participant.mpModel = participant.mpOwnedModel.get();
    participant.mpPlayer = participant.mpOwnedPlayer.get();
    participant.mpCamera = participant.mpOwnedCamera.get();
    participant.mpInteractionSystem =
      participant.mpOwnedInteractionSystem.get();
    participant.mpDamageSystem = participant.mpOwnedDamageSystem.get();

    dm.mParticipants.push_back(std::move(participant));
  }

  // Everyone starts with a fresh state, at spread-out positions
  *mpPersistentPlayerState = data::PersistentPlayerState{};
  mpPersistentPlayerState->tutorialMessages() =
    mPlayerModelAtLevelStart.tutorialMessages();

  for (auto i = 1; i < dm.mOptions.mNumPlayers; ++i)
  {
    respawnDeathmatchPlayer(i);
  }

  const auto viewports = deathmatchViewports();
  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    auto& participant = dm.mParticipants[i];
    participant.mpCamera->recenter(viewports[i].mSize);
    participant.mpCamera->centerViewOnPlayer();
    participant.mPreviousCameraPosition = participant.mpCamera->position();
  }
  state.mPreviousCameraPosition = state.mCamera.position();

  mMessageDisplay.setMessage(
    "DEATHMATCH! FIRST TO " + std::to_string(dm.mOptions.mFragLimit) +
    " KILLS WINS");

  LOG_F(INFO, "Deathmatch enabled with %d players", dm.mOptions.mNumPlayers);
}


void GameWorld::updateGameLogicMultiplayer(
  const std::vector<PlayerInput>& inputs)
{
  if (mpDeathmatch)
  {
    updateDeathmatchLogic(inputs);
  }
  else
  {
    updateGameLogic(inputs.empty() ? PlayerInput{} : inputs.front());
  }
}


void GameWorld::updateDeathmatchLogic(const std::vector<PlayerInput>& inputs)
{
  auto& state = *mpState;
  auto& dm = *mpDeathmatch;

  auto inputFor = [&](const int index)
  {
    // Freeze everyone during the end-of-round screen
    if (dm.mRoundWinner || index >= int(inputs.size()))
    {
      return PlayerInput{};
    }

    return inputs[index];
  };

  state.mBackdropFlashColor = std::nullopt;
  state.mScreenFlashColor = std::nullopt;

  if (state.mReactorDestructionFramesElapsed)
  {
    updateReactorDestructionEvent();
  }

  if (state.mEarthQuakeEffect)
  {
    state.mEarthQuakeEffect->update();
  }

  mHudRenderer.updateAnimation();
  mMessageDisplay.update();

  updateMotionSmoothingStates();

  const auto viewports = deathmatchViewports();

  state.mMapRenderer.updateAnimatedMapTiles();
  engine::updateAnimatedSprites(state.mEntities);
  ++state.mWaterAnimStep;
  if (state.mWaterAnimStep >= 4)
  {
    state.mWaterAnimStep = 0;
  }

  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    auto& participant = dm.mParticipants[i];
    const auto input = inputFor(i);

    participant.mpInteractionSystem->updatePlayerInteraction(
      input, state.mEntities);

    // Teleporters report their destination via an event, which GameWorld
    // stores. In single player, the teleport happens at the end of the
    // frame. Here, we need to know which player used the teleporter, so we
    // apply it right away.
    if (state.mTeleportTargetPosition)
    {
      participant.mpPlayer->position() = *state.mTeleportTargetPosition;
      state.mTeleportTargetPosition = std::nullopt;
      participant.mpCamera->centerViewOnPlayer();
      participant.mPreviousCameraPosition = participant.mpCamera->position();
    }

    participant.mpPlayer->update(input);

    participant.mPreviousCameraPosition = participant.mpCamera->position();
    participant.mpCamera->update(input, viewports[i].mSize);
  }

  state.mPreviousCameraPosition = dm.mParticipants[0].mPreviousCameraPosition;

  state.mDynamicGeometrySystem.updateShootableWalls();

  std::vector<base::Rect<int>> activeRegions;
  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    activeRegions.push_back(BoundingBox{
      dm.mParticipants[i].mpCamera->position(), viewports[i].mSize});
  }

  engine::markActiveEntities(state.mEntities, activeRegions);
  state.mBehaviorControllerSystem.update(
    state.mEntities,
    PerFrameState{
      inputFor(0),
      viewports[0].mSize,
      state.mRadarDishCounter.numRadarDishes(),
      state.mIsOddFrame,
      state.mEarthQuakeEffect && state.mEarthQuakeEffect->isQuaking()});

  state.mPhysicsSystem.updatePhase1(state.mEntities);

  state.mItemContainerSystem.updateItemBounce(state.mEntities);
  for (auto& participant : dm.mParticipants)
  {
    participant.mpInteractionSystem->updateItemCollection(state.mEntities);
  }

  for (auto& participant : dm.mParticipants)
  {
    participant.mpDamageSystem->update(state.mEntities);
  }

  applyPlayerVsPlayerDamage();

  state.mDamageInflictionSystem.update(state.mEntities);
  state.mItemContainerSystem.update(state.mEntities);
  state.mPlayerProjectileSystem.update(state.mEntities);

  state.mEffectsSystem.update(state.mEntities);
  state.mLifeTimeSystem.update(state.mEntities, activeRegions);

  state.mPhysicsSystem.updatePhase2(state.mEntities);

  state.mParticles.update();

  updateDeathmatchRules();
  updateItemRespawning();

  state.mIsOddFrame = !state.mIsOddFrame;
}


void GameWorld::applyPlayerVsPlayerDamage()
{
  auto& state = *mpState;
  auto& participants = mpDeathmatch->mParticipants;

  auto indexOfPlayerEntity = [&](const entityx::Entity entity)
  {
    for (auto i = 0u; i < participants.size(); ++i)
    {
      if (participants[i].mpPlayer->entity() == entity)
      {
        return std::optional<int>{int(i)};
      }
    }

    return std::optional<int>{};
  };

  std::vector<entityx::Entity> projectilesToDestroy;

  state.mEntities.each<
    components::ProjectileOwner,
    components::PlayerProjectile,
    WorldPosition,
    BoundingBox>(
    [&](
      entityx::Entity projectile,
      const components::ProjectileOwner& owner,
      const components::PlayerProjectile& projectileInfo,
      const WorldPosition& position,
      const BoundingBox& bbox)
    {
      const auto damage = pvpDamageForProjectile(projectileInfo.mType);
      if (damage == 0)
      {
        return;
      }

      const auto shooterIndex = indexOfPlayerEntity(owner.mOwner);
      const auto projectileBox = engine::toWorldSpace(bbox, position);

      for (auto i = 0u; i < participants.size(); ++i)
      {
        auto& target = participants[i];
        if (
          (shooterIndex && *shooterIndex == int(i)) ||
          target.mpPlayer->isDead())
        {
          continue;
        }

        if (!projectileBox.intersects(target.mpPlayer->worldSpaceHitBox()))
        {
          continue;
        }

        const auto wasInMercyFrames = target.mpPlayer->isInMercyFrames();
        target.mpPlayer->takeDamage(damage);

        if (!wasInMercyFrames && shooterIndex)
        {
          target.mLastAttacker = shooterIndex;
        }

        if (!projectilePassesThroughPlayers(projectileInfo.mType))
        {
          projectilesToDestroy.push_back(projectile);
          return;
        }
      }
    });

  for (auto projectile : projectilesToDestroy)
  {
    if (projectile.valid())
    {
      projectile.destroy();
    }
  }
}


void GameWorld::updateDeathmatchRules()
{
  auto& dm = *mpDeathmatch;
  auto& participants = dm.mParticipants;

  if (dm.mRoundWinner)
  {
    ++dm.mFramesSinceRoundEnd;
    if (dm.mFramesSinceRoundEnd >= ROUND_END_DURATION_FRAMES)
    {
      restartDeathmatchRound();
    }

    return;
  }

  for (auto i = 0u; i < participants.size(); ++i)
  {
    auto& participant = participants[i];
    auto& player = *participant.mpPlayer;

    if (!player.isDead())
    {
      continue;
    }

    if (!participant.mDeathCounted)
    {
      participant.mDeathCounted = true;
      ++participant.mDeaths;

      const auto killer = participant.mLastAttacker;
      if (killer && *killer != int(i))
      {
        auto& killerParticipant = participants[*killer];
        ++killerParticipant.mFrags;

        mMessageDisplay.setMessage(
          "P" + std::to_string(*killer + 1) + " FRAGGED P" +
          std::to_string(i + 1) + "!");

        if (killerParticipant.mFrags >= dm.mOptions.mFragLimit)
        {
          dm.mRoundWinner = *killer;
          dm.mFramesSinceRoundEnd = 0;
          mMessageDisplay.setMessage(
            "PLAYER " + std::to_string(*killer + 1) + " WINS THE ROUND!",
            ui::MessagePriority::Menu);
          mpServiceProvider->playSound(data::SoundId::BigExplosion);
          return;
        }
      }
      else
      {
        mMessageDisplay.setMessage("P" + std::to_string(i + 1) + " DIED");
      }
    }

    ++participant.mFramesDead;
    if (
      player.deathSequenceFinished() ||
      participant.mFramesDead >= MAX_FRAMES_DEAD)
    {
      respawnDeathmatchPlayer(int(i));
    }
  }
}


void GameWorld::updateItemRespawning()
{
  auto& state = *mpState;
  auto& dm = *mpDeathmatch;

  const auto respawnDelay = dm.mOptions.mItemRespawnSeconds * FRAMES_PER_SECOND;

  for (auto& spawner : dm.mItemSpawners)
  {
    if (spawner.mEntity.valid())
    {
      continue;
    }

    if (spawner.mFramesUntilRespawn < 0)
    {
      spawner.mFramesUntilRespawn = respawnDelay;
    }

    --spawner.mFramesUntilRespawn;
    if (spawner.mFramesUntilRespawn <= 0)
    {
      spawner.mEntity =
        state.mEntityFactory.createEntityForLevelActor(spawner.mActor);
      spawner.mFramesUntilRespawn = -1;
    }
  }
}


base::Vec2 GameWorld::chooseDeathmatchSpawnPoint(const int forPlayerIndex)
{
  auto& dm = *mpDeathmatch;

  // Score each spawn point by its distance to the closest other living
  // player, then pick randomly among the best few. This keeps players from
  // spawning right next to each other, without being too predictable.
  std::vector<std::pair<int, base::Vec2>> scored;
  for (const auto& spawnPoint : dm.mSpawnPoints)
  {
    auto closest = std::numeric_limits<int>::max();
    for (auto i = 0u; i < dm.mParticipants.size(); ++i)
    {
      const auto& other = dm.mParticipants[i];
      if (int(i) == forPlayerIndex || other.mpPlayer->isDead())
      {
        continue;
      }

      closest = std::min(
        closest, distanceSquared(spawnPoint, other.mpPlayer->position()));
    }

    scored.emplace_back(closest, spawnPoint);
  }

  std::sort(
    scored.begin(),
    scored.end(),
    [](const auto& lhs, const auto& rhs) { return lhs.first > rhs.first; });

  const auto numCandidates = std::min<int>(3, int(scored.size()));
  return scored[dm.nextRandom(numCandidates)].second;
}


void GameWorld::respawnDeathmatchPlayer(const int index)
{
  auto& participant = mpDeathmatch->mParticipants[index];
  auto& player = *participant.mpPlayer;
  auto& model = *participant.mpModel;

  const auto tutorialMessages = model.tutorialMessages();
  model = data::PersistentPlayerState{};
  model.tutorialMessages() = tutorialMessages;

  player.reSpawnAt(chooseDeathmatchSpawnPoint(index));

  auto playerEntity = player.entity();
  auto& sprite = *playerEntity.component<engine::components::Sprite>();
  sprite.mShow = true;
  sprite.mUseCloakEffect = false;

  participant.mLastAttacker.reset();
  participant.mDeathCounted = false;
  participant.mFramesDead = 0;

  participant.mpCamera->centerViewOnPlayer();
  participant.mPreviousCameraPosition = participant.mpCamera->position();
  if (index == 0)
  {
    mpState->mPreviousCameraPosition = participant.mPreviousCameraPosition;
  }
}


void GameWorld::restartDeathmatchRound()
{
  auto& dm = *mpDeathmatch;

  dm.mRoundWinner.reset();
  dm.mFramesSinceRoundEnd = 0;

  for (auto& participant : dm.mParticipants)
  {
    participant.mFrags = 0;
    participant.mDeaths = 0;
  }

  for (auto i = 0; i < int(dm.mParticipants.size()); ++i)
  {
    respawnDeathmatchPlayer(i);
  }

  mMessageDisplay.setMessage("NEW ROUND - FIGHT!", ui::MessagePriority::Menu);
}


void GameWorld::drawDeathmatchStatus(
  const std::vector<DeathmatchViewport>& viewports)
{
  const auto& dm = *mpDeathmatch;

  // Per-player status in the top-left corner of each view
  for (auto i = 0u; i < viewports.size(); ++i)
  {
    const auto& participant = dm.mParticipants[i];
    const auto& model = *participant.mpModel;
    const auto& offset = viewports[i].mOffset;

    const auto healthText = participant.mpPlayer->isDead()
      ? std::string{"DEAD"}
      : "HP " + std::to_string(model.health());
    mTextRenderer.drawText(
      offset.x,
      offset.y,
      "P" + std::to_string(i + 1) + " " + healthText + " K" +
        std::to_string(participant.mFrags));

    auto weaponText = std::string{shortWeaponName(model.weapon())};
    if (model.currentWeaponConsumesAmmo())
    {
      weaponText += " " + std::to_string(model.ammo());
    }
    mTextRenderer.drawText(offset.x, offset.y + 1, weaponText);
  }

  // Scoreboard below the views
  const auto area = deathmatchWorldAreaSize();
  const auto scoreboardY = area.height + 1;

  std::string scores;
  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    const auto& participant = dm.mParticipants[i];
    scores += "P" + std::to_string(i + 1) + ":" +
      std::to_string(participant.mFrags) + "/" +
      std::to_string(participant.mDeaths) + "  ";
  }

  mTextRenderer.drawText(1, scoreboardY, scores);
  mTextRenderer.drawText(
    1,
    scoreboardY + 1,
    "KILLS/DEATHS - LIMIT " + std::to_string(dm.mOptions.mFragLimit));
}

} // namespace rigel::game_logic
