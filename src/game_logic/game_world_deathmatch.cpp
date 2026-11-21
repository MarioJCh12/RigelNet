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
#include "game_logic/deathmatch_spawn_points.hpp"
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


/** Actors that attack or chase the player.
 *
 * Their behavior is written for a single player, so in deathmatch they
 * would only ever go after player 1. Hazards that hurt anyone who touches
 * them (fire, spikes, the smash hammer etc.) are not on this list.
 */
bool isEnemyActor(const data::ActorID id)
{
  using ID = data::ActorID;

  switch (id)
  {
    case ID::Hoverbot:
    case ID::Big_green_cat_LEFT:
    case ID::Big_green_cat_RIGHT:
    case ID::Watchbot:
    case ID::Watchbot_container_carrier:
    case ID::Rocket_launcher_turret:
    case ID::Enemy_rocket_left:
    case ID::Enemy_rocket_up:
    case ID::Enemy_rocket_right:
    case ID::Enemy_rocket_2_up:
    case ID::Enemy_rocket_2_down:
    case ID::Enemy_laser_shot_RIGHT:
    case ID::Bomb_dropping_spaceship:
    case ID::Green_slime_blob:
    case ID::Green_slime_container:
    case ID::Snake:
    case ID::Green_hanging_suction_plant:
    case ID::Wall_walker:
    case ID::Eyeball_thrower_LEFT:
    case ID::Sentry_robot_generator:
    case ID::Skeleton:
    case ID::Metal_grabber_claw:
    case ID::Hovering_laser_turret:
    case ID::Spider:
    case ID::Windblown_spider_generator:
    case ID::Ugly_green_bird:
    case ID::Spiked_green_creature_LEFT:
    case ID::Spiked_green_creature_RIGHT:
    case ID::Small_flying_ship_1:
    case ID::Small_flying_ship_2:
    case ID::Small_flying_ship_3:
    case ID::Blue_guard_RIGHT:
    case ID::Blue_guard_LEFT:
    case ID::Blue_guard_using_a_terminal:
    case ID::Laser_turret:
    case ID::BOSS_Episode_1:
    case ID::BOSS_Episode_2:
    case ID::BOSS_Episode_3:
    case ID::BOSS_Episode_4:
    case ID::BOSS_Episode_4_projectile:
    case ID::Red_bird:
    case ID::Unicycle_bot:
    case ID::Aggressive_prisoner:
    case ID::Rigelatin_soldier:
      return true;

    default:
      return false;
  }
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
  const auto& participants = mpDeathmatch->mParticipants;

  // Only players on this computer get a view on screen. Network players
  // are drawn on their own computers.
  std::vector<int> localPlayers;
  for (auto i = 0; i < int(participants.size()); ++i)
  {
    if (participants[i].mIsLocal)
    {
      localPlayers.push_back(i);
    }
  }

  if (localPlayers.size() <= 1)
  {
    return {{localPlayers.empty() ? 0 : localPlayers[0], {0, 0}, area}};
  }

  // Leave one tile of space between the views
  const auto leftWidth = (area.width - 1) / 2;
  const auto rightX = leftWidth + 1;
  const auto rightWidth = area.width - rightX;

  std::vector<DeathmatchViewport> layout;
  if (localPlayers.size() == 2)
  {
    layout = {
      {0, {0, 0}, {leftWidth, area.height}},
      {0, {rightX, 0}, {rightWidth, area.height}}};
  }
  else
  {
    const auto topHeight = (area.height - 1) / 2;
    const auto bottomY = topHeight + 1;
    const auto bottomHeight = area.height - bottomY;

    layout = {
      {0, {0, 0}, {leftWidth, topHeight}},
      {0, {rightX, 0}, {rightWidth, topHeight}},
      {0, {0, bottomY}, {leftWidth, bottomHeight}},
      {0, {rightX, bottomY}, {rightWidth, bottomHeight}}};
    layout.resize(localPlayers.size());
  }

  for (auto i = 0u; i < layout.size(); ++i)
  {
    layout[i].mParticipant = localPlayers[i];
  }

  return layout;
}


std::vector<base::Size> GameWorld::deathmatchViewportSizes() const
{
  const auto& participants = mpDeathmatch->mParticipants;

  std::vector<base::Size> result;
  for (const auto& participant : participants)
  {
    result.push_back(participant.mRemoteViewportSize);
  }

  for (const auto& viewport : deathmatchViewports())
  {
    result[viewport.mParticipant] = viewport.mSize;
  }

  return result;
}


