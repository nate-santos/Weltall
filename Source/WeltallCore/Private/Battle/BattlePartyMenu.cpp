#include "Battle/BattleGameState.h"
#include "Battle/BattlePartySteps.h"
#include "Battle/BattleSteps.h"
#include "Battle/BattleTurn.h"

namespace Weltall::BattleParty
{
	namespace
	{
		constexpr uint8 NoTarget = 0xFF;
		constexpr int8 Always = -1;
		constexpr uint32 GiantScale = 50;
		constexpr uint32 MaxGiantHp = 99999;
		constexpr uint16 BoosterBit = 0x8000;

		enum class EMove : uint8
		{
			/** No move (no handler for the direction). */
			None,
			/** Always to the target. */
			Direct,
			/** To the target when its command is enabled, else a refusal. */
			Plain,
			/** As Plain, but a second press of the same direction jumps past the disabled command to another. */
			Skipping,
		};

		struct FMove
		{
			EMove Kind = EMove::None;
			uint8 Target = 0;
			int8 Command = Always;
			uint8 Skip = 0;
			int8 SkipCommand = Always;
		};

		struct FStateMoves
		{
			uint8 State = 0;
			/** Right, Down, Left, Up. */
			std::array<FMove, 4> Moves{};
		};

		constexpr FMove Direct(uint8 Target) { return {EMove::Direct, Target}; }
		constexpr FMove Plain(int32 Command, uint8 Target) { return {EMove::Plain, Target, static_cast<int8>(Command)}; }
		constexpr FMove Skipping(int32 Command, uint8 Target, int32 SkipCommand, uint8 Skip)
		{
			return {EMove::Skipping, Target, static_cast<int8>(Command), Skip, static_cast<int8>(SkipCommand)};
		}

		namespace C = ECommand;
		namespace S = EState;

		/** The two rings of commands, as each state's handler moves (asymmetries included). */
		constexpr std::array<FStateMoves, 16> Rings{{
			{S::Attack, {Plain(C::Combo, S::Combo), Plain(C::Item, S::Item), Direct(S::Guard), Skipping(C::Ether, S::Ether, C::Board, S::Board)}},
			{S::Item, {Skipping(C::Attack, S::Attack, C::Combo, S::Combo), Plain(C::Item, S::OuterItem), Direct(S::Guard),
				Skipping(C::Ether, S::Ether, C::Board, S::Board)}},
			{S::Guard, {Skipping(C::Attack, S::Attack, C::Combo, S::Combo), Plain(C::Item, S::Item), Plain(C::Escape, S::Escape),
				Skipping(C::Ether, S::Ether, C::Board, S::Board)}},
			{S::Ether, {Skipping(C::Attack, S::Attack, C::Combo, S::Combo), Plain(C::Item, S::Item), Direct(S::Guard), Plain(C::Board, S::Board)}},
			{S::Combo, {Plain(C::Attack, S::Attack), Plain(C::Item, S::OuterItem), Skipping(C::Escape, S::Escape, Always, S::Guard),
				Skipping(C::Board, S::Board, C::Ether, S::Ether)}},
			{S::OuterItem, {Skipping(C::Combo, S::Combo, C::Attack, S::Attack), Plain(C::Item, S::Item), Skipping(C::Escape, S::Escape, Always, S::Guard),
				Skipping(C::Board, S::Board, C::Ether, S::Ether)}},
			{S::Escape, {Skipping(C::Combo, S::Combo, C::Attack, S::Attack), Plain(C::Item, S::OuterItem), Direct(S::Guard),
				Skipping(C::Board, S::Board, C::Ether, S::Ether)}},
			{S::Board, {Skipping(C::Combo, S::Combo, C::Attack, S::Attack), Plain(C::Item, S::OuterItem), Skipping(C::Escape, S::Escape, Always, S::Guard),
				Plain(C::Ether, S::Ether)}},
			{S::GearAttack, {Plain(C::Booster, S::Booster), Plain(C::GearItem, S::GearItem), Direct(S::Charge),
				Skipping(C::GearEther, S::GearEther, C::Special, S::Special)}},
			{S::GearItem, {Plain(C::Attack, S::GearAttack), Plain(C::GearItem, S::GearOuterItem), Direct(S::Charge),
				Skipping(C::GearEther, S::GearEther, C::Special, S::Special)}},
			{S::Charge, {Plain(C::Attack, S::GearAttack), Plain(C::GearItem, S::GearItem), Plain(C::Escape, S::GearEscape),
				Skipping(C::GearEther, S::GearEther, C::Special, S::Special)}},
			{S::GearEther, {Plain(C::Attack, S::GearAttack), Plain(C::GearItem, S::GearItem), Direct(S::Charge), Plain(C::Special, S::Special)}},
			{S::Booster, {Plain(C::Attack, S::GearAttack), Plain(C::GearItem, S::GearOuterItem), Skipping(C::Escape, S::GearEscape, Always, S::Charge),
				Skipping(C::Special, S::Special, C::GearEther, S::GearEther)}},
			{S::GearOuterItem, {Skipping(C::Booster, S::Booster, C::Attack, S::GearAttack), Plain(C::GearItem, S::GearItem),
				Skipping(C::Escape, S::GearEscape, Always, S::Charge), Skipping(C::Special, S::Special, C::GearEther, S::GearEther)}},
			{S::GearEscape, {Skipping(C::Booster, S::Booster, C::Attack, S::GearAttack), Plain(C::GearItem, S::GearOuterItem), Direct(S::Charge),
				Skipping(C::Special, S::Special, C::GearEther, S::GearEther)}},
			{S::Special, {Skipping(C::Booster, S::Booster, C::Attack, S::GearAttack), Plain(C::GearItem, S::GearOuterItem),
				Skipping(C::Escape, S::GearEscape, Always, S::Charge), Plain(C::GearEther, S::GearEther)}},
		}};

