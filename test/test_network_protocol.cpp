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

#include <base/warnings.hpp>
#include <network/connection.hpp>
#include <network/protocol.hpp>

RIGEL_DISABLE_WARNINGS
#include <catch2/catch_test_macros.hpp>
RIGEL_RESTORE_WARNINGS


using namespace rigel;
using namespace rigel::network;


TEST_CASE("Network protocol: messages survive encoding and decoding")
{
  SECTION("Welcome")
  {
    const auto original =
      WelcomeMessage{2, 4, data::GameSessionId{1, 0, data::Difficulty::Hard}};
    const auto decoded = decodeWelcome(encode(original));

    REQUIRE(decoded);
    CHECK(decoded->mPlayerIndex == 2);
    CHECK(decoded->mNumPlayers == 4);
    CHECK(decoded->mSessionId.mEpisode == 1);
    CHECK(decoded->mSessionId.mLevel == 0);
    CHECK(decoded->mSessionId.mDifficulty == data::Difficulty::Hard);
  }

  SECTION("Input")
  {
    InputMessage original;
    original.mInput.mLeft = true;
    original.mInput.mDown = true;
    original.mInput.mJump = {true, true};
    original.mInput.mFire = {true, false};
    original.mViewportSize = {41, 20};
    original.mSequence = 70000;
    original.mPredictedPosition = base::Vec2{120, 37};

    const auto decoded = decodeInput(encode(original));

    REQUIRE(decoded);
    CHECK(decoded->mInput.mLeft);
    CHECK(!decoded->mInput.mRight);
    CHECK(decoded->mInput.mDown);
    CHECK(decoded->mInput.mJump.mIsPressed);
    CHECK(decoded->mInput.mJump.mWasTriggered);
    CHECK(decoded->mInput.mFire.mIsPressed);
    CHECK(!decoded->mInput.mFire.mWasTriggered);
    CHECK(!decoded->mInput.mInteract.mIsPressed);
    CHECK(decoded->mViewportSize == base::Size{41, 20});
    CHECK(decoded->mSequence == 70000);
    CHECK(decoded->mPredictedPosition == base::Vec2{120, 37});

    SECTION("Without prediction (player dead)")
    {
      original.mPredictedPosition.reset();
      const auto decodedDead = decodeInput(encode(original));
      REQUIRE(decodedDead);
      CHECK(!decodedDead->mPredictedPosition);
    }
  }

  SECTION("Snapshot")
  {
    game_logic::NetSnapshot original;
    original.mFrame = 123456;
    original.mLastInputSequence = 99999;
    original.mPlayerPosition = {210, 50};
    original.mCameraPosition = {200, 37};
    original.mScreenFlashColor = base::Color{255, 0, 0, 255};
    original.mHealth = 7;
    original.mWeapon = 2;
    original.mAmmo = 31;
    original.mWeaponConsumesAmmo = true;
    original.mFragLimit = 10;
    original.mRoundWinner = 3;
    original.mScores = {{true, 4, 1}, {false, 0, 0}};

    game_logic::NetSprite tinted;
    tinted.mDestRect = {{-16, 1600}, {24, 40}};
    tinted.mImageId = 777;
    tinted.mFlags = game_logic::NetSprite::Tinted |
      game_logic::NetSprite::FlashingWhite;
    tinted.mTint = {130, 170, 255, 255};
    original.mSprites = {tinted, game_logic::NetSprite{}};
    original.mFirstForegroundSprite = 1;

    original.mDynamicSections = {{5, {10, 20}, {{10, 16}, {3, 5}}, 5}};

    const auto decoded = decodeSnapshot(encode(original));

    REQUIRE(decoded);
    CHECK(decoded->mFrame == 123456);
    CHECK(decoded->mLastInputSequence == 99999);
    CHECK(decoded->mPlayerPosition == base::Vec2{210, 50});
    CHECK(decoded->mCameraPosition == base::Vec2{200, 37});
    CHECK(decoded->mScreenFlashColor == base::Color{255, 0, 0, 255});
    CHECK(decoded->mHealth == 7);
    CHECK(decoded->mWeapon == 2);
    CHECK(decoded->mAmmo == 31);
    CHECK(decoded->mWeaponConsumesAmmo);
    CHECK(decoded->mRoundWinner == 3);
    REQUIRE(decoded->mScores.size() == 2);
    CHECK(decoded->mScores[0].mFrags == 4);
    CHECK(!decoded->mScores[1].mIsActive);

    REQUIRE(decoded->mSprites.size() == 2);
    CHECK(decoded->mSprites[0].mDestRect == tinted.mDestRect);
    CHECK(decoded->mSprites[0].mImageId == 777);
    CHECK(decoded->mSprites[0].mFlags == tinted.mFlags);
    CHECK(decoded->mSprites[0].mTint == tinted.mTint);
    CHECK(decoded->mFirstForegroundSprite == 1);

    REQUIRE(decoded->mDynamicSections.size() == 1);
    CHECK(decoded->mDynamicSections[0].mIndex == 5);
    CHECK(decoded->mDynamicSections[0].mPosition == base::Vec2{10, 20});
    CHECK(decoded->mDynamicSections[0].mSection.size == base::Size{3, 5});
  }

  SECTION("Tile changes, text and sounds")
  {
    const auto changes = std::vector<game_logic::NetTileChange>{
      {0, {255, 126}, 999}, {1, {3, 4}, 1100}};
    const auto decodedChanges = decodeTileChanges(encode(changes));
    REQUIRE(decodedChanges);
    REQUIRE(decodedChanges->size() == 2);
    CHECK((*decodedChanges)[0].mPosition == base::Vec2{255, 126});
    CHECK((*decodedChanges)[1].mLayer == 1);
    CHECK((*decodedChanges)[1].mTile == 1100);

    CHECK(
      decodeTextMessage(encodeTextMessage("P2 FRAGGED P1!")) ==
      std::string{"P2 FRAGGED P1!"});

    CHECK(decodeSounds(encodeSounds({0, 5, 40})) == std::vector<int>{0, 5, 40});
  }
}


