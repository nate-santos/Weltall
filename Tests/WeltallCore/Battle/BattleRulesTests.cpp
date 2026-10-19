#include "Battle/BattleAction.h"
#include "Battle/BattleContext.h"
#include "Battle/BattleItems.h"
#include "Battle/BattleTurn.h"
#include "Battle/BattleWaits.h"
#include "Game/GameState.h"
#include "Game/PsxRandom.h"

#include <catch_amalgamated.hpp>

#include <memory>
#include <utility>

using namespace Weltall;

namespace
{
	/** Fei (slot 0) against one enemy (slot 3), both in the battle, the enemies' command 0 a plain hit. */
	struct FDuel
	{
		FGameState State;
		FPsxRandom Random{1};
		std::unique_ptr<FBattleContext> Context = std::make_unique<FBattleContext>();

		FDuel()
		{
			Context->GameState = &State;
			Context->Random = &Random;
			for (const std::size_t Slot : {std::size_t{0}, std::size_t{3}})
			{
				Context->InBattle[Slot] = true;
				FBattleCombatant& Combatant = Context->Combatants[Slot];
				Combatant.MaxHp = 500;
				Combatant.Hp = 500;
				Combatant.Attack = 30;
				Combatant.Defense = 10;
				Combatant.Hit = 90;
			}
			for (FBattleCommand* Command : {&Context->Commands[FBattleContext::PartyCommandIndex(0, 0)], &Context->Commands[FBattleContext::EnemyCommandIndex(0)]})
			{
				Command->Power = 20;
				Command->HitBonus = 60;
			}
		}
	};
}

TEST_CASE("The kernel's random byte between two bounds draws only when there is a span", "[Battle]")
{
	FPsxRandom Random(1234);
	CHECK(RandomByteBetween(Random, 0xFF, 3) == 0xFF);
	CHECK(RandomByteBetween(Random, 4, 0) == 0);
	CHECK(RandomByteBetween(Random, 5, 5) == 5);
	CHECK(Random.Seed() == 1234);

	FPsxRandom Expected(1234);
	const uint8 Drawn = RandomByteBetween(Random, 0, 10);
	CHECK(Drawn == (Expected.Next() & 0xFF) % 11);
	CHECK(RandomByteBetween(Random, 0, 0xFF) == Expected.NextByte());
}

TEST_CASE("A hit lands, and its damage is applied and recorded for the show", "[Battle]")
{
	FDuel Duel;
	FBattleContext& Context = *Duel.Context;
	PerformBattleCommand(Context, 0, 0, 1u << 3, 0);
	CommitBattleResults(Context, 0);
	REQUIRE(Context.Fault == EBattleFault::None);
	CHECK(Context.Action.Results[3] == BattleResults::HpDamage);
	const uint32 Damage = Context.Action.Amounts[3];
	// Power 20 of 4 x 30 - 3 x 10, and a random tenth more.
	CHECK(Damage >= 90);
	CHECK(Damage <= 90 + 90 / 10 + 1);
	CHECK(Context.Combatants[3].Hp == 500 - Damage);
	CHECK(Context.Performances[0].Amounts[3] == Damage);
	CHECK(Context.ChangedSlots[3] == 1);
}

