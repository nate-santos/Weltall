#include "BattlingFightOps.h"
#include "BattlingMath.h"

namespace Weltall::BattlingFightOps
{
	namespace
	{
		using namespace BattlingMath;

		enum : uint8
		{
			CommandAttackA = 1,
			CommandEther = 3,
			CommandJump = 4,
		};

		constexpr int32 TraitA = 0;
		constexpr int32 TraitB = 1;
		constexpr int32 TraitC = 2;
		constexpr int32 TraitD = 3;

		int32 RandomByte(FBattlingFight& Fight)
		{
			return Fight.Random.Next() & 0xFF;
		}

		bool CanFire(const FBattlingFighter& Fighter, bool bAllowOverheat)
		{
			return bAllowOverheat ? HeatCanAdd(Fighter, Fighter.EtherHeat) : Fighter.Heat < 0x1000 - Fighter.EtherHeat;
		}

		/** 2 well below the heat an Ether Bullet would overheat at, 1 below it, 0 above. */
		int32 HeatLevel(const FBattlingFighter& Fighter, const FBattlingAiState& Ai)
		{
			if (Fighter.Heat <= Ai.HeatLimit - 0x200)
			{
				return 2;
			}
			return Fighter.Heat <= Ai.HeatLimit ? 1 : 0;
		}

