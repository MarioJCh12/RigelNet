/* Copyright (C) 2020, Nikolai Wuttke. All rights reserved.
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
#include "game_logic_common/input.hpp"

RIGEL_DISABLE_WARNINGS
#include <SDL.h>
RIGEL_RESTORE_WARNINGS

#include <vector>


namespace rigel
{

namespace data
{
struct GameOptions;
}


class InputHandler
{
public:
  enum class MenuCommand
  {
    None,
    QuickSave,
    QuickLoad
  };

  explicit InputHandler(const data::GameOptions* pOptions);

  /** Restrict this handler to the controls of one player in local multiplayer
   *
   * By default, an InputHandler reacts to the configured keyboard keys and
   * all game controllers. After calling this, it only reacts to the game
   * controller with the given index (0 = first connected controller), and
   * to a keyboard layout specific to that player:
   *
   *  - Player 1: The keys configured in the options menu
   *  - Player 2: W/A/S/D to move, G to jump, F to fire
   *  - Players 3 and 4: Game controller only
   */
  void setMultiplayerSlot(int slot);

  MenuCommand handleEvent(const SDL_Event& event, bool playerInShip);
  void reset();

  game_logic::PlayerInput fetchInput();

private:
  MenuCommand handleKeyboardInput(const SDL_Event& event);
  MenuCommand handleControllerInput(const SDL_Event& event, bool playerInShip);

  bool isEventForThisPlayer(const SDL_Event& event) const;

  game_logic::PlayerInput mPlayerInput;
  int mMultiplayerSlot = -1;
  base::Vec2 mAnalogStickVector;
  const data::GameOptions* mpOptions;
  bool mQuickSaveModifierHeld = false;
};

} // namespace rigel
