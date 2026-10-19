#include "Game/GearRecompute.h"

#include "Foundation/LittleEndian.h"
#include "Game/GameState.h"
#include "Game/GameStateLayout.h"
#include "Game/GearPartTables.h"

#include <algorithm>
#include <array>

namespace Weltall
{
	namespace
	{
		using namespace GameStateLayout;
		using LittleEndian::ReadU16;
		using LittleEndian::WriteU16;

		/** A byte of a table's record (From) and the gear record's byte it goes to (To). */
		struct FByteField
		{
			std::size_t From;
			std::size_t To;
		};

		namespace GearField
		{
			constexpr std::size_t Frame = 0x02;
			constexpr std::size_t Engine = 0x03;
			constexpr std::size_t Armor = 0x08;
			constexpr std::size_t Parts = 0x09;
			constexpr std::size_t Fuel = Gear::Fuel;
			constexpr std::size_t MaxFuel = Gear::MaxFuel;
			constexpr std::size_t PartsDefense = 0x40;
			constexpr std::size_t PartsEtherDefense = 0x42;
			constexpr std::size_t PartsWeight = 0x44;
			constexpr std::size_t PartsFlags = 0x48;
			constexpr std::size_t OverweightPenalty = 0x4A;
			constexpr std::size_t RepairLevel = 0x4F;
			constexpr std::size_t TerrainAgility = 0x50;
			constexpr std::size_t AttackMultiplierBonus = 0x56;
			constexpr std::size_t ChargeMultiplier = 0x57;
			constexpr std::size_t Hp = Gear::Hp;
			constexpr std::size_t MaxHp = Gear::MaxHp;
			constexpr std::size_t FrameWeight = 0x68;
			constexpr std::size_t Resistances = 0x6E;
			constexpr std::size_t Defense = 0x70;
			constexpr std::size_t EtherDefense = 0x72;
			constexpr std::size_t WeightTolerance = 0x75;
			constexpr std::size_t StatusImmunity = 0x7E;
			constexpr std::size_t Buffs = 0x82;
			constexpr std::size_t Element = 0x86;
			constexpr std::size_t DamageReduction = 0x88;
			constexpr int32 DamageCategories = 16;
			/** Bytes the parts' sums and specials start from: guard, hit, agility, repair, ether amplifier, one unused, the attack and Charge multipliers. */
			constexpr std::array<std::size_t, 8> ClearedBytes{0x4C, 0x4D, 0x4E, 0x4F, 0x54, 0x55, 0x56, 0x57};
		}

		namespace FrameField
		{
			constexpr std::size_t Hp = 0x04;
			constexpr std::size_t Weight = 0x08;
			constexpr std::array<FByteField, 4> Bytes{{{0x14, 0x98}, {0x15, 0x9E}, {0x16, 0x9D}, {0x17, 0x9F}}};
		}

		namespace ArmorField
		{
			constexpr std::size_t Defense = 0x08;
			constexpr std::size_t EtherDefense = 0x0A;
		}

		namespace EngineField
		{
			constexpr std::size_t MaxFuel = 0x06;
			/** The last byte sets both halves of the attack multiplier. */
			constexpr std::array<FByteField, 4> Bytes{{{0x0C, 0x3C}, {0x0D, 0x3D}, {0x0E, 0x3E}, {0x0E, 0x3F}}};
		}

		namespace PartField
		{
			constexpr std::size_t Weight = 0x06;
			constexpr std::size_t Defense = 0x0D;
			constexpr std::size_t EtherDefense = 0x0E;
			constexpr std::size_t Special = 0x15;
			constexpr std::size_t SpecialValue = 0x16;
			/** Perfect Works' rebalance: which of the Deathblow tiers a Deathblow part gives, bits 1, 2 and 4. */
			constexpr std::size_t DeathblowTiers = 0x17;
			/** Perfect Works' rebalance's defenses, as words. */
			constexpr std::size_t DefenseWord = 0x0C;
			constexpr std::size_t EtherDefenseWord = 0x0E;
			constexpr std::size_t DamageReduction = 0x1A;
			/** Bytes summed over the three parts: guard, hit, ether amplifier, agility on each terrain. */
			constexpr std::array<FByteField, 7> SummedBytes{{{0x18, 0x4C}, {0x14, 0x4D}, {0x1B, 0x54}, {0x10, 0x50}, {0x11, 0x51}, {0x12, 0x52}, {0x13, 0x53}}};
		}

