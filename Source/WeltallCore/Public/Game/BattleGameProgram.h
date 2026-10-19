#pragma once

#include "Foundation/Result.h"
#include "Game/GameProgram.h"
#include "Input/PadState.h"
#include "Text/MesWindow.h"

#include <memory>
#include <optional>
#include <span>

namespace Weltall
{
	class FBattleGameHud;
	class FBattleGameSound;
	class FBattlePresentation;
	class FBattleRuntime;
	class FGameSession;
	class FGameState;
	class FMenuSpriteSheet;
	struct FTextureRegion;
	struct FBattleOutcome;
	struct FKernelState;

	/**
	 * What the Battle program leaves the kernel as it returns: what its event asked for, written where the event's
	 * instructions write it in the original (the chained battle and its kind, the movie, the field's fresh start, the
	 * field's return fade), then the program after it picked from its result.
	 */
	WELTALLCORE_API void EndBattleProgram(const FBattleOutcome& Outcome, FKernelState& Kernel, FGameState& Game);

	/**
	 * The Battle program as the kernel runs it: the battle the program before it left in the kernel's globals (its
	 * formation from the kernel's table, or the chained battle's; its kind; the gears' lock; the song playing), fought frame
	 * by frame with its show (drawn with the player's culling), the game's HUD and the session's sound, the frames lasting
	 * as the runtime says, and its end
	 * handed to the kernel, which picks the program after it. What the port does not play yet stops it with an error
	 * naming it. While it is fought, L1 and R1, which the battle reads nowhere, step the session's battle speed.
	 */
	class FBattleGameProgram final : public IGameProgram
	{
	public:
		/** The program started as the kernel runs it, up to its first frame's wait. The session must outlive it. */
		WELTALLCORE_API static TResult<std::unique_ptr<FBattleGameProgram>> Start(FGameSession& Session);
		WELTALLCORE_API ~FBattleGameProgram() override;
		FBattleGameProgram(const FBattleGameProgram&) = delete;
		FBattleGameProgram& operator=(const FBattleGameProgram&) = delete;

		WELTALLCORE_API int32 VblanksPerFrame() const override;
		[[nodiscard]] WELTALLCORE_API std::optional<FError> Frame(std::span<const FPadState> HeldPerVblank) override;
		EGameProgramState State() const override { return ProgramState; }

		/** The battle's rules and program, and its show, whose frames the display draws. */
		const FBattleRuntime& Runtime() const { return *BattleRuntime; }
		const FBattlePresentation& Presentation() const { return *Show; }
		/** The sheet the HUD's pictures are drawn from, which holds the game's own pictures of the buttons. */
		WELTALLCORE_API const FMenuSpriteSheet& HudSheet() const;
		/** The pictures the HUD draws from its sheet (FBattleHud::TextureRegions), for texture replacements. */
		WELTALLCORE_API std::span<const FTextureRegion> HudTextureRegions() const;

	private:
		explicit FBattleGameProgram(FGameSession& InSession);

		/** Why the battle stopped, when a fault of its rules, its show or its HUD stopped it. */
		FError StopError() const;
		/** A vertical blank's buttons: L1 or R1 newly pressed steps the battle speed while the battle is fought. */
		void StepSpeed(FPadState Held);

		FGameSession& Session;
		/** What the HUD's messages read besides their text. */
		FMesContext Text;
		std::unique_ptr<FBattleGameSound> Sound;
		std::unique_ptr<FBattleGameHud> Hud;
		std::unique_ptr<FBattlePresentation> Show;
		std::unique_ptr<FBattleRuntime> BattleRuntime;
		/** The buttons at the vertical blank before; before the first, every one, so a button held into the battle is no press. */
		FPadState HeldBefore{0xFFFF};
		EGameProgramState ProgramState = EGameProgramState::Running;
	};
}
