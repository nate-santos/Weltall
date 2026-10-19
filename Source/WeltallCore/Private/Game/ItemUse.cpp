#include "Game/ItemUse.h"

#include "Foundation/LittleEndian.h"
#include "Game/GameState.h"
#include "Game/GameStateLayout.h"

#include <array>

namespace Weltall
{
	namespace
	{
		using namespace GameStateLayout;
		using LittleEndian::ReadU16;

		namespace Effect
		{
			constexpr uint16 Hp = 0x8000;
			constexpr uint16 Ep = 0x4000;
			constexpr uint16 Stats = 0x0004;
			constexpr uint16 Weight = 0x0002;
			constexpr uint16 Special = 0x0001;
		}

		/** The stats a stat item raises, by its parameter's bits: attack, defense, ether, ether defense. */
		struct FStatStep
		{
			uint16 Bit;
			std::size_t Field;
		};
		constexpr std::array<FStatStep, 4> StatSteps = {{{0x8000, 0x58}, {0x4000, 0x59}, {0x2000, 0x5B}, {0x1000, 0x5C}}};
		constexpr uint16 MaxHpStep = 0x0800;
		constexpr uint16 MaxEpStep = 0x0400;
		constexpr uint8 HighestStat = 200;
		constexpr uint16 HighestHp = 999;
		constexpr uint16 HighestEp = 99;
		constexpr uint8 HighestWeightGain = 200;
		/** Perfect Works' rebalance's further steps: agility up to 16, the learning rate up to 12. */
		constexpr uint16 AgilityStep = 0x0200;
		constexpr uint16 LearningRateStep = 0x0100;
		constexpr std::size_t AgilityField = 0x5A;
		constexpr std::size_t LearningRateField = 0x55;
		constexpr uint32 HighestAgility = 16;
		constexpr uint32 HighestLearningRate = 12;
		/** Weight: the parameter's bit 15 gains its low byte, else loses it. */
		constexpr uint16 WeightGains = 0x8000;

		/**
		 * What the debugging item "Debug Skill" teaches each character: its Deathblows, abilities, gear
		 * Deathblows and gear ether words, its AP a turn, its gear's special commands.
		 */
		struct FTaughtSkills
		{
			std::array<uint16, 4> Words;
			uint8 ApPerTurn;
			uint16 Special;
		};
		constexpr std::array<FTaughtSkills, 11> TaughtSkills = {{
			{{0xFFF8, 0xFF00, 0xFFF0, 0xFE00}, 7, 0xE000},
			{{0xFFE0, 0xFFF0, 0xFFF0, 0xFFF0}, 7, 0xC000},
			{{0xFFE0, 0xFFE0, 0xFFF0, 0xFF00}, 7, 0x8000},
			{{0xFFE0, 0xFFC0, 0xFFF0, 0xFF00}, 7, 0xF000},
			{{0xFFC0, 0xFFC0, 0xFFF0, 0xFFC0}, 7, 0xE000},
			{{0xFFC0, 0xF000, 0xFFF0, 0xF000}, 7, 0x8000},
			{{0xFFC0, 0xFF00, 0xFFF0, 0xFF00}, 7, 0x8000},
			{{0x0000, 0xFF00, 0x0000, 0xFF00}, 7, 0x0000},
			{{0x0000, 0xF800, 0xFFF0, 0x0000}, 7, 0xE000},
			{{0xFFE0, 0xFFE0, 0xFFF0, 0xFFE0}, 7, 0x8000},
			{{0xFFC0, 0xFF00, 0xFFF0, 0xFF00}, 7, 0x8000},
		}};

		void WriteU16(std::span<uint8> Block, std::size_t Offset, uint16 Value)
		{
			Block[Offset] = static_cast<uint8>(Value);
			Block[Offset + 1] = static_cast<uint8>(Value >> 8);
		}

