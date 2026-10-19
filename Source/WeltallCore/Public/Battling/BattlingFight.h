#pragma once

#include "Battling/BattlingLabels.h"
#include "Battling/BattlingData.h"
#include "Battling/BattlingEffects.h"
#include "Battling/BattlingFighter.h"
#include "Battling/BattlingFire.h"
#include "Battling/BattlingMenus.h"
#include "Battling/BattlingSound.h"
#include "Battling/BattlingTutorial.h"

#include <array>
#include <span>

namespace Weltall
{
	class FPsxRandom;

	/** The kinds of bout. */
	enum class EBattlingMode : uint8
	{
		None,
		VersusComputer,
		TwoPlayers,
		ComputerVersusComputer,
		/** No heat, nobody is knocked out; player 2 is the chosen practice style. */
		Practice,
		Tutorial,
		/** The menus' demonstration: both sides computer, no heat, nobody knocked out. */
		Demonstration,
	};

	/** Why a fight stopped where the port goes no further. */
	enum class EBattlingFault : uint8
	{
		None,
		/** A lookup past the heightfield's rows, where the program reads the heap beside it; no bout is known to reach one. */
		OutsideHeightfield,
		/** More letters printed in a pass than the labels' list holds, where the program writes past it. */
		LabelsOverflowed,
	};

	/** The computer's state of mind (each side keeps one). */
	enum class EBattlingAiMode : uint8
	{
		Wait,
		Attack,
		BackOff,
		Approach,
	};

	namespace BattlingAiFlags
	{
		/** Byte 0: a random byte drawn at each roll. */
		constexpr uint32 Random = 0xFFu;
		/** Rolled with trait A, three times in ten; nothing reads it. */
		constexpr uint32 Eager = 1u << 8;
		/** Rolled with trait A: fire at a downed opponent. */
		constexpr uint32 FiresAtDowned = 1u << 9;
		/** Rolled with trait B: longer runs while approaching, shorter ones while backing off. */
		constexpr uint32 Restless = 1u << 10;
		/** Rolled with trait C: no fire at a downed opponent. */
		constexpr uint32 Merciful = 1u << 11;
		/** A coin: attack rather than back off at close range; which side to dodge to. */
		constexpr uint32 Bold = 1u << 12;
		/** Practice's guard style: hold the guard. */
		constexpr uint32 Guard = 1u << 13;
		/** Byte 2: counts the bullets dodged since the last shot back. */
		constexpr uint32 DodgeCounter = 0xFFu << 16;
	}

	struct FBattlingAiState
	{
		int16 Timer = 0;
		EBattlingAiMode Mode = EBattlingAiMode::Wait;
		uint8 Counter = 0;
		/** The stick it pushes: a direction relative to the opponent (0 toward) and 0 or FFh. */
		int16 StickDirection = 0;
		int16 StickMagnitude = 0;
		bool bDash = false;
		/** 0 easy, 1 normal, 2 hard. */
		uint8 Level = 0;
		std::array<int32, 4> Traits{};
		int32 Phase = 0;
		/** The heat below which an Ether Bullet costs no HP. */
		int32 HeatLimit = 0;
		int32 KeepDistance = 0;
		uint32 Flags = 0;
		int16 HitsAtRoll = 0;
	};

	/** One pad as one vertical blank of the kernel's queue leaves it; the sticks are the latest reading. */
	struct FBattlingPad
	{
		/** The kernel's controller kind: 0 none, 1 digital, 2 mouse, 3 analog stick, 4 DualShock, -1 another. */
		int8 Type = 1;
		uint16 Held = 0;
		uint16 Pressed = 0;
		/** Pressed, and every held button again on the kernel's repeat beat. */
		uint16 Repeated = 0;
		uint8 LeftX = 0x80;
		uint8 LeftY = 0x80;
		/** The port's own: the directions among Held that only the stick holds, which a DualShock's stick never does (its bindings). */
		uint16 StickHeld = 0;
		/** The port's own: a controller of another kind whose motor the kernel's vibration runs, which gets the VIBRATION switch too. */
		bool bMotor = false;
	};

	/** What a fight tick reads of the pads: each vertical blank queued since the last tick. */
	struct FBattlingTickInput
	{
		std::span<const std::array<FBattlingPad, 2>> Blanks;
		bool bOverflowed = false;
	};