TEST_CASE("The rules stop where the game would read what it never set", "[Battle]")
{
	SECTION("a weapon slot the actor lacks, when the attack counts the weapons")
	{
		FDuel Duel;
		Duel.Context->Commands[FBattleContext::PartyCommandIndex(0, 0)].WeaponSlots = 0x08;
		PerformBattleCommand(*Duel.Context, 0, 0, 1u << 3, 0);
		CHECK(Duel.Context->Fault == EBattleFault::UnsetWeaponSlot);
	}
	SECTION("but not when an ether attack leaves the weapons out")
	{
		FDuel Duel;
		FBattleCommand& Command = Duel.Context->Commands[FBattleContext::PartyCommandIndex(0, 0)];
		Command.WeaponSlots = 0x08;
		Command.AttackFormula = 1;
		PerformBattleCommand(*Duel.Context, 0, 0, 1u << 3, 0);
		CHECK(Duel.Context->Fault == EBattleFault::None);
	}
	SECTION("an enemy command past the enemies' table")
	{
		FDuel Duel;
		PerformBattleCommand(*Duel.Context, 3, 98, 1u << 0, 0);
		CHECK(Duel.Context->Fault == EBattleFault::CommandOutsideTable);
	}
	SECTION("Billy's ammunition whose count lies past the game state")
	{
		FDuel Duel;
		FBattleCombatant& Billy = Duel.Context->Combatants[0];
		Billy.Character = 4;
		Billy.PistolAmmunition = 0xF0;
		Duel.Context->Commands[FBattleContext::PartyCommandIndex(0, 0)].WeaponSlots = 0x80;
		PerformBattleCommand(*Duel.Context, 0, 0, 1u << 3, 0);
		CHECK(Duel.Context->Fault == EBattleFault::OutsideGameState);
	}
}

TEST_CASE("A wait rolled from the record after a gear table reads it as empty", "[Battle]")
{
	FDuel Duel;
	FBattleContext& Context = *Duel.Context;
	FBattleCombatant& Fei = Context.Combatants[0];
	Fei.BattleFlags |= BattleFlags::InGear;
	Fei.GearPart.Agility = 12;
	Context.Commands[FBattleContext::GearCommandIndex(0, 0)].Speed = 0;
	Context.Commands[FBattleContext::GearCommandIndex(0, 40)].Speed = 5;

	Context.Action.Command = 0;
	FPsxRandom Same = Duel.Random;
	const uint8 FromFirst = RollBattleWait(Context, 0);
	Duel.Random = Same;
	Context.Action.Command = 41;
	const uint8 FromPast = RollBattleWait(Context, 0);
	REQUIRE(Context.Fault == EBattleFault::None);
	CHECK(FromPast == FromFirst);
	CHECK(Context.Action.CommandIndex == FBattleContext::GearCommandIndex(0, 41));
}

TEST_CASE("An item's buff lasts by its own user and target, or in the original by the last action's", "[Battle]")
{
	// Fei (slot 0) uses an Atk UP item on himself after the enemy, who has Ether Doubler, acted.
	const auto Turns = [](const FBattleRuleOptions& Options)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		Context.Combatants[3].PermanentEtherBuffs = 0x2000;
		Context.Action.ActorSlot = 3;
		Context.Action.TargetCombatant = 0;
		FBattleItem& Item = Context.Items[1];
		Item.Effect = 0x800;
		Item.Parameter = 0x4000;
		PerformBattleItem(Context, 0, 1u << 0, 1);
		REQUIRE(Context.Fault == EBattleFault::None);
		CHECK((Context.Combatants[0].Buffs & 0x4000) != 0);
		return Context.Combatants[0].Durations[5];
	};
	CHECK(Turns(FBattleRuleOptions()) == 5);
	CHECK(Turns(FBattleRuleOptions::Original()) == 10);
}

namespace
{
	/** Fei (slot 0) gives himself a status with his command 1: the status handler, sure to land. */
	void GiveSelfStatus(FDuel& Duel, uint8 Group, uint16 Bits, uint8 Turns)
	{
		FBattleCommand& Command = Duel.Context->Commands[FBattleContext::PartyCommandIndex(0, 1)];
		Command.Handler = 2;
		Command.StatusChance = 100;
		Command.StatusGroup = Group;
		Command.StatusBits = Bits;
		Command.Power = Turns;
		PerformBattleCommand(*Duel.Context, 0, 1, 1u << 0, 0);
	}
}

