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

#include "utils.hpp"

#include <base/warnings.hpp>
#include <data/game_options.hpp>
#include <data/game_traits.hpp>
#include <data/map.hpp>
#include <data/player_model.hpp>
#include <engine/base_components.hpp>
#include <engine/collision_checker.hpp>
#include <engine/entity_activation_system.hpp>
#include <engine/life_time_components.hpp>
#include <engine/life_time_system.hpp>
#include <engine/random_number_generator.hpp>
#include <engine/visual_components.hpp>
#include <frontend/input_handler.hpp>
#include <game_logic/camera.hpp>
#include <game_logic/deathmatch_spawn_points.hpp>
#include <game_logic/player.hpp>
#include <game_logic/player/components.hpp>

RIGEL_DISABLE_WARNINGS
#include <SDL.h>
#include <catch2/catch_test_macros.hpp>
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

#include <vector>


using namespace rigel;
using engine::components::Active;
using engine::components::AutoDestroy;
using engine::components::BoundingBox;
using engine::components::WorldPosition;


namespace
{

entityx::Entity
  createEntityAt(entityx::EntityManager& es, const base::Vec2& pos)
{
  auto entity = es.create();
  entity.assign<WorldPosition>(pos);
  entity.assign<BoundingBox>(BoundingBox{{0, 0}, {1, 1}});
  return entity;
}


SDL_Event keyEvent(const SDL_Keycode key, const bool pressed)
{
  SDL_Event event{};
  event.type = pressed ? SDL_KEYDOWN : SDL_KEYUP;
  event.key.keysym.sym = key;
  event.key.repeat = 0;
  return event;
}

} // namespace


TEST_CASE("Entity activation with multiple viewports")
{
  entityx::EntityX entityx;
  auto& es = entityx.entities;

  auto nearPlayer1 = createEntityAt(es, {5, 5});
  auto nearPlayer2 = createEntityAt(es, {85, 45});
  auto farFromBoth = createEntityAt(es, {45, 80});

  const auto regions = std::vector<base::Rect<int>>{
    BoundingBox{{0, 0}, {20, 10}}, BoundingBox{{80, 40}, {20, 10}}};

  engine::markActiveEntities(es, regions);

  CHECK(nearPlayer1.has_component<Active>());
  CHECK(nearPlayer2.has_component<Active>());
  CHECK(!farFromBoth.has_component<Active>());

  CHECK(nearPlayer1.component<Active>()->mIsOnScreen);
  CHECK(nearPlayer2.component<Active>()->mIsOnScreen);
}


TEST_CASE("Single viewport activation still works as before")
{
  entityx::EntityX entityx;
  auto& es = entityx.entities;

  auto inside = createEntityAt(es, {5, 5});
  auto outside = createEntityAt(es, {50, 5});

  engine::markActiveEntities(es, {0, 0}, {20, 10});

  CHECK(inside.has_component<Active>());
  CHECK(!outside.has_component<Active>());
}


TEST_CASE("Projectiles are only removed when leaving all viewports")
{
  entityx::EntityX entityx;
  auto& es = entityx.entities;

  auto inSecondView = createEntityAt(es, {85, 45});
  inSecondView.assign<AutoDestroy>(
    AutoDestroy{AutoDestroy::Condition::OnLeavingActiveRegion});

  auto outsideAllViews = createEntityAt(es, {45, 80});
  outsideAllViews.assign<AutoDestroy>(
    AutoDestroy{AutoDestroy::Condition::OnLeavingActiveRegion});

  const auto regions = std::vector<base::Rect<int>>{
    BoundingBox{{0, 0}, {20, 10}}, BoundingBox{{80, 40}, {20, 10}}};

  engine::LifeTimeSystem lifeTimeSystem;
  lifeTimeSystem.update(es, regions);

  CHECK(inSecondView.valid());
  CHECK(!outsideAllViews.valid());

  SECTION("Single viewport version only considers that viewport")
  {
    lifeTimeSystem.update(es, {0, 0}, {20, 10});
    CHECK(!inSecondView.valid());
  }
}


