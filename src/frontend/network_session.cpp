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

#include "network_session.hpp"

#include "data/sound_ids.hpp"
#include "network/connection.hpp"
#include "network/protocol.hpp"

#include <loguru.hpp>

#include <algorithm>
#include <deque>
#include <map>
#include <optional>
#include <utility>


namespace rigel
{

using game_logic::INetworkWorld;
using game_logic::PlayerInput;
using network::ByteBuffer;
using network::ClientId;
using network::MessageType;
using network::NetworkEvent;


void SoundRecordingServiceProvider::playSound(const data::SoundId id)
{
  if (mRecordingEnabled)
  {
    mRecordedSounds.push_back(static_cast<int>(id));
  }

  mpTarget->playSound(id);
}


std::vector<int> SoundRecordingServiceProvider::takeRecordedSounds()
{
  return std::exchange(mRecordedSounds, {});
}


namespace
{

class HostSession : public NetworkSession
{
public:
  HostSession(
    const std::uint16_t port,
    const int numPlayers,
    const data::GameSessionId& sessionId,
    INetworkWorld* pWorld,
    SoundRecordingServiceProvider* pSoundRecorder)
    // Allow a few more connections than player slots, so that we can tell
    // extra players that the game is full instead of not answering
    : mHost(port, numPlayers + 2)
    , mpWorld(pWorld)
    , mpSoundRecorder(pSoundRecorder)
    , mNumPlayers(numPlayers)
    , mSessionId(sessionId)
  {
    mpSoundRecorder->setRecordingEnabled(true);
  }

  ~HostSession() override { mpSoundRecorder->setRecordingEnabled(false); }

  void receive() override
  {
    for (auto& event : mHost.poll())
    {
      switch (event.mType)
      {
        case NetworkEvent::Type::Connected:
          mClients[event.mClient] = Client{};
          break;

        case NetworkEvent::Type::Disconnected:
          handleDisconnect(event.mClient);
          break;

        case NetworkEvent::Type::Message:
          handleMessage(event.mClient, event.mData);
          break;
      }
    }
  }

  void fillRemoteInputs(std::vector<PlayerInput>&) override
  {
    // Each network player gets their inputs one at a time, in the order the
    // client produced them. This keeps our simulation of their player in
    // step with the client's own (prediction). If no input has arrived yet,
    // their player waits for it.
    for (auto& [id, client] : mClients)
    {
      if (!client.mPlayerIndex)
      {
        continue;
      }

      // Inputs arrive irregularly, so a few pile up now and then. Without
      // that buffer, the player would sometimes have to wait. But a buffer
      // that's bigger than needed delays everything this player does. If
      // there was always at least one spare input for a while, we take two
      // this frame to make the buffer smaller.
      client.mQueueSizeHistory.push_back(client.mInputQueue.size());
      if (client.mQueueSizeHistory.size() > QUEUE_HISTORY_SIZE)
      {
        client.mQueueSizeHistory.pop_front();
      }

      auto numToApply = std::min<std::size_t>(1, client.mInputQueue.size());
      if (
        client.mQueueSizeHistory.size() == QUEUE_HISTORY_SIZE &&
        *std::min_element(
          client.mQueueSizeHistory.begin(), client.mQueueSizeHistory.end()) >=
          2)
      {
        numToApply = 2;
        for (auto& size : client.mQueueSizeHistory)
        {
          --size;
        }
      }

      for (auto i = 0u; i < numToApply; ++i)
      {
        auto message = client.mInputQueue.front();
        client.mInputQueue.pop_front();

        // If the client is far ahead (e.g. after a long lag spike), catch
        // up by combining inputs. The client will correct its prediction.
        while (client.mInputQueue.size() > MAX_QUEUED_INPUTS)
        {
          auto next = client.mInputQueue.front();
          client.mInputQueue.pop_front();
          next.mInput = network::mergeInputs(message.mInput, next.mInput);
          message = next;
        }

        mpWorld->setRemotePlayerFrame(
          *client.mPlayerIndex,
          game_logic::RemotePlayerFrame{
            message.mInput,
            message.mSequence,
            message.mPredictedPosition,
            message.mViewedFrame});
      }
    }
  }

