#pragma once

#include "Battle/Show/BattleShowScreen.h"
#include "Foundation/Types.h"
#include "Graphics/Gte.h"
#include "Graphics/SpriteSystem.h"

namespace Weltall
{
	/**
	 * The screen's effects as the kernel's tasks: the fade, the shake and the tint fade run in the one
	 * update list with the sprites and the numbers, linked when they start and unlinked when they end,
	 * as BATTLE's tasks for them are. The fade takes itself out of the playback's count of the tasks
	 * made; the others count while the kernel counts.
	 */
	class FBattleFadeTask final : public IKernelTask
	{
	public:
		FSpriteSystem* Sprites = nullptr;
		FBattleFade Fade;

		/** A fade's start; bEffect for an effect's script's. */
		WELTALLCORE_API void Start(int32 Steps, int32 BlendMode, uint8 Red, uint8 Green, uint8 Blue, bool bEffect = false);
		void Update() override;
	};

	class FBattleShakeTask final : public IKernelTask
	{
	public:
		FSpriteSystem* Sprites = nullptr;
		FBattleShake Shake;

		/** A shake's start. */
		WELTALLCORE_API void Start(const FGteVector& Target, int32 Steps);
		void Update() override;
	};

	/** The stage a tint fade recolours: its tint begun as a fade begins anew and ended with the fade's task. */
	class IBattleStageTinting
	{
	public:
		virtual ~IBattleStageTinting() = default;
		virtual void BeginTint() = 0;
		virtual void EndTint() = 0;
	};

	class FBattleTintFadeTask final : public IKernelTask
	{
	public:
		FSpriteSystem* Sprites = nullptr;
		FBattleTintLayers* Layers = nullptr;
		/** None when the fade runs without a stage (the layers alone, as the sprites' oracle's harness makes them). */
		IBattleStageTinting* Stage = nullptr;
		FBattleTintFade Fade;

		/** A fade of the tint; whether it made the task anew. */
		WELTALLCORE_API bool Start(int16 Target, int32 Steps, int16 Mode, int16 Red, int16 Green, int16 Blue);
		void Update() override;

	private:
		/** The fade's end: the task out of the list, then the stage's tint ended. */
		void End();
	};
}
