#pragma once

#include "CoreMinimal.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Game/GameProgram.h"
#include "Input/PadState.h"

class FGameLoopScreen;

/**
 * -Route=x,z;x,z;!x,z;...: the way a render's party walks, a point after another, on the field or the world map,
 * whichever runs (whole units of its own: the field's map, the world's). The d-pad is held toward the next point,
 * turned as the running program turns the pad by its view, until the leader stands near it; a point written with a
 * leading ! is where the confirm button is pressed, as at a door or a place on the world map. Any point is passed once
 * the program or the field's map it was begun on changes (a way out leads on before its point is reached). While the
 * scripts hold the party the route waits on its point.
 * Random battles are kept off while the route is walked, they are no part of the way, unless -RouteBattles lets them
 * come as they would.
 * -RouteAt=S starts the route at simulated second S, leaving earlier scripted input (such as a normal save load) alone.
 */
class WELTALL_API FRenderRoute
{
public:
	/** Reads -Route, if given; false, with the reason logged, where it does not read as a route. */
	static bool Parse(const FString& Params, FRenderRoute& Route);

	/** The buttons the route holds this frame. */
	Weltall::FPadState Step(FGameLoopScreen& Game, double Elapsed);

private:
	struct FPoint
	{
		int32 X = 0;
		int32 Z = 0;
		bool bConfirms = false;
	};

	/** On to the next point. */
	void Pass();

	TArray<FPoint> Points;
	bool bBattles = false;
	double StartsAt = 0.0;
	int32 Next = 0;
	int32 Frame = 0;
	/** Where the point was begun: the program and the field's map, to know when the way has led elsewhere. */
	TOptional<Weltall::EGameProgram> PointProgram;
	int32 PointMap = -1;
};

#endif
