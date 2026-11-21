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

#include "data/game_session_data.hpp"
#include "frontend/game_service_provider.hpp"
#include "game_logic_common/input.hpp"
#include "game_logic_common/network_world.hpp"

#include <memory>
#include <vector>


namespace rigel::network
{
class NetworkClient;
class NetworkHost;
} // namespace rigel::network


namespace rigel
{

/** Forwards everything to another service provider, but also keeps a record
 * of all sounds played. Used by network hosts to tell clients which sounds
 * to play.
 */
class SoundRecordingServiceProvider : public IGameServiceProvider
{
public:
  explicit SoundRecordingServiceProvider(IGameServiceProvider* pTarget)
    : mpTarget(pTarget)
  {
  }

  void setRecordingEnabled(const bool enabled) { mRecordingEnabled = enabled; }

  /** Sounds played since the last call */
  std::vector<int> takeRecordedSounds();

  void fadeOutScreen() override { mpTarget->fadeOutScreen(); }
  void fadeInScreen() override { mpTarget->fadeInScreen(); }
  void playSound(data::SoundId id) override;
  void stopSound(data::SoundId id) override { mpTarget->stopSound(id); }
  void stopAllSounds() override { mpTarget->stopAllSounds(); }
  void playMusic(const std::string& name) override
  {
    mpTarget->playMusic(name);
  }
  void stopMusic() override { mpTarget->stopMusic(); }
  void scheduleGameQuit() override { mpTarget->scheduleGameQuit(); }
  void switchGamePath(const std::filesystem::path& newGamePath) override
  {
    mpTarget->switchGamePath(newGamePath);
  }
  void markCurrentFrameAsWidescreen() override
  {
    mpTarget->markCurrentFrameAsWidescreen();
  }
  bool isSharewareVersion() const override
  {
    return mpTarget->isSharewareVersion();
  }
  const CommandLineOptions& commandLineOptions() const override
  {
    return mpTarget->commandLineOptions();
  }
  const GameControllerInfo& gameControllerInfo() const override
  {
    return mpTarget->gameControllerInfo();
  }

private:
  IGameServiceProvider* mpTarget;
  std::vector<int> mRecordedSounds;
  bool mRecordingEnabled = false;
};


/** Connects a GameWorld to the network, as host or as client.
 *
 * Owned by the GameRunner. Each game logic frame, call receive() first,
 * then let the GameRunner do its update (adding inputs from network players
 * on the host), then sendUpdates().
 */
class NetworkSession
{
public:
  virtual ~NetworkSession() = default;

  /** Process incoming network traffic */
  virtual void receive() = 0;

  /** Host only: Put inputs from network players into their slots */
  virtual void
    fillRemoteInputs(std::vector<game_logic::PlayerInput>& inputs) = 0;

  /** Send this frame's results (host) or input (client) */
  virtual void sendUpdates(const game_logic::PlayerInput& localInput) = 0;

  /** Keep connections alive while the game is paused (e.g. menu open) */
  virtual void keepAlive() = 0;

  virtual bool isClient() const = 0;

  /** Client only: True once the connection to the host is gone and the
   * player has seen the message about it */
  virtual bool shouldLeaveGame() const = 0;
};


std::unique_ptr<NetworkSession> createHostSession(
  std::uint16_t port,
  int numPlayers,
  const data::GameSessionId& sessionId,
  game_logic::INetworkWorld* pWorld,
  SoundRecordingServiceProvider* pSoundRecorder);

std::unique_ptr<NetworkSession> createClientSession(
  std::shared_ptr<network::NetworkClient> pClient,
  game_logic::INetworkWorld* pWorld,
  IGameServiceProvider* pServiceProvider);

} // namespace rigel
