#include "BattlingFightOps.h"
#include "BattlingMath.h"

#include <algorithm>

namespace Weltall::BattlingFightOps
{
	namespace
	{
		using namespace BattlingMath;

		constexpr int16 FullHp = 300;
		constexpr int16 EtherBulletHeat = 0x480;
		constexpr int16 MaximumHeat = 0x1000;
		constexpr int32 OverheatDivisor = 20;
		constexpr int32 DashHeat = 0x20;
		constexpr int32 JumpSpeed = 0x8C;
		constexpr int16 AttackCooldown = 15;
		constexpr int16 KnockDownTicks = 60;
		constexpr int16 KnockDownStun = 0x28;
		constexpr int32 FirstMoveAnimation = 0x12;

		enum : uint8
		{
			CommandNone,
			CommandAttackA,
			CommandAttackB,
			CommandEther,
			CommandJump,
			CommandBackDash,
		};

		enum : uint8
		{
			AnimationStanding = 0,
			AnimationMoving = 1,
			AnimationInAir = 2,
			AnimationEtherOnGround = 3,
			AnimationEtherInAir = 4,
			AnimationHitLow = 5,
			AnimationHitHigh = 6,
			AnimationHitAgain = 7,
			AnimationKnockedDown = 8,
			AnimationDown = 9,
			AnimationHitGettingUp = 12,
			AnimationKnockedOut = 13,
			AnimationBackDash = 15,
			AnimationGuard = 17,
		};

		enum : int32
		{
			SoundDashStart = 8,
			SoundJump = 10,
			SoundBackDash = 14,
			SoundOverheatDamage = 0x2D,
			SoundOverheatRefused = 0x2E,
		};

		constexpr uint8 SStrooper = 0x29;
		constexpr int32 ReplayState = 4;

		void StartAnimation(FBattlingFighter& Fighter, uint8 Animation)
		{
			Fighter.Animation = Animation;
			Fighter.LastAnimation = 0xFF;
			Fighter.Flags |= BattlingFlags::RestartAnimation;
		}

		void ApplyPush(FBattlingFighter& Fighter, int32 Angle, int32 Shift, int32 Lift)
		{
			Fighter.KnockBack[0] += GteSin(Angle) >> Shift;
			Fighter.KnockBack[2] += GteCos(Angle) >> Shift;
			if (-JumpSpeed < Fighter.Velocity[1])
			{
				Fighter.Velocity[1] -= Lift;
			}
			if (Fighter.Velocity[1] < -0x82)
			{
				Fighter.Velocity[1] = -0x82;
			}
		}

		void KnockDown(FBattlingFight& Fight, FBattlingFighter& Fighter, int32 Angle, int32 Shift, int32 Lift)
		{
			Fighter.Animation = AnimationKnockedDown;
			Fighter.LastAnimation = 0xFF;
			Fighter.KnockDownTimer = KnockDownTicks;
			Fighter.DownStun = ArenaRules(Fight) ? int16{0} : KnockDownStun;
			Fighter.Stun = KnockDownStun;
			Fighter.Movement = BattlingMovement::Down;
			Fighter.Action = BattlingAction::Free;
			Fighter.Flags |= BattlingFlags::RestartAnimation;
			Fighter.Flags2 &= ~BattlingFlags2::Bounced;
			Fighter.Flags &= ~BattlingFlags::ReleaseHeld;
			ApplyPush(Fighter, Angle, Shift, Lift);
		}

