#pragma once

#include "Foundation/Types.h"

namespace Weltall
{
	/** Fixes of the menus' quirks: each is on unless the original's behavior is asked for (the gear model's pace, off). */
	struct FMenuOptions
	{
		/**
		 * The Status screen shows an agility over 16 as 16, as battle uses it; the original shows 17 to 20
		 * as they are and only a larger one as 16.
		 */
		bool bAgilityShownCapped = true;
		/**
		 * A Deathblow learned 100 % (and not yet learned) shows a full learning bar; the original's bar is
		 * measured after the hundreds are taken off, so it shows empty.
		 */
		bool bDeathblowBarWhole = true;
		/**
		 * Billy's ammunition keeps its shots: a load that still has some goes back to the inventory when
		 * another replaces it, and a load taken again goes on where it stopped; only a type with no shots
		 * left opens a fresh load. The original fills every new load and returns the one it replaces only
		 * while the new type's count happens to be full.
		 */
		bool bAmmunitionKept = true;
		/**
		 * An Ether heals as battle heals: with the Ether its equipment raises, doubled by Ether Doubler,
		 * its cost doubled likewise (and halved by Economether). The original heals with the base Ether.
		 */
		bool bEtherHealsAsBattle = true;
		/** Fix Frame HP asks whether the caster's own gear needs it; the original compares with party slot 0's gear. */
		bool bGearRepairOwnGear = true;
		/**
		 * A name confirmed in Enter Name loses only its trailing blanks, as the original means to; the original cuts every
		 * trailing byte 4Fh, a blank in the Japanese font and "s" in the American one ("Crescens" becomes "Crescen").
		 */
		bool bNamesKeepTrailingS = true;
		/** Member Change labels the EP "EP", as every other screen does; the original's panels say "MP". */
		bool bMemberChangeSaysEp = true;
		/** The shop's pointer at Buy, Sell and Exit sits beside their words, as on its Sell arc; the original's covers them. */
		bool bShopPointerBesideWords = true;
		/**
		 * The gear shop recomputes a gear's parts as the rest of the Menu does: the original's own copy
		 * leaves the parts' ether amplifier out of every gear it recomputes (until the Menu recomputes
		 * them) and clears other Deathblow bits.
		 */
		bool bGearShopPartsAsMenu = true;
		/**
		 * Selling equipment a character wears recomputes the character, as Status and Equip do; the
		 * original only empties the slot, so a sold accessory's bonuses last until one of them is opened.
		 */
		bool bSaleRecomputesEquipment = true;
		/**
		 * A new game's play time starts at 0; the original's goes on counting from power-on, so the boot
		 * notice, the opening movie and the time on the title are on its clock.
		 */
		bool bNewGameClockFromZero = true;
		/**
		 * The gear shop's model animates at the field's pace, a tick every two frames, not the original's
		 * three ticks every other frame. Off unless asked for: the pace players know is the original's.
		 */
		bool bGearModelFieldPace = false;

		/** The original's behavior throughout. */
		static FMenuOptions Original()
		{
			FMenuOptions Options;
			Options.bAgilityShownCapped = false;
			Options.bDeathblowBarWhole = false;
			Options.bAmmunitionKept = false;
			Options.bEtherHealsAsBattle = false;
			Options.bGearRepairOwnGear = false;
			Options.bNamesKeepTrailingS = false;
			Options.bMemberChangeSaysEp = false;
			Options.bShopPointerBesideWords = false;
			Options.bGearShopPartsAsMenu = false;
			Options.bSaleRecomputesEquipment = false;
			Options.bNewGameClockFromZero = false;
			Options.bGearModelFieldPace = false;
			return Options;
		}
	};
}
