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

#include "InspectPackets.h"
#include "Item.h"
#include "Player.h"

namespace WorldPackets::Inspect
{
void Inspect::Read()
{
    _worldPacket >> Target;
}

ByteBuffer& operator<<(ByteBuffer& data, InspectEnchantData const& enchantData)
{
    data << uint32(enchantData.Id);
    data << uint8(enchantData.Index);

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, AzeriteEssenceData const& azeriteEssenceData)
{
    data << uint32(azeriteEssenceData.Index);
    data << uint32(azeriteEssenceData.AzeriteEssenceID);
    data << uint32(azeriteEssenceData.Rank);
    data.WriteBit(azeriteEssenceData.SlotUnlocked);
    data.FlushBits();

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, InspectItemData const& itemData)
{
    data << itemData.CreatorGUID;
    data << uint8(itemData.Index);
    data << uint32(itemData.AzeritePowers.size());
    data << uint32(itemData.AzeriteEssences.size());

    if (!itemData.AzeritePowers.empty())
        data.append(itemData.AzeritePowers.data(), itemData.AzeritePowers.size());

    data << itemData.Item;
    data.WriteBit(itemData.Usable);
    data.WriteBits(itemData.Enchants.size(), 4);
    data.WriteBits(itemData.Gems.size(), 2);
    data.FlushBits();

    for (AzeriteEssenceData const& azeriteEssenceData : itemData.AzeriteEssences)
        data << azeriteEssenceData;

    for (InspectEnchantData const& enchantData : itemData.Enchants)
        data << enchantData;

    for (Item::ItemGemData const& gem : itemData.Gems)
        data << gem;

    return data;
}

void PlayerModelDisplayInfo::Initialize(Player const* player)
{
    GUID = player->GetGUID();
    SpecializationID = AsUnderlyingType(player->GetPrimarySpecialization());
    Name = player->GetName();
    GenderID = player->GetNativeGender();
    Race = player->GetRace();
    ClassID = player->GetClass();

    for (UF::ChrCustomizationChoice const& customization : player->m_playerData->Customizations)
        Customizations.push_back(customization);

    for (uint8 i = 0; i < EQUIPMENT_SLOT_END; ++i)
        if (::Item* item = player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
            Items.emplace_back(item, i);
}

ByteBuffer& operator<<(ByteBuffer& data, PlayerModelDisplayInfo const& displayInfo)
{
    data << displayInfo.GUID;
    data << int32(displayInfo.SpecializationID);
    data << uint32(displayInfo.Items.size());
    data.WriteBits(displayInfo.Name.length(), 6);
    data << uint8(displayInfo.GenderID);
    data << uint8(displayInfo.Race);
    data << uint8(displayInfo.ClassID);
    data << uint32(displayInfo.Customizations.size());
    data.WriteString(displayInfo.Name);

    for (Character::ChrCustomizationChoice const& customization : displayInfo.Customizations)
        data << customization;

    for (InspectItemData const& item : displayInfo.Items)
        data << item;

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, InspectGuildData const& guildData)
{
    data << guildData.GuildGUID;
    data << int32(guildData.NumGuildMembers);
    data << int32(guildData.AchievementPoints);

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, PVPBracketData const& bracket)
{
    data << uint8(bracket.Bracket);
    data << int32(bracket.Unused3);
    data << int32(bracket.Rating);
    data << int32(bracket.Rank);
    data << int32(bracket.WeeklyPlayed);
    data << int32(bracket.WeeklyWon);
    data << int32(bracket.SeasonPlayed);
    data << int32(bracket.SeasonWon);
    data << int32(bracket.WeeklyBestRating);
    data << int32(bracket.SeasonBestRating);
    data << int32(bracket.PvpTierID);
    data << int32(bracket.Unused1);
    data << int32(bracket.UnknownRating);
    data << int32(bracket.Unused2);
    data << int32(bracket.Unused3);
    data << int32(bracket.Unused4);
    data << int32(bracket.Unused5);
    data << int32(bracket.Unused6);
    data.WriteBit(bracket.Disqualified);
    data.FlushBits();

    return data;
}

ByteBuffer& operator<<(ByteBuffer& data, TraitInspectInfo const& traits)
{
    data << int32(traits.Level);
    data << int32(traits.ChrSpecializationID);
    data << traits.Config;

    return data;
}

InspectItemData::InspectItemData(::Item const* item, uint8 index)
{
    CreatorGUID = item->GetCreator();

    Item.Initialize(item);
    Index = index;
    Usable = true; /// @todo

    for (uint8 i = 0; i < MAX_ENCHANTMENT_SLOT; ++i)
        if (uint32 enchId = item->GetEnchantmentId(EnchantmentSlot(i)))
            Enchants.emplace_back(enchId, i);

    uint8 i = 0;
    for (UF::SocketedGem const& gemData : item->m_itemData->Gems)
    {
        if (gemData.ItemID)
        {
            Gems.emplace_back();

            Item::ItemGemData& gem = Gems.back();
            gem.Slot = i;
            gem.Item.Initialize(&gemData);
        }
        ++i;
    }
}

WorldPacket const* InspectResult::Write()
{
    _worldPacket << DisplayInfo;

    _worldPacket << uint16(0);
    _worldPacket << uint16(0);

    _worldPacket << int32(ItemLevel);
    _worldPacket << uint8(LifetimeMaxRank);
    _worldPacket << uint16(TodayHK);
    _worldPacket << uint16(YesterdayHK);
    _worldPacket << uint32(LifetimeHK);
    _worldPacket << uint32(HonorLevel);

    // alistar: honestly inspecting works even with these random values that I got from 3.4.3 sniffs
    _worldPacket << uint64(2199023255552); // unk
    _worldPacket << uint32(1776384);       // unk1
    _worldPacket << uint32(101056512);     // unk2
    _worldPacket << uint32(33554432);      // unk3

    for (InspectTalentData const& talent : TalentRanks)
    {
        _worldPacket << uint32(talent.Id);
        _worldPacket << uint8(talent.Rank);
    }

    // unknown data below almost always 0 on 3.4.3
    for (int i = 0; i < 61; ++i)
        _worldPacket << uint64(0);

    _worldPacket << uint32(0);

    return &_worldPacket;
}

void QueryInspectAchievements::Read()
{
    _worldPacket >> Guid;
}
}

// ---- de Wrathion (3.4.3.54261)
namespace WorldPackets { namespace Inspect {

void InspectPvP::Read()
{
    _worldPacket >> Target;
}

void RequestHonorStats::Read()
{
    _worldPacket >> Target;
}

WorldPacket const* InspectHonorStatsResult::Write()
{
    _worldPacket << Target;
    
    _worldPacket << uint8(LifetimeMaxRank);

    _worldPacket << uint16(TodayHK);
    _worldPacket << uint16(TodayDK);

    _worldPacket << uint16(YesterdayHK);
    _worldPacket << uint16(YesterdayDK);

    _worldPacket << uint16(LastWeekHK);
    _worldPacket << uint16(LastWeekDK);

    _worldPacket << uint16(ThisWeekHK);
    _worldPacket << uint16(ThisWeekDK);

    _worldPacket << uint32(LifeTimeHK);
    _worldPacket << uint32(LifeTimeDK);

    _worldPacket << uint32(YesterdayHonor);
    _worldPacket << uint32(LastWeekHonor);
    _worldPacket << uint32(ThisWeekHonor);

    _worldPacket << uint32(Standing);

    _worldPacket << uint8(RankProgress);

    return &_worldPacket;
}

WorldPacket const* InspectPvPResult::Write()
{
    _worldPacket << Target;

    _worldPacket << int32(Bracket.size());

    _worldPacket << uint8(Unk);

    for (PVPBracketData const& bracket : Bracket)
        _worldPacket << bracket;

    // unused data below
    for (uint8 i = 0; i != 8; ++i)
        _worldPacket << int64(0);

    _worldPacket << int16(0);

    return &_worldPacket;
}
} }
