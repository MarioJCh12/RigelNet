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

#include "connection.hpp"

#include "base/warnings.hpp"

// ENet pulls in Windows headers, which define a lot of macros otherwise
#ifdef _WIN32
  #ifndef NOMINMAX
    #define NOMINMAX
  #endif
  #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
  #endif
#endif

RIGEL_DISABLE_WARNINGS
#include <enet/enet.h>
RIGEL_RESTORE_WARNINGS

#include <loguru.hpp>

#include <algorithm>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <utility>


namespace rigel::network
{

namespace
{

void initializeEnet()
{
  static std::once_flag initFlag;
  std::call_once(initFlag, []() {
    if (enet_initialize() != 0)
    {
      throw std::runtime_error("Could not initialize networking (ENet)");
    }
    std::atexit(enet_deinitialize);
  });
}


ByteBuffer packetData(const ENetPacket* pPacket)
{
  return ByteBuffer(pPacket->data, pPacket->data + pPacket->dataLength);
}


ENetPacket* makePacket(const ByteBuffer& data, const bool reliable)
{
  return enet_packet_create(
    data.data(),
    data.size(),
    // Unreliable packets are still sequenced: Old ones arriving late are
    // dropped instead of being delivered out of order.
    reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
}

} // namespace


std::pair<std::string, std::uint16_t>
  parseAddress(const std::string& address, const std::uint16_t defaultPort)
{
  const auto colon = address.rfind(':');
  if (colon == std::string::npos)
  {
    return {address, defaultPort};
  }

  const auto portText = address.substr(colon + 1);
  char* pEnd = nullptr;
  const auto port = std::strtol(portText.c_str(), &pEnd, 10);
  if (portText.empty() || *pEnd != '\0' || port <= 0 || port > 65535)
  {
    throw std::invalid_argument("Invalid port in address: " + address);
  }

  return {address.substr(0, colon), std::uint16_t(port)};
}


NetworkHost::NetworkHost(const std::uint16_t port, const int maxClients)
  : mPort(port)
{
  initializeEnet();

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;

  mpHost = enet_host_create(&address, size_t(maxClients), NUM_CHANNELS, 0, 0);
  if (!mpHost)
  {
    throw std::runtime_error(
      "Could not start hosting on port " + std::to_string(port) +
      ". Is another program using it?");
  }

  LOG_F(INFO, "Hosting network game on UDP port %d", int(port));
}


NetworkHost::~NetworkHost()
{
  for (auto& [id, pPeer] : mClients)
  {
    enet_peer_disconnect_now(pPeer, 0);
  }

  if (mpHost)
  {
    enet_host_flush(mpHost);
    enet_host_destroy(mpHost);
  }
}


std::vector<NetworkEvent> NetworkHost::poll()
{
  std::vector<NetworkEvent> events;

  ENetEvent event;
  while (enet_host_service(mpHost, &event, 0) > 0)
  {
    switch (event.type)
    {
      case ENET_EVENT_TYPE_CONNECT:
        {
          const auto id = mNextClientId++;
          event.peer->data = reinterpret_cast<void*>(std::uintptr_t(id));
          mClients[id] = event.peer;

          // Notice players whose game crashed or lost connection within a
          // few seconds, instead of ENet's default of up to 30 seconds
          enet_peer_timeout(event.peer, 8, 3000, 6000);

          char addressText[64] = {};
          enet_address_get_host_ip(
            &event.peer->address, addressText, sizeof(addressText));
          LOG_F(
            INFO,
            "Client %u connected from %s:%d",
            id,
            addressText,
            int(event.peer->address.port));

          events.push_back({NetworkEvent::Type::Connected, id, {}});
          break;
        }

      case ENET_EVENT_TYPE_DISCONNECT:
        {
          const auto id = ClientId(std::uintptr_t(event.peer->data));
          mClients.erase(id);
          LOG_F(INFO, "Client %u disconnected", id);
          events.push_back({NetworkEvent::Type::Disconnected, id, {}});
          break;
        }

      case ENET_EVENT_TYPE_RECEIVE:
        {
          const auto id = ClientId(std::uintptr_t(event.peer->data));
          events.push_back(
            {NetworkEvent::Type::Message, id, packetData(event.packet)});
          enet_packet_destroy(event.packet);
          break;
        }

      default:
        break;
    }
  }

  return events;
}


void NetworkHost::send(
  const ClientId client,
  const Channel channel,
  const ByteBuffer& data,
  const bool reliable)
{
  const auto iClient = mClients.find(client);
  if (iClient == mClients.end())
  {
    return;
  }

  enet_peer_send(iClient->second, channel, makePacket(data, reliable));
}


void NetworkHost::disconnect(const ClientId client)
{
  const auto iClient = mClients.find(client);
  if (iClient != mClients.end())
  {
    enet_peer_disconnect_later(iClient->second, 0);
  }
}


void NetworkHost::flush()
{
  enet_host_flush(mpHost);
}


NetworkClient::NetworkClient(
  const std::string& hostName,
  const std::uint16_t port,
  const std::chrono::milliseconds timeout)
{
  initializeEnet();

  mpHost = enet_host_create(nullptr, 1, NUM_CHANNELS, 0, 0);
  if (!mpHost)
  {
    throw std::runtime_error("Could not initialize network connection");
  }

  auto fail = [this](const std::string& message) {
    if (mpPeer)
    {
      enet_peer_reset(mpPeer);
      mpPeer = nullptr;
    }
    enet_host_destroy(mpHost);
    mpHost = nullptr;
    throw std::runtime_error(message);
  };

  ENetAddress address;
  if (enet_address_set_host(&address, hostName.c_str()) != 0)
  {
    fail("Could not find host '" + hostName + "'");
  }
  address.port = port;

  mpPeer = enet_host_connect(mpHost, &address, NUM_CHANNELS, 0);
  if (!mpPeer)
  {
    fail("Could not start connecting to " + hostName);
  }

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  auto timeLeft = [&]() {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
      deadline - std::chrono::steady_clock::now());
    return enet_uint32(std::max<long long>(0, remaining.count()));
  };