TEST_CASE("Counter UP is set as it is, or in the original checked as an element mode", "[Battle]")
{
	SECTION("a permanent attack element")
	{
		const auto Given = [](const FBattleRuleOptions& Options)
		{
			FDuel Duel;
			Duel.Context->RuleOptions = Options;
			Duel.Context->Combatants[0].PermanentElements = 0x2000;
			GiveSelfStatus(Duel, 7, 0x1000, 3);
			REQUIRE(Duel.Context->Fault == EBattleFault::None);
			return Duel.Context->Combatants[0].EtherBuffs;
		};
		CHECK(Given(FBattleRuleOptions()) == 0x1000);
		// "Can't add Elem".
		CHECK(Given(FBattleRuleOptions::Original()) == 0);
	}
	SECTION("a temporary attack element")
	{
		const auto Elements = [](const FBattleRuleOptions& Options)
		{
			FDuel Duel;
			Duel.Context->RuleOptions = Options;
			Duel.Context->Combatants[0].Elements = 0x2000;
			GiveSelfStatus(Duel, 7, 0x1000, 3);
			REQUIRE(Duel.Context->Fault == EBattleFault::None);
			CHECK(Duel.Context->Combatants[0].EtherBuffs == 0x1000);
			return Duel.Context->Combatants[0].Elements;
		};
		CHECK(Elements(FBattleRuleOptions()) == 0x2000);
		CHECK(Elements(FBattleRuleOptions::Original()) == 0);
	}
}

TEST_CASE("Atk UP, ACC UP and EV UP last their turns, or in the original the battle", "[Battle]")
{
	// Fei's Atk UP (0x2000) and Bart's ACC/EV UP (0x1800), for 4 turns, then 4 turns pass.
	const auto Left = [](const FBattleRuleOptions& Options, uint16 Bits)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		GiveSelfStatus(Duel, 5, Bits, 4);
		REQUIRE(Context.Fault == EBattleFault::None);
		REQUIRE(Context.Combatants[0].Buffs == Bits);
		for (int32 Turn = 0; Turn < 4; ++Turn)
		{
			TickStatusDurations(Context, 0);
		}
		return Context.Combatants[0].Buffs;
	};
	CHECK(Left(FBattleRuleOptions(), 0x2000) == 0);
	CHECK(Left(FBattleRuleOptions(), 0x1800) == 0);
	CHECK(Left(FBattleRuleOptions::Original(), 0x2000) == 0x2000);
	CHECK(Left(FBattleRuleOptions::Original(), 0x1800) == 0x1800);
}

TEST_CASE("Def UP raises defense on foot, or in the original does nothing", "[Battle]")
{
	// The enemy's plain hit on Fei, from the same draws each time.
	const auto Damage = [](const FBattleRuleOptions& Options, uint16 Buffs)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		Context.Combatants[0].Buffs = Buffs;
		PerformBattleCommand(Context, 3, 0, 1u << 0, 0);
		REQUIRE(Context.Fault == EBattleFault::None);
		REQUIRE(Context.Action.Results[0] == BattleResults::HpDamage);
		return Context.Action.Amounts[0];
	};
	const uint32 Plain = Damage(FBattleRuleOptions(), 0);
	CHECK(Damage(FBattleRuleOptions(), 0x4000) < Plain);
	CHECK(Damage(FBattleRuleOptions::Original(), 0x4000) == Plain);
}