		/** A part's special: what its value does. */
		enum class EPartSpecial : uint8
		{
			StatusImmunity = 1,
			Buffs = 2,
			Element = 3,
			DamageReduction = 4,
			Repair = 5,
			DeathblowTier1 = 6,
			DeathblowTier2 = 7,
			DeathblowTier3 = 8,
			Flags = 9,
			AttackMultiplier = 10,
			ChargeMultiplier = 11,
		};

		/** The gear Deathblow a special grants once its tier's other two are known. */
		struct FDeathblowGrant
		{
			uint16 Known;
			uint16 Granted;
		};

		constexpr std::array<FDeathblowGrant, 3> DeathblowGrants{{{0x1800, 0x0400}, {0x0300, 0x0080}, {0x0060, 0x0010}}};
		constexpr uint16 GrantedDeathblows = 0x0490;
		/** The Deathblow bits the gear shop's copy clears instead. */
		constexpr uint16 ShopClearedDeathblows = 0x2480;
		/** The parts' byte summed into the gear's ether amplifier, which the gear shop's copy leaves out. */
		constexpr std::size_t EtherAmplifierSum = 0x54;
		constexpr uint16 RepairAbility = 0x8000;
		constexpr uint16 KeptElementBits = 0xF000;
		/** Weight units per point of tolerance. */
		constexpr uint32 WeightPerTolerance = 120;

		std::span<uint8> GearRecord(FGameState& State, int32 Gear)
		{
			return State.Block().subspan(GearOffset(static_cast<std::size_t>(Gear)), GearSize);
		}

		void OrU16(std::span<uint8> Bytes, std::size_t Offset, uint16 Value)
		{
			WriteU16(Bytes, Offset, static_cast<uint16>(ReadU16(Bytes, Offset) | Value));
		}

		void AddU16(std::span<uint8> Bytes, std::size_t Offset, uint16 Value)
		{
			WriteU16(Bytes, Offset, static_cast<uint16>(ReadU16(Bytes, Offset) + Value));
		}

		/** Half of how far the parts' weight exceeds the gear's tolerance; the weight is read unsigned. */
		uint8 OverweightPenalty(std::span<const uint8> Record)
		{
			const int32 Excess = static_cast<int32>(ReadU16(Record, GearField::PartsWeight) / WeightPerTolerance) - Record[GearField::WeightTolerance];
			const int32 Penalty = Excess / 2;
			return static_cast<int16>(Penalty) < 0 ? 0 : static_cast<uint8>(Penalty);
		}