void GameWorld::setDeathmatchParticipantActive(
  const int index,
  const bool active)
{
  auto& participant = mpDeathmatch->mParticipants[index];
  if (participant.mIsActive == active)
  {
    return;
  }

  participant.mIsActive = active;
  participant.mFrags = 0;
  participant.mDeaths = 0;

  if (active)
  {
    respawnDeathmatchPlayer(index);
    mMessageDisplay.setMessage(
      "PLAYER " + std::to_string(index + 1) + " JOINED THE GAME");
  }
  else
  {
    auto playerEntity = participant.mpPlayer->entity();
    playerEntity.component<engine::components::Sprite>()->mShow = false;
    mMessageDisplay.setMessage(
      "PLAYER " + std::to_string(index + 1) + " LEFT THE GAME");
  }
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
  dm.mOptions.mNumLocalPlayers = options.mNumLocalPlayers <= 0
    ? dm.mOptions.mNumPlayers
    : std::min(options.mNumLocalPlayers, dm.mOptions.mNumPlayers);

  mpQuickSave.reset();
  state.mActivatedCheckpoint.reset();

  // Remove enemies. Hazards (fire, spikes etc.) stay, as they affect all
  // players equally.
  if (!dm.mOptions.mKeepEnemies)
  {
    std::vector<entityx::Entity> toRemove;
    state.mEntities.each<components::LevelActor>(
      [&](entityx::Entity entity, const components::LevelActor& actor) {
        if (isEnemyActor(actor.mId))
        {
          toRemove.push_back(entity);
        }
      });

    for (auto entity : toRemove)
    {
      entity.destroy();
    }

    state.mActiveBossEntity = {};
    LOG_F(INFO, "Removed %d enemies", int(toRemove.size()));
  }

  // Map sections that can be opened or destroyed during gameplay (shootable
  // walls, sliding doors etc.), needed for finding spawn points
  std::vector<base::Rect<int>> openableAreas;

  // Set up item respawning. To re-create an item, we need its description
  // from the level file, so we load the level's actor list once more and
  // match it up with the item entities currently in the world (which are
  // still at their initial positions at this point).
  {
    auto levelData = assets::loadLevel(
      assets::levelFileName(mSessionId.mEpisode, mSessionId.mLevel),
      *mpResources,
      mSessionId.mDifficulty);

    for (const auto& actor : levelData.mActors)
    {
      if (actor.mAssignedArea)
      {
        openableAreas.push_back(*actor.mAssignedArea);
      }
    }

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
  // usually good candidates, so we use those plus the original player start.
  {
    std::vector<base::Vec2> itemPositions;
    for (const auto& spawner : dm.mItemSpawners)
    {
      itemPositions.push_back(spawner.mActor.mPosition);
    }

    dm.mSpawnPoints = findDeathmatchSpawnPoints(
      state.mMap, state.mPlayer.position(), itemPositions, openableAreas);

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
    playerEntity.assign<engine::components::TintColor>(
      *deathmatchPlayerTint(i));

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

  // Slots beyond the local players are for network players. They stay
  // empty until someone connects.
  for (auto i = 0; i < dm.mOptions.mNumPlayers; ++i)
  {
    auto& participant = dm.mParticipants[i];
    participant.mIsLocal = i < dm.mOptions.mNumLocalPlayers;
    participant.mIsActive = participant.mIsLocal;
  }

  for (auto i = 1; i < dm.mOptions.mNumPlayers; ++i)
  {
    if (dm.mParticipants[i].mIsActive)
    {
      respawnDeathmatchPlayer(i);
    }
    else
    {
      auto playerEntity = dm.mParticipants[i].mpPlayer->entity();
      playerEntity.component<engine::components::Sprite>()->mShow = false;
    }
  }

  const auto viewportSizes = deathmatchViewportSizes();
  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    auto& participant = dm.mParticipants[i];
    participant.mpCamera->recenter(viewportSizes[i]);
    participant.mpCamera->centerViewOnPlayer();
    participant.mPreviousCameraPosition = participant.mpCamera->position();
  }
  state.mPreviousCameraPosition = state.mCamera.position();

  mMessageDisplay.setMessage(
    "DEATHMATCH! FIRST TO " + std::to_string(dm.mOptions.mFragLimit) +
    " KILLS WINS");

  initNetworkHostState();

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

  const auto viewportSizes = deathmatchViewportSizes();

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
    if (!participant.mIsActive)
    {
      continue;
    }

    auto input = inputFor(i);

    // Local players move once per frame. Network players move once for each
    // of their inputs that arrived: Usually one per frame, sometimes none
    // (late), sometimes two (catching up after a lag spike). This keeps our
    // simulation of them in step with the client's own (prediction), one
    // input at a time.
    std::vector<std::optional<RemotePlayerFrame>> steps;
    if (participant.mIsLocal)
    {
      steps.emplace_back();
    }
    else
    {
      for (auto& frame : std::exchange(participant.mPendingRemoteFrames, {}))
      {
        steps.emplace_back(frame);
      }

      if (steps.empty())
      {
        continue;
      }
    }

    for (const auto& remoteFrame : steps)
    {
      if (remoteFrame)
      {
        if (!dm.mRoundWinner)
        {
          input = remoteFrame->mInput;
        }

        // The client sees the other players slightly in the past. Shots
        // fired now get checked against where they were back then.
        participant.mRewindFrames = dm.mOptions.mLagCompensation
          ? lagCompensationFrames(
              dm.mFrameCounter + 1, remoteFrame->mViewedFrame)
          : 0;
      }

      participant.mpInteractionSystem->updatePlayerInteraction(
        input, state.mEntities);

      // Teleporters report their destination via an event, which GameWorld
      // stores. In single player, the teleport happens at the end of the
      // frame. Here, we need to know which player used the teleporter, so
      // we apply it right away.
      if (state.mTeleportTargetPosition)
      {
        participant.mpPlayer->position() = *state.mTeleportTargetPosition;
        state.mTeleportTargetPosition = std::nullopt;
        participant.mpCamera->centerViewOnPlayer();
        participant.mPreviousCameraPosition = participant.mpCamera->position();
      }

      participant.mpPlayer->update(input);

      if (remoteFrame)
      {
        applyPredictedPosition(int(i), *remoteFrame);
      }
    }

    participant.mPreviousCameraPosition = participant.mpCamera->position();
    participant.mpCamera->update(input, viewportSizes[i]);
  }

  state.mPreviousCameraPosition = dm.mParticipants[0].mPreviousCameraPosition;

  assignProjectileRewind();

  state.mDynamicGeometrySystem.updateShootableWalls();

  std::vector<base::Rect<int>> activeRegions;
  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    if (dm.mParticipants[i].mIsActive)
    {
      activeRegions.push_back(BoundingBox{
        dm.mParticipants[i].mpCamera->position(), viewportSizes[i]});
    }
  }

  engine::markActiveEntities(state.mEntities, activeRegions);
  state.mBehaviorControllerSystem.update(
    state.mEntities,
    PerFrameState{
      inputFor(0),
      viewportSizes[0],
      state.mRadarDishCounter.numRadarDishes(),
      state.mIsOddFrame,
      state.mEarthQuakeEffect && state.mEarthQuakeEffect->isQuaking()});

  state.mPhysicsSystem.updatePhase1(state.mEntities);

  state.mItemContainerSystem.updateItemBounce(state.mEntities);
  for (auto& participant : dm.mParticipants)
  {
    if (participant.mIsActive)
    {
      participant.mpInteractionSystem->updateItemCollection(state.mEntities);
    }
  }

  for (auto& participant : dm.mParticipants)
  {
    if (participant.mIsActive)
    {
      participant.mpDamageSystem->update(state.mEntities);
    }
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

  ++dm.mFrameCounter;

  for (auto& participant : dm.mParticipants)
  {
    const auto canBeHit =
      participant.mIsActive && !participant.mpPlayer->isDead();
    participant.mHitBoxHistory.record(
      dm.mFrameCounter,
      canBeHit
        ? std::optional<base::Rect<int>>{participant.mpPlayer->worldSpaceHitBox()}
        : std::nullopt);
  }

  state.mIsOddFrame = !state.mIsOddFrame;
}