	/**
	 * Whose rules the fight plays by: the game's, or Perfect Works Build's arena's (its Basic mode, or its Expert mode, Basic's
	 * with tweaks for two players), which its gears and gear table are made for.
	 */
	enum class EBattlingRules : uint8
	{
		Original,
		PerfectWorksBasic,
		PerfectWorksExpert,
	};

	/** The bout's settings: the menus' or the field's. */
	struct FBattlingSettings
	{
		EBattlingRules Rules = EBattlingRules::Original;
		EBattlingMode Mode = EBattlingMode::VersusComputer;
		/** The bout was chosen in the arena's menus (the field's first byte was 1), not by the story: its set ends back in them. */
		bool bFromMenus = false;
		/** The story's last bout against Ganador, which ends in Weltall's breakdown. */
		bool bScripted = false;
		bool bRubberBand = false;
		/** The rounds a side must win to take the set; 0 none. */
		uint8 WinsPerSet = 0;
		/** 0 easy, 1 normal, 2 hard. */
		uint8 Level = 0;
		std::array<bool, 2> Computer{};
		std::array<bool, 2> Vibration{};
		/** The practice frame rate's setting: the original runs a fight tick every 1 + FrameSkip of them. */
		uint8 FrameSkip = 0;
		/** Scales the gears' animations and speeds (100h = 1). */
		int32 MotionSpeed = 0x100;
		uint8 PracticeStyle = 0;
		/** The computer's moves from afar picked by their own chain state (a fix; the original pairs each with the next). */
		bool bRangedMovesByOwnState = true;
		/** Practice's easy, normal and hard computer styles play at the level they name (a fix; the original plays all three at the menus' level). */
		bool bPracticeComputerAtStyleLevel = true;
		/**
		 * Practice's frame rates are the ones their labels name (a fix): each pass of the fight waits 2 + FrameSkip vertical
		 * blanks. The original skips whole ticks instead, running slower than the labels say.
		 */
		bool bPracticeFrameRateAsLabelled = true;
	};

	/** What practice shows of a side's current move. */
	struct FBattlingPracticeInfo
	{
		/** The chain state whose name is shown (0 none), or EtherShown. */
		static constexpr uint8 EtherShown = 0xFF;
		uint8 Move = 0;
		uint8 Power = 0;
		uint8 ShownMove = 0;
		int32 FirstFrame = 0;
		int32 LastFrame = 0;
	};

	/**
	 * A bout in the arena: the two fighters, the computer's minds, the round and the camera, the effects, as Battling's fight
	 * tick and the logic its drawing does keep them.
	 */
	class FBattlingFight
	{
	public:
		WELTALLCORE_API FBattlingFight(const FBattlingProgramTables& InTables, const FBattlingTerrain& InTerrain, FPsxRandom& InRandom,
			IBattlingSoundDriver& InDriver);

		/** Puts a gear on a side, as the program loads one: its model, its kind, its sound set, its counts of moves. */
		WELTALLCORE_API void LoadFighter(int32 Side, int32 Gear, const FBattlingGearFile& File, const FBattlingGearStats& Stats, FMdlModel Model);

		/** Starts a round: both fighters reset at their marks, the camera at a random start, the intro's count. */
		WELTALLCORE_API void StartRound();

		/**
		 * Makes the bout the tutorial, as the program does before it loads the gears: both sides out of the pads' reach, the
		 * lesson menu set up and the welcome begun. The text context and the messages must be set.
		 */
		WELTALLCORE_API void StartTutorial();

		/**
		 * The sizes of the hit sparks' tiles (2 or 3 pixels high, twice as wide), then of the splashes' (2 to 4 pixels each
		 * way), each drawing the session's random numbers: the program draws the sparks' as it sets up its settings, the
		 * splashes' as it loads the effects' pictures.
		 */
		WELTALLCORE_API void InitSparkTiles();
		WELTALLCORE_API void InitSplashTiles();

		/** Lights the fire behind the versus screen, as each set of bouts begins. */
		WELTALLCORE_API void LightFire();

		/** Puts the fire out: a bout that shows no versus screen starts without it. */
		WELTALLCORE_API void PutOutFire();

		/**
		 * The bout fades in through the fire still burning once the gears are loaded: the fire stepped at every other vertical
		 * blank, the song faster and the gears moving at twice their speed until it has faded.
		 */
		WELTALLCORE_API void StartFadeIn();