		void ApplySpecial(std::span<uint8> Record, std::span<const uint8> Part, std::span<uint8> Block, std::size_t DeathblowsAt, bool bPerfectWorksRules)
		{
			const uint16 Value = ReadU16(Part, PartField::SpecialValue);
			const uint8 ValueByte = Part[PartField::SpecialValue];
			const uint8 Special = Part[PartField::Special];
			if (bPerfectWorksRules && Special >= static_cast<uint8>(EPartSpecial::DeathblowTier1) && Special <= static_cast<uint8>(EPartSpecial::DeathblowTier3))
			{
				// Perfect Works' rebalance: one case for the three, each tier given by a flag of the part, then on into the
				// flags' and the multiplier's cases.
				for (std::size_t Tier = 0; Tier < DeathblowGrants.size(); ++Tier)
				{
					const FDeathblowGrant Grant = DeathblowGrants[Tier];
					if ((Part[PartField::DeathblowTiers] & (1u << Tier)) != 0 && (ReadU16(Block, DeathblowsAt) & Grant.Known) == Grant.Known)
					{
						OrU16(Block, DeathblowsAt, Grant.Granted);
					}
				}
				OrU16(Record, GearField::PartsFlags, Value);
				Record[GearField::AttackMultiplierBonus] += ValueByte;
				return;
			}
			switch (static_cast<EPartSpecial>(Special))
			{
				case EPartSpecial::StatusImmunity:
					OrU16(Record, GearField::StatusImmunity, Value);
					break;
				case EPartSpecial::Buffs:
					OrU16(Record, GearField::Buffs, Value);
					break;
				case EPartSpecial::Element:
					OrU16(Record, GearField::Element, Value);
					break;
				case EPartSpecial::DamageReduction:
					OrU16(Record, GearField::Resistances, Value);
					for (int32 Category = 0; Category < GearField::DamageCategories; ++Category)
					{
						if ((Value & (0x8000 >> Category)) != 0)
						{
							Record[GearField::DamageReduction + static_cast<std::size_t>(Category)] += Part[PartField::DamageReduction];
						}
					}
					break;
				case EPartSpecial::Repair:
					Record[GearField::RepairLevel] += ValueByte;
					break;
				case EPartSpecial::DeathblowTier1:
				case EPartSpecial::DeathblowTier2:
				case EPartSpecial::DeathblowTier3:
				{
					const FDeathblowGrant Grant = DeathblowGrants[Part[PartField::Special] - static_cast<uint8>(EPartSpecial::DeathblowTier1)];
					if ((ReadU16(Block, DeathblowsAt) & Grant.Known) == Grant.Known)
					{
						OrU16(Block, DeathblowsAt, Grant.Granted);
					}
					break;
				}
				case EPartSpecial::Flags:
					OrU16(Record, GearField::PartsFlags, Value);
					// The original has no break here: the value's low byte also adds to the attack multiplier.
					Record[GearField::AttackMultiplierBonus] += ValueByte;
					break;
				case EPartSpecial::AttackMultiplier:
					Record[GearField::AttackMultiplierBonus] += ValueByte;
					break;
				case EPartSpecial::ChargeMultiplier:
					Record[GearField::ChargeMultiplier] += ValueByte;
					break;
				default:
					break;
			}
		}
	}

	bool ApplyGearFrame(FGameState& State, int32 Gear, const FGearPartTables& Tables)
	{
		const std::span<uint8> Record = GearRecord(State, Gear);
		const std::span<const uint8> Frame = Tables.Frame(Record[GearField::Frame]);
		if (Frame.empty())
		{
			return false;
		}
		const uint32 Hp = LittleEndian::ReadU32(Frame, FrameField::Hp);
		LittleEndian::WriteU32(Record, GearField::Hp, Hp);
		LittleEndian::WriteU32(Record, GearField::MaxHp, Hp);
		for (const FByteField Field : FrameField::Bytes)
		{
			Record[Field.To] = Frame[Field.From];
		}
		return true;
	}

	bool RefitGearFrame(FGameState& State, int32 Gear, const FGearPartTables& Tables)
	{
		const std::span<uint8> Record = GearRecord(State, Gear);
		const std::span<const uint8> Frame = Tables.Frame(Record[GearField::Frame]);
		if (Frame.empty())
		{
			return false;
		}
		const uint32 MaxHp = LittleEndian::ReadU32(Frame, FrameField::Hp);
		LittleEndian::WriteU32(Record, GearField::MaxHp, MaxHp);
		WriteU16(Record, GearField::FrameWeight, ReadU16(Frame, FrameField::Weight));
		for (const FByteField Field : FrameField::Bytes)
		{
			Record[Field.To] = Frame[Field.From];
		}
		if (MaxHp < LittleEndian::ReadU32(Record, GearField::Hp))
		{
			LittleEndian::WriteU32(Record, GearField::Hp, MaxHp);
		}
		return true;
	}

	bool ApplyGearArmor(FGameState& State, int32 Gear, const FGearPartTables& Tables)
	{
		const std::span<uint8> Record = GearRecord(State, Gear);
		const std::span<const uint8> Armor = Tables.Armor(Record[GearField::Armor]);
		if (Armor.empty())
		{
			return false;
		}
		WriteU16(Record, GearField::Defense, ReadU16(Armor, ArmorField::Defense));
		WriteU16(Record, GearField::EtherDefense, ReadU16(Armor, ArmorField::EtherDefense));
		return true;
	}

	bool ApplyGearEngine(FGameState& State, int32 Gear, const FGearPartTables& Tables)
	{
		const std::span<uint8> Record = GearRecord(State, Gear);
		const std::span<const uint8> Engine = Tables.Engine(Record[GearField::Engine]);
		if (Engine.empty())
		{
			return false;
		}
		const uint16 MaxFuel = ReadU16(Engine, EngineField::MaxFuel);
		WriteU16(Record, GearField::MaxFuel, MaxFuel);
		for (const FByteField Field : EngineField::Bytes)
		{
			Record[Field.To] = Engine[Field.From];
		}
		if (MaxFuel < ReadU16(Record, GearField::Fuel))
		{
			WriteU16(Record, GearField::Fuel, MaxFuel);
		}
		return true;
	}

