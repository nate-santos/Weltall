#pragma once

#include "Battle/BattleContext.h"
#include "Game/GameState.h"
#include "Game/PsxRandom.h"
#include "Support/OracleDigest.h"
#include "Support/XorShift32.h"

namespace Weltall::Testing
{
	/**
	 * A random battle state drawn exactly as the oracles' generator draws one: every part of
	 * the context the rules read or change, the game state's block and rand()'s seed. The context is
	 * made afresh, with the original's rules (no fixes), and pointed at the given state; returns the seed.
	 */
	uint32 DrawBattleState(FXorShift32& G, FBattleContext& Context, FGameState& State);

	/**
	 * What the game's data guarantees and Perfect Works Build's Battles assume, drawn as the oracles' `--perfect-works`
	 * modes draw it: every combatant's ether ammunition among the copied weapons, those weapons' attacks, and with
	 * bWeakFuel each party gear's weak attack at 10 fuel.
	 */
	void DrawPerfectWorksData(FXorShift32& G, FBattleContext& Context, bool bWeakFuel);

	/** The state in the order battle_scenario.digest_state takes the original's memory. */
	void DigestBattleState(FOracleDigest& Digest, const FBattleContext& Context, const FGameState& State, const FPsxRandom& Random);
}
