#include "Battle/BattleRules.h"
#include "Battle/BattleSteps.h"
#include "Battle/BattleTurn.h"

#include <span>

namespace Weltall
{
	using namespace BattleRules;

	namespace
	{
		constexpr uint16 PlayDead = 0x0002;
		constexpr uint8 NoDuration = 0xF;
		/** The message when an element change meets a permanent one. */
		constexpr uint8 CantAddElement = 0x39;
		/** Def DOWN in the statuses' word, and the battle messages' "Def DOWN". */
		constexpr uint16 DefDown = 0x0100;
		constexpr uint8 DefDownMessage = 6;
		/** The messages Perfect Works' rebalance gives its status 40h and its mental 800h (Can't use Ether): its own, and Block ETH effects'. */
		constexpr uint8 PerfectWorksDefDownMessage = 0x28;
		constexpr uint8 BlockEtherMessage = 13;

		/** Which duration counter a status's bits have: only these exact values have one. */
		uint8 DurationIndex(uint8 Group, uint16 Bits)
		{
			switch (Group)
			{
			case 0: return Bits == 0x1000 ? 1 : Bits == 0x2000 ? 0 : NoDuration;
			case 2: return Bits == 0x800 ? 3 : Bits == 0x1000 ? 2 : NoDuration;
			case 5: return Bits == 0x4000 ? 5 : Bits == 0x8000 ? 4 : NoDuration;
			case 7: return Bits == 0x4000 ? 10 : Bits == 0x1000 ? 11 : Bits == 0x8000 ? 9 : NoDuration;
			case 9:
				if (Bits == 0x100 || Bits == 0x200 || Bits == 0x400 || Bits == 0x800)
				{
					return 13;
				}
				if (Bits == 0x1000 || Bits == 0x2000 || Bits == 0x4000 || Bits == 0x8000)
				{
					return 12;
				}
				return NoDuration;
			default: return NoDuration;
			}
		}

		/**
		 * Perfect Works' rebalance's counters: Can't use Ether and Def UP have none, and an element change counts by
		 * any of its guard's or its attack's bits.
		 */
		uint8 PerfectWorksDurationIndex(uint8 Group, uint16 Bits)
		{
			switch (Group)
			{
			case 2: return Bits == 0x1000 ? 2 : NoDuration;
			case 5: return Bits == 0x8000 ? 4 : NoDuration;
			case 9: return (Bits & 0x0F00) != 0 ? 13 : (Bits & 0xF000) != 0 ? 12 : NoDuration;
			default: return DurationIndex(Group, Bits);
			}
		}

		/** The duration counters a status sets, one bit each: the original's one, and with the fix Atk UP's, ACC UP's and EV UP's. */
		uint16 DurationCounters(const FBattleContext& Context, uint8 Group, uint16 Bits)
		{
			const bool bEveryBuffExpires = Context.RuleOptions.bEveryBuffExpires;
			const uint8 Index = Context.bPerfectWorksRebalance ? PerfectWorksDurationIndex(Group, Bits) : DurationIndex(Group, Bits);
			uint16 Counters = Index == NoDuration ? 0 : static_cast<uint16>(1u << Index);
			if (Group == 5 && bEveryBuffExpires)
			{
				// Bart's ACC/EV UP sets both of its bits at once.
				constexpr std::array<uint16, 3> Buffs{0x2000, 0x1000, 0x800};
				for (std::size_t Buff = 0; Buff < Buffs.size(); ++Buff)
				{
					if ((Bits & Buffs[Buff]) != 0)
					{
						Counters |= static_cast<uint16>(1u << (6 + Buff));
					}
				}
			}
			return Counters;
		}

		struct FStatusMessage
		{
			uint8 Group;
			uint16 Bits;
			uint8 Message;
		};

