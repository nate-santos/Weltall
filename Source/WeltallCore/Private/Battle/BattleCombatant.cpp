#include "Battle/BattleCombatant.h"

#include "Battle/BattleRecordBytes.h"

#include <algorithm>

namespace Weltall
{
	FBattleCombatant FBattleCombatant::FromRecord(std::span<const uint8, RecordSize> Record)
	{
		FBattleCombatant Combatant;
		VisitBattleCombatantLayout(Combatant, BattleRecordBytes::FReader{Record});
		const std::span<const uint8> ModelBytes = Record.subspan(CharacterRecordSize + FBattleGearPart::ModelBytesOffset, Combatant.GearPart.ModelBytes.size());
		std::copy(ModelBytes.begin(), ModelBytes.end(), Combatant.GearPart.ModelBytes.begin());
		Combatant.EtherAmmunition = {Record[EtherAmmunitionOffset], Record[EtherAmmunitionOffset + 1]};
		std::copy_n(Record.begin() + ExtraBonusesOffset, Combatant.ExtraBonuses.size(), Combatant.ExtraBonuses.begin());
		return Combatant;
	}

	uint16& FBattleCombatant::StatusWord(std::size_t Offset)
	{
		switch (Offset)
		{
		case 0x7A: return DisabledCommands;
		case 0x7C: return Status;
		case 0x7E: return StatusImmunity;
		case 0x80: return Mental;
		case 0x82: return MentalImmunity;
		case 0x84: return Buffs;
		case 0x86: return PermanentBuffs;
		case 0x88: return EtherBuffs;
		case 0x8A: return PermanentEtherBuffs;
		case 0x8C: return Elements;
		default: return PermanentElements;
		}
	}
}
