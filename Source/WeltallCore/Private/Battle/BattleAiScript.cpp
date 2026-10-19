#include "Battle/BattleAi.h"

#include "Battle/BattleAiSteps.h"
#include "Battle/BattleContext.h"

namespace Weltall
{
	using BattleAiSteps::FWord;

	namespace
	{
		constexpr uint8 EndWord = 0xFD;
		constexpr uint8 StopWord = 0xFF;
		constexpr uint8 FirstCondition = 0x80;
		constexpr uint8 Always = 0x80;
		constexpr uint8 AlsoAlways = 0x9A;
		constexpr uint8 OrBlock = 0x99;
		/** The command whose running asks a combo to end. */
		constexpr uint8 EndComboCommand = 0x62;
		constexpr uint8 EmptyCode = 0xFF;
		constexpr uint16 ReactsWhileKnockedOut = 0x0800;

		enum EScript : std::size_t
		{
			TurnScript = 0,
			HitScript = 2,
			ReactionScript = 3,
		};

		/**
		 * Deus's first fight: the word of his turn script that queues his attack on one gear (command 11h, the queue's
		 * type 1 written). Perfect Works Build's scene sets its third byte, which keeps the battle camera for the attack;
		 * `bDeusKeepsCamera` reads the disc's word as if it did.
		 */
		constexpr uint8 DeusFirstScene = 64;
		constexpr uint32 DeusOneGearAttack = 0xC22;

		FWord AsRun(const FBattleContext& Context, uint32 Pc, FWord Word)
		{
			if (Context.RuleOptions.bDeusKeepsCamera && Context.Formation.Scene == DeusFirstScene && Pc == DeusOneGearAttack && Word.Op == 0x01
				&& Word.A == 0 && Word.B == 1)
			{
				Word.C = 1;
			}
			return Word;
		}

		/** The script's word at Pc; past the scene's file faults. */
		bool ReadWord(FBattleContext& Context, uint32 Pc, FWord& Word)
		{
			if (static_cast<std::size_t>(Pc) + 4 > Context.SceneFile.size())
			{
				Context.Fault = EBattleFault::ScriptOutsideScene;
				return false;
			}
			Word = {Context.SceneFile[Pc], Context.SceneFile[Pc + 1], Context.SceneFile[Pc + 2], Context.SceneFile[Pc + 3]};
			return true;
		}

		/**
		 * The conditions at Pc: a single word, or after an OR marker every word up to the block's first
		 * command, their truths or-ed. Words without a condition leave the last truth as it was.
		 */
		bool EvaluateCondition(FBattleContext& Context, uint8 Enemy, uint32& Pc)
		{
			bool bOr = false;
			bool bAny = false;
			bool bTruth = false;
			FWord Word;
			for (;;)
			{
				if (!ReadWord(Context, Pc, Word))
				{
					return false;
				}
				if (Word.Op == Always || Word.Op == AlsoAlways)
				{
					bTruth = true;
				}
				else if (Word.Op == OrBlock)
				{
					bOr = true;
				}
				else if (Word.Op <= 0x9B)
				{
					bTruth = BattleAiSteps::Condition(Context, Enemy, Word);
				}
				Pc += 4;
				if (!bOr)
				{
					return bTruth;
				}
				bAny = bAny || bTruth;
				if (!ReadWord(Context, Pc, Word))
				{
					return false;
				}
				if (Word.Op < FirstCondition)
				{
					return bAny;
				}
			}
		}

		/** A false block's skip: past its other conditions, then past its commands and every word from F0h up. */
		void SkipBlock(FBattleContext& Context, uint32& Pc)
		{
			FWord Word;
			while (ReadWord(Context, Pc, Word) && Word.Op >= FirstCondition)
			{
				Pc += 4;
			}
			while (Context.Fault == EBattleFault::None && ReadWord(Context, Pc, Word) && (Word.Op < FirstCondition || Word.Op >= 0xF0))
			{
				Pc += 4;
			}
		}

		/** Runs a script from Pc until an end word (or a fault); returns the queue's next entry. */
		uint8 RunScript(FBattleContext& Context, uint8 Enemy, uint32 Pc, uint8 Next, bool* bEndsCombo)
		{
			FWord Word;
			while (Context.Fault == EBattleFault::None && ReadWord(Context, Pc, Word) && Word.Op != EndWord && Word.Op != StopWord)
			{
				if (Word.Op < FirstCondition)
				{
					if (Word.Op == EndComboCommand && bEndsCombo != nullptr)
					{
						*bEndsCombo = true;
					}
					Next = BattleAiSteps::RunCommand(Context, Enemy, AsRun(Context, Pc, Word), Next);
					Pc += 4;
				}
				else if (!EvaluateCondition(Context, Enemy, Pc) && Context.Fault == EBattleFault::None)
				{
					SkipBlock(Context, Pc);
				}
			}
			return Next;
		}

		void ClearCodes(FBattleContext& Context)
		{
			for (FBattlePerformance& Performance : Context.Performances)
			{
				Performance.Code = EmptyCode;
			}
		}

		bool Reacts(const FBattleContext& Context, uint8 Slot)
		{
			const FBattleCombatant& Enemy = Context.Combatants[Slot];
			return (Enemy.Status & 0x8000) == 0 || (Enemy.Traits & ReactsWhileKnockedOut) != 0;
		}
	}

	void RunBattleTurnScript(FBattleContext& Context, uint8 Slot)
	{
		const uint8 Enemy = static_cast<uint8>(Slot - FBattleContext::PartySlotCount);
		const uint32 Pc = Context.AiRecords[Enemy].Scripts[TurnScript];
		Context.AiQueue.fill(0);
		ClearCodes(Context);
		RunScript(Context, Enemy, Pc, 0, nullptr);
	}

	bool RunBattleHitScript(FBattleContext& Context, uint8 Slot)
	{
		const uint8 Enemy = static_cast<uint8>(Slot - FBattleContext::PartySlotCount);
		bool bEndsCombo = false;
		if (!Reacts(Context, Slot))
		{
			return false;
		}
		Context.AiQueue[0] = 0;
		if (Context.HasHitScript[Enemy])
		{
			Context.AiQueue.fill(0);
			RunScript(Context, Enemy, Context.AiRecords[Enemy].Scripts[HitScript], 0, &bEndsCombo);
		}
		if (Context.Fault == EBattleFault::None && Context.AiQueue[0] != 0)
		{
			ExecuteBattleAiQueue(Context, Slot);
		}
		return bEndsCombo;
	}

	void BeginBattleReactions(FBattleContext& Context)
	{
		Context.ReactionQueueNext = 0;
		Context.ReactionTurnActor = Context.TurnActor;
	}

	bool RunBattleReactionScript(FBattleContext& Context, uint8 Enemy)
	{
		if (!Context.Targeted[Enemy] || !Context.HasReactionScript[Enemy])
		{
			return false;
		}
		const uint8 Slot = static_cast<uint8>(Enemy + FBattleContext::PartySlotCount);
		Context.TurnActor = Slot;
		if (!Reacts(Context, Slot))
		{
			return false;
		}
		ClearCodes(Context);
		Context.AiQueue.fill(0);
		Context.ReactionQueueNext = RunScript(Context, Enemy, Context.AiRecords[Enemy].Scripts[ReactionScript], Context.ReactionQueueNext, nullptr);
		return Context.Fault == EBattleFault::None;
	}

	void EndBattleReactions(FBattleContext& Context)
	{
		Context.TurnActor = Context.ReactionTurnActor;
	}
}
