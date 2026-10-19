#include "Support/BattleScenario.h"

#include "Support/BattleDigest.h"

#include <array>
#include <utility>

namespace Weltall::Testing
{
	namespace
	{
		constexpr std::array<uint8, 13> Results{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 0xFF};
		constexpr std::array<uint8, 5> TotalKinds{0, 2, 5, 0xFF, 1};
		constexpr std::array<uint8, 7> Groups{0, 2, 5, 7, 9, 0x6E, 1};
		constexpr std::array<uint8, 14> GearGroups{0, 0, 1, 1, 3, 3, 10, 11, 12, 13, 14, 15, 16, 2};
		constexpr std::array<uint8, 8> Elements{0, 0, 1, 2, 4, 8, 0x10, 0x20};
		constexpr std::array<uint8, 5> Presence{0, 0, 0, 0x80, 1};
		constexpr std::array<uint8, 5> Readiness{0, 0, 0, 1, 0xFF};

		/** A visitor that gives each member it is shown the generator's next value at its width. */
		struct FFillVisitor
		{
			FXorShift32& G;

			template <typename T>
			void operator()(std::size_t, T& Member) const
			{
				Member = static_cast<T>(G.Next());
			}
		};

		/** 0, or one to three random bits of a word. */
		uint16 Sparse16(FXorShift32& G)
		{
			const uint32 Kind = G.Next() % 4;
			if (Kind == 0)
			{
				return 0;
			}
			uint32 Value = 1u << (G.Next() % 16);
			if (Kind >= 2)
			{
				Value |= 1u << (G.Next() % 16);
			}
			if (Kind == 3)
			{
				Value |= 1u << (G.Next() % 16);
			}
			return static_cast<uint16>(Value);
		}

		void DrawCombatant(FXorShift32& G, FBattleCombatant& C, int32 Slot)
		{
			VisitBattleCombatantLayout(C, FFillVisitor{G});
			const bool bParty = Slot < 3;
			C.Character = static_cast<uint8>(bParty ? G.Next() % 11 : (G.Next() % 8 == 0 ? 4 : 0));
			C.MaxHp = static_cast<uint16>(G.Next() % (bParty ? 1000 : 10000));
			C.Hp = static_cast<uint16>(G.Next() % (C.MaxHp + 1u));
			C.MaxEp = static_cast<uint16>(G.Next() % 100);
			C.Ep = static_cast<uint16>(G.Next() % (C.MaxEp + 1u));
			C.GearPart.MaxHp = G.Next() % 100000;
			C.GearPart.Hp = G.Next() % (C.GearPart.MaxHp + 1);
			C.GearPart.MaxFuel = static_cast<uint16>(G.Next() % 10000);
			C.GearPart.Fuel = static_cast<uint16>(G.Next() % (C.GearPart.MaxFuel + 1u));
			for (uint16* Word : {&C.EquipmentFlags, &C.Traits, &C.Absorption, &C.Weakness, &C.Status, &C.StatusImmunity, &C.Mental,
					 &C.MentalImmunity, &C.Buffs, &C.PermanentBuffs, &C.EtherBuffs, &C.PermanentEtherBuffs, &C.Elements, &C.PermanentElements,
					 &C.GearPart.Status, &C.GearPart.StatusImmunity, &C.GearPart.Buffs, &C.GearPart.PermanentBuffs, &C.GearPart.Elements,
					 &C.GearPart.PermanentElements})
			{
				*Word = Sparse16(G);
			}
			C.Status &= 0x3FFD;
			if (G.Next() % 8 == 0)
			{
				C.Status |= 0x8000;
			}
			if (bParty && G.Next() % 6 == 0)
			{
				C.Status |= 2;
			}
			C.BattleFlags = G.Next() % 4 == 0 ? 0x80 : 0;
			if (G.Next() % 5 == 0)
			{
				C.BattleFlags |= 1;
			}
			if (bParty && C.IsInGear())
			{
				C.EtherBuffs &= static_cast<uint16>(~0x1000);
			}
			for (FBattleWeaponSlot& Weapon : C.Weapons)
			{
				Weapon.StatusBits &= 0xFFFD;
			}
			for (FBattleGearWeaponSlot& Weapon : C.GearPart.Weapons)
			{
				Weapon.StatusKind = GearGroups[G.Next() % GearGroups.size()];
				Weapon.StatusBits = static_cast<uint16>(1u << (G.Next() % 16));
				if (G.Next() % 2 == 0)
				{
					Weapon.StatusChance = 100;
				}
				if (Weapon.StatusKind == 1)
				{
					Weapon.StatusBits &= 0xDFFF;
				}
			}
			if (Slot >= 8)
			{
				C.GearPart.Buffs &= 0xDFFF;
			}
			C.PistolAmmunition %= 100;
			C.BigGunAmmunition %= 100;
			C.GearPart.AmmunitionIndex0 %= 0x60;
			C.GearPart.AmmunitionIndex2 %= 0x60;
			if (bParty)
			{
				C.AttackLevel = static_cast<uint8>(G.Next() % 6);
				C.MaxAttackLevel = static_cast<uint8>(G.Next() % 4);
				C.Gear = static_cast<uint8>(G.Next() % 20);
				C.Durations[6] = static_cast<uint8>(G.Next() % 4);
			}
			if (G.Next() % 4 == 0)
			{
				C.GearPart.Hp = C.GearPart.MaxHp;
			}
		}