		/**
		 * Perfect Works' arena's push or knock-down: a hit knocks down once the knock-down stun reaches the hitter's gear's
		 * threshold, a high hit (kind 2) too; otherwise at the fourth hit in a row (Basic's: the fifth after a swing).
		 */
		void ArenaHitReaction(FBattlingFight& Fight, FBattlingFighter& Fighter, uint32 Kind, int32 Angle, int32& Vibration)
		{
			const bool bOverThreshold = Opponent(Fight, Fighter).Stats->ArenaKnockDown <= Fighter.DownStun;
			if (Kind == 3 || (Kind == 2 && bOverThreshold))
			{
				KnockDown(Fight, Fighter, Angle, 11, 0x80);
				return;
			}
			if (Kind == 2)
			{
				StartAnimation(Fighter, AnimationHitHigh);
				ApplyPush(Fighter, Angle, 12, 100);
				return;
			}
			bool bKnockDown = bOverThreshold;
			if (!bKnockDown)
			{
				if (ExpertRules(Fight))
				{
					bKnockDown = 3 < Fighter.HitsInRow;
				}
				else
				{
					// Basic tests the counter's low byte, and a swing's mark only where the threshold was not reached.
					bKnockDown = (Fighter.SwingHit != 0 ? 4 : 3) < (Fighter.HitsInRow & 0xFF);
					Fighter.SwingHit = 0;
				}
			}
			if (!bKnockDown)
			{
				ApplyPush(Fighter, Angle, 10, Kind == 1 ? 0 : 0x1E);
				return;
			}
			KnockDown(Fight, Fighter, Angle, 10, 0xA0);
			Vibration = 15;
			Fighter.Stun = 0;
			if (!ExpertRules(Fight))
			{
				// Basic clears the counter's low byte.
				Fighter.HitsInRow = static_cast<int16>(Fighter.HitsInRow & 0xFF00);
			}
		}

		/** The reaction to this tick's hit: the push away from where it landed, a knock-down when it was hard enough. */
		void HitReaction(FBattlingFight& Fight, FBattlingFighter& Fighter)
		{
			int32 Vibration = 4;
			// Perfect Works' Expert arena counts the hits in a row as its projectiles land (CheckHits), not here.
			if (!ExpertRules(Fight))
			{
				++Fighter.HitsInRow;
			}
			if (Fighter.KnockDownTimer == 0)
			{
				const uint32 Flags = Fighter.Flags;
				if ((Flags & BattlingFlags::GuardActive) != 0)
				{
					Vibrate(Fight, Fighter, 2);
					return;
				}
				const bool bHigh = Fighter.Source[1] < (Fighter.Top[1] - Fighter.Base[1]) * 2 / 3 + Fighter.Base[1];
				const uint32 Alternation = bHigh ? 0x100000u : 0x200000u;
				if ((Flags & BattlingFlags::HitAlternation) == Alternation)
				{
					Fighter.Animation = AnimationHitAgain;
					Fighter.LastAnimation = 0xFF;
					Fighter.Flags = (Flags & ~BattlingFlags::HitAlternation) | BattlingFlags::RestartAnimation;
				}
				else
				{
					Fighter.Animation = bHigh ? AnimationHitHigh : AnimationHitLow;
					Fighter.LastAnimation = 0xFF;
					Fighter.Flags = (Flags & ~BattlingFlags::HitAlternation) | BattlingFlags::RestartAnimation | Alternation;
				}
			}
			else if (Fighter.Animation == AnimationDown)
			{
				StartAnimation(Fighter, AnimationHitGettingUp);
				Vibrate(Fight, Fighter, Vibration);
				return;
			}

			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			int32 Angle = GteRatan2(Fighter.Position[0] - Fighter.Source[0], Fighter.Position[2] - Fighter.Source[2], Fight.Tables.Arctangents);
			if (DistanceXZ(Fighter.Position, Other.Position) < DistanceXZ(Fighter.Source, Other.Position))
			{
				Angle += 0x800;
			}
			const uint32 Kind = static_cast<uint32>(Fighter.HitRequest - 1) & 7;
			if (ArenaRules(Fight))
			{
				ArenaHitReaction(Fight, Fighter, Kind, Angle, Vibration);
			}
			else if (Kind == 2)
			{
				StartAnimation(Fighter, AnimationHitHigh);
				ApplyPush(Fighter, Angle, 12, 100);
			}
			else if (Kind == 3)
			{
				KnockDown(Fight, Fighter, Angle, 11, 0x80);
			}
			else if (0x25 < Fighter.DownStun || 3 < Fighter.HitsInRow)
			{
				KnockDown(Fight, Fighter, Angle, 10, 0xA0);
				Vibration = 15;
				Fighter.Stun = 0;
				Fighter.DownStun = 0;
			}
			else
			{
				ApplyPush(Fighter, Angle, 10, Kind == 1 ? 0 : 0x1E);
			}
			Vibrate(Fight, Fighter, Vibration);
		}

