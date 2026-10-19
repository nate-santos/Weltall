#pragma once

#include "Foundation/Types.h"

#include <span>

namespace Weltall
{
	class FGameState;

	/** An item's record (0x10 bytes, the game data file's items table) as the Menu reads it. */
	namespace ItemRecord
	{
		constexpr std::size_t Size = 0x10;
		constexpr std::size_t Targeting = 0x04;
		constexpr std::size_t Use = 0x06;
		constexpr std::size_t Power = 0x08;
		constexpr std::size_t Effect = 0x0A;
		constexpr std::size_t Parameter = 0x0C;
		/** Targeting: the whole party; the tags the menus show: on an enemy, on oneself. */
		constexpr uint16 WholeParty = 0x0001;
		constexpr uint16 OnEnemy = 0x1000;
		constexpr uint16 OnSelf = 0x4000;
		/** Use flags: in the menu; only where saving is allowed; in battle. */
		constexpr uint8 InMenu = 0x80;
		constexpr uint8 AtSavePoints = 0x20;
		constexpr uint8 InBattle = 0x40;
	}

	/**
	 * An item used on a character from the Normal Menu, as the Menu applies it: HP (power x 50) and EP
	 * (power x 10) restored up to their maxima, the stat items' steps (capped: 200, max HP 999, max EP
	 * 99), weight gained or lost, and the two debugging items the data holds (every item ten times,
	 * every skill learned). True when it had an effect: a restoring item on a character already full
	 * has none (both full, for an item restoring both). With bPerfectWorksRules, Perfect Works Build's
	 * rebalance's stat steps: each sum capped whole, and its agility and learning-rate steps.
	 */
	WELTALLCORE_API bool ApplyItemInMenu(FGameState& State, int32 Character, std::span<const uint8> Item, bool bPerfectWorksRules = false);
}