		/**
		 * One pass of the bout: the main loop's pass counter, the latched state's work (the fight, the replay, the result
		 * screen, the test between rounds and the next round's start, the winner screen), then what drawing the frame changes.
		 */
		WELTALLCORE_API void RunPass(const FBattlingTickInput& Input);

		/**
		 * Starts the arena's menus, as the program does at its start from them and after each set from them: the top menu open,
		 * the fire lit, the set's counts cleared, nothing chosen.
		 */
		WELTALLCORE_API void StartMenus();

		/**
		 * One pass of the arena's menus (one vertical blank, two once a bout is chosen): the idle countdown to the
		 * demonstration, the open menu or the gear choice, the fire stepped on odd passes. Its frame is the fire, the menus'
		 * letters and help lines, the carousels. A demonstration started takes the pass before it counts: its first pass of
		 * the bout runs in it.
		 */
		WELTALLCORE_API void RunMenusPass(const FBattlingTickInput& Input);

		/**
		 * A pass once the menus have chosen a bout, as the program runs it while the gears are read (two vertical blanks) and
		 * at each step of their load (bLoading, one): the fire stepped and the carousels sliding on, but for the tutorial, whose
		 * one pass only shows the fire. Its frame is the menus', the fire darkening the screen under it in the load's.
		 */
		WELTALLCORE_API void RunLoadPass(bool bLoading);

		/** The camera's view of the arena, as the frame's drawing builds it, for positions relative to the target. */
		WELTALLCORE_API FGte View() const;

		/** The display's aspect as the program's views take it: the axes' scales for its columns and lines. */
		WELTALLCORE_API FGteMatrix DisplayAspect() const;

		const FBattlingProgramTables& Tables;
		const FBattlingTerrain& Terrain;
		FPsxRandom& Random;
		FBattlingSoundChannels Sound;

		FBattlingSettings Settings;
		std::array<FBattlingFighter, 2> Fighters;
		std::array<FBattlingAiState, 2> Ai;
		std::array<FBattlingPracticeInfo, 2> Practice;
		FBattlingEffects Effects;

		EBattlingFault Fault = EBattlingFault::None;
		FBattlingMenus Menus;
		/** The tutorial's script machine, which also runs the scripted bout's ending. */
		FBattlingTutorial Tutorial;
		/** What the script machine's windows type with, and Battling's messages: needed by the tutorial and the scripted bout. */
		FMesContext* Text = nullptr;
		const FMessageTable* Messages = nullptr;
		/** Pad 1's pressed buttons as the kernel's word last holds them: the last vertical blank read, or the blanks merged. */
		uint16 PadPressed = 0;
		/** The program's animation kinds: its table, which the result screen rewrites for the victory animation. */
		std::array<FBattlingAnimationKind, FBattlingProgramTables::AnimationCount> AnimationKinds{};
		/** The main loop's pass counter, and the draw buffer it picks (its low bit). */
		uint32 Pass = 0;
		/** The kernel's count of vertical blanks, which tags the system sounds. */
		uint32 VBlank = 0;
		/** The last pass drew a frame of the bout (the fight, the replay, the result screen, the tutorial); the other passes leave the screen to their own screens. */
		bool bFrameDrawn = false;
		/** The last pass drew a frame of the winner screen: the winning gear alone, its shadow and its text. */
		bool bWinnerDrawn = false;
		/** The last pass was paused: its frame is the pause's layer (its menu, help lines and letters) over the paused picture. */
		bool bPauseDrawn = false;
		/** The last pass was one of the arena's menus': its frame is the fire with the menus' letters and help lines, and the carousels. */
		bool bMenusDrawn = false;
		/** The count of vertical blanks as the pass drew its frame, before its wait. */
		uint32 FrameVBlank = 0;
		/** What the program prints as it runs, and the letters the last frame drawn took of it. */
		FBattlingLabels Labels;
		std::vector<FBattlingLabelGlyph> FrameLabels;
		/** The vertical blanks the last pass waited for: two, one in the pause's menus. The next pass reads as many queued. */
		uint8 PassBlanks = 2;
		/**
		 * The screen state: 0 pause, 1 the fight, 2 between rounds, 3 leaving, 4 the replay, 6 the winner screen, 7 the tutorial,
		 * 8 the result screen; each pass runs the one latched as it starts.
		 */
		uint8 State = 1;
		uint8 LatchedState = 1;
		/** The vibrations this pass started: the pad and the vertical blanks each lasts, in order. */
		static constexpr int32 VibrationLimit = 8;
		std::array<std::array<int32, 2>, VibrationLimit> Vibrations{};
		int32 VibrationCount = 0;
		/** The versus screen's fire, how much of it still covers the bout (FFh as it begins, 4 less a pass), and whether the vertical blanks step it. */
		FBattlingFire Fire;
		int16 FireFade = 0;
		bool bFireActive = false;
		/**
		 * The fire as the pass drew it over the bout when it began, before its waits stepped it again: whether it did, how
		 * brightly (the fade), and the rows of its picture the screen shows.
		 */
		bool bFireDrawn = false;
		int16 FireDrawnFade = 0;
		std::array<uint8, FBattlingFire::Width * FBattlingFire::ShownRows> FireDrawn{};
		/** The scripted bout's ending runs instead of the camera. */
		bool bEndingActive = false;
		/** The practice style last set up (a new one resets the computer's stick and guard). */
		uint8 PracticeStyleSeen = 0;
		uint8 RecordIndex = 0;
		uint8 SkipCounter = 0;
		bool bFightStarted = false;
		int32 IntroCounter = 0;
		int32 RoundTicks = 0;
		int32 SetTime = 0;
		int32 Round = 0;
		/** Set at the knock-out; then the ticks counted to the replay. */
		bool bRoundOver = false;
		int32 RoundOverTicks = 0;
		/** 0 player 1, 1 player 2, 2 a draw. */
		uint8 Winner = 0;
		int32 Draws = 0;
		/** The pad the result screens take. */
		uint8 MenuPad = 0;
		/** The field's result code (FE C0): 0 none, 1-3 won, 81h-88h lost. */
		uint8 Result = 0;
		/** The lighting: 0 day, 1 sunset, 2 night. */
		uint8 Lighting = 0;
		/** The gears player 1 has beaten against the computer (a bit each, kept in the game state), and Argento's unlock. */
		std::array<uint8, 8> DefeatedGears{};
		bool bArgentoUnlocked = false;