	bool ApplyGearParts(FGameState& State, int32 Gear, const FGearPartTables& Tables)
	{
		return ApplyGearParts(State, Gear, Tables, Tables.Pilot(Gear), EGearPartsCopy::Menu);
	}

	bool ApplyGearParts(FGameState& State, int32 Gear, const FGearPartTables& Tables, uint8 Pilot, EGearPartsCopy Copy)
	{
		if (CharacterOffset(Pilot) + Character::Gear >= FGameState::BlockSize || LearnedSkillsOffsetOf(Pilot) + Skills::Special + 2 > FGameState::BlockSize)
		{
			return false;
		}
		const std::span<uint8> Record = GearRecord(State, Gear);
		std::array<std::span<const uint8>, 3> Parts;
		for (std::size_t Slot = 0; Slot < Parts.size(); ++Slot)
		{
			Parts[Slot] = Tables.Part(Record[GearField::Parts + Slot]);
			if (Parts[Slot].empty())
			{
				return false;
			}
		}
		const std::span<uint8> Block = State.Block();
		const std::size_t DeathblowsAt = LearnedSkillsOffsetOf(Pilot) + Skills::GearDeathblows;
		const std::size_t SpecialAt = LearnedSkillsOffsetOf(Pilot) + Skills::Special;

		for (const std::size_t Word : {GearField::PartsDefense, GearField::PartsEtherDefense, GearField::PartsWeight, GearField::PartsFlags,
				 GearField::Resistances, GearField::StatusImmunity, GearField::Buffs})
		{
			WriteU16(Record, Word, 0);
		}
		for (const std::size_t Byte : GearField::ClearedBytes)
		{
			Record[Byte] = 0;
		}
		for (std::size_t Byte = 0; Byte < 4; ++Byte)
		{
			Record[GearField::TerrainAgility + Byte] = 0;
		}
		for (int32 Category = 0; Category < GearField::DamageCategories; ++Category)
		{
			Record[GearField::DamageReduction + static_cast<std::size_t>(Category)] = 0;
		}
		WriteU16(Record, GearField::Element, static_cast<uint16>(ReadU16(Record, GearField::Element) & KeptElementBits));
		const uint16 Cleared = Copy == EGearPartsCopy::Menu ? GrantedDeathblows : ShopClearedDeathblows;
		WriteU16(Block, DeathblowsAt, static_cast<uint16>(ReadU16(Block, DeathblowsAt) & ~Cleared));

		const bool bPerfectWorksRules = Tables.UsesPerfectWorksRules();
		for (const std::span<const uint8> Part : Parts)
		{
			AddU16(Record, GearField::PartsDefense, bPerfectWorksRules ? ReadU16(Part, PartField::DefenseWord) : Part[PartField::Defense]);
			AddU16(Record, GearField::PartsEtherDefense, bPerfectWorksRules ? ReadU16(Part, PartField::EtherDefenseWord) : Part[PartField::EtherDefense]);
			AddU16(Record, GearField::PartsWeight, ReadU16(Part, PartField::Weight));
			for (const FByteField Field : PartField::SummedBytes)
			{
				if (Copy == EGearPartsCopy::Menu || Field.To != EtherAmplifierSum)
				{
					Record[Field.To] += Part[Field.From];
				}
			}
			ApplySpecial(Record, Part, Block, DeathblowsAt, bPerfectWorksRules);
		}

		Record[GearField::OverweightPenalty] = OverweightPenalty(Record);
		if (Record[GearField::RepairLevel] != 0)
		{
			OrU16(Block, SpecialAt, RepairAbility);
		}
		else if (Block[CharacterOffset(Pilot) + Character::Gear] == Gear)
		{
			// Another of the pilot's gears may still give the ability; only the gear in use takes it away.
			WriteU16(Block, SpecialAt, static_cast<uint16>(ReadU16(Block, SpecialAt) & ~RepairAbility));
		}
		return true;
	}
}