TEST_CASE("Billy's ether guns fire the element of the ammunition loaded, or in the original their slot's old one", "[Battle]")
{
	// His right ether gun holds EthFire while its slot keeps the new game's wind: his action 4 fires it.
	constexpr uint8 EthFire = 69;
	constexpr uint8 Fire = 2;
	constexpr uint8 Wind = 8;
	const auto Fired = [](const FBattleRuleOptions& Options, uint8 Loaded)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		Context.AmmunitionAttacks[EthFire - FBattleTables::FirstAmmunition] = Fire;
		FBattleCombatant& Billy = Context.Combatants[0];
		Billy.Character = 4;
		Billy.Weapons[1].Attack = Wind;
		Billy.EtherAmmunition[0] = Loaded;
		PerformBattleCommand(Context, 0, 4, 1u << 3, 0);
		REQUIRE(Context.Fault == EBattleFault::None);
		return std::pair<uint8, uint8>{Context.Commands[FBattleContext::PartyCommandIndex(0, 4)].Element, Billy.Weapons[1].Attack};
	};
	CHECK(Fired(FBattleRuleOptions(), EthFire) == std::pair<uint8, uint8>{Fire, Fire});
	CHECK(Fired(FBattleRuleOptions::Original(), EthFire) == std::pair<uint8, uint8>{Wind, Wind});
	// With no ammunition named there, the slot's element stays.
	CHECK(Fired(FBattleRuleOptions(), 0) == std::pair<uint8, uint8>{Wind, Wind});
}

TEST_CASE("Def DOWN lowers defense on foot by a third and says so, or in the original does nothing", "[Battle]")
{
	// The enemy's plain hit on Fei, from the same draws each time.
	const auto Damage = [](const FBattleRuleOptions& Options, uint16 Status)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		Context.Combatants[0].Defense = 30;
		Context.Combatants[0].Status = Status;
		PerformBattleCommand(Context, 3, 0, 1u << 0, 0);
		REQUIRE(Context.Fault == EBattleFault::None);
		REQUIRE(Context.Action.Results[0] == BattleResults::HpDamage);
		return Context.Action.Amounts[0];
	};
	const uint32 Plain = Damage(FBattleRuleOptions(), 0);
	CHECK(Damage(FBattleRuleOptions(), 0x100) > Plain);
	CHECK(Damage(FBattleRuleOptions::Original(), 0x100) == Plain);

	// The status as enemy command 33 gives it: the table's "Def DOWN" (6), or no message in the original.
	const auto Message = [](const FBattleRuleOptions& Options)
	{
		FDuel Duel;
		Duel.Context->RuleOptions = Options;
		GiveSelfStatus(Duel, 0, 0x100, 0);
		REQUIRE(Duel.Context->Fault == EBattleFault::None);
		CHECK((Duel.Context->Combatants[0].Status & 0x100) != 0);
		return Duel.Context->Action.Message;
	};
	CHECK(Message(FBattleRuleOptions()) == 6);
	CHECK(Message(FBattleRuleOptions::Original()) != 6);
}

TEST_CASE("A status move that fails leaves the target's count alone, or in the original restarts it", "[Battle]")
{
	// Fei is slow for 7 more turns; a Slow of 3 turns lands, or his immunity blocks it.
	const auto Left = [](const FBattleRuleOptions& Options, uint16 Immunity)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		FBattleCombatant& Fei = Context.Combatants[0];
		Fei.Status = 0x1000;
		Fei.Durations[1] = 7;
		Fei.StatusImmunity = Immunity;
		GiveSelfStatus(Duel, 0, 0x1000, 3);
		REQUIRE(Context.Fault == EBattleFault::None);
		if (Immunity != 0)
		{
			CHECK(Context.Action.Results[0] == BattleResults::NoEffect);
		}
		return Fei.Durations[1];
	};
	CHECK(Left(FBattleRuleOptions(), 0x1000) == 7);
	CHECK(Left(FBattleRuleOptions::Original(), 0x1000) == 3);
	// A move that lands restarts the count either way.
	CHECK(Left(FBattleRuleOptions(), 0) == 3);
	CHECK(Left(FBattleRuleOptions::Original(), 0) == 3);
}

