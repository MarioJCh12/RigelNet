/* Copyright (C) 2016, Nikolai Wuttke. All rights reserved.
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

#include "game_runner.hpp"

#include "base/math_utils.hpp"
#include "frontend/game_service_provider.hpp"
#include "frontend/user_profile.hpp"
#include "game_logic/game_world.hpp"
#include "game_logic_classic/game_world_classic.hpp"
#include "ui/utils.hpp"

#include <sstream>


namespace rigel
{

namespace
{

template <typename... Args>
std::unique_ptr<game_logic::IGameWorld> createGameWorld(
  const data::GameplayStyle gameplayStyle,
  const bool isDeathmatch,
  Args&&... args)
{
  // Deathmatch is only implemented for the modern (non-classic) game logic
  if (gameplayStyle == data::GameplayStyle::Classic && !isDeathmatch)
  {
    return std::make_unique<game_logic::GameWorld_Classic>(
      std::forward<Args>(args)...);
  }
  else
  {
    return std::make_unique<game_logic::GameWorld>(std::forward<Args>(args)...);
  }
}

GameMode::Context withServiceProvider(
  GameMode::Context context,
  IGameServiceProvider* pServiceProvider)
{
  context.mpServiceProvider = pServiceProvider;
  return context;
}

} // namespace


GameRunner::GameRunner(
  data::PersistentPlayerState* pPersistentPlayerState,
  const data::GameSessionId& sessionId,
  GameMode::Context context,
  const std::optional<base::Vec2> playerPositionOverride,
  const bool showWelcomeMessage)
  : mContext(context)
  , mServiceProviderProxy(context.mpServiceProvider)
  , mpWorld(createGameWorld(
      context.mpUserProfile->mOptions.mGameplayStyle,
      context.mpServiceProvider->commandLineOptions().mDeathmatch.has_value(),
      pPersistentPlayerState,
      sessionId,
      withServiceProvider(context, &mServiceProviderProxy),
      playerPositionOverride,
      showWelcomeMessage))
  , mInputHandler(&context.mpUserProfile->mOptions)
  , mMenu(context, pPersistentPlayerState, mpWorld.get(), sessionId)
{
  const auto& commandLine = context.mpServiceProvider->commandLineOptions();
  const auto& deathmatchOptions = commandLine.mDeathmatch;
  auto pNetworkWorld = dynamic_cast<game_logic::INetworkWorld*>(mpWorld.get());

  if (commandLine.mpNetworkClient && pNetworkWorld)
  {
    // Network client: The game runs on the host, we only show it
    mpNetworkSession = createClientSession(
      commandLine.mpNetworkClient, pNetworkWorld, context.mpServiceProvider);
  }
  else if (deathmatchOptions && mpWorld->supportsDeathmatch())
  {
    mpWorld->enableDeathmatch(*deathmatchOptions);

    if (commandLine.mHostPort && pNetworkWorld)
    {
      mpNetworkSession = createHostSession(
        *commandLine.mHostPort,
        deathmatchOptions->mNumPlayers,
        sessionId,
        pNetworkWorld,
        &mServiceProviderProxy);
    }


    mInputHandler.setMultiplayerSlot(0);

    const auto numLocalPlayers = deathmatchOptions->mNumLocalPlayers <= 0
      ? deathmatchOptions->mNumPlayers
      : deathmatchOptions->mNumLocalPlayers;
    for (auto slot = 1; slot < numLocalPlayers; ++slot)
    {
      auto& handler =
        mExtraInputHandlers.emplace_back(&context.mpUserProfile->mOptions);
      handler.setMultiplayerSlot(slot);
    }
  }
}


void GameRunner::handleEvent(const SDL_Event& event)
{
  if (gameQuit() || requestedGameToLoad())
  {
    return;
  }

  mMenu.handleEvent(event);
  if (mMenu.isActive())
  {
    // The menu overrides game event handling when it is active, therefore
    // stop here
    return;
  }

  const auto menuCommand =
    mInputHandler.handleEvent(event, mpWorld->isPlayerInShip());

  for (auto& handler : mExtraInputHandlers)
  {
    handler.handleEvent(event, false);
  }

  switch (menuCommand)
  {
    case InputHandler::MenuCommand::QuickSave:
      mpWorld->quickSave();
      break;

    case InputHandler::MenuCommand::QuickLoad:
      mpWorld->quickLoad();
      break;

    default:
      break;
  }

  const auto debugModeEnabled =
    mContext.mpServiceProvider->commandLineOptions().mDebugModeEnabled;
  if (debugModeEnabled)
  {
    handleDebugKeys(event);
  }
}


void GameRunner::updateAndRender(engine::TimeDelta dt)
{
  if (gameQuit() || levelFinished() || requestedGameToLoad())
  {
    // TODO: This is a workaround to make the fadeout on quitting work.
    // Would be good to find a better way to do this.
    mpWorld->render(interpolationFactor(dt));
    return;
  }

  if (updateMenu(dt))
  {
    if (mpNetworkSession)
    {
      mpNetworkSession->keepAlive();
    }
    return;
  }

  updateWorld(dt);
  mpWorld->render(interpolationFactor(dt));

  renderDebugText();
  mpWorld->processEndOfFrameActions();
}


bool GameRunner::needsPerElementUpscaling() const
{
  return mpWorld->needsPerElementUpscaling();
}


float GameRunner::interpolationFactor(const engine::TimeDelta dt) const
{
  return mContext.mpUserProfile->mOptions.mMotionSmoothing
    ? static_cast<float>(mAccumulatedTime / game_logic::GAME_LOGIC_UPDATE_DELAY)
    : 1.0f;
}


void GameRunner::updateWorld(const engine::TimeDelta dt)
{
  auto update = [this]()
  {
    if (mpNetworkSession)
    {
      mpNetworkSession->receive();
    }

    auto localInput = mInputHandler.fetchInput();

    const auto& script =
      mContext.mpServiceProvider->commandLineOptions().mDebugInputScript;
    if (!script.empty())
    {
      const auto c = script[mScriptedInputFrame++ % script.size()];
      const auto previous =
        script[(mScriptedInputFrame + script.size() - 2) % script.size()];
      localInput = game_logic::PlayerInput{};
      localInput.mLeft = c == 'L';
      localInput.mRight = c == 'R';
      localInput.mUp = c == 'U';
      localInput.mDown = c == 'D';
      localInput.mJump = {c == 'J', c == 'J' && previous != 'J'};
      localInput.mFire = {c == 'F', c == 'F' && previous != 'F'};
    }

    if (mpNetworkSession && mpNetworkSession->isClient())
    {
      // The game runs on the host, our world only animates
      mpWorld->updateGameLogic(localInput);
    }
    else if (mExtraInputHandlers.empty() && !mpNetworkSession)
    {
      mpWorld->updateGameLogic(localInput);
    }
    else
    {
      std::vector<game_logic::PlayerInput> inputs;
      inputs.push_back(localInput);
      for (auto& handler : mExtraInputHandlers)
      {
        inputs.push_back(handler.fetchInput());
      }

      if (mpNetworkSession)
      {
        inputs.resize(size_t(mContext.mpServiceProvider->commandLineOptions()
                               .mDeathmatch->mNumPlayers));
        mpNetworkSession->fillRemoteInputs(inputs);
      }

      mpWorld->updateGameLogicMultiplayer(inputs);
    }

    if (mpNetworkSession)
    {
      mpNetworkSession->sendUpdates(localInput);
    }
  };


  if (mSingleStepping)
  {
    if (mDoNextSingleStep)
    {
      update();
      mDoNextSingleStep = false;
    }
  }
  else
  {
    mAccumulatedTime += dt;

    // Unusually long delta time - most likely, the game was paused in the
    // debugger, or something else happened (computer put to sleep?)
    // Ignore this delta time.
    if (mAccumulatedTime > 0.25)
    {
      mAccumulatedTime = 0.0;
    }

    if (mAccumulatedTime >= game_logic::GAME_LOGIC_UPDATE_DELAY)
    {
      update();
      mAccumulatedTime -= game_logic::GAME_LOGIC_UPDATE_DELAY;
    }

    mpWorld->updateBackdropAutoScrolling(dt);
  }
}


bool GameRunner::updateMenu(const engine::TimeDelta dt)
{
  if (mMenu.isActive())
  {
    mInputHandler.reset();
    for (auto& handler : mExtraInputHandlers)
    {
      handler.reset();
    }

    if (mMenu.isTransparent())
    {
      mpWorld->render(interpolationFactor(dt));
    }

    const auto result = mMenu.updateAndRender(dt);

    if (result == ui::IngameMenu::UpdateResult::FinishedNeedsFadeout)
    {
      mContext.mpServiceProvider->fadeOutScreen();
      mpWorld->render(interpolationFactor(dt));
      mContext.mpServiceProvider->fadeInScreen();
    }

    return true;
  }

  return false;
}


void GameRunner::handleDebugKeys(const SDL_Event& event)
{
  if (event.type != SDL_KEYDOWN || event.key.repeat != 0)
  {
    return;
  }

  switch (event.key.keysym.sym)
  {
    case SDLK_b:
      mpWorld->debugToggleBoundingBoxDisplay();
      break;

    case SDLK_c:
      mpWorld->debugToggleWorldCollisionDataDisplay();
      break;

    case SDLK_d:
      mShowDebugText = !mShowDebugText;
      break;

    case SDLK_g:
      mpWorld->debugToggleGridDisplay();
      break;

    case SDLK_s:
      mSingleStepping = !mSingleStepping;
      break;

    case SDLK_SPACE:
      if (mSingleStepping)
      {
        mDoNextSingleStep = true;
      }
      break;

    case SDLK_F10:
      mpWorld->toggleGodMode();
      break;

    case SDLK_F11:
      mLevelFinishedByDebugKey = true;
      break;
  }
}


void GameRunner::renderDebugText()
{
  std::stringstream debugText;

  if (mpWorld->isGodModeOn())
  {
    debugText << "GOD MODE on\n";
  }

  if (mShowDebugText)
  {
    mpWorld->printDebugText(debugText);
  }

  ui::drawText(debugText.str(), 0, 32, {255, 255, 255, 255});
}


bool GameRunner::levelFinished() const
{
  return mpWorld->levelFinished() || mLevelFinishedByDebugKey;
}


bool GameRunner::gameQuit() const
{
  return mMenu.quitRequested() ||
    (mpNetworkSession && mpNetworkSession->shouldLeaveGame());
}


std::optional<data::SavedGame> GameRunner::requestedGameToLoad() const
{
  return mMenu.requestedGameToLoad();
}


std::set<data::Bonus> GameRunner::achievedBonuses() const
{
  return mpWorld->achievedBonuses();
}

} // namespace rigel