namespace Weltall
{
	namespace
	{
		/** A gear weapon's record fields and the gear record's fields they go to. */
		namespace WeaponField
		{
			constexpr std::size_t Size = 0x14;
			constexpr std::size_t Attack = 0x0E;
			constexpr std::size_t Effect = 0x12;
			constexpr std::size_t Chance = 0x10;
			constexpr std::size_t Kind = 0x11;
			/** The weapon's first four bytes go to one byte each of the gear's; only the one of its own slot for a gun. */
			constexpr std::size_t Leading = 0x00;
		}
		namespace GunSlot
		{
			/** A slot's fields in the gear record, eight bytes apart: attack, effect (a word), chance, kind. */
			constexpr std::size_t Attack = 0x12;
			constexpr std::size_t Effect = 0x10;
			constexpr std::size_t Chance = 0x13;
			constexpr std::size_t Kind = 0x14;
			constexpr std::size_t Stride = 8;
			constexpr std::size_t Leading = 0x5C;
		}
		/** A weapon of kind 100 is elemental: its effect word's bits go into the gear's element word. */
		constexpr uint8 Elemental = 100;
		constexpr std::size_t GearWeapon = 0x0C;
		constexpr std::size_t GearGuns = 0x04;
		constexpr std::size_t GearElements = 0x86;
		constexpr uint16 KeptElements = 0x0FFF;
		/** Billy's gears, whose four gun slots each take a weapon. */
		constexpr std::array<int32, 2> GunGears = {5, 13};

		std::span<const uint8> GearWeaponRecord(std::span<const uint8> Weapons, uint8 Id)
		{
			const std::size_t At = static_cast<std::size_t>(Id) * WeaponField::Size;
			return At + WeaponField::Size <= Weapons.size() ? Weapons.subspan(At, WeaponField::Size) : std::span<const uint8>{};
		}

		void LoadGun(std::span<uint8> Record, std::span<const uint8> Weapon, std::size_t Slot)
		{
			const std::size_t Base = Slot * GunSlot::Stride;
			Record[Base + GunSlot::Attack] = Weapon[WeaponField::Attack];
			WriteU16(Record, Base + GunSlot::Effect, ReadU16(Weapon, WeaponField::Effect));
			Record[Base + GunSlot::Chance] = Weapon[WeaponField::Chance];
			Record[Base + GunSlot::Kind] = Weapon[WeaponField::Kind];
		}
	}

	bool ApplyGearWeapon(FGameState& State, int32 Gear, std::span<const uint8> GearWeapons)
	{
		const std::span<uint8> Record = State.Block().subspan(GearOffset(static_cast<std::size_t>(Gear)), GearSize);
		const bool bGuns = std::find(GunGears.begin(), GunGears.end(), Gear) != GunGears.end();
		// Billy's gears: each gun slot takes its own weapon (the first from the guns, not the weapon slot).
		const std::array<uint8, 3> Guns = {Record[GearGuns], Record[GearGuns + 1], Record[GearGuns + 3]};
		const std::span<const uint8> Weapon = GearWeaponRecord(GearWeapons, Record[GearWeapon]);
		if (Weapon.empty() || (bGuns && std::any_of(Guns.begin(), Guns.end(), [&](uint8 Gun) { return GearWeaponRecord(GearWeapons, Gun).empty(); })))
		{
			return false;
		}
		LoadGun(Record, Weapon, 0);
		for (std::size_t Byte = 0; Byte < 4; ++Byte)
		{
			Record[GunSlot::Leading + Byte] = Weapon[WeaponField::Leading + Byte];
		}
		if (Record[GunSlot::Kind] == Elemental)
		{
			WriteU16(Record, GearElements, static_cast<uint16>((ReadU16(Record, GearElements) & KeptElements) | ReadU16(Record, GunSlot::Effect)));
		}
		if (bGuns)
		{
			for (std::size_t Gun = 0; Gun < Guns.size(); ++Gun)
			{
				const std::span<const uint8> Loaded = GearWeaponRecord(GearWeapons, Guns[Gun]);
				LoadGun(Record, Loaded, Gun);
				Record[GunSlot::Leading + Gun + 1] = Loaded[WeaponField::Leading + Gun + 1];
			}
		}
		return true;
	}
}