  void sendUpdates(const PlayerInput&) override
  {
    const auto events = mpWorld->collectNetEvents();
    const auto sounds = mpSoundRecorder->takeRecordedSounds();

    std::optional<ByteBuffer> tileChanges;
    if (!events.mTileChanges.empty())
    {
      tileChanges = network::encode(events.mTileChanges);
    }

    std::optional<ByteBuffer> soundsMessage;
    if (!sounds.empty())
    {
      soundsMessage = network::encodeSounds(sounds);
    }

    for (auto& [id, client] : mClients)
    {
      if (!client.mPlayerIndex)
      {
        continue;
      }

      if (tileChanges)
      {
        mHost.send(id, network::CHANNEL_RELIABLE, *tileChanges);
      }

      for (const auto& message : events.mMessages)
      {
        mHost.send(
          id, network::CHANNEL_RELIABLE, network::encodeTextMessage(message));
      }

      if (soundsMessage)
      {
        mHost.send(id, network::CHANNEL_RELIABLE, *soundsMessage);
      }

      mHost.send(
        id,
        network::CHANNEL_SNAPSHOTS,
        network::encode(mpWorld->captureSnapshot(*client.mPlayerIndex)),
        false);
    }

    mHost.flush();
  }

  void keepAlive() override
  {
    // Process connects/disconnects, but don't let anyone join while paused.
    // Input from clients is dropped.
    for (auto& event : mHost.poll())
    {
      if (event.mType == NetworkEvent::Type::Disconnected)
      {
        handleDisconnect(event.mClient);
      }
      else if (event.mType == NetworkEvent::Type::Connected)
      {
        mClients[event.mClient] = Client{};
      }
      else if (
        network::messageType(event.mData) == MessageType::Hello &&
        mClients.count(event.mClient))
      {
        reject(event.mClient, "The host has paused the game, try again");
      }
    }

    mHost.flush();
  }

  bool isClient() const override { return false; }

private:
  // About half a second of input. More than that means the client is
  // running ahead of us.
  static constexpr auto MAX_QUEUED_INPUTS = 8u;

  // Two seconds' worth of frames
  static constexpr auto QUEUE_HISTORY_SIZE = 30u;

  struct Client
  {
    std::optional<int> mPlayerIndex;
    std::deque<network::InputMessage> mInputQueue;
    std::deque<std::size_t> mQueueSizeHistory;
  };

  void handleDisconnect(const ClientId id)
  {
    const auto iClient = mClients.find(id);
    if (iClient == mClients.end())
    {
      return;
    }

    if (iClient->second.mPlayerIndex)
    {
      mpWorld->disconnectRemotePlayer(*iClient->second.mPlayerIndex);
    }

    mClients.erase(iClient);
  }

  void reject(const ClientId id, const std::string& reason)
  {
    LOG_F(INFO, "Rejecting client %u: %s", id, reason.c_str());
    mHost.send(
      id, network::CHANNEL_RELIABLE, network::encode(network::RejectMessage{reason}));
    mHost.disconnect(id);
  }

