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

#include "base/warnings.hpp"
#include "engine/sprite_rendering_system.hpp"
#include "frontend/game_service_provider.hpp"
#include "game_logic_common/network_world.hpp"

RIGEL_DISABLE_WARNINGS
#include <entityx/entityx.h>
RIGEL_RESTORE_WARNINGS

#include <memory>
#include <vector>


namespace rigel::game_logic
{

/** State of a GameWorld that displays a game hosted on another computer */
struct RemoteViewState
{
  int mPlayerIndex = 0;
  NetSnapshot mSnapshot;

  // Sprites from the host's snapshot. Our own player is drawn from the
  // local simulation (prediction) and merged in when rendering.
  std::vector<engine::SpriteDrawSpec> mHostSprites;
  int mHostFirstForegroundSprite = 0;

  /** True while our own player is alive and simulated locally */
  bool mIsPredicting = false;
  int mLastHealth = 0;

  std::unique_ptr<IGameServiceProvider> mpMutedServiceProvider;

  // Same order as DeathmatchState::mDynamicSections on the host
  std::vector<entityx::Entity> mDynamicSections;
};

} // namespace rigel::game_logic