TEST_CASE("Igissol and Aegissol give their immunities, or in the original nothing", "[Battle]")
{
	const auto Immunity = [](const FBattleRuleOptions& Options, uint16 Effect, uint16 Parameter)
	{
		FDuel Duel;
		FBattleContext& Context = *Duel.Context;
		Context.RuleOptions = Options;
		FBattleItem& Item = Context.Items[1];
		Item.Effect = Effect;
		Item.Parameter = Parameter;
		PerformBattleItem(Context, 0, 1u << 0, 1);
		REQUIRE(Context.Fault == EBattleFault::None);
		return Context.Combatants[0].StatusImmunity;
	};
	CHECK(Immunity(FBattleRuleOptions(), 0x80, 0) == 0x3F7C);
	CHECK(Immunity(FBattleRuleOptions(), 0x40, 0) == 0xFFFE);
	// Tragedienne's Play Dead carries 80h too.
	CHECK(Immunity(FBattleRuleOptions(), 0x80, 2) == 0);
	CHECK(Immunity(FBattleRuleOptions::Original(), 0x80, 0) == 0);
	CHECK(Immunity(FBattleRuleOptions::Original(), 0x40, 0) == 0);
}

TEST_CASE("A counter during an attack on the party spares only the counterer, or in the original the rest too", "[Battle]")
{
	// An enemy (slot 3) hits the whole party; Fei (slot 0) has Counter UP. Enemies stand in slots 3 to 5.
	const auto Run = [](const FBattleRuleOptions& Options, uint32 Seed)
	{
		FGameState State;
		FPsxRandom Random{Seed};
		auto Context = std::make_unique<FBattleContext>();
		Context->GameState = &State;
		Context->Random = &Random;
		Context->RuleOptions = Options;
		for (std::size_t Slot = 0; Slot < 6; ++Slot)
		{
			Context->InBattle[Slot] = true;
			FBattleCombatant& Combatant = Context->Combatants[Slot];
			Combatant.MaxHp = 500;
			Combatant.Hp = 500;
			Combatant.Attack = 30;
			Combatant.Defense = 10;
			Combatant.Hit = 90;
		}
		Context->Combatants[1].Character = 1;
		Context->Combatants[2].Character = 3;
		Context->Combatants[0].EtherBuffs = 0x1000;
		FBattleCommand& Attack = Context->Commands[FBattleContext::EnemyCommandIndex(5)];
		Attack.Power = 20;
		Attack.HitBonus = 60;
		Attack.Flags = BattleCommandFlags::NeverMisses;
		FBattleCommand& Counter = Context->Commands[FBattleContext::PartyCommandIndex(0, 20)];
		Counter.Flags = 3;
		Counter.Animation = 20;
		Counter.Power = 20;
		PerformBattleCommand(*Context, 3, 5, 0b111, 0);
		REQUIRE(Context->Fault == EBattleFault::None);
		return std::make_pair(Context->Action.Results, Context->Action.ActorSlot);
	};
	// A seed whose first draw counters.
	uint32 Seed = 1;
	while (Run(FBattleRuleOptions::Original(), Seed).first[0] != BattleResults::Counterattack)
	{
		REQUIRE(++Seed < 100);
	}
	const auto [Fixed, FixedActor] = Run(FBattleRuleOptions(), Seed);
	CHECK(Fixed[0] == BattleResults::Counterattack);
	CHECK(Fixed[1] == BattleResults::HpDamage);
	CHECK(Fixed[2] == BattleResults::HpDamage);
	CHECK(Fixed[3] == BattleResults::Counterattack);
	CHECK(Fixed[4] == BattleResults::NoResult);
	CHECK(Fixed[5] == BattleResults::NoResult);
	CHECK(FixedActor == 0);

	const auto [Original, OriginalActor] = Run(FBattleRuleOptions::Original(), Seed);
	CHECK(Original[1] == BattleResults::NoResult);
	CHECK(Original[2] == BattleResults::NoResult);
	CHECK(Original[4] == BattleResults::Counterattack);
	CHECK(Original[5] == BattleResults::Counterattack);
	CHECK(OriginalActor == 0);
}