		/**
		 * Perfect Works' arena's stuns each tick: the knock-down stun falls only while the stun is 0 (Basic's by 2; Expert's by 1,
		 * 2 then), then the stun, capped at 15 while guarding, 20 otherwise.
		 */
		void TickArenaStuns(const FBattlingFight& Fight, FBattlingFighter& Fighter)
		{
			if (Fighter.DownStun != 0)
			{
				const int32 Fall = ExpertRules(Fight) ? 1 + (Fighter.Stun == 0 ? 1 : 0) : (Fighter.Stun == 0 ? 2 : 0);
				Fighter.DownStun = static_cast<int16>(std::max(Fighter.DownStun - Fall, 0));
			}
			if (Fighter.Stun == 0)
			{
				Fighter.Flags &= ~BattlingFlags::RestartAnimation;
				return;
			}
			const int32 Cap = (Fighter.Flags & BattlingFlags::GuardActive) != 0 ? 15 : 20;
			Fighter.Stun = std::min(Fighter.Stun, Cap) - 1;
		}

		/** The power of the chain state's move: its strength, by the gear's percentage but in Perfect Works' arena. */
		int32 AttackPower(const FBattlingFight& Fight, const FBattlingFighter& Fighter)
		{
			const int32 Strength = Fighter.Stats->StrengthOfState(Fighter.Chain);
			return ArenaRules(Fight) ? Strength : Strength * Fighter.Stats->Power / 100;
		}

		/** The chain moves on by the attack (A or B) and the move of its new state starts, on the ground. */
		void StartAttack(FBattlingFight& Fight, FBattlingFighter& Fighter, bool bAttackA, bool& bLunge)
		{
			const auto& Next = Fight.Tables.Chain[Fighter.Chain];
			Fighter.Chain = bAttackA ? Next[0] : Next[1];
			if ((Fighter.Movement & BattlingMovement::InAir) != 0)
			{
				return;
			}
			const FBattlingMove& Move = Fighter.File->Moves[Fighter.Chain];
			if (Move.Animation == 0)
			{
				Fighter.Cooldown = ArenaRules(Fight) ? int16{0} : AttackCooldown;
				QueueClear(Fighter);
				return;
			}
			Fighter.Animation = static_cast<uint8>(Move.Animation + FirstMoveAnimation);
			Fighter.LastAnimation = 0xFF;
			Fighter.Flags |= BattlingFlags::RestartAnimation | BattlingFlags::ActionStarted;
			bLunge = Move.Lunge != 0;
			Fighter.Action = BattlingAction::Attacking;
			Fighter.MoveState = static_cast<int8>(Fighter.Chain);
			Fighter.AttackPower = AttackPower(Fight, Fighter);
		}
	}

	void QueuePush(FBattlingFighter& Fighter, uint8 Command)
	{
		if (Fighter.QueueCount < FBattlingFighter::QueueSize)
		{
			Fighter.Queue[Fighter.QueueWrite & 0x1F] = Command;
			++Fighter.QueueWrite;
			++Fighter.QueueCount;
		}
	}

	uint8 QueuePop(FBattlingFighter& Fighter)
	{
		if (Fighter.QueueCount == 0)
		{
			return CommandNone;
		}
		const uint8 Command = Fighter.Queue[Fighter.QueueRead & 0x1F];
		++Fighter.QueueRead;
		--Fighter.QueueCount;
		return Command;
	}

	void QueueClear(FBattlingFighter& Fighter)
	{
		Fighter.QueueCount = 0;
		Fighter.QueueWrite = 0;
		Fighter.QueueRead = 0;
		Fighter.Chain = 0;
	}

	bool HeatCanAdd(const FBattlingFighter& Fighter, int32 Heat)
	{
		const int32 Total = Fighter.Heat + Heat;
		if (Total <= MaximumHeat)
		{
			return true;
		}
		return (Total - 0xFF1) / OverheatDivisor < Fighter.Hp;
	}

	bool HeatAdd(FBattlingFight& Fight, FBattlingFighter& Fighter, int32 Heat, int32 Source)
	{
		if (NoHeat(Fight))
		{
			return true;
		}
		const int16 Total = static_cast<int16>(static_cast<uint16>(Fighter.Heat) + Heat);
		Fighter.Heat = Total;
		if (Total <= MaximumHeat)
		{
			return true;
		}
		// Perfect Works' arena takes the excess's 3/64 (/ 21 1/3).
		const int32 Damage = ArenaRules(Fight) ? (Total - MaximumHeat) * 3 / 64 : (Total - MaximumHeat) / OverheatDivisor;
		if (Fighter.Hp <= Damage)
		{
			Fighter.Heat = static_cast<int16>(Total - Heat);
			Fighter.OverheatDamage = 0;
			if (Fight.LatchedState != ReplayState)
			{
				Fight.Sound.Play(SoundOverheatRefused, Fight.VBlank);
			}
			return false;
		}
		Fighter.Heat = MaximumHeat;
		Fighter.OverheatDamage = static_cast<int16>(Damage);
		if (Source == 1)
		{
			Fighter.OverheatFromEther += static_cast<int16>(Damage);
		}
		else if (Source == 2)
		{
			Fighter.OverheatFromDash += static_cast<int16>(Damage);
		}
		return true;
	}