void GameWorld::assignProjectileRewind()
{
  auto& participants = mpDeathmatch->mParticipants;

  mpState->mEntities.each<components::ProjectileOwner>(
    [&](entityx::Entity, components::ProjectileOwner& owner)
    {
      if (owner.mRewindFrames)
      {
        return;
      }

      // Fired during this frame
      owner.mRewindFrames = 0;
      for (const auto& participant : participants)
      {
        if (participant.mpPlayer->entity() == owner.mOwner)
        {
          owner.mRewindFrames = participant.mIsLocal ? 0 : participant.mRewindFrames;
          break;
        }
      }
    });
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
          (shooterIndex && *shooterIndex == int(i)) || !target.mIsActive ||
          target.mpPlayer->isDead())
        {
          continue;
        }

        // Lag compensation: Check against where the target was on the
        // shooter's screen. The target's current hit box applies if there's
        // nothing to compensate for.
        const auto rewind = owner.mRewindFrames.value_or(0);
        const auto targetBox = rewind > 0
          ? target.mHitBoxHistory.at(
              mpDeathmatch->mFrameCounter + 1 - std::uint32_t(rewind))
          : std::optional<base::Rect<int>>{
              target.mpPlayer->worldSpaceHitBox()};
        if (!targetBox || !projectileBox.intersects(*targetBox))
        {
          continue;
        }

        const auto wasInMercyFrames = target.mpPlayer->isInMercyFrames();
        target.mpPlayer->takeDamage(damage);

        if (rewind > 0 && !wasInMercyFrames)
        {
          LOG_F(
            INFO,
            "Player %d hit player %d, judged %d frames in the past (lag "
            "compensation)%s",
            shooterIndex ? *shooterIndex + 1 : 0,
            int(i) + 1,
            rewind,
            projectileBox.intersects(target.mpPlayer->worldSpaceHitBox())
              ? ""
              : ", would have missed without");
        }

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

    if (!participant.mIsActive || !player.isDead())
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
      if (
        int(i) == forPlayerIndex || !other.mIsActive ||
        other.mpPlayer->isDead())
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
    if (dm.mParticipants[i].mIsActive)
    {
      respawnDeathmatchPlayer(i);
    }
  }

  mMessageDisplay.setMessage("NEW ROUND - FIGHT!", ui::MessagePriority::Menu);
}


