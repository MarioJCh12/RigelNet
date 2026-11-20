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

#include "protocol.hpp"

#include <algorithm>
#include <limits>


namespace rigel::network
{

using game_logic::NetDynamicSection;
using game_logic::NetPlayerScore;
using game_logic::NetSnapshot;
using game_logic::NetSprite;
using game_logic::NetTileChange;
using game_logic::PlayerInput;

namespace
{

// Upper bounds for list sizes, to reject garbage instead of allocating
// huge amounts of memory
constexpr auto MAX_SPRITES = 4096u;
constexpr auto MAX_DYNAMIC_SECTIONS = 4096u;
constexpr auto MAX_PLAYERS = 16u;
constexpr auto MAX_TILE_CHANGES = 1u << 20;
constexpr auto MAX_TEXT_LENGTH = 1024u;
constexpr auto MAX_SOUNDS = 256u;


class Writer
{
public:
  explicit Writer(const MessageType type) { u8(static_cast<std::uint8_t>(type)); }

  void u8(const std::uint32_t value)
  {
    mData.push_back(static_cast<std::uint8_t>(value));
  }

  void u16(const std::uint32_t value)
  {
    u8(value & 0xFF);
    u8((value >> 8) & 0xFF);
  }

  void u32(const std::uint32_t value)
  {
    u16(value & 0xFFFF);
    u16((value >> 16) & 0xFFFF);
  }

  void s16(const int value)
  {
    const auto clamped = std::clamp(
      value,
      int(std::numeric_limits<std::int16_t>::min()),
      int(std::numeric_limits<std::int16_t>::max()));
    u16(static_cast<std::uint16_t>(static_cast<std::int16_t>(clamped)));
  }

  void s32(const int value) { u32(static_cast<std::uint32_t>(value)); }

  void boolean(const bool value) { u8(value ? 1 : 0); }

  void text(const std::string& value)
  {
    const auto length = std::min<size_t>(value.size(), MAX_TEXT_LENGTH);
    u16(std::uint32_t(length));
    mData.insert(mData.end(), value.begin(), value.begin() + length);
  }

  void color(const base::Color& value)
  {
    u8(value.r);
    u8(value.g);
    u8(value.b);
    u8(value.a);
  }

  void rect(const base::Rect<int>& value)
  {
    s16(value.topLeft.x);
    s16(value.topLeft.y);
    s16(value.size.width);
    s16(value.size.height);
  }

  ByteBuffer take() { return std::move(mData); }

private:
  ByteBuffer mData;
};


class Reader
{
public:
  Reader(const ByteBuffer& data, const MessageType expectedType)
    : mData(data)
  {
    if (u8() != static_cast<std::uint8_t>(expectedType))
    {
      mOk = false;
    }
  }

  bool ok() const { return mOk; }

  /** True if everything was read without running out of data */
  bool finishedOk() const { return mOk && mPosition == mData.size(); }

  std::uint32_t u8()
  {
    if (mPosition >= mData.size())
    {
      mOk = false;
      return 0;
    }

    return mData[mPosition++];
  }

  std::uint32_t u16()
  {
    const auto low = u8();
    const auto high = u8();
    return low | (high << 8);
  }

  std::uint32_t u32()
  {
    const auto low = u16();
    const auto high = u16();
    return low | (high << 16);
  }

  int s16() { return static_cast<std::int16_t>(u16()); }

  int s32() { return static_cast<std::int32_t>(u32()); }

  bool boolean() { return u8() != 0; }

  std::string text()
  {
    const auto length = u16();
    if (length > MAX_TEXT_LENGTH || mPosition + length > mData.size())
    {
      mOk = false;
      return {};
    }

    std::string result(
      mData.begin() + mPosition, mData.begin() + mPosition + length);
    mPosition += length;
    return result;
  }

  base::Color color()
  {
    base::Color result;
    result.r = std::uint8_t(u8());
    result.g = std::uint8_t(u8());
    result.b = std::uint8_t(u8());
    result.a = std::uint8_t(u8());
    return result;
  }

