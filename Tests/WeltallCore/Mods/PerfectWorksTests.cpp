#include "Mods/GameMods.h"
#include "Mods/PerfectWorks.h"

#include <catch_amalgamated.hpp>

#include <algorithm>
#include <fstream>
#include <map>
#include <string>
#include <utility>

using namespace Weltall;

namespace
{
	/** A release laid out as the mod's: a README naming its version, and folders of files named by their records. */
	struct FRelease
	{
		explicit FRelease(const char* Name) : Path(std::filesystem::temp_directory_path() / Name)
		{
			std::filesystem::remove_all(Path);
			Write("README.md", "# Perfect_Works_Build\n\n## Version history\n\n### Version 0.11.4 \r\n\n### Version 0.11.3\n");
			for (const char* Folder : {"script1/0610", "script1/0038", "script1/3087", "script1/3088", "script1/2593.unk8", "script1/1170", "script2/2593.unk8", "bug_fix1/0610",
					 "bug_fix1/0852", "bug_fix2/0610", "storyfiles_cd1/0610", "storyfiles_cd1/2618", "storyfiles_script_cd1/0610", "portraits/493.tim",
					 "resized_portraits/0493.tim", "resized_portraits/0548.tim", "roni_pw/default/0548.tim", "roni_pw/resized/0548.tim",
					 "music_1/1170", "music_1/1172", "music_subfiles/1170_5", "title_screen/1587.raw1", "emeralda_fix/og_script/0852",
					 "emeralda_fix/new_script/0852", "jpn_ctrl_subfiles/2614/file0", "script1/0035", "filesbasic/0035", "filesbasic/0047",
					 "filesbasic/2746"})
			{
				Write(std::filesystem::path("gamefiles") / Folder, Folder);
			}
		}
		~FRelease() { std::filesystem::remove_all(Path); }

		void Write(const std::filesystem::path& File, std::string_view Text) const
		{
			std::filesystem::create_directories((Path / File).parent_path());
			std::ofstream(Path / File, std::ios::binary) << Text;
		}

		std::filesystem::path Path;
	};

	/** Each record chosen, with the folder its file came from (and its spliced scripts' folder). */
	std::map<int32, std::string> Chosen(const FPerfectWorksRelease& Release, const FPerfectWorksOptions& Options, int32 Disc = 1)
	{
		std::map<int32, std::string> Records;
		for (const FModFile& File : Release.Files(Options, Disc))
		{
			std::string From = File.Path.parent_path().filename().string();
			if (!File.Section5.empty())
			{
				From += " + " + File.Section5.filename().string();
			}
			Records[File.Record] = From;
		}
		return Records;
	}
}

TEST_CASE("A Perfect Works release is found by its folders, its version read from its README", "[Mods]")
{
	const FRelease Folder("WeltallPerfectWorksFind");
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(Folder.Path);
	REQUIRE(Release);
	CHECK(Release->Version() == "0.11.4");
	CHECK_FALSE(FPerfectWorksRelease::Find(Folder.Path / "gamefiles"));
	CHECK_FALSE(FPerfectWorksRelease::Find(Folder.Path / "missing"));
}

TEST_CASE("The Mods folder's Perfect Works folder is found as the mod", "[Mods]")
{
	const FRelease Folder("WeltallModsFolder/PerfectWorks");
	const FFoundMods Found = FindMods(Folder.Path.parent_path());
	REQUIRE(Found.PerfectWorks);
	CHECK(Found.PerfectWorks->Version() == "0.11.4");
	CHECK_FALSE(FindMods(Folder.Path).PerfectWorks);
}

