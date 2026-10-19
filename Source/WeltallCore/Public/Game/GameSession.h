#pragma once

#include "Field/FieldDrawingTables.h"
#include "Field/FieldMovieCues.h"
#include "Foundation/Result.h"
#include "Foundation/UnreachedCases.h"
#include "Game/ChapterCard.h"
#include "Game/ChapterTitles.h"
#include "Game/GameState.h"
#include "Game/GameStateNames.h"
#include "Game/KernelState.h"
#include "Game/PlayerSettings.h"
#include "Game/PsxRandom.h"
#include "Graphics/GteMath.h"
#include "Input/KernelPad.h"
#include "Input/KernelVibration.h"
#include "Media/DiscIdentification.h"
#include "Media/MovieSubtitles.h"
#include "Text/MesResident.h"
#include "Timing/PlayClock.h"

#include <array>
#include <memory>
#include <span>
#include <string>

namespace Weltall
{
	class FCdImage;
	class FGameFileIndex;
	class FGameSoundSession;
	struct FBattleGameFiles;
	struct FBattlingGameFiles;
	class FPsxExecutable;
	class FSaveStore;
	class IButtonGlyphs;
	class IGameMenus;
	class FStoryPacks;

	/** A disc of the game as the session reads it: its image and its file index. */
	struct FGameDisc
	{
		const FCdImage* Image = nullptr;
		const FGameFileIndex* Index = nullptr;
	};

	/**
	 * Where the host keeps the player's discs: asked for a disc the session was not given (the second as the first's
	 * story ends, or a second disc's save loaded), it looks again and gives it with FGameSession::AddDisc when it is there.
	 */
	class IGameDiscDrive
	{
	public:
		virtual ~IGameDiscDrive() = default;
		virtual void Seek(int32 Number) = 0;
	};

	/**
	 * What every program of the game shares for a whole session, as the console keeps it from power-on:
	 * the disc and its file index, the kernel's tables, the text's resources and the files every battle
	 * reads (read once, where the console reads them for each battle), the game's state, its
	 * random numbers, the pad, the play clock, the kernel's globals the programs hand each other, and the
	 * game's sound; and beside them the player's settings, which the programs take their options from.
	 */
	class FGameSession
	{
	public:
		/**
		 * The console powered on with a disc of the given release (its kernel the executable), the game's
		 * sound Sound: the kernel's resident data read, a new game's state loaded and the kernel's globals
		 * set as its start-up leaves them. The disc, its index and the sound must outlive the session.
		 */
		WELTALLCORE_API static TResult<std::unique_ptr<FGameSession>> PowerOn(const FCdImage& Image, const FGameFileIndex& Index,
			const FPsxExecutable& Executable, EDiscRelease Release, FGameSoundSession& Sound);

		FGameSession(const FGameSession&) = delete;
		FGameSession& operator=(const FGameSession&) = delete;
		WELTALLCORE_API ~FGameSession();

		/**
		 * The disc the programs read their files from: the first once the session has it, which holds every file, else the
		 * second it powered on with, whose files outside its own part of the story are the mastering tool's stand-ins. A
		 * second disc's programs agree with the first's but for their file tables, so its release reads either.
		 */
		const FCdImage& Image() const { return Discs[0].Image != nullptr ? *Discs[0].Image : DiscImage; }
		const FGameFileIndex& Index() const { return Discs[0].Index != nullptr ? *Discs[0].Index : FileIndex; }
		/** The release of the disc the console powered on with. */
		EDiscRelease Release() const { return DiscRelease; }
		/** The disc in the drive, 0 the first and 1 the second: the one powered on with, until the Menu takes the other. */
		int32 DiscNumber() const { return KernelGlobals.Disc; }
		/** Gives the session the game's other disc, Number 0 or 1, whose movies play while the drive holds it. It must outlive the session. */
		WELTALLCORE_API void AddDisc(int32 Number, const FCdImage& Image, const FGameFileIndex& Index);
		/** The disc in the drive, which the movies are read from; an error when the session was not given it. */
		WELTALLCORE_API TResult<FGameDisc> DriveDisc() const;
		/** An optional USA or undub voice track. Language is English or Japanese, Number is zero or one. */
		WELTALLCORE_API void AddVoiceDisc(EVoiceLanguage Language, int32 Number, const FCdImage& Image, const FGameFileIndex& Index);
		/** The selected voice track's disc for the movie in the drive, with the gameplay disc as fallback. */
		WELTALLCORE_API TResult<FGameDisc> MovieDisc() const;
		/**
		 * The undub's subtitles for movie Number of the disc in the drive, as the player's setting shows them: none
		 * when it has them off, when the undub disc of that number was not supplied or does not subtitle the movie,
		 * or, on Auto, when the movie's sound is not the undub's.
		 */
		WELTALLCORE_API TResult<std::optional<FMovieSubtitles>> MovieSubtitles(int32 Number) const;
		/** Whether the session has disc Number (0 or 1), the host's drive asked for it first when it has not. */
		WELTALLCORE_API bool HasDisc(int32 Number);
		void SetDiscDrive(IGameDiscDrive* InDrive) { DiscDrive = InDrive; }