  base::Rect<int> rect()
  {
    base::Rect<int> result;
    result.topLeft.x = s16();
    result.topLeft.y = s16();
    result.size.width = s16();
    result.size.height = s16();
    return result;
  }

  /** Read a list length, failing if it exceeds the given maximum */
  std::uint32_t count(const std::uint32_t maximum, const bool wide = false)
  {
    const auto value = wide ? u32() : u16();
    if (value > maximum)
    {
      mOk = false;
      return 0;
    }

    return value;
  }

private:
  const ByteBuffer& mData;
  size_t mPosition = 0;
  bool mOk = true;
};


void writeButton(Writer& writer, const game_logic::Button& button)
{
  writer.u8(
    (button.mIsPressed ? 1u : 0u) | (button.mWasTriggered ? 2u : 0u));
}


game_logic::Button readButton(Reader& reader)
{
  const auto bits = reader.u8();
  return game_logic::Button{(bits & 1) != 0, (bits & 2) != 0};
}

} // namespace


std::optional<MessageType> messageType(const ByteBuffer& message)
{
  if (message.empty())
  {
    return std::nullopt;
  }

  const auto type = message[0];
  if (
    type < static_cast<std::uint8_t>(MessageType::Hello) ||
    type > static_cast<std::uint8_t>(MessageType::Sounds))
  {
    return std::nullopt;
  }

  return static_cast<MessageType>(type);
}


ByteBuffer encode(const HelloMessage& message)
{
  Writer writer{MessageType::Hello};
  writer.u32(message.mMagic);
  writer.u16(message.mVersion);
  return writer.take();
}


std::optional<HelloMessage> decodeHello(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Hello};
  HelloMessage result;
  result.mMagic = reader.u32();
  result.mVersion = std::uint16_t(reader.u16());

  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return result;
}


ByteBuffer encode(const WelcomeMessage& message)
{
  Writer writer{MessageType::Welcome};
  writer.u8(std::uint32_t(message.mPlayerIndex));
  writer.u8(std::uint32_t(message.mNumPlayers));
  writer.u8(std::uint32_t(message.mSessionId.mEpisode));
  writer.u8(std::uint32_t(message.mSessionId.mLevel));
  writer.u8(static_cast<std::uint32_t>(message.mSessionId.mDifficulty));
  return writer.take();
}


std::optional<WelcomeMessage> decodeWelcome(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Welcome};
  WelcomeMessage result;
  result.mPlayerIndex = int(reader.u8());
  result.mNumPlayers = int(reader.u8());
  const auto episode = int(reader.u8());
  const auto level = int(reader.u8());
  const auto difficulty = reader.u8();

  if (
    !reader.finishedOk() || episode >= 4 || level >= 8 || difficulty > 2 ||
    result.mPlayerIndex >= result.mNumPlayers ||
    result.mNumPlayers > int(MAX_PLAYERS))
  {
    return std::nullopt;
  }

  result.mSessionId = data::GameSessionId{
    episode, level, static_cast<data::Difficulty>(difficulty)};
  return result;
}


ByteBuffer encode(const RejectMessage& message)
{
  Writer writer{MessageType::Reject};
  writer.text(message.mReason);
  return writer.take();
}


std::optional<RejectMessage> decodeReject(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Reject};
  RejectMessage result{reader.text()};
  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return result;
}


ByteBuffer encode(const InputMessage& message)
{
  const auto& input = message.mInput;

  Writer writer{MessageType::Input};
  writer.u32(message.mSequence);
  writer.u8(
    (input.mLeft ? 1u : 0u) | (input.mRight ? 2u : 0u) |
    (input.mUp ? 4u : 0u) | (input.mDown ? 8u : 0u));
  writeButton(writer, input.mInteract);
  writeButton(writer, input.mJump);
  writeButton(writer, input.mFire);
  writer.u8(std::uint32_t(std::clamp(message.mViewportSize.width, 0, 255)));
  writer.u8(std::uint32_t(std::clamp(message.mViewportSize.height, 0, 255)));
  writer.boolean(message.mPredictedPosition.has_value());
  if (message.mPredictedPosition)
  {
    writer.s16(message.mPredictedPosition->x);
    writer.s16(message.mPredictedPosition->y);
  }
  return writer.take();
}