		void Reroll(FBattlingFight& Fight, FBattlingAiState& Ai, const FBattlingFighter& Fighter)
		{
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::FiresAtDowned) | (RandomByte(Fight) < Ai.Traits[TraitA] ? BattlingAiFlags::FiresAtDowned : 0);
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::Restless) | (RandomByte(Fight) < Ai.Traits[TraitB] ? BattlingAiFlags::Restless : 0);
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::Bold) | ((Fight.Random.Next() & 1) != 0 ? BattlingAiFlags::Bold : 0);
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::Merciful) | (RandomByte(Fight) < Ai.Traits[TraitC] ? BattlingAiFlags::Merciful : 0);
			bool bEager = false;
			if (RandomByte(Fight) < Ai.Traits[TraitA])
			{
				bEager = Fight.Random.Next() % 10 < 3;
			}
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::Eager) | (bEager ? BattlingAiFlags::Eager : 0);
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::Random) | static_cast<uint8>(Fight.Random.Next());
			Ai.HitsAtRoll = Fighter.HitsTaken;
		}

		bool WantsToFire(FBattlingFight& Fight, FBattlingFighter& Fighter)
		{
			const FBattlingAiState& Ai = AiOf(Fight, Fighter);
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			if (RandomByte(Fight) < (Ai.Traits[TraitA] * 0x140 >> 4))
			{
				if (!CanFire(Fighter, false) && !HpAbove(Fighter.Hp, Fighter.MaxHp, 0xC0) && (Ai.Traits[TraitD] * 0xC0 >> 4) <= RandomByte(Fight))
				{
					if (!HpAbove(Fighter.Hp, Fighter.MaxHp, 0x80) || Fighter.Hp <= Other.Hp)
					{
						return false;
					}
				}
			}
			else if ((Ai.Traits[TraitD] * 0x140 >> 4) <= RandomByte(Fight))
			{
				return false;
			}
			if (RandomByte(Fight) < Ai.Traits[TraitC])
			{
				if (Other.Movement == BattlingMovement::Down)
				{
					return false;
				}
				if (HpAbove(Other.Hp, Other.MaxHp, 0x20))
				{
					return true;
				}
				if (Other.Hp < Fighter.Hp)
				{
					return false;
				}
			}
			return true;
		}

		bool WantsDash(FBattlingFight& Fight, FBattlingFighter& Fighter, bool bApproaching)
		{
			const FBattlingAiState& Ai = AiOf(Fight, Fighter);
			if (bApproaching && RandomByte(Fight) < Ai.Traits[TraitA] && Fight.DistanceXZ <= 0x5FF)
			{
				return false;
			}
			if (HeatLevel(Fighter, Ai) == 0)
			{
				return RandomByte(Fight) < Ai.Traits[TraitD];
			}
			return true;
		}

		void Fire(FBattlingFight& Fight, FBattlingFighter& Fighter)
		{
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			if (Other.Ground() == EBattlingGround::Water && (Fight.Random.Next() & 3) != 0)
			{
				return;
			}
			if (Fight.Settings.bRubberBand)
			{
				BackDashTry(Fight, Fighter);
				QueuePush(Fighter, CommandJump);
			}
			else if (Fighter.Ground() == EBattlingGround::Water)
			{
				QueuePush(Fighter, CommandJump);
			}
			QueuePush(Fighter, CommandEther);
		}

		void Guard(FBattlingFighter& Fighter, const FBattlingAiState& Ai)
		{
			const uint8 Random = static_cast<uint8>(Ai.Flags & BattlingAiFlags::Random);
			if ((Ai.Level == 0 && Random % 6 != 0) || (Ai.Level == 1 && (Random & 1) == 0))
			{
				return;
			}
			Fighter.Flags |= BattlingFlags::GuardInput;
			QueueClear(Fighter);
		}

		void Combo(FBattlingFight& Fight, FBattlingFighter& Fighter)
		{
			const int32 Roll = Fight.Random.Next() % 10;
			int32 Count = 1;
			if (1 < Roll)
			{
				Count = 2;
			}
			if (4 < Roll)
			{
				++Count;
			}
			for (; Count != 0; --Count)
			{
				QueuePush(Fighter, static_cast<uint8>((Fight.Random.Next() & 1) + CommandAttackA));
			}
		}

		/** The ticks until the computer acts again after an attack: Step ticks a level below hard, 1-20 more. */
		int16 AttackTimer(FBattlingFight& Fight, const FBattlingAiState& Ai, int32 Step = 0x14)
		{
			return static_cast<int16>((2 - Ai.Level) * Step + Fight.Random.Next() % 0x14 + 1);
		}

		void AttackStep(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (Opponent(Fight, Fighter).Movement == BattlingMovement::Down)
			{
				if ((Ai.Flags & (BattlingAiFlags::FiresAtDowned | BattlingAiFlags::Merciful)) == BattlingAiFlags::FiresAtDowned)
				{
					Fire(Fight, Fighter);
					Ai.Timer = 3;
				}
			}
			else
			{
				Combo(Fight, Fighter);
				// Perfect Works' arena's computer attacks again sooner after a combo: 10 ticks a level, not 20.
				Ai.Timer = AttackTimer(Fight, Ai, ArenaRules(Fight) ? 10 : 0x14);
			}
			Reroll(Fight, Ai, Fighter);
		}

		/** Whether chain state State's move may be used from afar, by the program's pairing or the fix's. */
		bool FromAfar(const FBattlingFight& Fight, const FBattlingFighter& Fighter, int32 Index)
		{
			if (Fight.Settings.bRangedMovesByOwnState)
			{
				return Fighter.Stats->Strengths[static_cast<std::size_t>(Index)] != 0 && Fighter.File->Moves[static_cast<std::size_t>(Index + 1)].FromAfar != 0;
			}
			return Fighter.Stats->Strengths[static_cast<std::size_t>(Index)] != 0 && Fighter.File->Moves[static_cast<std::size_t>(Index)].FromAfar != 0;
		}

		/** A move usable from afar, picked at random, its chain's inputs queued; false when the gear has none. */
		bool RangedMove(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (Fighter.FarMoves == 0)
			{
				return false;
			}
			int32 Pick = Fight.Random.Next() % Fighter.FarMoves;
			for (int32 Index = 0; Index < BattlingGears::Moves; ++Index)
			{
				if (!FromAfar(Fight, Fighter, Index))
				{
					continue;
				}
				if (Pick == 0)
				{
					const int32 State = Fight.Settings.bRangedMovesByOwnState ? Index + 1 : Index;
					for (const uint8 Input : Fight.Tables.ChainInputs[static_cast<std::size_t>(State)])
					{
						if (Input != 0)
						{
							QueuePush(Fighter, Input);
						}
					}
					break;
				}
				--Pick;
			}
			Ai.Timer = AttackTimer(Fight, Ai);
			Reroll(Fight, Ai, Fighter);
			return true;
		}

		void EnterWait(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			Ai.Phase = 0;
			Ai.Mode = EBattlingAiMode::Wait;
			// Perfect Works' arena's computer waits (2 - level) x 5 + 25 ticks.
			Ai.Timer = static_cast<int16>(ArenaRules(Fight) ? (2 - Ai.Level) * 5 + 25 : (2 - Ai.Level) * 0x1E + 0x5A);
			Reroll(Fight, Ai, Fighter);
		}

		void EnterAttack(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			Ai.Mode = EBattlingAiMode::Attack;
			AttackStep(Fight, Fighter, Ai);
			const int32 Range = Ai.Level >= 2 ? 8 : (Ai.Level == 1 ? 6 : 4);
			Ai.Counter = static_cast<uint8>(Fight.Random.Next() % Range + 1);
			Ai.StickMagnitude = 0;
			Ai.bDash = false;
			Reroll(Fight, Ai, Fighter);
		}

		void EnterApproach(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			Ai.Mode = EBattlingAiMode::Approach;
			Ai.Counter = static_cast<uint8>(Fight.Random.Next() % 4 + 1);
			Ai.Timer = 0;
			Reroll(Fight, Ai, Fighter);
			Ai.bDash = WantsDash(Fight, Fighter, true);
			Ai.Flags &= ~BattlingAiFlags::DodgeCounter;
		}

		void EnterBackOff(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			Ai.Mode = EBattlingAiMode::BackOff;
			if (Fight.Random.Next() % 3 == 0)
			{
				BackDashTry(Fight, Fighter);
			}
			const int32 Keep = Fight.Random.Next();
			Ai.Timer = 0;
			Ai.KeepDistance = Keep % 0x600 + 0x100;
			Ai.Counter = static_cast<uint8>(Fight.Random.Next() % 5 + 3);
			Reroll(Fight, Ai, Fighter);
			Ai.bDash = WantsDash(Fight, Fighter, false);
			Ai.Flags &= ~BattlingAiFlags::DodgeCounter;
		}

		/** Fire, or a ranged move when close enough; true when it did neither. */
		bool Approach(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (Fighter.FarMoves == 0)
			{
				if (WantsToFire(Fight, Fighter))
				{
					Fire(Fight, Fighter);
					return false;
				}
				return true;
			}
			if ((Fight.Random.Next() & 1) == 0)
			{
				return true;
			}
			if ((Fight.Random.Next() & 1) != 0 && WantsToFire(Fight, Fighter))
			{
				Fire(Fight, Fighter);
				return false;
			}
			if (Fight.DistanceXZ < 0x1001)
			{
				RangedMove(Fight, Fighter, Ai);
				return false;
			}
			return true;
		}

		void FaceOutOfLake(FBattlingFight& Fight, const FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			const uint32 Bearing = static_cast<uint32>(GteRatan2(Fighter.Position[0] - 0x3F80, Fighter.Position[2] - 0x3F80, Fight.Tables.Arctangents));
			Ai.StickDirection = static_cast<int16>(((Bearing & 0xFFF) > 0x200 ? 0x800 : 0xC00) - Fight.FightAngle);
			Ai.StickMagnitude = 0xFF;
		}

		/** The scripted bout's player 2 stays out of the lake and out of Fei's way while he is in it; true when that is all it does. */
		bool StayOutOfLake(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (!Fight.Settings.bScripted || (Fighter.Flags & BattlingFlags::Side) == 0)
			{
				return false;
			}
			if (InLakeZone(Fight, Fighter.Position))
			{
				FaceOutOfLake(Fight, Fighter, Ai);
				return true;
			}
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			if (!InLakeZone(Fight, Other.Position))
			{
				return false;
			}
			if (Other.NearestDistance < 0x800)
			{
				Fighter.Flags |= BattlingFlags::DashInput;
				Ai.bDash = true;
				Ai.StickMagnitude = 0xFF;
				Ai.StickDirection = 0x800;
				Fighter.WantedDirection = 0x800;
				return true;
			}
			if (Other.NearestDistance < 0x1001)
			{
				return true;
			}
			Fighter.Flags &= ~BattlingFlags::DashInput;
			Ai.bDash = false;
			Ai.StickMagnitude = 0;
			Fighter.WantedDirection = 0x800;
			return true;
		}

		/** Whether the opponent's nearest projectile is an Ether Bullet within Reach. */
		bool BulletWithin(const FBattlingFighter& Other, int32 Reach)
		{
			return Other.NearestProjectile >= 0 && Other.Projectiles[static_cast<std::size_t>(Other.NearestProjectile)].SpeedMode == 1
				&& Other.NearestDistance < Reach;
		}

		void WaitTick(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			Ai.StickMagnitude = 0;
			Ai.bDash = false;
			Ai.StickDirection = 0;
			if (StayOutOfLake(Fight, Fighter, Ai))
			{
				return;
			}
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			if (Fight.DistanceXZ < 0x200)
			{
				if ((Ai.Flags & BattlingAiFlags::Bold) != 0)
				{
					EnterAttack(Fight, Fighter, Ai);
				}
				else if (Ai.Level != 0)
				{
					EnterBackOff(Fight, Fighter, Ai);
				}
				if (Other.Action == BattlingAction::Attacking)
				{
					Guard(Fighter, Ai);
				}
				if (Fighter.HitsTaken + 2 < Ai.HitsAtRoll)
				{
					EnterAttack(Fight, Fighter, Ai);
				}
			}
			if (Other.NearestDistance < 0x400)
			{
				EnterBackOff(Fight, Fighter, Ai);
				Ai.bDash = true;
			}
			if (Ai.Phase == 0)
			{
				Ai.Timer = static_cast<int16>(Ai.Timer - 1);
				if (Ai.Timer < 0)
				{
					++Ai.Phase;
				}
				if (Other.Movement != 0 || (Ai.Flags & BattlingAiFlags::Bold) == 0)
				{
					return;
				}
				if (WantsToFire(Fight, Fighter))
				{
					Fire(Fight, Fighter);
				}
			}
			else if (Ai.Phase != 1 || !CanFire(Fighter, false))
			{
				return;
			}
			EnterApproach(Fight, Fighter, Ai);
		}

		void AttackTick(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (StayOutOfLake(Fight, Fighter, Ai))
			{
				return;
			}
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			if (Fighter.Action != BattlingAction::Attacking && Other.Action == BattlingAction::Attacking)
			{
				Guard(Fighter, Ai);
			}
			if (static_cast<uint32>(Other.NearestDistance - 0x201) < 0x3FF && Ai.Level != 0)
			{
				Guard(Fighter, Ai);
			}
			Ai.Timer = static_cast<int16>(Ai.Timer - 1);
			if (0 < Ai.Timer)
			{
				return;
			}
			if (0x300 < Fight.DistanceXZ)
			{
				bool bWait = false;
				switch (Fight.Random.Next() % 10)
				{
				case 0:
					if (Ai.Traits[TraitB] <= RandomByte(Fight))
					{
						EnterBackOff(Fight, Fighter, Ai);
					}
					break;
				case 1:
					bWait = true;
					break;
				case 2:
					bWait = !RangedMove(Fight, Fighter, Ai);
					break;
				case 3:
				case 4:
				case 5:
					bWait = Approach(Fight, Fighter, Ai);
					break;
				default:
					EnterApproach(Fight, Fighter, Ai);
					break;
				}
				if (bWait)
				{
					EnterWait(Fight, Fighter, Ai);
				}
			}
			const uint8 Counter = Ai.Counter;
			Ai.Counter = static_cast<uint8>(Counter - 1);
			if (Counter == 0)
			{
				const bool bCoin = (Fight.Random.Next() & 1) != 0;
				static_cast<void>(bCoin);
				EnterBackOff(Fight, Fighter, Ai);
			}
			else
			{
				AttackStep(Fight, Fighter, Ai);
			}
		}

		/** Dodging the opponent's nearby Ether Bullet sideways, firing back now and then; shared by backing off and approaching. */
		void DodgeBullets(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai, bool bApproaching)
		{
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			if (!BulletWithin(Other, 0x500) || Ai.Level == 0)
			{
				return;
			}
			Ai.bDash = true;
			Ai.StickDirection = static_cast<int16>((Ai.Flags & BattlingAiFlags::Bold) != 0 ? 0x400 : -0x400);
			const bool bShoot = 1 < Ai.Level && (!bApproaching || RandomByte(Fight) < Ai.Traits[TraitB]) && WantsToFire(Fight, Fighter)
				&& (Ai.Flags & BattlingAiFlags::Bold) != 0;
			if (bShoot)
			{
				if ((Fight.Random.Next() & 3) == 0)
				{
					QueuePush(Fighter, CommandJump);
				}
				Fire(Fight, Fighter);
				Ai.Flags &= ~BattlingAiFlags::DodgeCounter;
			}
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::DodgeCounter) | ((Ai.Flags + 0x10000) & BattlingAiFlags::DodgeCounter);
		}

		void ApproachTick(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (InLakeZone(Fight, Opponent(Fight, Fighter).Position) && Fight.Settings.bScripted && (Fighter.Flags & BattlingFlags::Side) != 0)
			{
				EnterWait(Fight, Fighter, Ai);
				return;
			}
			DodgeBullets(Fight, Fighter, Ai, true);
			if (Fight.DistanceXZ < 0x180)
			{
				if ((Fight.Random.Next() & 3) == 0)
				{
					QueuePush(Fighter, CommandJump);
				}
				EnterAttack(Fight, Fighter, Ai);
			}
			if (HeatLevel(Fighter, Ai) == 0)
			{
				Ai.bDash = false;
			}
			if (Ai.Timer < 0)
			{
				Ai.StickDirection = static_cast<int16>(Fight.Random.Next() % 0x600 - 0x300);
				const int32 Run = Fight.Random.Next();
				Ai.Timer = static_cast<int16>(Run % ((Ai.Flags & BattlingAiFlags::Restless) != 0 ? 0x78 : 100) + 10);
				Ai.StickMagnitude = 0xFF;
				if (Ai.Counter == 0)
				{
					if ((Fight.Random.Next() & 3) == 0)
					{
						QueuePush(Fighter, CommandJump);
					}
					if (Approach(Fight, Fighter, Ai))
					{
						EnterWait(Fight, Fighter, Ai);
					}
					Ai.Counter = static_cast<uint8>(Fight.Random.Next() % 4 + 1);
				}
				else
				{
					--Ai.Counter;
				}
			}
			Ai.Timer = static_cast<int16>(Ai.Timer - 1);
		}

		void BackOffTick(FBattlingFight& Fight, FBattlingFighter& Fighter, FBattlingAiState& Ai)
		{
			if (StayOutOfLake(Fight, Fighter, Ai))
			{
				return;
			}
			DodgeBullets(Fight, Fighter, Ai, false);
			if (HeatLevel(Fighter, Ai) == 0)
			{
				Ai.bDash = false;
			}
			if (Ai.KeepDistance < Fight.DistanceXZ || (Fight.Settings.bRubberBand && 0x4B0 < Fight.Distance))
			{
				if (Fight.Settings.bRubberBand)
				{
					QueuePush(Fighter, CommandJump);
				}
				RangedMove(Fight, Fighter, Ai);
				EnterWait(Fight, Fighter, Ai);
			}
			if (Ai.Timer < 0 || Ai.StickMagnitude == 0)
			{
				Ai.StickDirection = static_cast<int16>(Fight.Random.Next() % 0x600 + 0x500);
				const int32 Run = Fight.Random.Next();
				Ai.Timer = static_cast<int16>(Run % ((Ai.Flags & BattlingAiFlags::Restless) != 0 ? 0x28 : 0x3C) + 10);
				Ai.StickMagnitude = 0xFF;
				if (Ai.Counter == 0)
				{
					if (WantsToFire(Fight, Fighter))
					{
						QueuePush(Fighter, CommandJump);
						Fire(Fight, Fighter);
					}
					Ai.Counter = static_cast<uint8>(Fight.Random.Next() % 5 + 3);
				}
				else
				{
					--Ai.Counter;
				}
			}
			Ai.Timer = static_cast<int16>(Ai.Timer - 1);
		}
	}

	void InitAi(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		FBattlingAiState& Ai = AiOf(Fight, Fighter);
		Ai.Timer = 0;
		Ai.StickDirection = 0;
		Ai.StickMagnitude = 0;
		Ai.Mode = EBattlingAiMode::Wait;
		Ai.Counter = 0;
		Fighter.Stick = 0;
		// Perfect Works' arena's computer keeps under half an Ether Bullet's heat from the top.
		Ai.HeatLimit = 0x1000 - (ArenaRules(Fight) ? Fighter.EtherHeat >> 1 : Fighter.EtherHeat);
		Ai.Level = Fight.Settings.Level;
		for (std::size_t Trait = 0; Trait < Ai.Traits.size(); ++Trait)
		{
			Ai.Traits[Trait] = Fighter.Stats->Traits[Trait];
		}
		switch (Fight.Random.Next() % 3)
		{
		case 0:
			EnterWait(Fight, Fighter, Ai);
			break;
		case 1:
			EnterAttack(Fight, Fighter, Ai);
			break;
		default:
			EnterBackOff(Fight, Fighter, Ai);
			break;
		}
		QueueClear(Fighter);
	}

	void SteerAi(FBattlingFight& Fight, FBattlingFighter& Fighter, int32 Direction, int32 Magnitude)
	{
		int32 Down = Fighter.Deceleration;
		const bool bFrozen = (Fighter.Flags & BattlingFlags::NoSteering) != 0;
		if (bFrozen)
		{
			Down = (Down << 1) / 3;
		}
		const int32 Cap = static_cast<int16>(Magnitude);
		for (int32 Read = 0; Read < 2; ++Read)
		{
			if (Cap == 0 || bFrozen)
			{
				Fighter.Stick -= Down;
				if (Fighter.Stick < 0)
				{
					Fighter.Stick = 0;
				}
			}
			else
			{
				Fighter.Stick += Fighter.Acceleration;
				if (Cap < Fighter.Stick)
				{
					Fighter.Stick = Cap;
				}
			}
		}
		Fighter.WantedDirection = TurnAngle(Fighter.WantedDirection, static_cast<int16>(Direction), 0x40, Fight.Random);
		Fighter.SteerOffset = 0;
	}

	void TickAi(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		TickAiAtLevel(Fight, Fighter, Fight.Settings.Level);
	}

	void TickAiAtLevel(FBattlingFight& Fight, FBattlingFighter& Fighter, uint8 Level)
	{
		if (!Fighter.IsComputer())
		{
			return;
		}
		FBattlingAiState& Ai = AiOf(Fight, Fighter);
		Ai.Level = Level;
		switch (Ai.Mode)
		{
		case EBattlingAiMode::Wait:
			WaitTick(Fight, Fighter, Ai);
			break;
		case EBattlingAiMode::Attack:
			AttackTick(Fight, Fighter, Ai);
			break;
		case EBattlingAiMode::BackOff:
			BackOffTick(Fight, Fighter, Ai);
			break;
		case EBattlingAiMode::Approach:
			ApproachTick(Fight, Fighter, Ai);
			break;
		}
		if (Ai.bDash)
		{
			Fighter.Flags |= BattlingFlags::DashInput;
		}
		if ((Ai.Flags & BattlingAiFlags::Guard) != 0)
		{
			Fighter.Flags |= BattlingFlags::GuardInput;
		}
		SteerAi(Fight, Fighter, Ai.StickDirection, Ai.StickMagnitude);
	}

	void TickPracticeAi(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		enum : uint8
		{
			Bystander,
			OnGuard,
			Controller2,
			UpAndAtEm,
			Slowpoke,
			MagicFirer,
			MagicJumper,
			Kangaroo,
			GiveChase,
			RunAway,
			BackDash,
			EasyComputer,
			NormalComputer,
			HardComputer,
		};

		FBattlingAiState& Ai = AiOf(Fight, Fighter);
		const uint8 Style = Fight.Settings.PracticeStyle;
		Fight.Settings.Computer[1] = true;
		if (Fight.PracticeStyleSeen != Style)
		{
			Ai.Timer = 0;
			Ai.StickMagnitude = 0;
			Fighter.Stick = 0;
			Fighter.Flags &= ~(BattlingFlags::GuardInput | BattlingFlags::GuardCounter);
			Ai.Flags &= ~BattlingAiFlags::Guard;
			Fight.PracticeStyleSeen = Style;
			Fighter.SteerOffset = static_cast<int16>(Fighter.CameraOffset + 0x800);
		}
		if ((Ai.Flags & BattlingAiFlags::Guard) != 0)
		{
			Fighter.Flags |= BattlingFlags::GuardInput;
		}
		switch (Style)
		{
		case Controller2:
		case GiveChase:
		case RunAway:
		case EasyComputer:
		case NormalComputer:
		case HardComputer:
			break;
		default:
			Ai.Timer = static_cast<int16>(Ai.Timer - 1);
			if (Ai.Timer != -1)
			{
				return;
			}
			break;
		}
		switch (Style)
		{
		case OnGuard:
		{
			const bool bGuard = (Ai.Flags & BattlingAiFlags::Guard) == 0;
			Ai.Flags = (Ai.Flags & ~BattlingAiFlags::Guard) | (bGuard ? BattlingAiFlags::Guard : 0);
			if (bGuard)
			{
				Fighter.Flags |= BattlingFlags::GuardInput;
				Ai.Timer = 0x3C;
			}
			else
			{
				Fighter.Flags &= ~(BattlingFlags::GuardInput | BattlingFlags::GuardCounter);
				Ai.Timer = 0x1E;
			}
			break;
		}
		case Controller2:
			Fight.Settings.Computer[1] = false;
			return;
		case UpAndAtEm:
		case Slowpoke:
			Combo(Fight, Fighter);
			Ai.Timer = Style == Slowpoke ? 0x1E : 10;
			break;
		case MagicFirer:
			QueuePush(Fighter, CommandEther);
			Ai.Timer = 0x3C;
			break;
		case MagicJumper:
			QueuePush(Fighter, CommandJump);
			QueuePush(Fighter, CommandEther);
			Ai.Timer = 0x3C;
			break;
		case Kangaroo:
			QueuePush(Fighter, CommandJump);
			Ai.Timer = 0x3C;
			break;
		case GiveChase:
			if (Fight.DistanceXZ < 0x100)
			{
				Ai.Timer = 1;
				Ai.StickMagnitude = 0;
			}
			else
			{
				Ai.Timer = static_cast<int16>(Ai.Timer - 1);
				if (Ai.Timer == -1)
				{
					Ai.StickDirection = static_cast<int16>(Fight.Random.Next() % 0x600 - 0x300);
					const int32 Run = Fight.Random.Next();
					Ai.StickMagnitude = 0xFF;
					Ai.Timer = static_cast<int16>(Run % 0x78 + 10);
				}
			}
			break;
		case RunAway:
			if (Fight.DistanceXZ < 0x801)
			{
				Ai.Timer = static_cast<int16>(Ai.Timer - 1);
				if (Ai.Timer == -1)
				{
					Ai.StickDirection = static_cast<int16>(Fight.Random.Next() % 0x600 + 0x500);
					const int32 Run = Fight.Random.Next();
					Ai.StickMagnitude = 0xFF;
					Ai.Timer = static_cast<int16>(Run % 0x32 + 10);
				}
			}
			else
			{
				Ai.Timer = 1;
				Ai.StickMagnitude = 0;
			}
			break;
		case BackDash:
			BackDashTry(Fight, Fighter);
			Ai.Timer = 0x3C;
			break;
		case EasyComputer:
		case NormalComputer:
		case HardComputer:
			// The original sets the style's level, but the computer's tick takes the menus' level again at once.
			Ai.Level = static_cast<uint8>(Style - EasyComputer);
			if (Fight.Settings.bPracticeComputerAtStyleLevel)
			{
				TickAiAtLevel(Fight, Fighter, Ai.Level);
			}
			else
			{
				TickAi(Fight, Fighter);
			}
			return;
		default:
			Ai.Timer = 1;
			break;
		}
		SteerAi(Fight, Fighter, Ai.StickDirection, Ai.StickMagnitude);
	}
}