	void Vibrate(FBattlingFight& Fight, const FBattlingFighter& Fighter, int32 Strength)
	{
		if (Fight.LatchedState == ReplayState)
		{
			return;
		}
		int32 Pad = 0;
		if ((Fighter.Flags & BattlingFlags::Side) == 0)
		{
			if (!Fight.Settings.Vibration[0])
			{
				return;
			}
		}
		else
		{
			if (Fight.Settings.Mode != EBattlingMode::TwoPlayers && Fight.Settings.Mode != EBattlingMode::Practice)
			{
				return;
			}
			if (!Fight.Settings.Vibration[1])
			{
				return;
			}
			Pad = 1;
		}
		if (!Fighter.IsComputer() && Fight.VibrationCount < FBattlingFight::VibrationLimit)
		{
			Fight.Vibrations[static_cast<std::size_t>(Fight.VibrationCount++)] = {Pad, Strength};
		}
	}

	void BackDashTry(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		const uint32 Flags = Fighter.Flags;
		const bool bInWater = (Flags & BattlingFlags::Ground) == (static_cast<uint32>(EBattlingGround::Water) << 29);
		if (Fight.DistanceXZ < 0x201 && Fighter.Action != BattlingAction::BackDashing && (Flags & BattlingFlags::StickAway) == 0
			&& (Flags & BattlingFlags::OnGround) != 0 && (Opponent(Fight, Fighter).Flags & BattlingFlags::OnGround) != 0
			&& (!bInWater || (Fighter.Kind & 2) != 0))
		{
			QueueClear(Fighter);
			QueuePush(Fighter, CommandBackDash);
		}
	}

	void BeginTick(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		Fighter.GroundSpeed = 0;
		Fighter.HitRequest = 0;
		if (Fight.LatchedState != 7)
		{
			Fighter.Flags &= ~(BattlingFlags::DashInput | BattlingFlags::GuardInput);
		}
		const int32 Scale = static_cast<int16>(Fighter.GearSpeedScale) * Fight.Settings.MotionSpeed;
		Fighter.Flags &= ~BattlingFlags::BackDashDust;
		Fighter.SpeedScale = static_cast<uint16>(static_cast<uint32>(Scale) >> 8);
		Fight.Labels.SetPosition(Fighter.SideIndex() != 0 ? 0xF0 : 0x28, 0x28);
		if (Fighter.Chain != 0)
		{
			FBattlingPracticeInfo& Info = Fight.Practice[static_cast<std::size_t>(Fighter.SideIndex())];
			Info.Move = Fighter.Chain;
			Info.Power = static_cast<uint8>(AttackPower(Fight, Fighter));
		}
	}