		constexpr std::array<FStatusMessage, 33> StatusMessages{{
			{0, 0x2000, 1}, {0, 0x1000, 2}, {0, 0x800, 3}, {0, 0x400, 4}, {0, 0x200, 5}, {0, 0x001, 8},
			{2, 0x020, 7}, {2, 0x2000, 9}, {2, 0x1000, 10}, {2, 0x800, 11}, {2, 0x400, 12}, {2, 0x001, 13},
			{5, 0x8000, 14}, {5, 0x4000, 15}, {5, 0x2000, 16}, {5, 0x1000, 17}, {5, 0x800, 18}, {5, 0x1800, 19},
			{7, 0x8000, 21}, {7, 0x4000, 22}, {7, 0x1000, 24}, {7, 0x008, 25}, {7, 0x002, 25}, {7, 0x004, 26}, {7, 0x001, 26},
			{9, 0x8000, 27}, {9, 0x4000, 28}, {9, 0x2000, 29}, {9, 0x1000, 30}, {9, 0x400, 31}, {9, 0x800, 32}, {9, 0x100, 33}, {9, 0x200, 34},
		}};

		/** Counts a duration down by one and reports whether it just ran out. */
		bool CountDown(uint8& Turns)
		{
			Turns = static_cast<uint8>(Turns - 1);
			return Turns == 0;
		}

		void TickGearDurations(FBattleCombatant& Combatant, bool bElementsExpire)
		{
			FBattleGearPart& Gear = Combatant.GearPart;
			std::array<uint8, FBattleCombatant::DurationCount>& Turns = Combatant.Durations;
			if ((Gear.Status & 0x200) != 0 && CountDown(Turns[1])) Gear.Status &= 0xFDFF;
			if ((Gear.Status & 0x100) != 0 && CountDown(Turns[2])) Gear.Status &= 0xFEFF;
			if ((Gear.Status & 0x80) != 0 && CountDown(Turns[3])) Gear.Status &= 0xFF7F;
			if ((Gear.Status & 0x20) != 0 && CountDown(Turns[4]))
			{
				Gear.Status &= 0xFFDF;
				Combatant.Status &= 0xEFFF;
			}
			if ((Gear.Status & 0x10) != 0 && CountDown(Turns[5])) Gear.Status &= 0xFFEF;
			// The element changes' counts; the original tests and clears the statuses' word with them.
			uint16& Changed = bElementsExpire ? Gear.Elements : Gear.Status;
			if ((Changed & 0xF000) != 0 && CountDown(Turns[7])) Changed &= 0x0FFF;
			if ((Changed & 0x0F00) != 0 && CountDown(Turns[8])) Changed &= 0xF0FF;
			if ((Gear.Buffs & 0x1000) != 0 && CountDown(Turns[10])) Gear.Buffs &= 0xEFFF;
			if ((Gear.Buffs & 0x40) != 0 && CountDown(Turns[11])) Gear.Buffs &= 0xFFBF;
			if ((Gear.Buffs & 0x20) != 0 && CountDown(Turns[12])) Gear.Buffs &= 0xFFDF;
		}

		/** A temporary buff counts down only when the equipment does not give it for good. */
		bool Temporary(uint16 Word, uint16 Permanent, uint16 Bit)
		{
			return (Word & Bit) != 0 && (Permanent & Bit) == 0;
		}

		/** Citan's ether records and his gear's, which the target-all ether marks. */
		constexpr std::array<int32, 10> FootEtherCommands{22, 24, 25, 26, 27, 28, 29, 30, 31, 32};
		constexpr std::array<int32, 7> GearEtherCommands{21, 23, 24, 25, 26, 27, 28};
		/** Perfect Works' rebalance: Bart's records the target-all ether marks, the first four reset to 2000h and the others to 4000h. */
		constexpr std::array<int32, 4> BartEtherCommands{28, 29, 30, 31};
		constexpr std::array<int32, 2> BartOtherCommands{25, 26};
		constexpr uint8 Bart = 3;

		void SetCommandsTargeting(FBattleContext& Context, uint8 Slot, bool bGear, std::span<const int32> Commands, uint16 Targeting)
		{
			for (const int32 Command : Commands)
			{
				const int32 Index = bGear ? FBattleContext::GearCommandIndex(Slot, Command) : FBattleContext::PartyCommandIndex(Slot, Command);
				if (Index >= FBattleContext::CommandRecordCount)
				{
					Context.Fault = EBattleFault::CommandOutsideTable;
					return;
				}
				Context.Commands[static_cast<std::size_t>(Index)].Targeting = Targeting;
			}
		}