TEST_CASE("Network protocol: garbage is rejected")
{
  const auto snapshot = encode(game_logic::NetSnapshot{});

  SECTION("Truncated message")
  {
    auto truncated = snapshot;
    truncated.pop_back();
    CHECK(!decodeSnapshot(truncated));
  }

  SECTION("Extra data at the end")
  {
    auto extended = snapshot;
    extended.push_back(0);
    CHECK(!decodeSnapshot(extended));
  }

  SECTION("Wrong message type")
  {
    CHECK(!decodeInput(snapshot));
    CHECK(!decodeHello(ByteBuffer{}));
    CHECK(!messageType(ByteBuffer{200}));
  }

  SECTION("Invalid level in welcome")
  {
    auto welcome = encode(WelcomeMessage{0, 2, data::GameSessionId{0, 0}});
    welcome[3] = 9; // episode
    CHECK(!decodeWelcome(welcome));
  }
}


TEST_CASE("Network input merging keeps button presses")
{
  game_logic::PlayerInput older;
  older.mFire = {true, true};
  older.mLeft = true;

  game_logic::PlayerInput newer;
  newer.mFire = {false, false};
  newer.mRight = true;

  const auto merged = mergeInputs(older, newer);

  // A quick tap between two frames still fires
  CHECK(merged.mFire.mWasTriggered);
  CHECK(!merged.mFire.mIsPressed);

  // Held directions come from the newest input
  CHECK(merged.mRight);
  CHECK(!merged.mLeft);
}


TEST_CASE("Network address parsing")
{
  CHECK(
    parseAddress("192.168.1.20", 7777) ==
    std::pair<std::string, std::uint16_t>{"192.168.1.20", 7777});
  CHECK(
    parseAddress("example.com:9000", 7777) ==
    std::pair<std::string, std::uint16_t>{"example.com", 9000});
  CHECK_THROWS(parseAddress("host:abc", 7777));
  CHECK_THROWS(parseAddress("host:70000", 7777));
}