  const auto addressText = hostName + ":" + std::to_string(port);

  // Step 1: Establish the connection
  auto connected = false;
  ENetEvent event;
  while (!connected && enet_host_service(mpHost, &event, timeLeft()) > 0)
  {
    if (event.type == ENET_EVENT_TYPE_CONNECT)
    {
      connected = true;
    }
    else if (event.type == ENET_EVENT_TYPE_RECEIVE)
    {
      enet_packet_destroy(event.packet);
    }
  }

  if (!connected)
  {
    fail(
      "Could not connect to " + addressText +
      " (no answer - is the game hosted there, and is the port open?)");
  }

  // Step 2: Handshake
  send(CHANNEL_RELIABLE, encode(HelloMessage{}));
  flush();

  auto welcomed = false;
  while (!welcomed && enet_host_service(mpHost, &event, timeLeft()) > 0)
  {
    if (event.type == ENET_EVENT_TYPE_DISCONNECT)
    {
      mpPeer = nullptr;
      fail("The host closed the connection");
    }

    if (event.type != ENET_EVENT_TYPE_RECEIVE)
    {
      continue;
    }

    auto data = packetData(event.packet);
    enet_packet_destroy(event.packet);

    const auto type = messageType(data);
    if (type == MessageType::Welcome)
    {
      if (auto welcome = decodeWelcome(data))
      {
        mWelcome = *welcome;
        welcomed = true;
      }
      else
      {
        fail("Received an invalid answer from the host");
      }
    }
    else if (type == MessageType::Reject)
    {
      const auto reject = decodeReject(data);
      fail(
        "The host rejected the connection: " +
        (reject ? reject->mReason : std::string{"(unknown reason)"}));
    }
    else
    {
      mEarlyMessages.push_back(std::move(data));
    }
  }

  if (!welcomed)
  {
    fail("The host at " + addressText + " did not answer the handshake");
  }