		/**
		 * Citan's ether records (and his gear's) as the target-all ether marks them: 1 for the whole party,
		 * 2000h back to one. The game works out the records from the slot as if every slot had the party's
		 * tables; an enemy in a gear in slot 8 to 10 would reach past them (a fault).
		 */
		void SetEtherTargeting(FBattleContext& Context, uint8 Slot, bool bGear, uint16 Targeting)
		{
			SetCommandsTargeting(Context, Slot, bGear, bGear ? std::span<const int32>(GearEtherCommands) : std::span<const int32>(FootEtherCommands), Targeting);
		}

		/**
		 * Perfect Works' rebalance marks the records on foot in a gear too, and Bart's own: Targeting, and for the
		 * mark's end (2000h) Bart's other records 4000h.
		 */
		void SetPerfectWorksEtherTargeting(FBattleContext& Context, uint8 Slot, uint16 Targeting)
		{
			if (Context.Combatants[Slot].Character != Bart)
			{
				SetCommandsTargeting(Context, Slot, false, FootEtherCommands, Targeting);
				return;
			}
			SetCommandsTargeting(Context, Slot, false, BartEtherCommands, Targeting);
			SetCommandsTargeting(Context, Slot, false, BartOtherCommands, Targeting == 1 ? Targeting : uint16{0x4000});
		}
	}

	namespace BattleSteps
	{
		bool CanPlayDead(const FBattleContext& Context)
		{
			int32 Out = 0;
			for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
			{
				Out += (Context.Combatants[Slot].Status & 0xC002) != 0 ? 1 : 0;
			}
			if (Context.PartyCount == 2)
			{
				return Out == 0;
			}
			return Context.PartyCount == 3 && Out != 2;
		}

		bool TryInflictStatus(FBattleContext& Context, uint8 Chance, uint8 Group, uint16 Bits)
		{
			FBattleAction& Action = Context.Action;
			if (InGear(Context, Action.TargetSlot))
			{
				return false;
			}
			if (static_cast<int32>(Chance) < Percent(Context))
			{
				return false;
			}
			FBattleCombatant& Target = BattleRules::Target(Context);
			bool bElement = Group == 9;
			switch (Group)
			{
			case 0:
				if ((Target.StatusImmunity & Bits & 0xFFFD) != 0)
				{
					return false;
				}
				if ((Bits & 0x1000) != 0)
				{
					Target.Buffs &= 0x7FFF;
				}
				break;
			case 2:
				if ((Bits & Target.MentalImmunity) != 0)
				{
					return false;
				}
				break;
			case 5:
				if (((Target.Status | Target.StatusImmunity) & 1) != 0)
				{
					return false;
				}
				if ((Bits & 0x8000) != 0)
				{
					Target.Status &= 0xEFFF;
				}
				break;
			case 7:
				if (((Target.Mental | Target.MentalImmunity) & 1) != 0)
				{
					return false;
				}
				if ((Bits & 0xA) != 0)
				{
					if ((Target.EtherBuffs & 5) != 0)
					{
						Target.EtherBuffs &= 0xFFFA;
						return true;
					}
				}
				else if ((Bits & 5) != 0)
				{
					if ((Target.EtherBuffs & 0xA) != 0)
					{
						Target.EtherBuffs &= 0xFFF5;
						return true;
					}
				}
				else
				{
					bElement = !Context.RuleOptions.bEtherBuffsSetDirectly;
				}
				break;
			default:
				break;
			}
			if (bElement)
			{
				if ((Bits & 0xF000) != 0)
				{
					if ((Target.PermanentElements & 0xF000) != 0)
					{
						Action.Message = CantAddElement;
						return false;
					}
					Target.Elements &= 0x0FFF;
				}
				if ((Bits & 0x0F00) != 0)
				{
					if ((Target.PermanentElements & 0x0F00) != 0)
					{
						Action.Message = CantAddElement;
						return false;
					}
					Target.Elements &= 0xF0FF;
				}
			}
			switch (Group)
			{
			case 0:
				Target.Status = static_cast<uint16>((Bits | Target.Status) & ~PlayDead);
				if ((Bits & PlayDead) != 0)
				{
					if (CanPlayDead(Context))
					{
						Target.Status |= Bits;
						Action.Message = 0x31;
						Target.DisabledCommands = 0xFFEF;
					}
					else
					{
						Action.Message = 0x32;
					}
				}
				break;
			case 2:
				Target.Mental |= Bits;
				if ((Bits & 0x800) != 0)
				{
					Target.DisabledCommands |= 0x20;
				}
				break;
			case 5:
			case 7:
			case 9:
				Target.StatusWord(0x7A + 2u * Group) |= Bits;
				break;
			default:
				break;
			}
			SetStatusMessage(Context, Group, Bits);
			return true;
		}