		/**
		 * Perfect Works' rebalance's stat items: each step on its whole sum, capped (200, max HP 999, max EP 99), and its
		 * agility and learning rate steps.
		 */
		void ApplyPerfectWorksStatSteps(std::span<uint8> Block, std::size_t Record, uint16 Parameter, uint8 Power)
		{
			const uint32 Step = Power;
			for (const FStatStep& Stat : StatSteps)
			{
				if ((Parameter & Stat.Bit) != 0)
				{
					Block[Record + Stat.Field] = static_cast<uint8>(std::min<uint32>(Block[Record + Stat.Field] + Step, HighestStat));
				}
			}
			const auto Points = [&](std::size_t Field) { return static_cast<uint32>(Block[Record + Field] | Block[Record + Field + 1] << 8); };
			if ((Parameter & MaxHpStep) != 0)
			{
				WriteU16(Block, Record + Character::MaxHp, static_cast<uint16>(std::min<uint32>(Points(Character::MaxHp) + Step, HighestHp)));
			}
			if ((Parameter & MaxEpStep) != 0)
			{
				const uint32 MaxEp = Points(Character::MaxEp) + Step;
				WriteU16(Block, Record + Character::MaxEp, static_cast<uint16>(MaxEp > HighestStat ? HighestEp : MaxEp));
			}
			if ((Parameter & AgilityStep) != 0)
			{
				Block[Record + AgilityField] = static_cast<uint8>(std::min<uint32>(Block[Record + AgilityField] + Step, HighestAgility));
			}
			if ((Parameter & LearningRateStep) != 0)
			{
				Block[Record + LearningRateField] = static_cast<uint8>(std::min<uint32>(Block[Record + LearningRateField] + Step, HighestLearningRate));
			}
			if (Points(Character::MaxEp) > HighestEp)
			{
				WriteU16(Block, Record + Character::MaxEp, HighestEp);
			}
		}

		/** "Debug Item": ten of every weapon, accessory, item, gear weapon and gear part up to its id, each in the slot of its number. */
		void GiveEverything(FGameState& State)
		{
			const auto Fill = [&State](int32 List, int32 LastId, int32 SlotShift)
			{
				const std::span<uint8> Items = State.InventoryItems(List);
				const std::span<uint8> Counts = State.InventoryCounts(List);
				for (int32 Id = 1; Id < LastId; ++Id)
				{
					Items[static_cast<std::size_t>(Id + SlotShift)] = static_cast<uint8>(Id);
					Counts[static_cast<std::size_t>(Id + SlotShift)] = 10;
				}
			};
			// The items' run starts two slots on.
			Fill(1, 0x48, 0);
			Fill(2, 0x96, 0);
			Fill(0, 0x4C, 2);
			Fill(3, 0x48, 0);
			Fill(4, 0x69, 0);
		}

		/** "Debug Skill": every character joined, and taught as the table has it. */
		void TeachEverything(FGameState& State)
		{
			const std::span<uint8> Block = State.Block();
			WriteU16(Block, JoinedCharactersOffset, 0x7FF);
			for (std::size_t Character = 0; Character < TaughtSkills.size(); ++Character)
			{
				const FTaughtSkills& Taught = TaughtSkills[Character];
				const std::size_t Skills = LearnedSkillsOffsetOf(Character);
				for (std::size_t Word = 0; Word < Taught.Words.size(); ++Word)
				{
					WriteU16(Block, Skills + 2 * Word, Taught.Words[Word]);
				}
				Block[Skills + Skills::ApPerTurn] = Taught.ApPerTurn;
				WriteU16(Block, Skills + Skills::Special, Taught.Special);
			}
		}
	}