		const FGteArctangents& Arctangents() const { return GteArctangents; }
		const FMesResident& Text() const { return TextResources; }
		const FFieldDrawingTables& FieldTables() const { return DrawingTables; }
		const FFieldMovieCues& FieldMovieCues() const { return MovieCues; }
		/** What the Battle program reads for every battle. */
		const FBattleGameFiles& BattleFiles() const { return *BattleData; }
		/** What the Battling program reads as it starts. */
		const FBattlingGameFiles& BattlingFiles() const { return *BattlingData; }
		FGameSoundSession& Sound() const { return GameSound; }

		FGameState& Game() { return State; }
		const FGameState& Game() const { return State; }
		/** The names the messages read, from the game's state. */
		FGameStateNames& Names() { return StateNames; }
		FPsxRandom& Random() { return Rand; }
		FKernelPad& Pad() { return KernelPad; }
		/** The controllers' motors, which the arena starts and every vertical blank runs. */
		FKernelVibration& Vibration() { return KernelVibration; }
		const FKernelVibration& Vibration() const { return KernelVibration; }
		FPlayClock& Clock() { return PlayClock; }
		FKernelState& Kernel() { return KernelGlobals; }
		const FKernelState& Kernel() const { return KernelGlobals; }

		/** Where the programs get the Menu they run inside themselves; none: a menu stops them, as the port does not run it yet. */
		IGameMenus* Menus() const { return GameMenus; }
		void SetMenus(IGameMenus* InMenus) { GameMenus = InMenus; }

		/** The port's own documents, which the field adds to the maps they are written for; none: nothing is added. It must outlive the session. */
		const FStoryPacks* Story() const { return StoryPacks; }
		void SetStory(const FStoryPacks* InStory) { StoryPacks = InStory; }

		/** The player's settings (their defaults until the host gives its own). */
		const FPlayerSettings& Settings() const { return PlayerSettings; }
		/** Counts the times the settings were set: a program reads its options again when it changes. */
		uint32 SettingsRevision() const { return SettingsSet; }
		/** Takes the player's settings: the sound follows them at once, the programs from their next frame. */
		WELTALLCORE_API void SetSettings(const FPlayerSettings& Settings);
		/** The player's framing of the field, which the field changes as the player moves the view. */
		FFieldCameraOffsets& CameraOffsets() { return PlayerSettings.Camera; }
		/** The player's framing of the world map, which the world map changes as the player moves the view. */
		FWorldMapCameraOffsets& WorldMapCameraOffsets() { return PlayerSettings.WorldMapCamera; }
		/** How fast battles are fought, which a battle changes as the player steps it. */
		EBattleSpeed& BattleSpeed() { return PlayerSettings.BattleSpeed; }
		/**
		 * The cases no script or data of the game reaches: the programs stop at them, as in the tests and the
		 * editor, until the host lets play carry past them (the player's builds), noting each for its log.
		 */
		FUnreachedCases& Unreached() { return UnreachedCases; }

