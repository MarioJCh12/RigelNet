/* Copyright (C) 2026, RigelEngine deathmatch contributors
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once


namespace rigel::game_logic
{

constexpr auto MIN_DEATHMATCH_PLAYERS = 2;
constexpr auto MAX_DEATHMATCH_PLAYERS = 4;


/** Settings for a local (split-screen) deathmatch session */
struct DeathmatchOptions
{
  /** Number of players, between MIN_DEATHMATCH_PLAYERS and
   * MAX_DEATHMATCH_PLAYERS */
  int mNumPlayers = 2;

  /** A round ends once a player reaches this many kills */
  int mFragLimit = 10;

  /** If false, all enemies are removed from the map at the start */
  bool mKeepEnemies = false;

  /** Seconds until a picked-up item or destroyed item box re-appears */
  int mItemRespawnSeconds = 20;

  /** How many of the players play on this computer (split-screen).
   *
   * The remaining player slots are for players joining over the network.
   * 0 means all players are local.
   */
  int mNumLocalPlayers = 0;
};

} // namespace rigel::game_logic
