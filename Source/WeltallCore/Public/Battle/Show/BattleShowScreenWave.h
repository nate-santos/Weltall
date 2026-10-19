#pragma once

#include "Battle/Show/BattleShowTransition.h"

namespace Weltall
{
	/**
	 * The animation extension's screen wave (effect 218, Alkanshel's Mass Limit) while the battle's frame it runs in
	 * waits on it: the screen's picture as a mesh (EBattleScreenMesh::ScreenWave) for 164 frames of two vertical blanks
	 * of its own, then black until the battle's frame goes on. The screen is never cleared while it runs: each frame's
	 * triangles are averaged over what the draw buffer they go into held two frames before, both buffers holding the
	 * picture at first.
	 */
	class FBattleScreenWave
	{
	public:
		static constexpr int32 FrameCount = 164;
		/** Its vertical blanks: two a frame, then three waits of two. */
		static constexpr int32 Blanks = 2 * (FrameCount + 3);
		/** The screen is cleared to black once this many have passed. */
		static constexpr int32 BlackFrom = 2 * (FrameCount + 2);

		WELTALLCORE_API void Start();
		/** One of its vertical blanks: every second one ends a frame's wait, and the next frame's step follows. */
		WELTALLCORE_API void Blank();
		/** The frame it ran in goes on: its blanks have passed, or a host that does not show them leaves them out. */
		void Stop() { Passed = Blanks; }

		bool IsRunning() const { return Passed < Blanks; }
		int32 BlanksPassed() const { return Passed; }
		bool IsBlack() const { return Passed >= BlackFrom; }
		/** The mesh as its latest frame drew it: frame n steps once 2n blanks have passed, and is drawn into buffer n & 1. */
		const FBattleScreenMesh& Mesh() const { return Frames; }

	private:
		FBattleScreenMesh Frames;
		int32 Passed = Blanks;
	};
}