TEST_CASE("Per-player keyboard input in local multiplayer")
{
  data::GameOptions options;

  InputHandler player1{&options};
  player1.setMultiplayerSlot(0);

  InputHandler player2{&options};
  player2.setMultiplayerSlot(1);

  InputHandler player3{&options};
  player3.setMultiplayerSlot(2);

  auto sendToAll = [&](const SDL_Event& event)
  {
    player1.handleEvent(event, false);
    player2.handleEvent(event, false);
    player3.handleEvent(event, false);
  };

  SECTION("Player 1 uses the configured keys")
  {
    sendToAll(keyEvent(options.mLeftKeybinding, true));
    sendToAll(keyEvent(options.mFireKeybinding, true));

    const auto input1 = player1.fetchInput();
    const auto input2 = player2.fetchInput();

    CHECK(input1.mLeft);
    CHECK(input1.mFire.mIsPressed);
    CHECK(!input2.mLeft);
    CHECK(!input2.mFire.mIsPressed);
  }

  SECTION("Player 2 uses WASD, G and F")
  {
    sendToAll(keyEvent(SDLK_a, true));
    sendToAll(keyEvent(SDLK_g, true));
    sendToAll(keyEvent(SDLK_f, true));

    const auto input1 = player1.fetchInput();
    const auto input2 = player2.fetchInput();

    CHECK(input2.mLeft);
    CHECK(input2.mJump.mIsPressed);
    CHECK(input2.mJump.mWasTriggered);
    CHECK(input2.mFire.mIsPressed);
    CHECK(!input1.mLeft);
    CHECK(!input1.mJump.mIsPressed);
    CHECK(!input1.mFire.mIsPressed);

    SECTION("Up key also triggers interaction")
    {
      sendToAll(keyEvent(SDLK_w, true));
      const auto input = player2.fetchInput();
      CHECK(input.mUp);
      CHECK(input.mInteract.mWasTriggered);
    }
  }

  SECTION("Player 3 ignores the keyboard")
  {
    sendToAll(keyEvent(options.mLeftKeybinding, true));
    sendToAll(keyEvent(SDLK_a, true));

    const auto input3 = player3.fetchInput();
    CHECK(!input3.mLeft);
  }

  SECTION("Only player 1 gets quick save commands")
  {
    const auto event = keyEvent(options.mQuickSaveKeybinding, true);
    CHECK(
      player1.handleEvent(event, false) ==
      InputHandler::MenuCommand::QuickSave);
    CHECK(player2.handleEvent(event, false) == InputHandler::MenuCommand::None);
  }

  SECTION("Single player handler reacts to configured keys")
  {
    InputHandler singlePlayer{&options};
    singlePlayer.handleEvent(keyEvent(options.mRightKeybinding, true), false);
    CHECK(singlePlayer.fetchInput().mRight);
  }
}