	void TickStatus(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		if (Fighter.GaugeFlash != 0)
		{
			Fighter.GaugeFlash = static_cast<uint8>(Fighter.GaugeFlash - 0x10);
		}
		if (Fighter.Movement == BattlingMovement::KnockedOut)
		{
			return;
		}
		if (ArenaRules(Fight))
		{
			TickArenaStuns(Fight, Fighter);
		}
		else
		{
			if (Fighter.Stun > 20)
			{
				Fighter.Stun = 20;
			}
			if (Fighter.Stun == 0)
			{
				Fighter.Flags &= ~BattlingFlags::RestartAnimation;
			}
			else
			{
				--Fighter.Stun;
			}
			if (Fighter.DownStun != 0)
			{
				--Fighter.DownStun;
			}
		}
		if (Fighter.Cooldown != 0)
		{
			--Fighter.Cooldown;
		}
		if (Fighter.ReactionTimer == 0)
		{
			Fighter.HitsInRow = 0;
			Fighter.Flags = (Fighter.Flags & ~BattlingFlags::HitAlternation) | 0x300000;
		}
		else
		{
			--Fighter.ReactionTimer;
		}
		if (Fighter.BackDashTimer == 0)
		{
			if (Fighter.Action == BattlingAction::BackDashing)
			{
				Fighter.Action = BattlingAction::Free;
			}
		}
		else
		{
			Fighter.Action = BattlingAction::BackDashing;
			--Fighter.BackDashTimer;
		}
		if (Fighter.KnockDownTimer != 0)
		{
			--Fighter.KnockDownTimer;
		}
		if (Fighter.Heat != 0 && ArenaRules(Fight))
		{
			// Perfect Works' arena: 6 moving, 8 standing; 1 more on sand, 1 less in water but for a desert gear.
			const EBattlingGround Ground = Fighter.Ground();
			const int32 Terrain = Ground == EBattlingGround::Sand ? 1 : (Ground == EBattlingGround::Water && (Fighter.Kind & 4) == 0 ? -1 : 0);
			const int16 Cooled = static_cast<int16>(Fighter.Heat - Terrain - ((Fighter.Movement & BattlingMovement::Moving) != 0 ? 6 : 8));
			Fighter.Heat = Cooled < 0 ? static_cast<int16>(0) : Cooled;
		}
		else if (Fighter.Heat != 0)
		{
			int16 Cooled = static_cast<int16>(Fighter.Heat - 8);
			if ((Fighter.Movement & BattlingMovement::Moving) != 0)
			{
				const EBattlingGround Ground = Fighter.Ground();
				if (Ground == EBattlingGround::Sand)
				{
					Cooled = static_cast<int16>(Fighter.Heat - ((Fighter.Kind & 4) != 0 ? 8 : 2));
				}
				else
				{
					Cooled = static_cast<int16>(Fighter.Heat - (Ground == EBattlingGround::Water ? 6 : 4));
				}
			}
			Fighter.Heat = Cooled < 0 ? static_cast<int16>(0) : Cooled;
		}
		if (Fighter.Movement == BattlingMovement::Down)
		{
			if (Fighter.KnockDownTimer == 0)
			{
				Fighter.Movement = 0;
				Fighter.Action = BattlingAction::Free;
			}
		}
		else
		{
			if (Fighter.GroundHeight - 0x30 < Fighter.Position[1])
			{
				Fighter.Movement &= static_cast<uint8>(~BattlingMovement::InAir);
			}
			Fighter.Movement = Fighter.Stick == 0 ? static_cast<uint8>(Fighter.Movement & ~BattlingMovement::Moving)
												  : static_cast<uint8>(Fighter.Movement | BattlingMovement::Moving);
		}
		if ((Fighter.Flags & BattlingFlags::AnimationEnded) != 0)
		{
			// The held animations let go: down once the knock-down ends, in the air while still airborne (without effect: it has
			// no next), the back-dash once it ends.
			const uint8 Held = Fighter.HeldAnimation;
			const bool bRelease = (Held == AnimationDown && Fighter.KnockDownTimer == 0)
				|| (Held == AnimationInAir && (Fighter.Movement & BattlingMovement::InAir) != 0)
				|| (Held == AnimationBackDash && Fighter.Action != BattlingAction::BackDashing);
			if (bRelease)
			{
				Fighter.Flags |= BattlingFlags::ReleaseHeld;
			}
		}
		Fighter.Hp = static_cast<int16>(Fighter.Hp - Fighter.OverheatDamage);
		Fighter.Hp = static_cast<int16>(Fighter.Hp - static_cast<int16>(Fighter.Damage));
		if (Fighter.OverheatDamage == 0)
		{
			Fighter.AlarmTimer = 0;
		}
		else
		{
			if (Fighter.AlarmTimer == 0)
			{
				if (Fight.LatchedState != ReplayState)
				{
					Fight.Sound.Play(SoundOverheatDamage, Fight.VBlank);
				}
				Fighter.AlarmTimer = 20;
			}
			--Fighter.AlarmTimer;
		}
		if (Fight.Settings.bScripted && !HpAbove(Fighter.Hp, Fighter.MaxHp, 0x50) && (Fighter.Flags & BattlingFlags::Side) != 0)
		{
			Fighter.Hp = static_cast<int16>(static_cast<uint32>(Over256(Fighter.MaxHp * 0x50)));
		}
		if (Fighter.Hp < 0)
		{
			Fighter.Hp = 0;
		}
		if (Fighter.DrawnHp != Fighter.Hp)
		{
			const uint32 Gauge = static_cast<uint32>((Fighter.Hp * 0x80 + Fighter.MaxHp - 1) / Fighter.MaxHp);
			Fighter.DrawnHp = Fighter.Hp;
			Fighter.PreviousHpGauge = Fighter.HpGauge;
			Fighter.HpGauge = static_cast<uint8>(Gauge);
			if ((Gauge & 0xFF) == 1)
			{
				Fighter.HpGauge = 2;
			}
			Fighter.GaugeFlash = 0xF0;
		}
		Fighter.OverheatDamage = 0;
		Fighter.Damage = 0;
		if (!NoHeat(Fight) && Fighter.Hp == 0 && (Fighter.Flags & BattlingFlags::KnockedOut) == 0)
		{
			Fighter.Animation = AnimationKnockedOut;
			Fighter.LastAnimation = 0xFF;
			Fighter.Movement = BattlingMovement::KnockedOut;
			Fighter.Action = BattlingAction::Free;
			Fighter.Flags |= BattlingFlags::RestartAnimation;
			Fighter.Velocity[1] -= 0x50;
			Fighter.Flags |= BattlingFlags::ReleaseHeld;
			const int32 Sin = GteSin(Fight.FightAngle) >> 9;
			const int32 Cos = GteCos(Fight.FightAngle) >> 9;
			if ((Fighter.Flags & BattlingFlags::Side) == 0)
			{
				Fighter.KnockBack[0] += Sin;
				Fighter.KnockBack[2] += Cos;
			}
			else
			{
				Fighter.KnockBack[0] -= Sin;
				Fighter.KnockBack[2] -= Cos;
			}
			Fighter.Flags |= BattlingFlags::KnockedOut;
			Vibrate(Fight, Fighter, 20);
			Fighter.HitRequest = 0;
		}
	}

