#pragma once

#include "Battle/BattlePartyState.h"
#include "Foundation/Types.h"

#include <span>

namespace Weltall
{
	class FVram;
	struct FBattleContext;
	struct FBattleEventCue;
	struct FBattleHudFrame;

	/**
	 * The sound driver as the show calls it. Sounds are named by the driver's number: a set's id in the
	 * high half, the effect in the low. The show is silent without one.
	 */
	class IBattleShowSound
	{
	public:
		virtual ~IBattleShowSound() = default;

		/** A sound effect played, a system effect played, an effect stopped, and an effect played on a voice. */
		virtual void PlayEffect(uint32 Sound) = 0;
		virtual void PlaySystemEffect(uint32 Sound) = 0;
		virtual void StopEffect(uint32 Sound) = 0;
		virtual void PlayEffectOn(uint32 Sound, int32 Voice) = 0;
		/** An effect played at a volume and a pan of its own. */
		virtual void PlayEffectAt(uint32 Sound, int32 Volume, int32 Pan) = 0;
		/** Whether the effect still sounds on some voice. */
		virtual bool IsEffectPlaying(uint32 Sound) = 0;
		/** A playing effect's volume at once, or over Steps. */
		virtual void SetEffectVolume(uint32 Sound, int32 Volume) = 0;
		virtual void FadeEffectVolume(uint32 Sound, int32 Volume, int32 Steps) = 0;
		/** The battle's music to Volume over Steps. */
		virtual void SetMusicVolume(int32 Volume, int32 Steps) = 0;
		/** A `seds` file's effects registered or unregistered under its set's id. */
		virtual void RegisterEffects(std::span<const uint8> Seds) = 0;
		virtual void UnregisterEffects(std::span<const uint8> Seds) = 0;
		/** Every playing effect stops. */
		virtual void StopAllEffects() = 0;
		/** Every playing effect of a `seds` file's set stops. */
		virtual void StopEffects(std::span<const uint8> Seds) = 0;
		/** Effects of the set's id are registered. */
		virtual bool HasEffects(uint16 Id) = 0;
		/**
		 * The kernel's gear battle bank leaves sound memory as the show begins: every bank the battle loads takes its
		 * room.
		 */
		virtual void UnloadGearBattleBank() = 0;
		/** The bank the battle loaded last (its own, the enemies', an effect's) unloaded, if one is loaded. */
		virtual void UnloadBattleBank() = 0;
		/** A `wds` bank into sound memory as the battle's, the one UnloadBattleBank gives up. */
		virtual void LoadBattleBank(std::span<const uint8> Wds) = 0;
		/** As LoadBattleBank, for a bank of recorded voices (one the voice track's disc gives), which the Voices volume plays. */
		virtual void LoadBattleVoiceBank(std::span<const uint8> Wds) = 0;
		/** The battle's end: its own bank loaded as the kernel's gear battle bank again, which the programs after it keep. */
		virtual void LoadGearBattleBank(std::span<const uint8> Wds) = 0;
		/** The driver holds a bank of the id already. */
		virtual bool HasBank(uint16 Id) = 0;
		/** Every voice stands still where it is, until it goes on from there. */
		virtual void PauseSound() = 0;
		virtual void ResumeSound() = 0;
	};

	/**
	 * The battle's HUD as the show and the program reach it: made at the setup's end, its message boxes after the opening's
	 * first frame, drawn each frame after the tasks and before the input step into the ordering table's entry 1, its
	 * counters stepped by the input step, and the boxes the rules and the playback type, show and hide.
	 */
	class IBattleShowHud
	{
	public:
		virtual ~IBattleShowHud() = default;

		/**
		 * The setup's HUD (its images, portraits, panels, labels and digits) and the gauges, for the battle set up,
		 * drawing into the show's VRAM.
		 */
		virtual void Begin(FBattleContext& Context, FVram& Vram) = 0;
		/** The message boxes, after the opening's first frame. */
		virtual void BeginMessageBoxes() = 0;
		/** A new frame: the draw buffer flips with the show's. */
		virtual void Flip() = 0;
		/** The HUD by the input mode, into Hud (emptied first); the battle's set is left out while a script hides the HUD. */
		virtual void Draw(FBattleHudFrame& Hud, bool bHidden) = 0;
		/** The input step's HUD counters, by the input mode (1 the battle's, 2 the messages', 0 the result screen's). */
		virtual void Tick(uint8 InputMode) = 0;
		/** An enemy's message: entry Entry of the scene's names typed into message box Box. */
		virtual void TypeMessage(uint8 Box, uint8 Entry) = 0;
		/** The action's name: the battle's message Message typed into box 7. */
		virtual void TypeActionName(uint8 Message) = 0;
		/** The acting enemy's name box: entry Entry of the scene's names in box 0, shown with its window. */
		virtual void ShowNameBox(uint8 Entry) = 0;
		/** A message box shown or hidden. */
		virtual void ShowMessageBox(int32 Box, bool bShown) = 0;
		/** The message boxes hidden: every box, and the windows behind them. */
		virtual void HideMessageBoxes() = 0;
		/** The party's menus as the turn's code asks the HUD to draw them (IBattleShow::PlayMenuCue). */
		virtual void PlayMenuCue(EBattleMenuCue Cue, uint8 Slot, uint8 Arg) = 0;
		/** A battle event's instruction that is the HUD's: a message's window and portrait, the gear cockpit opened and closed. */
		virtual void PlayEventCue(const FBattleEventCue& Cue) = 0;
		/** The HUD met what its game reads or writes outside what the port models: the battle stops. */
		virtual bool HasStopped() const = 0;
	};
}