		/** The fighters' distance (3D and across the ground), and the angle from player 2 to player 1, for the next tick. */
		int32 Distance = 0;
		int32 DistanceXZ = 0;
		int32 FightAngle = 0;
		/** 1 when the camera's left side is the nearer. */
		uint8 CameraSide = 0;
		int32 CameraSideAngle = 0;
		/** The eye's easing divisor (1/k of the way a tick). */
		uint8 CameraEase = 0;
		FBattlingPoint Eye{};
		FBattlingPoint Target{};
		/** The camera's distance from the target (the replay's orbit: C00h) and the tutorial's lift; the replay's orbit angle and speed. */
		int32 CameraRadius = 0;
		int32 CameraLift = 0;
		int32 OrbitAngle = 0;
		int32 OrbitSpeed = 0;
		/** The tutorial camera's height offset for the target. */
		int32 TargetLift = 0;
		/** The result screen's camera: the winner's gear's angle, the target's and the eye's heights, the distance. */
		int32 ResultAngle = 0;
		int32 ResultTargetHeight = 0;
		int32 ResultEyeHeight = 0;
		int32 ResultDistance = 0;
		/** The replay's ticks left (the result screen counts its own in it too). */
		int32 ReplayTicks = 0;
		/** Where the gears stood and their bases as the replay began: the result screen puts them back there. */
		std::array<FBattlingPoint, 2> ReplayPositions{};
		std::array<FBattlingPoint, 2> ReplayBases{};
		/** The winner screen: its gear's side, the text shown (odd), the pitch, the spin's speed, the camera's height and distance, the scale and the yaw. */
		int32 WinnerSide = 0;
		int32 WinnerText = 0;
		int32 WinnerPitch = 0;
		int32 WinnerSpin = 0;
		int32 WinnerCameraHeight = 0;
		int32 WinnerCameraDistance = 0;
		int32 WinnerScale = 0;
		int16 WinnerYaw = 0;
		/** The winner screen's confirm leaves at the next pass, as the program yields inside its end. */
		bool bWinnerEnding = false;
		int32 DisplayWidth = 320;
		int32 DisplayHeight = 218;
		uint16 ProjectionDistance = 0xC0;

		/** Counts the hit volumes set by the events of one call. */
		int32 VolumesSet = 0;
		/** Where the last segment test placed its contact (x and z). */
		std::array<int32, 2> Contact{};
	};
}