	void ProcessCommands(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		if (Fighter.Movement == BattlingMovement::KnockedOut)
		{
			return;
		}
		if (Fighter.HitRequest != 0)
		{
			HitReaction(Fight, Fighter);
			return;
		}
		bool bLunge = false;
		if (Fighter.Stun != 0)
		{
			QueueClear(Fighter);
			return;
		}
		Fighter.Flags &= ~BattlingFlags::ActionStarted;
		if (Fighter.Cooldown != 0)
		{
			QueueClear(Fighter);
		}
		if ((Fighter.Flags & BattlingFlags::AnimationEnded) != 0 && Fighter.Action != BattlingAction::BackDashing)
		{
			Fighter.Action = BattlingAction::Free;
		}
		if (Fighter.Action == BattlingAction::Free && Fighter.Movement != BattlingMovement::Down)
		{
			const uint8 Command = QueuePop(Fighter);
			switch (Command)
			{
			case CommandNone:
				QueueClear(Fighter);
				break;
			case CommandAttackA:
			case CommandAttackB:
				if (Fighter.Cooldown == 0)
				{
					StartAttack(Fight, Fighter, Command == CommandAttackA, bLunge);
				}
				break;
			case CommandEther:
				if (Fighter.Cooldown == 0 && Fighter.Stats->Ether != 0 && Fighter.Gear != SStrooper)
				{
					StartAnimation(Fighter, (Fighter.Movement & BattlingMovement::InAir) == 0 ? AnimationEtherOnGround : AnimationEtherInAir);
					bLunge = true;
					Fighter.Action = BattlingAction::Attacking;
					Fighter.Flags |= BattlingFlags::ActionStarted;
				}
				break;
			case CommandJump:
				if (Fighter.KnockDownTimer != 0)
				{
					return;
				}
				if ((Fighter.Movement & BattlingMovement::InAir) == 0)
				{
					SpawnEffect(Fight, Fighter.Base, 9);
					Fighter.Velocity[1] -= JumpSpeed;
					Fighter.Movement |= BattlingMovement::InAir;
					StopFighterSound(Fight, Fighter, SoundJump);
					PlayFighterSound(Fight, Fighter, SoundJump, &Fighter.Base, EBattlingSoundPlace::Following);
				}
				break;
			case CommandBackDash:
				StartAnimation(Fighter, AnimationBackDash);
				PlayFighterSound(Fight, Fighter, SoundBackDash, &Fighter.Position, EBattlingSoundPlace::Following);
				Fighter.BackDashTimer = 6;
				Fighter.Action = BattlingAction::BackDashing;
				break;
			default:
				break;
			}
		}

		Fighter.SpeedBonus = 0;
		Fighter.Flags &= ~BattlingFlags::GuardActive;
		switch (Fighter.Movement)
		{
		case 0:
			Fighter.WantedDirection = 0;
			if (Fighter.Action == BattlingAction::Attacking)
			{
				if (bLunge)
				{
					Fighter.Lunge += 0x40;
				}
			}
			else if (Fighter.Action == BattlingAction::Free)
			{
				if ((Fighter.Flags & BattlingFlags::GuardInput) == 0)
				{
					Fighter.Animation = AnimationStanding;
				}
				else
				{
					// The guard holds from its third tick: the counter (bits 3-5) reaches 2 on the second.
					const uint32 Flags = Fighter.Flags;
					Fighter.Animation = AnimationGuard;
					if ((Flags & BattlingFlags::GuardCounter) == 0x10)
					{
						Fighter.Flags = Flags | BattlingFlags::GuardActive;
					}
					else
					{
						Fighter.Flags = (Flags & ~BattlingFlags::GuardCounter) | ((((Flags >> 3) & 7) + 1) & 7) << 3;
					}
				}
			}
			else if (Fighter.Action == BattlingAction::BackDashing)
			{
				Fighter.Flags |= BattlingFlags::BackDashDust;
			}
			Fighter.Steer = TurnAngle(Fighter.Steer, Fighter.WantedDirection, 0x100, Fight.Random);
			break;
		case BattlingMovement::Moving:
			Fighter.SpeedBonus = static_cast<uint8>(Over4096(Fighter.Stick * static_cast<int16>(Fighter.SpeedScale)));
			if (Fighter.Action == BattlingAction::Attacking)
			{
				if (bLunge)
				{
					Fighter.Lunge += 0x40;
				}
			}
			else if (Fighter.Action == BattlingAction::Free)
			{
				Fighter.Animation = AnimationMoving;
			}
			else if (Fighter.Action == BattlingAction::BackDashing)
			{
				Fighter.Flags |= BattlingFlags::BackDashDust;
			}
			Fighter.Steer = TurnAngle(Fighter.Steer, Fighter.WantedDirection, 0x100, Fight.Random);
			if (Fighter.Ground() == EBattlingGround::Water && (Fighter.Kind & 2) == 0)
			{
				Fighter.Flags &= ~BattlingFlags::DashInput;
			}
			break;
		case BattlingMovement::InAir:
			Fighter.Stick = 0;
			Fighter.Lunge = 0;
			Fighter.WantedDirection = 0;
			if (Fighter.Action == BattlingAction::Free)
			{
				Fighter.Animation = AnimationInAir;
			}
			break;
		case BattlingMovement::InAir | BattlingMovement::Moving:
			if (Fighter.Action == BattlingAction::Free)
			{
				Fighter.Animation = AnimationInAir;
			}
			break;
		case BattlingMovement::Down:
			Fighter.Stick = 0;
			break;
		default:
			break;
		}

		// Perfect Works' arena runs by the gear table's speed scale, not the one the motion speed scales.
		const int16 RunScale = ArenaRules(Fight) ? Fighter.RunScale : static_cast<int16>(Fighter.SpeedScale);
		Fighter.GroundSpeed = Fighter.Stick * static_cast<int16>(Fighter.MoveScale) * RunScale >> 16;
		if ((Fighter.Flags & BattlingFlags::DashInput) == 0)
		{
			Fighter.Flags2 &= ~BattlingFlags2::DashRefused;
		}
		if ((Fighter.Flags & BattlingFlags::DashInput) != 0 && Fighter.Action == BattlingAction::Free && (Fighter.Flags2 & BattlingFlags2::DashRefused) == 0
			&& 0x30 < Fighter.GroundSpeed && 1u < static_cast<uint32>(Fighter.Movement - 2))
		{
			if (HeatAdd(Fight, Fighter, ArenaRules(Fight) ? Fighter.DashHeat : DashHeat, 2))
			{
				Fighter.GroundSpeed <<= 1;
				Fighter.SpeedBonus = static_cast<uint8>(Fighter.SpeedBonus << 1);
			}
			else
			{
				Fighter.Flags &= ~BattlingFlags::DashInput;
				Fighter.Flags2 |= BattlingFlags2::DashRefused;
			}
			if (1 < Fight.CameraEase)
			{
				Fight.CameraEase = 1;
			}
		}
		if (Fighter.Movement == 0 || (Fighter.Flags2 & BattlingFlags2::DashRefused) != 0)
		{
			Fighter.Flags &= ~BattlingFlags::DashInput;
		}
	}

