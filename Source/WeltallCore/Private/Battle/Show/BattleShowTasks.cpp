#include "Battle/Show/BattleShowTasks.h"

namespace Weltall
{
	void FBattleFadeTask::Start(int32 Steps, int32 BlendMode, uint8 Red, uint8 Green, uint8 Blue, bool bEffect)
	{
		if (Fade.bBlocked)
		{
			return;
		}
		if (!Fade.bOn)
		{
			// The fade's task never counts among the tasks the playback waits for.
			Sprites->LinkTask(*this);
			Sprites->Uncount(*this);
		}
		Fade.Start(Steps, BlendMode, Red, Green, Blue, bEffect);
		if (!Fade.bOn)
		{
			Sprites->UnlinkTask(*this);
		}
	}

	void FBattleFadeTask::Update()
	{
		Fade.Step();
		if (!Fade.bOn)
		{
			Sprites->UnlinkTask(*this);
		}
	}

	void FBattleShakeTask::Start(const FGteVector& Target, int32 Steps)
	{
		if (!Shake.bOn)
		{
			Sprites->LinkTask(*this);
		}
		Shake.Start(Target, Steps);
		if (!Shake.bOn)
		{
			Sprites->UnlinkTask(*this);
		}
	}

	void FBattleShakeTask::Update()
	{
		Shake.Step();
		if (!Shake.bOn)
		{
			Sprites->UnlinkTask(*this);
		}
	}

	bool FBattleTintFadeTask::Start(int16 Target, int32 Steps, int16 Mode, int16 Red, int16 Green, int16 Blue)
	{
		if (!Fade.bOn)
		{
			Sprites->LinkTask(*this);
		}
		const bool bNew = Fade.Start(*Layers, Target, Steps, Mode, Red, Green, Blue);
		if (bNew && Stage != nullptr)
		{
			Stage->BeginTint();
		}
		if (!Fade.bOn)
		{
			End();
		}
		return bNew;
	}

	void FBattleTintFadeTask::Update()
	{
		if (!Fade.Step())
		{
			End();
		}
	}

	void FBattleTintFadeTask::End()
	{
		Sprites->UnlinkTask(*this);
		if (Stage != nullptr)
		{
			Stage->EndTint();
		}
	}
}
