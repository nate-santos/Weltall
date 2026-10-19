#pragma once

#include "Foundation/Types.h"
#include "Media/DiscModFiles.h"

#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace Weltall
{
	/** The dialogue portraits Perfect Works Build gives: the game's own, its fixed ones, or its fixed ones resized. */
	enum class EPerfectWorksPortraits : uint8
	{
		Original,
		Fixed,
		Resized,
	};

	/** What the player takes from Perfect Works Build, the fan mod, read from their own copy of its release. */
	struct FPerfectWorksOptions
	{
		/** The mod is used at all; off, the game is the discs' own whatever the rest say. */
		bool bEnabled = false;
		/** Its translation of the script and of every name. */
		bool bScript = true;
		EPerfectWorksPortraits Portraits = EPerfectWorksPortraits::Fixed;
		/** The mod's own portrait of Roni. */
		bool bRoniPortrait = false;
		/** Emeralda sits at the cafe as herself, not in the sprites of her nanomachine tube. */
		bool bEmeraldaCafe = true;
		/** Songs chosen again for some scenes. */
		bool bMusic = true;
		/** The mod's story mode: battles that pose no danger and no random battles. Its patcher takes it alone of the rebalances. */
		bool bStoryMode = false;
		/** The mod's title screen. */
		bool bTitleScreen = true;
		/** The mod's rebalanced party and items: its stats, abilities and items, and its battle and menu rules for them. */
		bool bRebalancedParty = false;
		/** The mod's rebalanced enemies: the stats and AI of some enemies and bosses. */
		bool bRebalancedEnemies = false;
		/** The mod's rebalance of the Battling arena: its gears, their numbers and its rules of the fight. Its patcher takes it without story mode. */
		EPerfectWorksArena Arena = EPerfectWorksArena::Original;

		bool operator==(const FPerfectWorksOptions&) const = default;
	};

	/** A copy of Perfect Works Build's release, unpacked in a folder: its version and its folders of the game's files. */
	class FPerfectWorksRelease
	{
	public:
		/** The release in Folder (its README beside the gamefiles folder its patcher reads), or none. */
		WELTALLCORE_API static std::optional<FPerfectWorksRelease> Find(const std::filesystem::path& Folder);

		/** The version its README names, empty where it names none. */
		const std::string& Version() const { return VersionName; }
		/**
		 * Its battle events' code is a version whose message windows the port re-creates: with another, its battle
		 * events' scripts are left out and the battles' dialogue keeps the disc's text.
		 */
		bool KnowsBattleEventCode() const { return bKnownBattleEventCode; }
		/**
		 * Its rebalanced party's Battle and MENU_MAIN are versions whose rules the port re-creates: with others, the
		 * rebalance is left out and the game keeps the discs' party, items and rules.
		 */
		bool KnowsRebalanceCode() const { return bKnownRebalanceCode; }
		/** Its rebalanced enemies' Battle is a version whose rules the port has: with another, they are left out. */
		bool KnowsEnemiesCode() const { return bKnownEnemiesCode; }
		/**
		 * The Battling program of the arena's mode (with or without the script) is a version whose rules the port re-creates:
		 * with another, the arena's files are left out and the arena keeps the discs' gears and rules.
		 */
		WELTALLCORE_API bool KnowsArenaCode(EPerfectWorksArena Arena, bool bScript) const;
		/** Options take the rebalanced party: chosen, without story mode, and with code the port knows. */
		WELTALLCORE_API bool UsesRebalancedParty(const FPerfectWorksOptions& Options) const;
		/** Options take the rebalanced enemies: chosen, without story mode, and with code the port knows. */
		WELTALLCORE_API bool UsesRebalancedEnemies(const FPerfectWorksOptions& Options) const;
		/** The arena's mode Options take: the one chosen, without story mode, with code the port knows; else Original. */
		WELTALLCORE_API EPerfectWorksArena UsesArena(const FPerfectWorksOptions& Options) const;

		/**
		 * The files Options replace on Disc (1 or 2), each record once and in order of records: the folders the mod's
		 * patcher would copy, in its order, a later folder's file taking a record's place, its scenes' songs then spliced
		 * in. The programs' files are left out: the port runs code of its own for them, with the mod's changes that the
		 * data needs (Changes), and the battle events' scripts with them unless the port knows the mod's version of
		 * that code (KnowsBattleEventCode). Battling's is kept where it is a version the port knows, for the tables the
		 * port reads from it (its roster's names). Of the arena's folder only the arena's own files are taken.
		 */
		WELTALLCORE_API std::vector<FModFile> Files(const FPerfectWorksOptions& Options, int32 Disc) const;

		/**
		 * The mod's changes to the game's code that Options and the files they take (Files) need: its battle messages'
		 * with its battle events' scripts, its rebalance's rules with its rebalanced party, its arena's rules with its arena.
		 */
		WELTALLCORE_API FModProgramChanges Changes(const FPerfectWorksOptions& Options, std::span<const FModFile> Files) const;

	private:
		std::filesystem::path GameFiles;
		std::string VersionName;
		bool bKnownBattleEventCode = false;
		bool bKnownRebalanceCode = false;
		bool bKnownEnemiesCode = false;
		/** Each arena mode's Battling (Basic, Expert), without and with the script. */
		std::array<std::array<bool, 2>, 2> KnownArenaCode{};
	};
}
