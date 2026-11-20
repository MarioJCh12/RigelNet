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
#include "data/game_session_data.hpp"
#include "game_logic_common/deathmatch_options.hpp"

#include <cstdint>
#include <memory>


namespace rigel::network
{
class NetworkClient;
}

#include <optional>
#include <string>
#include <utility>


namespace rigel
{

/** Settings that can be changed via command-line arguments/switches
 *
 * This represents the parsed representation of all command line arguments
 * (argc/argv). The parsing happens in main.cpp
 * For documentation on the individual members, refer to the parser code over
 * there.
 *
 * If you add something here, you'll want to extend the code in main.cpp
 * as well!
 */
struct CommandLineOptions
{
  std::string mGamePath;
  std::optional<data::GameSessionId> mLevelToJumpTo;
  bool mSkipIntro = false;
  bool mDebugModeEnabled = false;
  bool mDisableAudio = false;
  bool mPlayDemo = false;
  std::optional<base::Vec2> mPlayerPosition;

  /** If set, start a local split-screen deathmatch on mLevelToJumpTo */
  std::optional<game_logic::DeathmatchOptions> mDeathmatch;

  /** If set, host a network deathmatch on this UDP port */
  std::optional<std::uint16_t> mHostPort;

  /** If set, join a network deathmatch at this address ("host[:port]") */
  std::string mConnectAddress;

  /** For testing network play: Added round-trip time in milliseconds */
  int mSimulatedLatencyMs = 0;

  /** For automated testing: Replaces keyboard/controller input with a
   * repeating sequence. Each character is one frame: L/R = left/right,
   * U/D = up/down, J = jump, F = fire, anything else = nothing. */
  std::string mDebugInputScript;

  /** If set, this is a network client already connected to a host */
  std::shared_ptr<network::NetworkClient> mpNetworkClient;
};

} // namespace rigel