		/** The story's chapters, and the title card of the one the story has moved into. */
		const FChapterTitles& Chapters() const { return ChapterList; }
		FChapterCard& ChapterCard() { return Card; }
		const FChapterCard& ChapterCard() const { return Card; }
		/** The chapter the game's state is in, 0 when the disc names none. */
		WELTALLCORE_API int32 Chapter() const;
		/**
		 * The subtitle (StorySubtitles.h) a game of the expanded second disc is under with its story at Story, known by
		 * its first value, when it lies in the chapter of Story; 0 for none.
		 */
		WELTALLCORE_API int32 SubtitleAt(uint16 Story) const;
		/** The subtitle the game's state is under, as SubtitleAt. */
		WELTALLCORE_API int32 Subtitle() const;
		/** The line of the card shown (ChapterCardDrawing.h): its chapter's, or its subtitle's; empty where none is. */
		WELTALLCORE_API std::string ChapterCardLine() const;

		/** The player's saves, which the Menu's File screen shows; none: the File screen cannot open. */
		FSaveStore* Saves() const { return SaveStore; }
		void SetSaves(FSaveStore* InSaves) { SaveStore = InSaves; }

		/** How the game's PlayStation buttons are shown right now; none: the host shows none. */
		const IButtonGlyphs* ButtonGlyphs() const { return Glyphs; }
		void SetButtonGlyphs(const IButtonGlyphs* InGlyphs) { Glyphs = InGlyphs; }

		/**
		 * One of the console's vertical blanks with Held down on the first controller: the play time counted, the pads
		 * read and queued, the play clock, the motors.
		 */
		WELTALLCORE_API void Vblank(FPadState Held);

		/**
		 * How many vertical blanks pass in the time one does on the console: 1, or a battle's speed while it is fought
		 * fast. Only one of every Pace vertical blanks is then the player's own time: the play time and the play clock
		 * count those alone, and the pads' repeat lasts as many times the vertical blanks.
		 */
		WELTALLCORE_API void SetPace(int32 InPace);

		/**
		 * The second controller's buttons at each of the next vertical blanks, the last of them at any after; until it
		 * is told, nothing is held on it.
		 */
		WELTALLCORE_API void SetSecondPad(std::span<const FPadState> HeldPerVblank);

		/**
		 * The kernel's new game: the new game's state loaded over the game's (the play time and what the
		 * last program left the next go on), and what a new game sets beside it.
		 */
		[[nodiscard]] WELTALLCORE_API std::optional<FError> InitNewGame();

	private:
		FGameDisc VoiceDisc(int32 Number) const;
		EVoiceLanguage SelectedVoiceLanguage() const;
		void FollowVoiceLanguage();
		FGameSession(const FCdImage& Image, const FGameFileIndex& Index, EDiscRelease Release, FGameSoundSession& Sound, FGteArctangents Arctangents,
			FMesResident Text, FFieldDrawingTables Tables, FFieldMovieCues Cues, std::unique_ptr<FBattleGameFiles> Battle);

		const FCdImage& DiscImage;
		const FGameFileIndex& FileIndex;
		EDiscRelease DiscRelease;
		/** Each disc the session was given, by number. */
		std::array<FGameDisc, 2> Discs{};
		std::array<std::array<FGameDisc, 2>, 2> VoiceDiscs{};
		FGameSoundSession& GameSound;
		FGteArctangents GteArctangents;
		FMesResident TextResources;
		FFieldDrawingTables DrawingTables;
		FFieldMovieCues MovieCues;
		std::unique_ptr<FBattleGameFiles> BattleData;
		std::unique_ptr<FBattlingGameFiles> BattlingData;
		FGameState State;
		FGameStateNames StateNames{State};
		FPsxRandom Rand;
		FKernelPad KernelPad;
		FKernelVibration KernelVibration;
		/** What the second controller holds at the coming vertical blanks, and how many of them have passed. */
		std::array<FPadState, 8> SecondPadHeld{};
		int32 SecondPadCount = 0;
		int32 SecondPadTaken = 0;
		FPlayClock PlayClock;
		int32 Pace = 1;
		/** The vertical blanks since the play time last counted one, fewer than Pace. */
		int32 PaceVblanks = 0;
		FKernelState KernelGlobals;
		IGameMenus* GameMenus = nullptr;
		const FStoryPacks* StoryPacks = nullptr;
		IGameDiscDrive* DiscDrive = nullptr;
		FPlayerSettings PlayerSettings;
		uint32 SettingsSet = 0;
		FUnreachedCases UnreachedCases;
		const IButtonGlyphs* Glyphs = nullptr;
		FSaveStore* SaveStore = nullptr;
		FChapterTitles ChapterList;
		FChapterCard Card;
	};
}
