#pragma once

#include "Battle/BattleEventOptions.h"
#include "Battle/BattleRuleOptions.h"
#include "Battle/Show/BattleShowOptions.h"
#include "Battling/BattlingOptions.h"
#include "Field/EventContext.h"
#include "Field/FieldCameraOffsets.h"
#include "WorldMap/WorldMapCameraOffsets.h"
#include "Field/FieldRenderOptions.h"
#include "Foundation/Types.h"
#include "Game/BattleSpeed.h"
#include "Game/GameBalance.h"
#include "Game/SaveLoad.h"
#include "Input/ControlBindings.h"
#include "Input/TouchPad.h"
#include "Menu/MenuOptions.h"
#include "Mods/PerfectWorks.h"
#include "Media/VoiceLanguage.h"
#include "Presentation/DisplayOptions.h"
#include "Sound/GameSoundSession.h"
#include "Text/MesWindow.h"
#include "WorldMap/WorldMapOptions.h"

#include <string>
#include <string_view>

namespace Weltall
{
	/** The fixes of the original's broken effects: each on unless the original's behavior is asked for. */
	struct FGameFixes
	{
		FEventOptions Field;
		FFieldMusicOptions Music;
		FSoundDriverOptions SoundDriver;
		FSaveLoadOptions Load;
		FMenuOptions Menu;
		FBattleRuleOptions BattleRules;
		FBattleEventOptions BattleEvents;
		FBattleShowOptions BattleShow;
		FWorldMapOptions WorldMap;
		FBattlingOptions Battling;
	};

	/**
	 * Everything the player chooses, kept between sessions: the fixes, the window, how the field is drawn,
	 * the sound's output, the player's framing of the field and the controls. The game's systems read
	 * their options from it.
	 */
	struct FPlayerSettings
	{
		FGameFixes Fixes;
		FDisplayOptions Display;
		FFieldRenderOptions Field;
		ESoundOutput SoundOutput = ESoundOutput::Stereo;
		EVoiceLanguage VoiceLanguage = EVoiceLanguage::Automatic;
		EMovieSubtitles MovieSubtitles = EMovieSubtitles::Automatic;
		/** Read replacements from the Assets folder, falling back to the discs for absent files. */
		bool bExternalAssets = false;
		/** Save original texture pages as PNG files when they are first drawn. */
		bool bCaptureTextures = false;
		FFieldCameraOffsets Camera = FFieldCameraOffsets::Defaults();
		/** The player's framing of the world map. */
		FWorldMapCameraOffsets WorldMapCamera;
		/** A chapter's title card is shown as the chapter begins. */
		bool bChapterCards = true;
		/** How fast messages are typed: twice the original's until the player chooses. */
		EMesTextSpeed TextSpeed = EMesTextSpeed::Double;
		/** How fast battles are fought, which L1 and R1 step during one. */
		EBattleSpeed BattleSpeed = EBattleSpeed::Normal;
		/** The player's changes to the game's balance: rewards and how often battles come. */
		FGameBalance Balance;
		/** What the game takes from Perfect Works Build where the player has it, from the game's next start. */
		FPerfectWorksOptions PerfectWorks;
		FControlBindings Controls = FControlBindings::Defaults();
		/** Android's original touch overlay; desktop input is unaffected. */
		FTouchOptions Touch;
	};

	/**
	 * The settings as text, one "key = value" a line, every key in a fixed order. A setting whose value
	 * is its default is written too, so the text lists every choice there is.
	 */
	WELTALLCORE_API std::string WritePlayerSettings(const FPlayerSettings& Settings);

	/**
	 * The settings a text keeps: a key it lacks, or whose value it cannot read, takes its default; keys
	 * it does not know and lines that are not settings are passed over.
	 */
	WELTALLCORE_API FPlayerSettings ReadPlayerSettings(std::string_view Text);
}