		void DrawCommand(FXorShift32& G, FBattleCommand& Command, int32 Index)
		{
			const bool bOnFoot = Index < FBattleContext::FirstGearCommand;
			const bool bGear = !bOnFoot && Index < FBattleContext::FirstEnemyCommand;
			VisitBattleCommandLayout(Command, FFillVisitor{G});
			Command.WeaponSlots &= bOnFoot ? 0xF0 : 0xE0;
			const uint32 Handler = G.Next() % (bGear ? 16 : 12);
			Command.Handler = static_cast<uint8>(Handler < 5 ? 0 : Handler - (bGear ? 5 : 4));
			Command.ChanceSource = static_cast<uint8>(G.Next() % 2);
			Command.AttackFormula = static_cast<uint8>(G.Next() % (Command.Handler == 0 ? 3 : 9));
			Command.DefenseFormula = static_cast<uint8>(G.Next() % 3);
			uint8 Group = static_cast<uint8>(G.Next());
			if (G.Next() % 4 != 0)
			{
				const bool bGearGroup = bGear || (!bOnFoot && G.Next() % 2 == 0);
				Group = bGearGroup ? GearGroups[G.Next() % GearGroups.size()] : Groups[G.Next() % Groups.size()];
			}
			Command.StatusGroup = Group;
			uint16 Bits = 0;
			if (!bOnFoot && G.Next() % 2 == 0)
			{
				Bits = static_cast<uint16>(1u << (G.Next() % 16));
			}
			else
			{
				Bits = Sparse16(G);
			}
			Command.StatusBits = Bits & 0xFFFD;
			if (!bOnFoot && G.Next() % 2 == 0)
			{
				Command.StatusChance = 100;
			}
			if (!bOnFoot && Group == 1)
			{
				Command.StatusBits &= 0xDFFF;
			}
			uint16 Flags = Sparse16(G);
			Flags |= Sparse16(G);
			Flags &= 0xFFEF;
			const int32 Number = bOnFoot ? Index % FBattleContext::PartyTableSize : Index - FBattleContext::FirstEnemyCommand;
			if (!bGear && Number >= 15 && G.Next() % 6 == 0)
			{
				Flags |= BattleCommandFlags::GearAction;
			}
			Command.Flags = Flags;
			Command.Element = Elements[G.Next() % Elements.size()];
		}

