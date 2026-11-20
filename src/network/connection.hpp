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

#include "network/protocol.hpp"

#include <chrono>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>


struct _ENetHost;
struct _ENetPeer;


namespace rigel::network
{

using ClientId = std::uint32_t;


struct NetworkEvent
{
  enum class Type
  {
    Connected,
    Disconnected,
    Message
  };

  Type mType;
  ClientId mClient = 0;
  ByteBuffer mData;
};


/** Server side of a network game, built on ENet (UDP) */
class NetworkHost
{
public:
  /** Start listening on the given port. Throws on failure. */
  NetworkHost(std::uint16_t port, int maxClients);
  ~NetworkHost();

  NetworkHost(const NetworkHost&) = delete;
  NetworkHost& operator=(const NetworkHost&) = delete;

  /** Process network traffic without blocking, return what happened */
  std::vector<NetworkEvent> poll();

  void send(
    ClientId client,
    Channel channel,
    const ByteBuffer& data,
    bool reliable = true);

  /** Disconnect a client after any pending messages have been sent */
  void disconnect(ClientId client);

  /** Send out queued messages right away */
  void flush();

  std::uint16_t port() const { return mPort; }

private:
  _ENetHost* mpHost = nullptr;
  std::unordered_map<ClientId, _ENetPeer*> mClients;
  ClientId mNextClientId = 1;
  std::uint16_t mPort;
};


/** Client side of a network game */
class NetworkClient
{
public:
  /** Connect to a host and do the handshake.
   *
   * Blocks until connected or failed. Throws std::runtime_error with a
   * user-readable message on failure (including rejection by the host).
   */
  NetworkClient(
    const std::string& hostName,
    std::uint16_t port,
    std::chrono::milliseconds timeout);
  ~NetworkClient();

  NetworkClient(const NetworkClient&) = delete;
  NetworkClient& operator=(const NetworkClient&) = delete;

  const WelcomeMessage& welcome() const { return mWelcome; }

  /** Process network traffic without blocking, return what happened.
   *
   * Only produces Message and Disconnected events. */
  std::vector<NetworkEvent> poll();

  void send(Channel channel, const ByteBuffer& data, bool reliable = true);
  void flush();

  bool isConnected() const { return mpPeer != nullptr; }

  /** Leave the game, letting the host know right away */
  void disconnect();

  /** Messages that arrived during the handshake, after the welcome */
  std::vector<ByteBuffer> takeEarlyMessages();

private:
  _ENetHost* mpHost = nullptr;
  _ENetPeer* mpPeer = nullptr;
  WelcomeMessage mWelcome;
  std::vector<ByteBuffer> mEarlyMessages;
};


/** Split "host:port" into its parts. The port is optional. */
std::pair<std::string, std::uint16_t>
  parseAddress(const std::string& address, std::uint16_t defaultPort);

} // namespace rigel::network