TEST_CASE("Perfect Works' files follow its patcher's folders and order, without the programs' files", "[Mods]")
{
	const FRelease Folder("WeltallPerfectWorksFiles");
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(Folder.Path);
	REQUIRE(Release);
	FPerfectWorksOptions Options;
	CHECK(Release->Files(Options, 1).empty());
	Options.bEnabled = true;

	// The defaults: the script's files (the programs' left out, and the battle events' scripts with a version of the
	// battle events' code the port doesn't know), the fixed portraits, the title, the cafe's map for the script, and the
	// scene's songs spliced into the script's map.
	CHECK(Chosen(*Release, Options) == std::map<int32, std::string>{{493, "portraits"}, {610, "script1"}, {852, "new_script"},
		{1170, "script1 + 1170_5"}, {1587, "title_screen"}, {2593, "script1"}});
	// The second disc: its own folders, no cafe.
	CHECK(Chosen(*Release, Options, 2) == std::map<int32, std::string>{{493, "portraits"}, {1587, "title_screen"}, {2593, "script2"}});

	// Without the script: the bug fixes and the whole maps of the songs, the cafe's map for the original script.
	Options.bScript = false;
	CHECK(Chosen(*Release, Options) == std::map<int32, std::string>{{493, "portraits"}, {610, "bug_fix1"}, {852, "og_script"}, {1170, "music_1"},
		{1172, "music_1"}, {1587, "title_screen"}});

	// Story mode before the bug fixes, which take its map; resized portraits, and Roni's resized after them.
	Options.bStoryMode = true;
	Options.Portraits = EPerfectWorksPortraits::Resized;
	Options.bRoniPortrait = true;
	Options.bMusic = false;
	Options.bTitleScreen = false;
	Options.bEmeraldaCafe = false;
	CHECK(Chosen(*Release, Options) == std::map<int32, std::string>{{493, "resized_portraits"}, {548, "resized"}, {610, "bug_fix1"},
		{852, "bug_fix1"}, {2618, "storyfiles_cd1"}});
	Options.bScript = true;
	Options.Portraits = EPerfectWorksPortraits::Original;
	CHECK(Chosen(*Release, Options) == std::map<int32, std::string>{{548, "default"}, {610, "storyfiles_script_cd1"}, {1170, "script1"},
		{2593, "script1"}});
}

TEST_CASE("Perfect Works' battle events' scripts are left out with a version of its battle events' code the port doesn't know", "[Mods]")
{
	const FRelease Folder("WeltallPerfectWorksUnknownCode");
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(Folder.Path);
	REQUIRE(Release);
	CHECK_FALSE(Release->KnowsBattleEventCode());
	FPerfectWorksOptions Options;
	Options.bEnabled = true;
	const std::vector<FModFile> Files = Release->Files(Options, 1);
	CHECK(std::none_of(Files.begin(), Files.end(), [](const FModFile& File) { return File.Record == 3088; }));
	CHECK_FALSE(Release->Changes(Options, Files).bPerfectWorksBattleMessages);
}

TEST_CASE("Perfect Works 0.11.4's battle events' scripts come with its change to their message windows", "[Mods][Content]")
{
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(std::filesystem::path(WELTALL_GAMES_DIR).parent_path() / "Mods" / "PerfectWorks");
	if (!Release || Release->Version() != "0.11.4")
	{
		SKIP("Perfect Works Build 0.11.4 not found in Mods/PerfectWorks");
	}
	CHECK(Release->KnowsBattleEventCode());
	FPerfectWorksOptions Options;
	Options.bEnabled = true;
	for (const int32 Disc : {1, 2})
	{
		CHECK(Release->Changes(Options, Release->Files(Options, Disc)).bPerfectWorksBattleMessages);
	}
	Options.bScript = false;
	CHECK_FALSE(Release->Changes(Options, Release->Files(Options, 1)).bPerfectWorksBattleMessages);
}

TEST_CASE("Perfect Works' rebalances are left out with versions of their code the port doesn't know", "[Mods]")
{
	const FRelease Folder("WeltallPerfectWorksUnknownRebalance");
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(Folder.Path);
	REQUIRE(Release);
	CHECK_FALSE(Release->KnowsRebalanceCode());
	CHECK_FALSE(Release->KnowsEnemiesCode());
	FPerfectWorksOptions Options;
	Options.bEnabled = true;
	const std::map<int32, std::string> Plain = Chosen(*Release, Options);
	Options.bRebalancedParty = true;
	Options.bRebalancedEnemies = true;
	CHECK(Chosen(*Release, Options) == Plain);
	CHECK_FALSE(Release->Changes(Options, Release->Files(Options, 1)).bPerfectWorksRebalance);
}