void GameWorld::drawDeathmatchStatus(
  const std::vector<DeathmatchViewport>& viewports)
{
  const auto& dm = *mpDeathmatch;

  // Per-player status in the top-left corner of each view
  for (const auto& viewport : viewports)
  {
    const auto& participant = dm.mParticipants[viewport.mParticipant];
    const auto& model = *participant.mpModel;

    drawDeathmatchPlayerStatus(
      viewport.mOffset,
      DeathmatchPlayerStatus{
        viewport.mParticipant,
        participant.mpPlayer->isDead(),
        model.health(),
        model.weapon(),
        model.ammo(),
        model.currentWeaponConsumesAmmo(),
        participant.mFrags});
  }

  std::vector<DeathmatchScore> scores;
  for (auto i = 0u; i < dm.mParticipants.size(); ++i)
  {
    const auto& participant = dm.mParticipants[i];
    scores.push_back(DeathmatchScore{
      participant.mIsActive, participant.mFrags, participant.mDeaths});
  }

  drawDeathmatchScoreboard(scores, dm.mOptions.mFragLimit);
}


void GameWorld::drawDeathmatchPlayerStatus(
  const base::Vec2& offset,
  const DeathmatchPlayerStatus& status)
{
  const auto healthText = status.mIsDead
    ? std::string{"DEAD"}
    : "HP " + std::to_string(status.mHealth);
  mTextRenderer.drawText(
    offset.x,
    offset.y,
    "P" + std::to_string(status.mPlayerIndex + 1) + " " + healthText + " K" +
      std::to_string(status.mFrags));

  auto weaponText = std::string{shortWeaponName(status.mWeapon)};
  if (status.mWeaponConsumesAmmo)
  {
    weaponText += " " + std::to_string(status.mAmmo);
  }
  mTextRenderer.drawText(offset.x, offset.y + 1, weaponText);
}


void GameWorld::drawDeathmatchScoreboard(
  const std::vector<DeathmatchScore>& scores,
  const int fragLimit)
{
  // Below the views
  const auto area = deathmatchWorldAreaSize();
  const auto scoreboardY = area.height + 1;

  std::string text;
  for (auto i = 0u; i < scores.size(); ++i)
  {
    if (scores[i].mIsActive)
    {
      text += "P" + std::to_string(i + 1) + ":" +
        std::to_string(scores[i].mFrags) + "/" +
        std::to_string(scores[i].mDeaths) + "  ";
    }
  }

  mTextRenderer.drawText(1, scoreboardY, text);
  mTextRenderer.drawText(
    1, scoreboardY + 1, "KILLS/DEATHS - LIMIT " + std::to_string(fragLimit));
}

} // namespace rigel::game_logic
