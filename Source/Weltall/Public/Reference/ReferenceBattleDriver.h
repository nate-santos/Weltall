#pragma once

#include "CoreMinimal.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Input/PadState.h"

namespace Weltall { struct FBattleContext; }

namespace WeltallReference
{
	/** Optional Development reference input: save unused AP, then spend it through Combo.
	 * Reads the ordinary battle menus and emits pad holds only. Does not stage game state,
	 * change combat rules, select story choices, dismiss results or grant acceptance.
	 */
	class WELTALL_API FReferenceBattleDriver
	{
	public:
		bool Parse(const FString& Params);
		bool IsEnabled() const { return bEnabled; }
		Weltall::FPadState Step(const Weltall::FBattleContext* Context, double Seconds);

	private:
		bool bEnabled = false;
		double HeldFor = 0.0;
		double ReleasedFor = 0.0;
		Weltall::FPadState Held;
	};
}

#endif
