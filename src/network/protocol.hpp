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

#include "base/spatial_types.hpp"
#include "data/game_session_data.hpp"
#include "game_logic_common/input.hpp"
#include "game_logic_common/network_world.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>


/** Network protocol for deathmatch.
 *
 * All messages start with a one-byte message type. Numbers are little
 * endian. The host is authoritative: Clients send their input, the host
 * sends back snapshots of what each client should display.
 *
 * Message flow:
 *
 *   client -> host: Hello
 *   host -> client: Welcome (or Reject, followed by disconnect)
 *   host -> client: TileChanges with all map changes so far
 *   then every frame:
 *     client -> host: Input
 *     host -> client: Snapshot, plus TileChanges/Message/Sounds if needed
 */
namespace rigel::network
{

constexpr std::uint32_t PROTOCOL_MAGIC = 0x4d443252; // "R2DM"
constexpr std::uint16_t PROTOCOL_VERSION = 3;
constexpr std::uint16_t DEFAULT_PORT = 7777;

enum Channel : std::uint8_t
{
  CHANNEL_RELIABLE = 0,
  CHANNEL_SNAPSHOTS = 1,
  NUM_CHANNELS
};


enum class MessageType : std::uint8_t
{
  Hello = 1,
  Welcome = 2,
  Reject = 3,
  Input = 4,
  Snapshot = 5,
  TileChanges = 6,
  Message = 7,
  Sounds = 8,
};


struct HelloMessage
{
  std::uint32_t mMagic = PROTOCOL_MAGIC;
  std::uint16_t mVersion = PROTOCOL_VERSION;
};


struct WelcomeMessage
{
  int mPlayerIndex = 0;
  int mNumPlayers = 0;
  data::GameSessionId mSessionId;
};


struct RejectMessage
{
  std::string mReason;
};


struct InputMessage
{
  /** Counts up by one for each frame on the client */
  std::uint32_t mSequence = 0;

  game_logic::PlayerInput mInput;
  base::Size mViewportSize;

  /** Where the client's own simulation put the player after applying this
   * input (client-side prediction). Not set while the player is dead. */
  std::optional<base::Vec2> mPredictedPosition;
};


using ByteBuffer = std::vector<std::uint8_t>;


/** Peek at the type of an encoded message */
std::optional<MessageType> messageType(const ByteBuffer& message);

ByteBuffer encode(const HelloMessage& message);
ByteBuffer encode(const WelcomeMessage& message);
ByteBuffer encode(const RejectMessage& message);
ByteBuffer encode(const InputMessage& message);
ByteBuffer encode(const game_logic::NetSnapshot& message);
ByteBuffer encode(const std::vector<game_logic::NetTileChange>& message);
ByteBuffer encodeTextMessage(const std::string& text);
ByteBuffer encodeSounds(const std::vector<int>& soundIds);

// Decoding returns nothing for malformed messages
std::optional<HelloMessage> decodeHello(const ByteBuffer& message);
std::optional<WelcomeMessage> decodeWelcome(const ByteBuffer& message);
std::optional<RejectMessage> decodeReject(const ByteBuffer& message);
std::optional<InputMessage> decodeInput(const ByteBuffer& message);
std::optional<game_logic::NetSnapshot>
  decodeSnapshot(const ByteBuffer& message);
std::optional<std::vector<game_logic::NetTileChange>>
  decodeTileChanges(const ByteBuffer& message);
std::optional<std::string> decodeTextMessage(const ByteBuffer& message);
std::optional<std::vector<int>> decodeSounds(const ByteBuffer& message);


/** Combine inputs received during one frame into one.
 *
 * Buttons count as triggered if they were triggered in any of the inputs,
 * everything else is taken from the most recent input.
 */
game_logic::PlayerInput mergeInputs(
  const game_logic::PlayerInput& older,
  const game_logic::PlayerInput& newer);

} // namespace rigel::network