		const FStateMoves* FindRing(uint8 State)
		{
			for (const FStateMoves& Ring : Rings)
			{
				if (Ring.State == State)
				{
					return &Ring;
				}
			}
			return nullptr;
		}

		bool Enabled(const FBattleContext& Context, uint8 Slot, int8 Command)
		{
			return Command == Always || !IsDisabled(Context, Slot, Command);
		}

		void Move(FBattleContext& Context, uint8 Slot, const FMove& Move, uint8 Direction)
		{
			FBattlePartyInput& Party = Context.Party;
			switch (Move.Kind)
			{
			case EMove::Direct:
				Party.MenuState = Move.Target;
				return;
			case EMove::Plain:
				if (Enabled(Context, Slot, Move.Command))
				{
					Party.MenuState = Move.Target;
					return;
				}
				PlayMenuSound(Context, ESound::Refuse);
				return;
			case EMove::Skipping:
				if (Enabled(Context, Slot, Move.Command))
				{
					Party.MenuState = Move.Target;
					return;
				}
				if (Party.bBlockedOnce && Party.LastDirection == Direction)
				{
					if (Enabled(Context, Slot, Move.SkipCommand))
					{
						Party.MenuState = Move.Skip;
					}
					else
					{
						PlayMenuSound(Context, ESound::Refuse);
					}
					Party.bBlockedOnce = false;
					return;
				}
				Party.bBlockedOnce = true;
				PlayMenuSound(Context, ESound::Refuse);
				return;
			case EMove::None:
				return;
			}
		}

		/** Boarding clears the statuses (Confusion kept); Chu-Chu's giant form gets fifty times her HP; Bart's boarding clears a mark of the enemies'. */
		void ClearStatusesForBoarding(FBattleContext& Context, uint8 Slot)
		{
			constexpr uint8 Bart = 3;
			Context.Action.Command = 0;
			FBattleCombatant& Member = Context.Combatants[Slot];
			Member.Status = 0;
			Member.Buffs = 0;
			Member.EtherBuffs = 0;
			Member.Elements = 0;
			Member.Mental &= 0x2000;
			if (Member.Character == ChuChu)
			{
				const uint32 Hp = static_cast<uint32>(Member.Hp) * GiantScale;
				const uint32 MaxHp = static_cast<uint32>(Member.MaxHp) * GiantScale;
				if (MaxHp < MaxGiantHp + 1)
				{
					Member.GearPart.Hp = Hp;
					Member.GearPart.MaxHp = MaxHp;
				}
				else
				{
					Member.GearPart.Hp = Hp < MaxGiantHp + 1 ? Hp : MaxGiantHp;
					Member.GearPart.MaxHp = MaxGiantHp;
				}
			}
			if (Member.Character == Bart)
			{
				for (uint8 Enemy = FBattleContext::PartySlotCount; Enemy < FBattleContext::SlotCount; ++Enemy)
				{
					Context.Combatants[Enemy].Mental &= 0xFFDF;
				}
			}
		}