		void SetStatusDuration(FBattleContext& Context, uint8 Slot, uint8 Group, uint16 Bits, uint8 Turns)
		{
			SetStatusDurationBy(Context, Context.Combatants[Context.Action.ActorSlot], nullptr, Slot, Group, Bits, Turns);
		}

		void SetStatusDurationBy(FBattleContext& Context, const FBattleCombatant& Actor, const FBattleCombatant* Target, uint8 Slot, uint8 Group,
			uint16 Bits, uint8 Turns)
		{
			uint32 Value = Turns;
			if ((Actor.PermanentEtherBuffs & 0x2000) != 0)
			{
				Value = (Value & 0xFF) << 1;
			}
			const uint16 Counters = DurationCounters(Context, Group, Bits);
			if (Counters == 0)
			{
				return;
			}
			const bool bBuff = Group == 5 || Group == 7 || Group == 9;
			if (bBuff)
			{
				const FBattleCombatant& BuffTarget = Target != nullptr ? *Target : BattleRules::Target(Context);
				const uint16 EtherBuffs = BuffTarget.EtherBuffs | BuffTarget.PermanentEtherBuffs;
				if ((EtherBuffs & 5) != 0)
				{
					Value = (Value & 0xFF) << 1;
				}
				if ((EtherBuffs & 0xA) != 0)
				{
					Value = (Value & 0xFF) >> 1;
				}
			}
			FBattleCombatant& Holder = Context.Combatants[Slot];
			if ((Holder.EquipmentFlags & 0x40) != 0 && bBuff)
			{
				Value = (Value & 0xFF) << 1;
			}
			for (std::size_t Index = 0; Index < Holder.Durations.size(); ++Index)
			{
				if ((Counters & (1u << Index)) != 0)
				{
					Holder.Durations[Index] = static_cast<uint8>(Value);
				}
			}
		}

		void SetStatusMessage(FBattleContext& Context, uint8 Group, uint16 Bits)
		{
			// Perfect Works' rebalance's own: its status 40h, which lowers defense, and Can't use Ether shown as Block ETH effects.
			if (Context.bPerfectWorksRebalance && ((Group == 0 && Bits == 0x40) || (Group == 2 && Bits == 0x800)))
			{
				Context.Action.Message = Group == 0 ? PerfectWorksDefDownMessage : BlockEtherMessage;
				return;
			}
			// The table's message 6, which the original's status table leaves out.
			if (Context.RuleOptions.bDefDownOnFoot && Group == 0 && Bits == DefDown)
			{
				Context.Action.Message = DefDownMessage;
				return;
			}
			for (const FStatusMessage& Entry : StatusMessages)
			{
				if (Entry.Group == Group && Entry.Bits == Bits)
				{
					Context.Action.Message = Entry.Message;
					return;
				}
			}
		}

		void ApplyEtherTargetAll(FBattleContext& Context, uint8 Slot)
		{
			const FBattleCombatant& Combatant = Context.Combatants[Slot];
			const bool bGear = Combatant.IsInGear();
			if (bGear ? (Combatant.GearPart.Buffs & 0x2000) == 0 : (Combatant.EtherBuffs & 0x400) == 0)
			{
				return;
			}
			if (Context.bPerfectWorksRebalance)
			{
				SetPerfectWorksEtherTargeting(Context, Slot, 1);
				return;
			}
			SetEtherTargeting(Context, Slot, bGear, 1);
		}

