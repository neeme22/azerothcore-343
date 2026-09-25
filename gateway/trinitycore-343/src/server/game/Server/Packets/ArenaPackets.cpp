/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ArenaPackets.h"

void WorldPackets::Arena::ArenaTeamRoster::Read()
{
    _worldPacket >> TeamSlot;
}

WorldPacket const* WorldPackets::Arena::ArenaTeamRosterResponse::Write()
{
    _worldPacket << uint32(TeamSlot);
    _worldPacket << uint32(TeamSize);
    _worldPacket << uint32(TeamPlayed);
    _worldPacket << uint32(TeamWins);
    _worldPacket << uint32(SeasonPlayed);
    _worldPacket << uint32(SeasonWins);
    _worldPacket << uint32(TeamRating);
    _worldPacket << uint32(PlayerRating);
    _worldPacket << int32(MembersCount);
    _worldPacket << uint8(Unk);

    return &_worldPacket;
}