		void DrawItem(FXorShift32& G, FBattleItem& Item)
		{
			VisitBattleItemLayout(Item, FFillVisitor{G});
			if (G.Next() % 2 != 0)
			{
				Item.Setting = 0;
			}
			uint16 Effect = 1;
			if (G.Next() % 5 != 0)
			{
				Effect = Sparse16(G);
				Effect |= Sparse16(G);
			}
			Item.Effect = Effect;
			if (Effect == 1)
			{
				Item.Power = static_cast<uint8>(10 + G.Next() % 6);
			}
			else if ((Effect & 8) != 0)
			{
				Item.Power = static_cast<uint8>(G.Next() % 6);
			}
			else
			{
				Item.Power = static_cast<uint8>(G.Next());
			}
			uint16 Parameter = Sparse16(G);
			if (Effect == 1)
			{
				Parameter &= 0xFFFD;
			}
			Item.Parameter = Parameter;
		}

		template <std::size_t Count>
		void DigestWords(FOracleDigest& Digest, const std::array<uint16, Count>& Words)
		{
			for (const uint16 Word : Words)
			{
				Digest.I16(Word);
			}
		}
	}

	uint32 DrawBattleState(FXorShift32& G, FBattleContext& Context, FGameState& State)
	{
		Context = FBattleContext{};
		Context.GameState = &State;
		Context.RuleOptions = FBattleRuleOptions::Original();
		for (int32 Slot = 0; Slot < FBattleContext::SlotCount; ++Slot)
		{
			DrawCombatant(G, Context.Combatants[static_cast<std::size_t>(Slot)], Slot);
		}
		for (int32 Index = 0; Index < FBattleContext::CommandRecordCount; ++Index)
		{
			if (FBattleContext::IsLoadedCommand(Index))
			{
				DrawCommand(G, Context.Commands[static_cast<std::size_t>(Index)], Index);
			}
		}
		for (FBattleItem& Item : Context.Items)
		{
			DrawItem(G, Item);
		}
		for (std::size_t Slot = 0; Slot < FBattleContext::SlotCount; ++Slot)
		{
			FBattleSlot& Record = Context.Slots[Slot];
			Record.Group = static_cast<uint8>(G.Next() % 8);
			Record.Place = static_cast<uint8>(G.Next() % 4);
			Record.Occupant = static_cast<uint8>(G.Next());
			const uint8 Shown = Presence[G.Next() % Presence.size()];
			Record.Presence = Slot >= 3 ? Shown : 0;
			const bool bExtraGear = G.Next() % 8 == 0;
			Record.InGear = Context.Combatants[Slot].IsInGear() || bExtraGear ? 1 : 0;
			Record.Byte5 = static_cast<uint8>(G.Next());
			Record.Position.X = static_cast<uint16>(G.Next());
			Record.Position.Z = static_cast<uint16>(G.Next());
		}
		for (std::size_t Slot = 0; Slot < FBattleContext::SlotCount; ++Slot)
		{
			Context.InBattle[Slot] = G.Next() % (Slot < 3 ? 8 : 4) != 0;
		}
		for (FBattleApRecord& Ap : Context.ApRecords)
		{
			Ap.StoredAp = static_cast<uint8>(G.Next() % 29);
			Ap.InGear = static_cast<uint8>(G.Next() % 3);
			Ap.ApPerTurn = static_cast<uint8>(G.Next() % 8);
			Ap.Level = static_cast<uint8>(G.Next());
			Ap.EtherLevel = static_cast<uint8>(G.Next());
		}
		for (FBattleOccupancy& Entry : Context.Occupancy)
		{
			Entry.Count = static_cast<uint8>(G.Next() % 5);
			Entry.Places = static_cast<uint8>(G.Next() & 0xF);
		}
		for (uint16& Commands : Context.StartingDisabledCommands)
		{
			Commands = static_cast<uint16>(G.Next());
		}
		for (std::size_t Enemy = 0; Enemy < FBattleContext::EnemySlotCount; ++Enemy)
		{
			Context.HasHitScript[Enemy] = G.Next() % 2 != 0;
			Context.HasReactionScript[Enemy] = G.Next() % 2 != 0;
			Context.Escaped[Enemy] = G.Next() % 2 != 0;
		}
		for (std::size_t Slot = 0; Slot < FBattleContext::PartySlotCount; ++Slot)
		{
			Context.PartyCharacters[Slot] = G.Next() % 6 == 0 ? FBattleContext::NoCharacter : Context.Combatants[Slot].Character;
		}
		Context.PartyCount = static_cast<uint8>(G.Next() % 4);
		for (uint16& Wait : Context.Waits)
		{
			Wait = static_cast<uint16>(G.Next() % 300);
		}
		for (uint16& Wait : Context.FullWaits)
		{
			Wait = static_cast<uint16>(G.Next() % 300);
		}
		for (uint8& Ready : Context.Ready)
		{
			Ready = Readiness[G.Next() % Readiness.size()];
		}
		for (uint16& Toggle : Context.SlowToggles)
		{
			Toggle = static_cast<uint16>(G.Next() % 2);
		}
		for (std::size_t Slot = 0; Slot < FBattleContext::SlotCount; ++Slot)
		{
			Context.TurnOrder[Slot] = static_cast<uint8>(Slot);
		}
		for (std::size_t Index = 10; Index > 0; --Index)
		{
			const std::size_t Other = G.Next() % (Index + 1);
			std::swap(Context.TurnOrder[Index], Context.TurnOrder[Other]);
		}
		Context.TurnOrderNext = static_cast<uint8>(G.Next() % 11);
		const bool bForce = G.Next() % 4 == 0;
		Context.ForcedActor = bForce ? static_cast<uint8>(G.Next() % 12) : 0;
		Context.bWaitsRunning = G.Next() % 8 != 0;
		Context.TurnActor = static_cast<uint8>(G.Next() % 11);

		FBattleAction& Action = Context.Action;
		for (uint32& Amount : Action.Amounts)
		{
			Amount = G.Next() % 5000;
		}
		for (uint8& Result : Action.Results)
		{
			Result = Results[G.Next() % Results.size()];
		}
		Action.Targets = static_cast<uint16>(G.Next() & 0x7FF);
		Action.LivingAtStart = static_cast<uint16>(G.Next() & 0x7FF);
		Action.Animation = static_cast<uint16>(G.Next());
		Action.Revived = static_cast<uint16>(G.Next());
		for (uint8& Byte : Action.Info)
		{
			Byte = static_cast<uint8>(G.Next());
		}
		Action.InfoCommand = static_cast<uint8>(G.Next());
		Action.Actor = static_cast<uint8>(G.Next() % 11);
		Action.Command = static_cast<uint8>(G.Next() % 38);
		Action.Message = static_cast<uint8>(G.Next());
		Action.ActorSlot = static_cast<uint8>(G.Next() % 11);
		Action.TargetSlot = static_cast<uint8>(G.Next() % 11);
		for (uint8* Pointer : {&Action.ActorCombatant, &Action.ActorGear, &Action.TargetCombatant, &Action.TargetGear, &Action.TargetFields,
				 &Action.ActorFields})
		{
			*Pointer = static_cast<uint8>(G.Next() % 11);
		}
		int32 Index = static_cast<int32>(G.Next() % FBattleContext::CommandRecordCount);
		while (!FBattleContext::IsLoadedCommand(Index))
		{
			Index = (Index + 1) % FBattleContext::CommandRecordCount;
		}
		Action.CommandIndex = static_cast<uint16>(Index);
		Action.bEtherFailed = G.Next() % 2 != 0;
		Action.bNoAmmunition = G.Next() % 2 != 0;
		Action.KnockedOut = static_cast<uint16>(G.Next() & 0x7FF);

		for (FBattlePerformance& Performance : Context.Performances)
		{
			for (uint16& Amount : Performance.Amounts)
			{
				Amount = static_cast<uint16>(G.Next());
			}
			Performance.Targets = static_cast<uint16>(G.Next());
			for (uint8& Result : Performance.Results)
			{
				Result = Results[G.Next() % Results.size()];
			}
			Performance.Actor = static_cast<uint8>(G.Next() % 11);
			for (uint16& Total : Performance.Totals)
			{
				Total = static_cast<uint16>(G.Next());
			}
			Performance.Parameter = static_cast<uint16>(G.Next());
			for (uint8& Kind : Performance.TotalKinds)
			{
				Kind = TotalKinds[G.Next() % TotalKinds.size()];
			}
			Performance.Code = static_cast<uint8>(G.Next());
		}
		Context.PerformanceIndex = static_cast<uint8>(G.Next() % 32);
		for (uint16& Total : Context.TurnTotals)
		{
			Total = static_cast<uint16>(G.Next() % 2000);
		}
		for (uint8& Kind : Context.TurnTotalKinds)
		{
			Kind = TotalKinds[G.Next() % TotalKinds.size()];
		}
		Context.bItemHealsPilot = G.Next() % 2 != 0;
		FBattleGearTurn& Turn = Context.GearTurn;
		for (uint16& Cost : Turn.Costs)
		{
			Cost = static_cast<uint16>(G.Next());
		}
		Turn.Charge = static_cast<uint16>(G.Next());
		Turn.ShownAttack = static_cast<uint16>(G.Next());
		Turn.Agility = static_cast<uint8>(G.Next());
		Turn.ArmorBreak = static_cast<uint8>(G.Next());
		Turn.InfinityChance = static_cast<uint8>(G.Next());
		Turn.Lamps = static_cast<uint16>(G.Next());
		Turn.bBooster = G.Next() % 2 != 0;
		Turn.Level = static_cast<uint8>(G.Next() % 5);
		Context.Living = static_cast<uint16>(G.Next() & 0x7FF);
		Context.ReactingWhileKnockedOut = static_cast<uint16>(G.Next() & 0x7F8);
		for (uint8& Byte : State.Block())
		{
			Byte = static_cast<uint8>(G.Next());
		}
		return G.Next();
	}