	bool ApplyItemInMenu(FGameState& State, int32 Character, std::span<const uint8> Item, bool bPerfectWorksRules)
	{
		if (Item.size() < ItemRecord::Size)
		{
			return false;
		}
		const std::span<uint8> Block = State.Block();
		const std::size_t Record = CharacterOffset(static_cast<std::size_t>(Character));
		const uint16 Effects = ReadU16(Item, ItemRecord::Effect);
		const uint16 Parameter = ReadU16(Item, ItemRecord::Parameter);
		const uint8 Power = Item[ItemRecord::Power];
		bool bHpFull = false;
		bool bEpFull = false;
		const auto Points = [&](std::size_t Field) { return ReadU16(Block, Record + Field); };
		if ((Effects & Effect::Hp) != 0)
		{
			bHpFull = Points(Character::Hp) == Points(Character::MaxHp);
			if (!bHpFull)
			{
				WriteU16(Block, Record + Character::Hp, static_cast<uint16>(Points(Character::Hp) + Power * 50));
			}
		}
		if ((Effects & Effect::Ep) != 0)
		{
			bEpFull = Points(Character::Ep) == Points(Character::MaxEp);
			if (!bEpFull)
			{
				WriteU16(Block, Record + Character::Ep, static_cast<uint16>(Points(Character::Ep) + Power * 10));
			}
		}
		if (Points(Character::Hp) > Points(Character::MaxHp))
		{
			WriteU16(Block, Record + Character::Hp, Points(Character::MaxHp));
		}
		if (Points(Character::Ep) > Points(Character::MaxEp))
		{
			WriteU16(Block, Record + Character::Ep, Points(Character::MaxEp));
		}
		if ((Effects & Effect::Stats) != 0 && bPerfectWorksRules)
		{
			ApplyPerfectWorksStatSteps(Block, Record, Parameter, Power);
		}
		else if ((Effects & Effect::Stats) != 0)
		{
			// The step is signed for the stats and unsigned for the maxima; a stat wraps within its byte before its cap.
			for (const FStatStep& Step : StatSteps)
			{
				if ((Parameter & Step.Bit) != 0)
				{
					Block[Record + Step.Field] = static_cast<uint8>(Block[Record + Step.Field] + static_cast<int8>(Power));
				}
			}
			if ((Parameter & MaxHpStep) != 0)
			{
				WriteU16(Block, Record + Character::MaxHp, static_cast<uint16>(Points(Character::MaxHp) + Power));
			}
			if ((Parameter & MaxEpStep) != 0)
			{
				WriteU16(Block, Record + Character::MaxEp, static_cast<uint16>(Points(Character::MaxEp) + Power));
			}
			for (const FStatStep& Step : StatSteps)
			{
				if (Block[Record + Step.Field] > HighestStat)
				{
					Block[Record + Step.Field] = HighestStat;
				}
			}
			if (Points(Character::MaxHp) > HighestHp)
			{
				WriteU16(Block, Record + Character::MaxHp, HighestHp);
			}
			if (Points(Character::MaxEp) > HighestEp)
			{
				WriteU16(Block, Record + Character::MaxEp, HighestEp);
			}
		}
		if ((Effects & Effect::Weight) != 0)
		{
			uint8& Gain = Block[Record + Character::WeightGain];
			const uint8 Step = static_cast<uint8>(Parameter);
			if ((Parameter & WeightGains) != 0)
			{
				Gain = static_cast<uint8>(Gain + Step);
				if (Gain > HighestWeightGain)
				{
					Gain = HighestWeightGain;
				}
			}
			else
			{
				Gain = Gain < Step ? 0 : static_cast<uint8>(Gain - Step);
			}
		}
		if ((Effects & Effect::Special) != 0)
		{
			if (Power == 1)
			{
				GiveEverything(State);
			}
			else if (Power == 2)
			{
				TeachEverything(State);
			}
		}
		// A restoring item did nothing when what it restores was full: both, for one restoring both.
		if ((Effects & Effect::Hp) != 0 && (Effects & Effect::Ep) != 0)
		{
			return !(bHpFull && bEpFull);
		}
		if ((Effects & Effect::Hp) != 0)
		{
			return !bHpFull;
		}
		if ((Effects & Effect::Ep) != 0)
		{
			return !bEpFull;
		}
		return true;
	}
}