  void handleMessage(const ClientId id, const ByteBuffer& data)
  {
    const auto iClient = mClients.find(id);
    if (iClient == mClients.end())
    {
      return;
    }
    auto& client = iClient->second;

    const auto type = network::messageType(data);
    if (type == MessageType::Hello && !client.mPlayerIndex)
    {
      const auto hello = network::decodeHello(data);
      if (!hello || hello->mMagic != network::PROTOCOL_MAGIC)
      {
        reject(id, "Not a RigelEngine deathmatch client");
        return;
      }

      if (hello->mVersion != network::PROTOCOL_VERSION)
      {
        reject(
          id,
          "Different game version (host uses network protocol " +
            std::to_string(network::PROTOCOL_VERSION) + ", you use " +
            std::to_string(hello->mVersion) + ")");
        return;
      }

      const auto playerIndex = mpWorld->findFreeRemoteSlot();
      if (!playerIndex)
      {
        reject(id, "The game is full");
        return;
      }

      client.mPlayerIndex = *playerIndex;
      mpWorld->connectRemotePlayer(*playerIndex);

      mHost.send(
        id,
        network::CHANNEL_RELIABLE,
        network::encode(
          network::WelcomeMessage{*playerIndex, mNumPlayers, mSessionId}));

      // Bring the new player's map up to date
      const auto tileChanges = mpWorld->allTileChanges();
      if (!tileChanges.empty())
      {
        mHost.send(id, network::CHANNEL_RELIABLE, network::encode(tileChanges));
      }

      LOG_F(INFO, "Client %u joined as player %d", id, *playerIndex + 1);
    }
    else if (type == MessageType::Input && client.mPlayerIndex)
    {
      if (const auto input = network::decodeInput(data))
      {
        client.mInputQueue.push_back(*input);
        mpWorld->setRemoteViewportSize(
          *client.mPlayerIndex, input->mViewportSize);
      }
    }
  }

  network::NetworkHost mHost;
  INetworkWorld* mpWorld;
  SoundRecordingServiceProvider* mpSoundRecorder;
  int mNumPlayers;
  data::GameSessionId mSessionId;
  std::map<ClientId, Client> mClients;
};


class ClientSession : public NetworkSession
{
public:
  ClientSession(
    std::shared_ptr<network::NetworkClient> pClient,
    INetworkWorld* pWorld,
    IGameServiceProvider* pServiceProvider)
    : mpClient(std::move(pClient))
    , mpWorld(pWorld)
    , mpServiceProvider(pServiceProvider)
  {
    mpWorld->enableRemoteView(mpClient->welcome().mPlayerIndex);
  }

  ~ClientSession() override
  {
    // Leaving the game (e.g. quitting to the main menu) ends the
    // connection. Joining again requires restarting with --connect.
    mpClient->disconnect();
  }

  void receive() override
  {
    // All snapshots that arrived since the last frame, oldest first. Each
    // one is needed for smooth movement (interpolation) and for checking
    // our prediction.
    std::vector<game_logic::NetSnapshot> newSnapshots;

    for (auto& event : mpClient->poll())
    {
      if (event.mType == NetworkEvent::Type::Disconnected)
      {
        mpWorld->showRemoteMessage("CONNECTION TO HOST LOST");
        continue;
      }

      switch (network::messageType(event.mData).value_or(MessageType::Hello))
      {
        case MessageType::Snapshot:
          if (auto snapshot = network::decodeSnapshot(event.mData))
          {
            // Snapshots can arrive out of order or get lost. Once we have
            // a newer one, older ones are of no use anymore.
            if (!mLastFrame || snapshot->mFrame > *mLastFrame)
            {
              mLastFrame = snapshot->mFrame;
              newSnapshots.push_back(std::move(*snapshot));
            }
          }
          break;

        case MessageType::TileChanges:
          if (const auto changes = network::decodeTileChanges(event.mData))
          {
            mpWorld->applyTileChanges(*changes);
          }
          break;

        case MessageType::Message:
          if (const auto text = network::decodeTextMessage(event.mData))
          {
            mpWorld->showRemoteMessage(*text);
          }
          break;

        case MessageType::Sounds:
          if (const auto sounds = network::decodeSounds(event.mData))
          {
            for (const auto id : *sounds)
            {
              if (
                id >= 0 &&
                id <= static_cast<int>(data::SoundId::IntroDukeSpeaks2))
              {
                mpServiceProvider->playSound(static_cast<data::SoundId>(id));
              }
            }
          }
          break;

        default:
          break;
      }
    }

    for (const auto& snapshot : newSnapshots)
    {
      const auto wasPredicting = mpWorld->predictedPlayerPosition().has_value();
      mpWorld->applySnapshot(snapshot);
      reconcilePrediction(snapshot, wasPredicting);
    }
  }