TEST_CASE("Perfect Works 0.11.4's rebalances come in its patcher's merged folders, and not with story mode", "[Mods][Content]")
{
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(std::filesystem::path(WELTALL_GAMES_DIR).parent_path() / "Mods" / "PerfectWorks");
	if (!Release || Release->Version() != "0.11.4")
	{
		SKIP("Perfect Works Build 0.11.4 not found in Mods/PerfectWorks");
	}
	CHECK(Release->KnowsRebalanceCode());
	CHECK(Release->KnowsEnemiesCode());
	FPerfectWorksOptions Options;
	Options.bEnabled = true;
	Options.bRebalancedParty = true;
	Options.bRebalancedEnemies = true;
	// The item tables (2594) and a scene's enemies (2618), by the script, the party and the enemies chosen.
	const auto Folders = [&](int32 Disc = 1)
	{
		std::map<int32, std::string> Records = Chosen(*Release, Options, Disc);
		return std::pair{Records[2594], Records[2618]};
	};
	CHECK(Folders() == std::pair<std::string, std::string>{"Script_items", "monsters_both"});
	CHECK(Folders(2) == std::pair<std::string, std::string>{"Script_items2", "monsters_both"});
	CHECK(Release->Changes(Options, Release->Files(Options, 1)).bPerfectWorksRebalance);
	Options.bScript = false;
	CHECK(Folders() == std::pair<std::string, std::string>{"items1", "monsters_items"});
	Options.bRebalancedParty = false;
	CHECK(Folders().second == "Monsters");
	CHECK_FALSE(Release->Changes(Options, Release->Files(Options, 1)).bPerfectWorksRebalance);
	Options.bScript = true;
	CHECK(Folders() == std::pair<std::string, std::string>{"script1", "monsters_script"});
	// Story mode takes neither: its own scenes' files, and the original's rules.
	Options.bRebalancedParty = true;
	Options.bStoryMode = true;
	CHECK(Folders().second == "storyfiles_script_cd1");
	CHECK_FALSE(Release->Changes(Options, Release->Files(Options, 1)).bPerfectWorksRebalance);
}

TEST_CASE("Perfect Works' arena and its script's Battling are left out with versions of their code the port doesn't know", "[Mods]")
{
	const FRelease Folder("WeltallPerfectWorksUnknownArena");
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(Folder.Path);
	REQUIRE(Release);
	CHECK_FALSE(Release->KnowsArenaCode(EPerfectWorksArena::Basic, false));
	FPerfectWorksOptions Options;
	Options.bEnabled = true;
	Options.bScript = false;
	const std::map<int32, std::string> Plain = Chosen(*Release, Options);
	Options.Arena = EPerfectWorksArena::Basic;
	CHECK(Chosen(*Release, Options) == Plain);
	CHECK(Release->UsesArena(Options) == EPerfectWorksArena::Original);
	CHECK(Release->Changes(Options, Release->Files(Options, 1)).PerfectWorksArena == EPerfectWorksArena::Original);
	// The script's Battling, of a version the port doesn't know, stays the disc's.
	Options.bScript = true;
	CHECK_FALSE(Chosen(*Release, Options).contains(35));
}

TEST_CASE("Perfect Works 0.11.4's arena modes take their folder's own files, and not with story mode", "[Mods][Content]")
{
	const std::optional<FPerfectWorksRelease> Release = FPerfectWorksRelease::Find(std::filesystem::path(WELTALL_GAMES_DIR).parent_path() / "Mods" / "PerfectWorks");
	if (!Release || Release->Version() != "0.11.4")
	{
		SKIP("Perfect Works Build 0.11.4 not found in Mods/PerfectWorks");
	}
	FPerfectWorksOptions Options;
	Options.bEnabled = true;
	// With the script alone, Battling is the script's: the tables the port reads from it (its roster's names).
	CHECK(Chosen(*Release, Options)[35] == "script1");
	for (const EPerfectWorksArena Arena : {EPerfectWorksArena::Basic, EPerfectWorksArena::Expert})
	{
		for (const bool bScript : {false, true})
		{
			Options.Arena = Arena;
			Options.bScript = bScript;
			CHECK(Release->KnowsArenaCode(Arena, bScript));
			const std::string Folder = std::string(Arena == EPerfectWorksArena::Basic ? "filesbasic" : "filesexpert") + (bScript ? "_script" : "");
			for (const int32 Disc : {1, 2})
			{
				std::map<int32, std::string> Records = Chosen(*Release, Options, Disc);
				// Battling, its 48/3, its gears (49/2-49/50) and with the script map 222; the scenes (17, 64) are the other folders'.
				CHECK(Records[35] == Folder);
				CHECK(Records[47] == Folder);
				CHECK(Records[52] == Folder);
				CHECK(Records[100] == Folder);
				CHECK(Records[2746] != Folder);
				CHECK(Records[2652] != Folder);
				// With the script, map 222 is the arena folder's, its scenes' songs spliced in.
				CHECK(Records[1050].starts_with(Folder) == bScript);
				CHECK(Release->Changes(Options, Release->Files(Options, Disc)).PerfectWorksArena == Arena);
			}
		}
	}
	// Story mode takes the original arena.
	Options.bStoryMode = true;
	CHECK(Release->UsesArena(Options) == EPerfectWorksArena::Original);
	CHECK(Chosen(*Release, Options)[35] == "script1");
}