		void ResetEtherTargetAll(FBattleContext& Context, uint8 Slot, bool bOnlyForEther)
		{
			if (bOnlyForEther && (BattleRules::Command(Context).Flags & BattleCommandFlags::Ether) == 0)
			{
				return;
			}
			FBattleCombatant& Combatant = Context.Combatants[Slot];
			const bool bGear = Combatant.IsInGear();
			if (bGear ? (Combatant.GearPart.Buffs & 0x2000) == 0 : (Combatant.EtherBuffs & 0x400) == 0)
			{
				return;
			}
			if (Context.bPerfectWorksRebalance)
			{
				SetPerfectWorksEtherTargeting(Context, Slot, 0x2000);
				Combatant.EtherBuffs &= 0xFBFF;
				return;
			}
			SetEtherTargeting(Context, Slot, bGear, 0x2000);
			if (!bGear)
			{
				Combatant.EtherBuffs &= 0xFBFF;
			}
			else
			{
				Combatant.GearPart.Buffs &= 0xDFFF;
			}
		}
	}

	uint16 TickStatusDurations(FBattleContext& Context, uint8 Slot)
	{
		FBattleCombatant& Combatant = Context.Combatants[Slot];
		if (Combatant.IsInGear())
		{
			TickGearDurations(Combatant, Context.RuleOptions.bGearElementsExpire);
			return 0;
		}
		std::array<uint8, FBattleCombatant::DurationCount>& Turns = Combatant.Durations;
		uint16 Ended = 0;
		if ((Combatant.Status & 0x1000) != 0 && CountDown(Turns[1]))
		{
			Ended = 0x4000;
			Combatant.Status &= 0xEFFF;
		}
		if ((Combatant.Mental & 0x1000) != 0 && CountDown(Turns[2]))
		{
			Ended |= 0x2000;
			Combatant.Mental &= 0xEFFF;
		}
		if ((Combatant.Mental & 0x800) != 0 && CountDown(Turns[3]))
		{
			Ended |= 0x1000;
			Combatant.Mental &= 0xF7FF;
			Combatant.DisabledCommands &= 0xFFDF;
		}
		struct FBuff
		{
			uint16 Bit;
			std::size_t Turns;
			uint16 Ends;
		};
		constexpr std::array<FBuff, 5> PhysicalBuffs{{{0x8000, 4, 0x800}, {0x4000, 5, 0x400}, {0x2000, 6, 0x200}, {0x1000, 7, 0x100}, {0x800, 8, 0x80}}};
		for (const FBuff& Buff : PhysicalBuffs)
		{
			if (Temporary(Combatant.Buffs, Combatant.PermanentBuffs, Buff.Bit) && CountDown(Turns[Buff.Turns]))
			{
				Ended |= Buff.Ends;
				Combatant.Buffs &= static_cast<uint16>(~Buff.Bit);
			}
		}
		constexpr std::array<FBuff, 3> EtherBuffs{{{0x8000, 9, 0x40}, {0x4000, 10, 0x20}, {0x1000, 11, 0x10}}};
		for (const FBuff& Buff : EtherBuffs)
		{
			if (Temporary(Combatant.EtherBuffs, Combatant.PermanentEtherBuffs, Buff.Bit) && CountDown(Turns[Buff.Turns]))
			{
				Ended |= Buff.Ends;
				Combatant.EtherBuffs &= static_cast<uint16>(~Buff.Bit);
			}
		}
		if ((Combatant.Elements & 0xF000) != 0 && (Combatant.PermanentElements & 0xF000) == 0 && CountDown(Turns[12]))
		{
			Ended |= 8;
			Combatant.Elements &= 0x0FFF;
		}
		if ((Combatant.Elements & 0x0F00) != 0 && (Combatant.PermanentElements & 0x0F00) == 0 && CountDown(Turns[13]))
		{
			Ended |= 4;
			Combatant.Elements &= 0xF0FF;
		}
		return Ended;
	}
}