		void ChargeFuel(FBattleContext& Context, uint8 Slot)
		{
			DefendInBattle(Context, Slot);
			FBattleGearPart& Gear = Context.Combatants[Slot].GearPart;
			Gear.Fuel = static_cast<uint16>(Gear.Fuel + Context.GearTurn.Charge);
			if (Gear.MaxFuel < Gear.Fuel)
			{
				Gear.Fuel = Gear.MaxFuel;
			}
			Context.ChangedSlots[Slot] = 1;
			Context.Party.bActed = false;
			Context.Party.bTurnOver = true;
		}

		/** The Booster switches on (clearing the gear's slowing statuses) or off. */
		void ToggleBooster(FBattleContext& Context, uint8 Slot)
		{
			FBattleCombatant& Member = Context.Combatants[Slot];
			if (Context.bPerfectWorksRebalance)
			{
				// Perfect Works' rebalance keeps it in the gear's permanent buffs alone.
				if ((Member.GearPart.PermanentBuffs & BoosterBit) == 0)
				{
					Member.GearPart.Status &= 0xFFDF;
					Member.Status &= 0xEFFF;
					Member.GearPart.PermanentBuffs |= BoosterBit;
				}
				else
				{
					Member.GearPart.PermanentBuffs &= static_cast<uint16>(~BoosterBit);
				}
			}
			else if ((Member.GearPart.Buffs & BoosterBit) == 0)
			{
				Member.GearPart.Status &= 0xFFDF;
				Member.Status &= 0xEFFF;
				Member.GearPart.Buffs |= BoosterBit;
				Member.Buffs |= BoosterBit;
			}
			else
			{
				Member.GearPart.Buffs &= static_cast<uint16>(~BoosterBit);
				Member.Buffs &= static_cast<uint16>(~BoosterBit);
			}
			Context.Party.bTurnOver = true;
		}

		/** The ring's command under the cursor carried out (the three action buttons all confirm). */
		EFlow Confirm(FBattleContext& Context, uint8 Slot, uint8 State)
		{
			FBattlePartyInput& Party = Context.Party;
			switch (State)
			{
			case S::Attack:
			case S::GearAttack:
				if (Context.SlotTargets[Slot] == NoTarget)
				{
					PlayMenuSound(Context, ESound::Refuse);
					return EFlow::Return;
				}
				return Call(Context, EBattlePartyRoutine::StartAttack);
			case S::Item:
			case S::OuterItem:
			case S::GearItem:
			case S::GearOuterItem:
				return Call(Context, EBattlePartyRoutine::ItemMenu);
			case S::Guard:
				// Perfect Works' rebalance stores the turn's unused AP as an attack would.
				Party.bStoresAp = Party.bStoresAp || Context.bPerfectWorksRebalance;
				DefendInBattle(Context, Slot);
				Party.bActed = false;
				Party.bTurnOver = true;
				return EFlow::Return;
			case S::Ether:
			case S::GearEther:
				return Call(Context, EBattlePartyRoutine::EtherMenu);
			case S::Combo:
				if (Context.SlotTargets[Slot] == NoTarget)
				{
					PlayMenuSound(Context, ESound::Refuse);
					return EFlow::Return;
				}
				return Call(Context, EBattlePartyRoutine::ComboMenu);
			case S::Escape:
			case S::GearEscape:
				if (TryBattleEscape(Context))
				{
					Context.End = BattleEnd::Escaped;
				}
				Party.bTurnOver = true;
				return EFlow::Return;
			case S::Board:
				return Call(Context, EBattlePartyRoutine::Board);
			case S::Charge:
				ChargeFuel(Context, Slot);
				return EFlow::Return;
			case S::Booster:
				ToggleBooster(Context, Slot);
				return EFlow::Return;
			case S::Special:
				return Call(Context, EBattlePartyRoutine::SpecialMenu);
			default:
				return EFlow::Return;
			}
		}
	}