std::optional<InputMessage> decodeInput(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Input};
  InputMessage result;
  auto& input = result.mInput;

  result.mSequence = reader.u32();
  const auto directions = reader.u8();
  input.mLeft = (directions & 1) != 0;
  input.mRight = (directions & 2) != 0;
  input.mUp = (directions & 4) != 0;
  input.mDown = (directions & 8) != 0;
  input.mInteract = readButton(reader);
  input.mJump = readButton(reader);
  input.mFire = readButton(reader);
  result.mViewportSize.width = int(reader.u8());
  result.mViewportSize.height = int(reader.u8());
  if (reader.boolean())
  {
    base::Vec2 position;
    position.x = reader.s16();
    position.y = reader.s16();
    result.mPredictedPosition = position;
  }

  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return result;
}


ByteBuffer encode(const NetSnapshot& snapshot)
{
  Writer writer{MessageType::Snapshot};
  writer.u32(snapshot.mFrame);
  writer.u32(snapshot.mLastInputSequence);
  writer.s16(snapshot.mPlayerPosition.x);
  writer.s16(snapshot.mPlayerPosition.y);
  writer.s16(snapshot.mCameraPosition.x);
  writer.s16(snapshot.mCameraPosition.y);
  writer.s16(snapshot.mScreenShakeOffsetX);
  writer.boolean(snapshot.mScreenFlashColor.has_value());
  if (snapshot.mScreenFlashColor)
  {
    writer.color(*snapshot.mScreenFlashColor);
  }

  writer.boolean(snapshot.mIsDead);
  writer.s16(snapshot.mHealth);
  writer.u8(std::uint32_t(snapshot.mWeapon));
  writer.s16(snapshot.mAmmo);
  writer.boolean(snapshot.mWeaponConsumesAmmo);

  writer.s16(snapshot.mFragLimit);
  writer.s16(snapshot.mRoundWinner ? *snapshot.mRoundWinner : -1);

  writer.u16(std::uint32_t(snapshot.mScores.size()));
  for (const auto& score : snapshot.mScores)
  {
    writer.boolean(score.mIsActive);
    writer.s16(score.mFrags);
    writer.s16(score.mDeaths);
  }

  writer.u16(std::uint32_t(snapshot.mSprites.size()));
  for (const auto& sprite : snapshot.mSprites)
  {
    writer.rect(sprite.mDestRect);
    writer.u16(std::uint32_t(sprite.mImageId));
    writer.u8(sprite.mFlags);
    if (sprite.mFlags & NetSprite::Tinted)
    {
      writer.color(sprite.mTint);
    }
  }
  writer.u16(std::uint32_t(snapshot.mFirstForegroundSprite));

  writer.u16(std::uint32_t(snapshot.mDynamicSections.size()));
  for (const auto& section : snapshot.mDynamicSections)
  {
    writer.u16(std::uint32_t(section.mIndex));
    writer.s16(section.mPosition.x);
    writer.s16(section.mPosition.y);
    writer.rect(section.mSection);
    writer.s16(section.mPreviousHeight);
  }

  return writer.take();
}