	void ResetFighter(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		Fighter.Position = {};
		Fighter.Velocity = {};
		Fighter.KnockBack = {};
		Fighter.Skeleton.Scale = {Fighter.File->ModelScale, Fighter.File->ModelScale, Fighter.File->ModelScale};
		Fighter.Acceleration = 0x10;
		Fighter.Deceleration = 0x10;
		Fighter.Hp = FullHp;
		Fighter.DrawnHp = FullHp;
		Fighter.MaxHp = FullHp;
		Fighter.EtherHeat = EtherBulletHeat;
		if (ArenaRules(Fight))
		{
			// Perfect Works' arena: each gear's own HP, Ether heat, stick, running speed and dash heat.
			const FBattlingGearStats& Stats = *Fighter.Stats;
			Fighter.Acceleration = Stats.ArenaAcceleration;
			Fighter.Deceleration = Stats.ArenaDeceleration;
			Fighter.Hp = Stats.ArenaHp;
			Fighter.DrawnHp = Stats.ArenaHp;
			Fighter.MaxHp = Stats.ArenaHp;
			Fighter.EtherHeat = static_cast<int16>(Stats.ArenaEtherHeat * 8);
			Fighter.RunScale = Stats.Power;
			Fighter.DashHeat = Stats.ArenaDashHeat;
		}
		Fighter.GaugeFlash = 0xF0;
		Fighter.Yaw = 0;
		Fighter.Movement = 0;
		Fighter.Action = BattlingAction::Free;
		Fighter.Heat = 0;
		Fighter.OverheatDamage = 0;
		Fighter.AlarmTimer = 0;
		Fighter.HpGauge = 0x7F;
		// Perfect Works' arena writes the two gauges as one halfword.
		Fighter.PreviousHpGauge = ArenaRules(Fight) ? uint8{0} : uint8{0x7F};
		Fighter.Damage = 0;
		Fighter.Chain = 0;
		Fighter.Stun = 0;
		Fighter.DownStun = 0;
		Fighter.ReactionTimer = 0;
		Fighter.KnockDownTimer = 0;
		for (FBattlingProjectile& Projectile : Fighter.Projectiles)
		{
			Projectile.bActive = false;
			Projectile.Life = 0;
		}
		for (FBattlingHitVolume& Volume : Fighter.Volumes)
		{
			Volume.State = 0;
		}
		QueueClear(Fighter);
		Fighter.LastAnimation = 0xFF;
		Fighter.Animation = AnimationStanding;
		Fighter.BackDashTimer = 0;
		Fighter.Flags = (Fighter.Flags & ~BattlingFlags::RestartAnimation) | BattlingFlags::NoBlend | BattlingFlags::CameraSide;
		Fighter.SteerOffset = static_cast<int16>(Fighter.CameraOffset + 0x800);
		Fighter.Flags2 &= ~(BattlingFlags2::MovementSound | BattlingFlags2::LastMovementSound);
		Fighter.Flags &= ~BattlingFlags::KnockedOut;
		Fighter.MoveScale = Fighter.File->MoveScale;
		Fighter.OverheatFromEther = 0;
		Fighter.OverheatFromDash = 0;
		Fighter.R1Presses = 0;
		Fighter.AttackPresses = 0;
		Fighter.SandDust = 0;
		Fighter.Flags &= ~(BattlingFlags::GuardInput | BattlingFlags::GuardActive | BattlingFlags::GuardCounter);
		Fighter.GearSpeedScale = static_cast<uint16>(Fighter.File->SpeedScale);
		Fighter.SpeedScale = static_cast<uint16>(Fighter.File->SpeedScale);
		Fighter.Flags2 &= ~BattlingFlags2::Bounced;
		Fighter.MoveState = -1;
		Fight.Practice[static_cast<std::size_t>(Fighter.SideIndex())] = {};
		InitAi(Fight, Fighter);
		Fighter.Cooldown = 0;
		Fighter.HitsTaken = 0;
		Fighter.Flags2 &= ~BattlingFlags2::DashRefused;
	}
}