	void PlaceBoardedGear(FBattleContext& Context, uint8 Slot)
	{
		constexpr uint8 PartyGears = 16;
		FBattleSlot& Place = Context.Slots[Slot];
		uint8 Group = Place.Group;
		if (Context.Occupancy[PartyGears + Group].Count != 0)
		{
			Group = FBattleStage::GroupCount;
			for (uint8 Free = 0; Free < FBattleStage::GroupCount; ++Free)
			{
				if (Context.Occupancy[PartyGears + Free].Count == 0)
				{
					Group = Free;
					break;
				}
			}
			if (Group == FBattleStage::GroupCount)
			{
				Context.Fault = EBattleFault::NoGearGroup;
				return;
			}
		}
		Place.Group = Group;
		Place.Place = 0;
		Context.Occupancy[PartyGears + Group].Places = 1;
		Context.Occupancy[PartyGears + Group].Count = 1;
		Place.Position = Context.Stage.PartyGearPlace(Group);
	}

	void BeginBoarding(FBattleContext& Context, uint8 Slot)
	{
		PlaceBoardedGear(Context, Slot);
		if (Context.Fault != EBattleFault::None)
		{
			return;
		}
		ClearStatusesForBoarding(Context, Slot);
		if (Context.Show != nullptr)
		{
			Context.Show->BoardGear(Slot);
		}
	}

	void FinishBoarding(FBattleContext& Context, uint8 Slot)
	{
		constexpr uint8 BoardedInBattle = 2;
		Context.Combatants[Slot].BattleFlags |= BattleFlags::InGear;
		BattleSteps::LeaveGroup(Context, Slot);
		Context.ApRecords[Slot].InGear = BoardedInBattle;
		Context.Slots[Slot].InGear = 1;
		Context.ChangedSlots[Slot] = 1;
		if (Context.Show != nullptr)
		{
			Context.Show->PlayMenuCue(EBattleMenuCue::BoardedPanel, Slot, 0);
		}
		FGameState& State = *Context.GameState;
		for (std::size_t Member = 0; Member < FBattleContext::PartySlotCount; ++Member)
		{
			if (BattleGameState::Byte(State, GameStateLayout::PartyOffset + Member) == Context.PartyCharacters[Slot] && !Context.bGearsLocked)
			{
				BattleGameState::Byte(State, GameStateLayout::PartyInGearOffset + Member) = 1;
			}
		}
	}

	EFlow RunBoard(FBattleContext& Context, FBattlePartyFrame& Frame)
	{
		const uint8 Slot = Context.Party.Actor;
		if (Frame.Pc == 0)
		{
			BeginBoarding(Context, Slot);
			Frame.Pc = 1;
		}
		if (Context.Fault != EBattleFault::None || ShowBlocks(Context))
		{
			return EFlow::Yield;
		}
		FinishBoarding(Context, Slot);
		Context.Party.bTurnOver = true;
		return EFlow::Return;
	}

	EFlow RunMenuState(FBattleContext& Context, uint8 Slot)
	{
		FBattlePartyInput& Party = Context.Party;
		switch (Party.MenuState)
		{
		case EState::FadingAttackMode:
			Party.ShownState = 0xFF;
			[[fallthrough]];
		case EState::AttackMode:
			return Call(Context, EBattlePartyRoutine::AttackMode);
		case EState::FadingGearAttackMode:
			Party.ShownState = 0xFF;
			[[fallthrough]];
		case EState::GearAttackMode:
			return Call(Context, EBattlePartyRoutine::GearAttackMode);
		default:
			break;
		}
		const FStateMoves* Ring = FindRing(Party.MenuState);
		const uint8 Event = Party.Event;
		if (Ring == nullptr)
		{
			return EFlow::Return;
		}
		if (Event <= BattleMenuEvents::Up)
		{
			Move(Context, Slot, Ring->Moves[Event], Event);
			return EFlow::Return;
		}
		if (Event == BattleMenuEvents::Confirm || Event == BattleMenuEvents::Weak || Event == BattleMenuEvents::Strong)
		{
			return Confirm(Context, Slot, Party.MenuState);
		}
		return EFlow::Return;
	}
}
