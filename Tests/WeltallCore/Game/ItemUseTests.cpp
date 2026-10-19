#include "Game/ItemUse.h"

#include "Foundation/LittleEndian.h"
#include "Foundation/Sha1.h"
#include "Game/GameState.h"
#include "Support/XorShift32.h"

#include <catch_amalgamated.hpp>

#include <string>
#include <vector>

using namespace Weltall;

// The Menu's own item use on random items, characters and states under the oracle harness
// (item_use_digests), drawing the same cases.
namespace
{
	std::vector<uint8> RandomBytes(Testing::FXorShift32& Random, std::size_t Size)
	{
		std::vector<uint8> Bytes(Size);
		for (std::size_t Offset = 0; Offset < Size; Offset += 4)
		{
			LittleEndian::WriteU32(Bytes, Offset, Random.Next());
		}
		return Bytes;
	}

	std::string ItemUseDigest(bool bPerfectWorksRules)
	{
		constexpr std::size_t Items = 256;
		Testing::FXorShift32 Random(0x17E3A991);
		const std::vector<uint8> Table = RandomBytes(Random, Items * ItemRecord::Size);
		FSha1 Digest;
		FGameState State;
		for (int32 Case = 0; Case < 600; ++Case)
		{
			const std::vector<uint8> Block = RandomBytes(Random, FGameState::BlockSize);
			std::copy(Block.begin(), Block.end(), State.Block().begin());
			const int32 Character = static_cast<int32>(Random.Next() % 11);
			const std::size_t Item = Random.Next() % Items;
			const bool bApplied = ApplyItemInMenu(State, Character, std::span(Table).subspan(Item * ItemRecord::Size, ItemRecord::Size), bPerfectWorksRules);
			Digest.Update(State.Block());
			const uint8 Applied = bApplied ? 1 : 0;
			Digest.Update(std::span(&Applied, 1));
		}
		return FSha1::ToHex(Digest.Finish());
	}
}

TEST_CASE("Items used from the menu change the characters as the Menu's own code does", "[Game][Menu]")
{
	CHECK(ItemUseDigest(false) == "5c2cad841010bb9fc8bf7e8743b0afe697b8d336");
}

// Perfect Works Build's rebalanced party's MENU_MAIN (item_use_digests --perfect-works-rebalance).
TEST_CASE("Items used from the menu change the characters as Perfect Works Build's rebalanced Menu does", "[Game][Menu]")
{
	CHECK(ItemUseDigest(true) == "0a1973f7b77ea4a00b0035cab6ae7815049b50d5");
}

// The random cases' stats and powers nearly always reach the caps; Fei's new-game figures under one item raising max HP, max
// EP, agility and the learning rate by 3, as the rebalanced MENU_MAIN leaves them under the oracle harness.
TEST_CASE("A Perfect Works stat item raises each of its stats by its own value", "[Game][Menu]")
{
	constexpr std::size_t Fei = 0x26C;
	FGameState State;
	const std::span<uint8> Block = State.Block();
	LittleEndian::WriteU16(Block, Fei + 0x4E, 50);
	LittleEndian::WriteU16(Block, Fei + 0x52, 10);
	Block[Fei + 0x5A] = 11;
	Block[Fei + 0x55] = 1;
	std::vector<uint8> Item(ItemRecord::Size, 0);
	Item[ItemRecord::Power] = 3;
	LittleEndian::WriteU16(Item, ItemRecord::Effect, 0x0004);
	LittleEndian::WriteU16(Item, ItemRecord::Parameter, 0x0F00);
	CHECK(ApplyItemInMenu(State, 0, Item, true));
	CHECK(LittleEndian::ReadU16(Block, Fei + 0x4E) == 53);
	CHECK(LittleEndian::ReadU16(Block, Fei + 0x52) == 13);
	CHECK(Block[Fei + 0x5A] == 14);
	CHECK(Block[Fei + 0x55] == 4);
}