  void fillRemoteInputs(std::vector<PlayerInput>&) override { }

  void sendUpdates(const PlayerInput& localInput) override
  {
    network::InputMessage message;
    message.mSequence = mNextSequence++;
    message.mViewportSize = mpWorld->remoteViewportSize();
    message.mPredictedPosition = mpWorld->predictedPlayerPosition();
    message.mViewedFrame = mpWorld->viewedFrame();

    // While we're not simulating our player (not spawned yet, or dead), our
    // input isn't applied locally. So it shouldn't be applied on the host
    // either, otherwise the two simulations get out of step.
    message.mInput =
      message.mPredictedPosition ? localInput : game_logic::PlayerInput{};

    if (message.mPredictedPosition)
    {
      mPredictions.push_back({message.mSequence, *message.mPredictedPosition});
      while (mPredictions.size() > MAX_PREDICTIONS)
      {
        mPredictions.pop_front();
      }
    }

    mpClient->send(network::CHANNEL_RELIABLE, network::encode(message));
    mpClient->flush();
  }

  void keepAlive() override
  {
    // Still need to apply what the host sends, so that we don't miss
    // anything important. Sending neutral input makes our player stand
    // still while our menu is open.
    receive();
    sendUpdates(PlayerInput{});
  }

  bool isClient() const override { return true; }

private:
  struct Prediction
  {
    std::uint32_t mSequence;
    base::Vec2 mPosition;
  };

  // Ten seconds' worth. Older predictions can't be matched anymore.
  static constexpr auto MAX_PREDICTIONS = 150u;

  /** Compare where we predicted our player to be with where the host says
   * it actually was, after the same input. Correct any difference.
   *
   * Inputs we sent after that one were already applied to our player, so we
   * shift it by the difference instead of setting the host's position. */
  void reconcilePrediction(
    const game_logic::NetSnapshot& snapshot,
    const bool wasPredicting)
  {
    if (snapshot.mIsDead || !wasPredicting)
    {
      // Dying or respawning: The world starts predicting from scratch
      mPredictions.clear();
      return;
    }

    while (!mPredictions.empty() &&
           mPredictions.front().mSequence < snapshot.mLastInputSequence)
    {
      mPredictions.pop_front();
    }

    if (
      mPredictions.empty() ||
      mPredictions.front().mSequence != snapshot.mLastInputSequence)
    {
      return;
    }

    const auto error =
      snapshot.mPlayerPosition - mPredictions.front().mPosition;
    mPredictions.pop_front();

    if (error != base::Vec2{})
    {
      ++mNumCorrections;
      LOG_F(
        INFO,
        "Prediction corrected by %d,%d (input %u, %d corrections so far)",
        error.x,
        error.y,
        snapshot.mLastInputSequence,
        mNumCorrections);

      mpWorld->correctPrediction(error);
      for (auto& prediction : mPredictions)
      {
        prediction.mPosition += error;
      }
    }
  }

  std::shared_ptr<network::NetworkClient> mpClient;
  INetworkWorld* mpWorld;
  IGameServiceProvider* mpServiceProvider;
  std::optional<std::uint32_t> mLastFrame;

  std::uint32_t mNextSequence = 1;
  std::deque<Prediction> mPredictions;
  int mNumCorrections = 0;
};

} // namespace


std::unique_ptr<NetworkSession> createHostSession(
  const std::uint16_t port,
  const int numPlayers,
  const data::GameSessionId& sessionId,
  INetworkWorld* pWorld,
  SoundRecordingServiceProvider* pSoundRecorder)
{
  return std::make_unique<HostSession>(
    port, numPlayers, sessionId, pWorld, pSoundRecorder);
}


std::unique_ptr<NetworkSession> createClientSession(
  std::shared_ptr<network::NetworkClient> pClient,
  INetworkWorld* pWorld,
  IGameServiceProvider* pServiceProvider)
{
  return std::make_unique<ClientSession>(
    std::move(pClient), pWorld, pServiceProvider);
}

} // namespace rigel