	void DigestBattleState(FOracleDigest& Digest, const FBattleContext& Context, const FGameState& State, const FPsxRandom& Random)
	{
		for (const FBattleCombatant& Combatant : Context.Combatants)
		{
			DigestCombatant(Digest, Combatant);
		}
		DigestCommands(Digest, Context);
		for (const FBattleItem& Item : Context.Items)
		{
			DigestItem(Digest, Item);
		}
		for (const FBattleSlot& Slot : Context.Slots)
		{
			Digest.U8(Slot.Group, Slot.Place, Slot.Occupant, Slot.Presence, Slot.InGear, Slot.Byte5);
			Digest.I16(Slot.Position.X, Slot.Position.Z);
		}
		for (const bool bIn : Context.InBattle)
		{
			Digest.U8(bIn ? 1 : 0);
		}
		for (const FBattleApRecord& Ap : Context.ApRecords)
		{
			Digest.U8(Ap.StoredAp, Ap.InGear, Ap.ApPerTurn, Ap.Level, Ap.EtherLevel);
		}
		for (const FBattleOccupancy& Entry : Context.Occupancy)
		{
			Digest.U8(Entry.Count, Entry.Places);
		}
		DigestWords(Digest, Context.StartingDisabledCommands);
		for (std::size_t Enemy = 0; Enemy < FBattleContext::EnemySlotCount; ++Enemy)
		{
			Digest.U8(Context.HasHitScript[Enemy] ? 1 : 0, Context.HasReactionScript[Enemy] ? 1 : 0, Context.Escaped[Enemy] ? 1 : 0);
		}
		Digest.Raw(Context.PartyCharacters);
		Digest.U8(Context.PartyCount);
		DigestWords(Digest, Context.Waits);
		DigestWords(Digest, Context.FullWaits);
		Digest.Raw(Context.Ready);
		DigestWords(Digest, Context.SlowToggles);
		Digest.Raw(Context.TurnOrder);
		Digest.U8(Context.TurnOrderNext, Context.ForcedActor);
		Digest.I16(Context.ScriptedTurns);
		Digest.U8(Context.bWaitsRunning ? 1 : 0, Context.TurnActor);

		const FBattleAction& Action = Context.Action;
		for (const uint32 Amount : Action.Amounts)
		{
			Digest.I32(Amount);
		}
		Digest.Raw(Action.Results);
		Digest.I16(Action.Targets, Action.LivingAtStart, Action.Animation, Action.Revived);
		Digest.Raw(Action.Info);
		Digest.U8(Action.InfoCommand, Action.Actor, Action.Command, Action.Message);
		Digest.U8(Action.ActorSlot, Action.TargetSlot);
		Digest.U8(RecordSlot(Action.ActorCombatant), RecordSlot(Action.ActorGear), RecordSlot(Action.TargetCombatant),
			RecordSlot(Action.TargetGear), RecordSlot(Action.TargetFields), RecordSlot(Action.ActorFields));
		Digest.I16(Action.CommandIndex);
		Digest.U8(Action.bEtherFailed ? 1 : 0, Action.bNoAmmunition ? 1 : 0);
		Digest.I16(Action.KnockedOut);

		for (const FBattlePerformance& Performance : Context.Performances)
		{
			DigestWords(Digest, Performance.Amounts);
			Digest.I16(Performance.Targets);
			Digest.Raw(Performance.Results);
			Digest.U8(Performance.Actor);
			DigestWords(Digest, Performance.Totals);
			Digest.I16(Performance.Parameter);
			Digest.Raw(Performance.TotalKinds);
			Digest.U8(Performance.Code);
		}
		Digest.U8(Context.PerformanceIndex);
		DigestWords(Digest, Context.TurnTotals);
		Digest.Raw(Context.TurnTotalKinds);
		Digest.Raw(Context.ChangedSlots);
		Digest.Raw(Context.PanelRefresh);
		Digest.U8(Context.bItemHealsPilot ? 1 : 0);
		const FBattleGearTurn& Turn = Context.GearTurn;
		DigestWords(Digest, Turn.Costs);
		Digest.I16(Turn.Charge, Turn.ShownAttack);
		Digest.U8(Turn.Agility, Turn.ArmorBreak);
		Digest.I16(Turn.InfinityChance, Turn.Lamps);
		Digest.U8(Turn.bBooster ? 1 : 0, Turn.Level);
		Digest.I16(Context.Living, Context.ReactingWhileKnockedOut);
		Digest.U8(Context.End);
		Digest.Raw(State.Block());
		Digest.I32(Random.Seed());
	}

	void DrawPerfectWorksData(FXorShift32& G, FBattleContext& Context, bool bWeakFuel)
	{
		for (FBattleCombatant& Combatant : Context.Combatants)
		{
			for (uint8& Loaded : Combatant.EtherAmmunition)
			{
				Loaded = static_cast<uint8>(FBattleTables::FirstAmmunition + G.Next() % FBattleTables::AmmunitionCount);
			}
		}
		for (uint8& Attack : Context.AmmunitionAttacks)
		{
			Attack = static_cast<uint8>(G.Next());
		}
		for (int32 Slot = 0; Slot < (bWeakFuel ? FBattleContext::PartySlotCount : 0); ++Slot)
		{
			Context.Commands[FBattleContext::GearCommandIndex(Slot, 0)].FuelCost = 10;
		}
	}
}