  LOG_F(
    INFO,
    "Connected to %s as player %d",
    addressText.c_str(),
    mWelcome.mPlayerIndex + 1);
}


NetworkClient::~NetworkClient()
{
  disconnect();

  if (mpHost)
  {
    enet_host_destroy(mpHost);
  }
}


void NetworkClient::disconnect()
{
  if (!mpPeer)
  {
    return;
  }

  enet_peer_disconnect(mpPeer, 0);

  // Give the host a moment to acknowledge, so that it knows we left
  ENetEvent event;
  auto acknowledged = false;
  while (!acknowledged && enet_host_service(mpHost, &event, 500) > 0)
  {
    if (event.type == ENET_EVENT_TYPE_RECEIVE)
    {
      enet_packet_destroy(event.packet);
    }
    else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
    {
      acknowledged = true;
    }
  }

  if (!acknowledged)
  {
    enet_peer_reset(mpPeer);
  }

  mpPeer = nullptr;
}


std::vector<NetworkEvent> NetworkClient::poll()
{
  const auto now = Clock::now();

  // Simulated latency: Send what's due
  {
    auto iDue = std::stable_partition(
      mDelayedSends.begin(),
      mDelayedSends.end(),
      [&](const DelayedSend& s) { return s.mDue <= now; });
    for (auto it = mDelayedSends.begin(); it != iDue; ++it)
    {
      sendNow(it->mChannel, it->mData, it->mReliable);
    }
    mDelayedSends.erase(mDelayedSends.begin(), iDue);
  }

  std::vector<NetworkEvent> received;

  for (auto& data : takeEarlyMessages())
  {
    received.push_back({NetworkEvent::Type::Message, 0, std::move(data)});
  }

  ENetEvent event;
  while (mpPeer && enet_host_service(mpHost, &event, 0) > 0)
  {
    switch (event.type)
    {
      case ENET_EVENT_TYPE_RECEIVE:
        received.push_back(
          {NetworkEvent::Type::Message, 0, packetData(event.packet)});
        enet_packet_destroy(event.packet);
        break;

      case ENET_EVENT_TYPE_DISCONNECT:
        LOG_F(WARNING, "Lost connection to host");
        mpPeer = nullptr;
        received.push_back({NetworkEvent::Type::Disconnected, 0, {}});
        break;

      default:
        break;
    }
  }

  if (mSimulatedDelay == Clock::duration{})
  {
    return received;
  }

  // Simulated latency: Hold back incoming messages until they're due
  for (auto& event : received)
  {
    mDelayedEvents.push_back({now + mSimulatedDelay, std::move(event)});
  }

  std::vector<NetworkEvent> events;
  auto iDue = std::stable_partition(
    mDelayedEvents.begin(),
    mDelayedEvents.end(),
    [&](const DelayedEvent& e) { return e.mDue <= now; });
  for (auto it = mDelayedEvents.begin(); it != iDue; ++it)
  {
    events.push_back(std::move(it->mEvent));
  }
  mDelayedEvents.erase(mDelayedEvents.begin(), iDue);

  return events;
}


void NetworkClient::send(
  const Channel channel,
  const ByteBuffer& data,
  const bool reliable)
{
  if (mSimulatedDelay != Clock::duration{})
  {
    mDelayedSends.push_back(
      {Clock::now() + mSimulatedDelay, channel, data, reliable});
    return;
  }

  sendNow(channel, data, reliable);
}


void NetworkClient::sendNow(
  const Channel channel,
  const ByteBuffer& data,
  const bool reliable)
{
  if (mpPeer)
  {
    enet_peer_send(mpPeer, channel, makePacket(data, reliable));
  }
}


void NetworkClient::setSimulatedLatency(
  const std::chrono::milliseconds roundTripTime)
{
  mSimulatedDelay =
    std::chrono::duration_cast<Clock::duration>(roundTripTime) / 2;
  LOG_F(
    INFO, "Simulating %d ms of network latency", int(roundTripTime.count()));
}


void NetworkClient::flush()
{
  if (mpHost)
  {
    enet_host_flush(mpHost);
  }
}


std::vector<ByteBuffer> NetworkClient::takeEarlyMessages()
{
  return std::exchange(mEarlyMessages, {});
}

} // namespace rigel::network
