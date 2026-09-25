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

#ifndef ArenaPackets_h__
#define ArenaPackets_h__

namespace WorldPackets
{
    namespace Arena
    {
        class ArenaTeamRoster final : public ClientPacket
        {
        public:
            ArenaTeamRoster(WorldPacket&& packet) : ClientPacket(CMSG_ARENA_TEAM_ROSTER, std::move(packet)) { }

            void Read() override;

            uint32 TeamSlot = 0;
        };

        class ArenaTeamRosterResponse final : public ServerPacket
        {
        public:
            ArenaTeamRosterResponse() : ServerPacket(SMSG_ARENA_TEAM_ROSTER, 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 4 + 1) { }

            WorldPacket const* Write() override;

            uint32 TeamSlot     = 0;
            uint32 TeamSize     = 0;
            uint32 TeamPlayed   = 0;
            uint32 TeamWins     = 0;
            uint32 SeasonPlayed = 0;
            uint32 SeasonWins   = 0;
            uint32 TeamRating   = 0;
            uint32 PlayerRating = 0;
            int32  MembersCount = 0;
            uint8  Unk          = 0;
        };
    }
}

#endif // ArenaPackets_h__