std::optional<NetSnapshot> decodeSnapshot(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Snapshot};
  NetSnapshot snapshot;

  snapshot.mFrame = reader.u32();
  snapshot.mLastInputSequence = reader.u32();
  snapshot.mPlayerPosition.x = reader.s16();
  snapshot.mPlayerPosition.y = reader.s16();
  snapshot.mCameraPosition.x = reader.s16();
  snapshot.mCameraPosition.y = reader.s16();
  snapshot.mScreenShakeOffsetX = reader.s16();
  if (reader.boolean())
  {
    snapshot.mScreenFlashColor = reader.color();
  }

  snapshot.mIsDead = reader.boolean();
  snapshot.mHealth = reader.s16();
  snapshot.mWeapon = int(reader.u8());
  snapshot.mAmmo = reader.s16();
  snapshot.mWeaponConsumesAmmo = reader.boolean();

  snapshot.mFragLimit = reader.s16();
  const auto roundWinner = reader.s16();
  if (roundWinner >= 0)
  {
    snapshot.mRoundWinner = roundWinner;
  }

  const auto numScores = reader.count(MAX_PLAYERS);
  for (auto i = 0u; i < numScores && reader.ok(); ++i)
  {
    NetPlayerScore score;
    score.mIsActive = reader.boolean();
    score.mFrags = reader.s16();
    score.mDeaths = reader.s16();
    snapshot.mScores.push_back(score);
  }

  const auto numSprites = reader.count(MAX_SPRITES);
  snapshot.mSprites.reserve(numSprites);
  for (auto i = 0u; i < numSprites && reader.ok(); ++i)
  {
    NetSprite sprite;
    sprite.mDestRect = reader.rect();
    sprite.mImageId = int(reader.u16());
    sprite.mFlags = std::uint8_t(reader.u8());
    if (sprite.mFlags & NetSprite::Tinted)
    {
      sprite.mTint = reader.color();
    }
    snapshot.mSprites.push_back(sprite);
  }
  snapshot.mFirstForegroundSprite = int(reader.u16());

  const auto numSections = reader.count(MAX_DYNAMIC_SECTIONS);
  for (auto i = 0u; i < numSections && reader.ok(); ++i)
  {
    NetDynamicSection section;
    section.mIndex = int(reader.u16());
    section.mPosition.x = reader.s16();
    section.mPosition.y = reader.s16();
    section.mSection = reader.rect();
    section.mPreviousHeight = reader.s16();
    snapshot.mDynamicSections.push_back(section);
  }

  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return snapshot;
}


ByteBuffer encode(const std::vector<NetTileChange>& changes)
{
  Writer writer{MessageType::TileChanges};
  writer.u32(std::uint32_t(changes.size()));
  for (const auto& change : changes)
  {
    writer.u8(std::uint32_t(change.mLayer));
    writer.u16(std::uint32_t(change.mPosition.x));
    writer.u16(std::uint32_t(change.mPosition.y));
    writer.u16(std::uint32_t(change.mTile));
  }
  return writer.take();
}


std::optional<std::vector<NetTileChange>>
  decodeTileChanges(const ByteBuffer& message)
{
  Reader reader{message, MessageType::TileChanges};

  std::vector<NetTileChange> changes;
  const auto count = reader.count(MAX_TILE_CHANGES, true);
  for (auto i = 0u; i < count && reader.ok(); ++i)
  {
    NetTileChange change;
    change.mLayer = int(reader.u8());
    change.mPosition.x = int(reader.u16());
    change.mPosition.y = int(reader.u16());
    change.mTile = int(reader.u16());
    changes.push_back(change);
  }

  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return changes;
}


ByteBuffer encodeTextMessage(const std::string& text)
{
  Writer writer{MessageType::Message};
  writer.text(text);
  return writer.take();
}


std::optional<std::string> decodeTextMessage(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Message};
  auto text = reader.text();
  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return text;
}


ByteBuffer encodeSounds(const std::vector<int>& soundIds)
{
  Writer writer{MessageType::Sounds};
  const auto count = std::min<size_t>(soundIds.size(), MAX_SOUNDS);
  writer.u16(std::uint32_t(count));
  for (auto i = 0u; i < count; ++i)
  {
    writer.u8(std::uint32_t(soundIds[i]));
  }
  return writer.take();
}


std::optional<std::vector<int>> decodeSounds(const ByteBuffer& message)
{
  Reader reader{message, MessageType::Sounds};

  std::vector<int> soundIds;
  const auto count = reader.count(MAX_SOUNDS);
  for (auto i = 0u; i < count && reader.ok(); ++i)
  {
    soundIds.push_back(int(reader.u8()));
  }

  if (!reader.finishedOk())
  {
    return std::nullopt;
  }
  return soundIds;
}


PlayerInput mergeInputs(const PlayerInput& older, const PlayerInput& newer)
{
  auto result = newer;
  result.mInteract.mWasTriggered |= older.mInteract.mWasTriggered;
  result.mJump.mWasTriggered |= older.mJump.mWasTriggered;
  result.mFire.mWasTriggered |= older.mFire.mWasTriggered;
  return result;
}

} // namespace rigel::network