TEST_CASE("Camera keeps player visible in short split-screen views")
{
  using game_logic::Camera;
  using game_logic::Player;

  entityx::EntityX entityx;

  data::map::Map map{100, 100, data::map::TileAttributeDict{{0x0, 0xF}}};
  for (auto x = 0; x < 100; ++x)
  {
    map.setTileAt(0, x, 80, 1);
  }

  engine::CollisionChecker collisionChecker{
    &map, entityx.entities, entityx.events};
  data::PersistentPlayerState persistentPlayerState;
  MockEntityFactory mockEntityFactory{&entityx.entities};
  MockServiceProvider mockServiceProvider;
  engine::RandomNumberGenerator randomGenerator;
  data::GameOptions options;

  auto playerEntity = entityx.entities.create();
  playerEntity.assign<WorldPosition>(40, 79);
  playerEntity.assign<engine::components::Sprite>();
  game_logic::assignPlayerComponents(
    playerEntity, engine::components::Orientation::Right);

  Player player(
    playerEntity,
    data::Difficulty::Medium,
    &persistentPlayerState,
    &mockServiceProvider,
    &options,
    &collisionChecker,
    &map,
    &mockEntityFactory,
    &entityx.events,
    &randomGenerator);

  Camera camera{&player, map, entityx.events};

  auto playerIsFullyVisible = [&](const base::Size& viewportSize)
  {
    const auto view = BoundingBox{camera.position(), viewportSize};
    const auto playerBox = player.worldSpaceCollisionBox();
    return view.containsPoint(playerBox.topLeft) &&
      view.containsPoint(playerBox.bottomRight());
  };

  SECTION("Short view (4 player split-screen)")
  {
    const auto viewportSize = base::Size{20, 9};
    camera.recenter(viewportSize);
    camera.centerViewOnPlayer();

    CHECK(playerIsFullyVisible(viewportSize));

    // Moving the player far down: The camera follows and keeps them visible
    for (auto i = 0; i < 4; ++i)
    {
      playerEntity.component<WorldPosition>()->y -= 6;
      for (auto frame = 0; frame < 10; ++frame)
      {
        camera.update({}, viewportSize);
      }

      CHECK(playerIsFullyVisible(viewportSize));
    }
  }

  SECTION("Narrow view (2 player split-screen, 4:3)")
  {
    const auto viewportSize = base::Size{18, 20};
    camera.recenter(viewportSize);
    camera.centerViewOnPlayer();

    CHECK(playerIsFullyVisible(viewportSize));

    for (auto i = 0; i < 4; ++i)
    {
      playerEntity.component<WorldPosition>()->x += 7;
      for (auto frame = 0; frame < 10; ++frame)
      {
        camera.update({}, viewportSize);
      }

      CHECK(playerIsFullyVisible(viewportSize));
    }
  }

  SECTION("Regular view is unchanged")
  {
    const auto viewportSize = data::GameTraits::mapViewportSize;
    camera.recenter(viewportSize);
    camera.centerViewOnPlayer();

    // Same as the original: player's bottom is 19 tiles below the camera
    CHECK(player.worldSpaceCollisionBox().bottom() - camera.position().y == 19);
  }
}


TEST_CASE("Deathmatch spawn points stay inside the level")
{
  using game_logic::findDeathmatchSpawnPoints;
  using game_logic::ReachabilityMap;

  // Tile 1 is a solid wall
  data::map::Map map{60, 30, data::map::TileAttributeDict{{0x0, 0xF}}};

  auto wallRect = [&](int x, int y, int width, int height) {
    for (auto row = y; row < y + height; ++row)
    {
      for (auto col = x; col < x + width; ++col)
      {
        map.setTileAt(0, col, row, 1);
      }
    }
  };

  // Room A: x 1..19, floor at y 20
  wallRect(0, 0, 21, 1); // ceiling
  wallRect(0, 0, 1, 21); // left wall
  wallRect(0, 20, 41, 1); // floor (shared with room B)
  // Wall between A and B, which can be opened (e.g. a shootable wall)
  wallRect(20, 0, 1, 20);
  // Room B: x 21..39
  wallRect(20, 0, 21, 1);
  wallRect(40, 0, 1, 21);

  // Outside the level: A pocket of empty space with a floor, at x 45..58
  wallRect(44, 26, 15, 1);

  const auto start = base::Vec2{3, 19};
  const auto itemInRoomB = base::Vec2{30, 19};
  const auto itemOutside = base::Vec2{50, 25};
  const auto openableWall = base::Rect<int>{{20, 1}, {1, 19}};

  SECTION("Positions outside of the level's walls aren't reachable")
  {
    const auto reachability = ReachabilityMap{map, start, {openableWall}};

    CHECK(reachability.isReachable(start));
    CHECK(reachability.isReachable(itemInRoomB));
    CHECK(!reachability.isReachable(itemOutside));
  }

  SECTION("Openable walls are treated as passable")
  {
    const auto withoutOpenable = ReachabilityMap{map, start};
    CHECK(!withoutOpenable.isReachable(itemInRoomB));
  }

  SECTION("Spawn points only come from reachable places")
  {
    const auto spawnPoints = findDeathmatchSpawnPoints(
      map, start, {itemInRoomB, itemOutside}, {openableWall});

    REQUIRE(!spawnPoints.empty());

    auto hasSpawnInRoomB = false;
    for (const auto& spawn : spawnPoints)
    {
      CHECK(spawn.x < 40); // never in the outside pocket
      CHECK(game_logic::isValidSpawnSpot(map, spawn));

      if (spawn.x > 20)
      {
        hasSpawnInRoomB = true;
      }
    }

    CHECK(hasSpawnInRoomB);
  }
}
