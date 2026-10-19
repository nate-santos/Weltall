#pragma once

#include "Foundation/Types.h"

namespace Weltall
{
	/** The fixes of the battle rules' broken effects: each on unless the original's behavior is asked for. */
	struct FBattleRuleOptions
	{
		/**
		 * An undamaged gear's Infinity chance counts as a tenth of its HP lost, the least a damaged gear's
		 * can; the original counts the party slot plus one there, so the third slot's is three times the first's.
		 */
		bool bInfinityByDamageAlone = true;
		/**
		 * An item's buffs last by the item's own user and target (Ether Doubler, the Atk/Def trade-offs);
		 * the original reads the previous action's, which items never set.
		 */
		bool bItemBuffsByItsUser = true;
		/**
		 * Ether level 99 still rolls its EP growth, the level's remaining distance divided by 1; the
		 * original divides by zero there and never grows.
		 */
		bool bEpGrowsAtLastLevel = true;
		/**
		 * The ether buffs other than the Atk/Def pairs (Eth effect UP, Eth Def UP, Counter UP, the
		 * party-wide ether) are set as they are; the original runs them through the element modes' checks,
		 * so a permanent element refuses them and a temporary one is lost.
		 */
		bool bEtherBuffsSetDirectly = true;
		/**
		 * Atk UP, ACC UP and EV UP last their power's turns, as Speed UP and Def UP do; the original gives
		 * them no count, so they last the battle.
		 */
		bool bEveryBuffExpires = true;
		/** Def UP raises defense on foot by half; the original tests a bit nothing sets. */
		bool bDefUpOnFoot = true;
		/**
		 * Def DOWN (enemy command 33) lowers defense on foot by a third, the mirror of Def UP, and shows its message; the
		 * original sets a status nothing reads and shows no message.
		 */
		bool bDefDownOnFoot = true;
		/**
		 * A status move that misses or is blocked leaves the target's count of that status alone, as the weapons' statuses
		 * do; the original restarts the count before it looks at the outcome. A move that lands restarts it either way.
		 */
		bool bFailedStatusKeepsCount = true;
		/** Igissol and Aegissol give their immunities; the original tests effect bits no item carries. */
		bool bImmunityItemsWork = true;
		/**
		 * A gear's element change ends when its count runs out; the original counts it against the gear's
		 * statuses, so the change stays and the count clears other statuses instead.
		 */
		bool bGearElementsExpire = true;
		/** A stopped gear stops being hit for sure when its Stop ends; in the original that lasts the battle. */
		bool bGearStopEnds = true;
		/**
		 * A hidden enemy that shows itself keeps its place in its group; the original takes it out, and its
		 * later fall takes it out again, so the group can end up counted as full.
		 */
		bool bRevealKeepsGroup = true;
		/**
		 * PWR loss on a gear (enemy command 71) takes 2 × the gear's Output off its attack, the mirror of PWR UP, until a
		 * cure or the battle's end; the original sets a status nothing reads.
		 */
		bool bGearPowerLoss = true;
		/**
		 * A gear's HP takes an amount whole (Chu-Chu's giant form under "HP − 1" or "HP / 2", a big heal); the original
		 * applies it from a 16-bit record, so an amount over 65,535 wraps.
		 */
		bool bWholeAmounts = true;
		/**
		 * A counter during an attack on several targets spares only the counterer: the attack goes on to the rest, and
		 * the counter strikes only the attacker. The original goes on from the attacker's slot with the counterattack,
		 * so it spares the rest of the party and strikes the enemies after the attacker, dead ones too.
		 */
		bool bCounterKeepsGroupAttack = true;
		/**
		 * The last boss's second form starts with the 74,992 HP its script sets on its first turn; the script names no
		 * combatant there, so the original writes the HP into the party's command table and the form keeps its
		 * record's 70,000.
		 */
		bool bDeusSecondFormHp = true;
		/**
		 * Billy's ether guns fire the element of the ammunition loaded in them; the original reads it from his record's gun
		 * slots, which only the battle's unreachable reload writes, so they stay the new game's wind and fire.
		 */
		bool bEtherAmmunitionElement = true;
		/**
		 * Deus's attack on one gear in his first fight (scene 64, command 11h) leaves the camera to the effect's framing.
		 * The original's sequence hands the camera to the model camera, which holds it still, before the effect has
		 * finished framing its target: from a view far enough away the effect waits for the camera forever.
		 */
		bool bDeusKeepsCamera = true;

		/** Every fix off: the original's behavior throughout. */
		static constexpr FBattleRuleOptions Original()
		{
			FBattleRuleOptions Options;
			Options.bInfinityByDamageAlone = false;
			Options.bItemBuffsByItsUser = false;
			Options.bEpGrowsAtLastLevel = false;
			Options.bEtherBuffsSetDirectly = false;
			Options.bEveryBuffExpires = false;
			Options.bDefUpOnFoot = false;
			Options.bDefDownOnFoot = false;
			Options.bFailedStatusKeepsCount = false;
			Options.bImmunityItemsWork = false;
			Options.bGearElementsExpire = false;
			Options.bGearStopEnds = false;
			Options.bRevealKeepsGroup = false;
			Options.bGearPowerLoss = false;
			Options.bWholeAmounts = false;
			Options.bCounterKeepsGroupAttack = false;
			Options.bDeusSecondFormHp = false;
			Options.bEtherAmmunitionElement = false;
			Options.bDeusKeepsCamera = false;
			return Options;
		}
	};
}
