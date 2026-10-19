#include "Battle/Show/BattleShowScreenWave.h"

namespace Weltall
{
	void FBattleScreenWave::Start()
	{
		Frames.Create(EBattleScreenMesh::ScreenWave);
		Passed = 0;
	}

	void FBattleScreenWave::Blank()
	{
		if (!IsRunning())
		{
			return;
		}
		++Passed;
		const int32 Frame = Passed / 2;
		if (Passed % 2 == 0 && Frame <= FrameCount)
		{
			Frames.Step(Frame & 1);
		}
	}
}
