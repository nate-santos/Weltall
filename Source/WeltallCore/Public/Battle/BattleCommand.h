#pragma once

#include "Foundation/Types.h"

#include <cstddef>
#include <span>

namespace Weltall
{
	/** The bits of a command's flags word. */
	namespace BattleCommandFlags
	{
		/** The show plays the command's own animation number rather than the command's number. */
		inline constexpr uint16 OwnAnimation = 0x0001;
		inline constexpr uint16 SureHit = 0x0002;
		/** A landed hit may take the target's buffs away. */
		inline constexpr uint16 Dispels = 0x0004;
		/** The command is run by the gears' rules even for an actor on foot. */
		inline constexpr uint16 GearAction = 0x0010;
		/** The actor's attack stat is left out; only the weapons count. */
		inline constexpr uint16 WeaponsOnly = 0x0020;
		inline constexpr uint16 MissesGears = 0x0040;
		inline constexpr uint16 Ether = 0x0100;
		/** Misses gears and the targets whose flags shield them from such commands. */
		inline constexpr uint16 MissesShielded = 0x0200;
		inline constexpr uint16 Deathblow = 0x0400;
		/** A landed hit may inflict the actor's weapon's status. */
		inline constexpr uint16 WeaponStatus = 0x0800;
		inline constexpr uint16 AlwaysMisses = 0x1000;
		inline constexpr uint16 Uncounterable = 0x2000;
		/** A landed hit may inflict the command's own status, for 5 turns. */
		inline constexpr uint16 StatusOnHit = 0x4000;
		inline constexpr uint16 NeverMisses = 0x8000;
		/** Perfect Works' rebalance: the target's defense is left out and the power is 20 (its Pierce). */
		inline constexpr uint16 PerfectWorksPierce = 0x0008;
		/** Perfect Works' rebalance: a landed hit gives the actor itself the command's status. */
		inline constexpr uint16 PerfectWorksSelfStatus = 0x0080;
		/** Perfect Works' rebalance: a physical attack of formula 2 takes the actor's ether for its weapons (the bit of Uncounterable). */
		inline constexpr uint16 PerfectWorksEtherPowered = 0x2000;
	}

	/** A command of a character's, a gear's or the enemies' table: one action, as the battle's rules read it. */
	struct FBattleCommand
	{
		static constexpr std::size_t RecordSize = 0x28;

		/** Who can be chosen: the target selector's word. */
		uint16 Targeting = 0;
		/** What the show plays when the flags ask for the command's own animation. */
		uint16 Animation = 0;
		/**
		 * Four bytes no rule reads (the record's 04h), kept because an enemy's script can read and write them as
		 * another record's; they are not among the layout's members.
		 */
		uint32 Bytes04 = 0;
		/** Gears: the kind of damage, whose highest bit picks the target gear's resistance. */
		uint16 DamageKind = 0;
		uint16 Flags = 0;
		/** The weapon slots whose attack counts: bits 0x80, 0x40, 0x20 and 0x10 for slots 0 to 3. */
		uint8 WeaponSlots = 0;
		/** The damage's scale, a heal's factor, a status's turns. */
		uint8 Power = 0;
		/**
		 * Perfect Works' rebalance: the turns of the command's own status on a Deathblow or on the actor itself (the
		 * record's 12h). Only its rules read it, so it lies outside the layout of the original's.
		 */
		uint8 OwnStatusTurns = 0;
		uint8 EpCost = 0;
		/** Added to Elly's ether for her chance not to fail. */
		uint8 EtherBonus = 0;
		int8 HitBonus = 0;
		/** Which of the rules' handlers carries the command out. */
		uint8 Handler = 0;
		uint8 ApCost = 0;
		/** Drains and fixed damage: 0 takes the actor's status rate as the chance, 1 the command's. */
		uint8 ChanceSource = 0;
		uint8 AttackFormula = 0;
		uint8 DefenseFormula = 0;
		uint8 StatusChance = 0;
		uint8 StatusGroup = 0;
		uint16 StatusBits = 0;
		/** The kind of command the display and the enemies' reactions are told: basic attack, Deathblow, ether... */
		uint8 Category = 0;
		uint8 Info21 = 0;
		/** Wind 8, earth 4, fire 2, water 1, light 0x20, dark 0x10. */
		uint8 Element = 0;
		uint8 Info23 = 0;
		uint16 FuelCost = 0;
		/** Taken from the actor's agility when its next wait is rolled. */
		int8 Speed = 0;

		WELTALLCORE_API static FBattleCommand FromRecord(std::span<const uint8, RecordSize> Record);
	};

	/** An item as the battle uses it. */
	struct FBattleItem
	{
		static constexpr std::size_t RecordSize = 0x10;

		uint16 Targeting = 0;
		uint8 Power = 0;
		/** Play Dead: 0 inflicts it, 1 ends it; the specials that inflict a status: its turns. */
		uint8 Setting = 0;
		uint16 Effect = 0;
		uint16 Parameter = 0;
		uint16 Animation = 0;

		WELTALLCORE_API static FBattleItem FromRecord(std::span<const uint8, RecordSize> Record);
	};

	/**
	 * Calls Visit(offset, member) for each member of a command at its place in the game's record; the
	 * bytes left out are read by nothing in the rules.
	 */
	template <typename TCommand, typename FVisit>
	void VisitBattleCommandLayout(TCommand& Command, FVisit&& Visit)
	{
		Visit(0x00, Command.Targeting);
		Visit(0x02, Command.Animation);
		Visit(0x08, Command.DamageKind);
		Visit(0x0A, Command.Flags);
		Visit(0x10, Command.WeaponSlots);
		Visit(0x11, Command.Power);
		Visit(0x13, Command.EpCost);
		Visit(0x14, Command.EtherBonus);
		Visit(0x15, Command.HitBonus);
		Visit(0x16, Command.Handler);
		Visit(0x17, Command.ApCost);
		Visit(0x18, Command.ChanceSource);
		Visit(0x1A, Command.AttackFormula);
		Visit(0x1B, Command.DefenseFormula);
		Visit(0x1C, Command.StatusChance);
		Visit(0x1D, Command.StatusGroup);
		Visit(0x1E, Command.StatusBits);
		Visit(0x20, Command.Category);
		Visit(0x21, Command.Info21);
		Visit(0x22, Command.Element);
		Visit(0x23, Command.Info23);
		Visit(0x24, Command.FuelCost);
		Visit(0x27, Command.Speed);
	}

	template <typename TItem, typename FVisit>
	void VisitBattleItemLayout(TItem& Item, FVisit&& Visit)
	{
		Visit(0x04, Item.Targeting);
		Visit(0x08, Item.Power);
		Visit(0x09, Item.Setting);
		Visit(0x0A, Item.Effect);
		Visit(0x0C, Item.Parameter);
		Visit(0x0E, Item.Animation);
	}
}
