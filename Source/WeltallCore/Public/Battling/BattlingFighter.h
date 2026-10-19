#pragma once

#include "Battling/BattlingData.h"
#include "Battling/BattlingSkeleton.h"

#include <array>
#include <vector>

namespace Weltall
{
	/** A fighter's flags (its record's word at D0h). */
	namespace BattlingFlags
	{
		constexpr uint32 ActionStarted = 1u << 0;
		constexpr uint32 GuardInput = 1u << 1;
		constexpr uint32 GuardActive = 1u << 2;
		/** Three bits counting the ticks the guard has been held, from bit 3. */
		constexpr uint32 GuardCounter = 7u << 3;
		constexpr uint32 Computer = 1u << 6;
		/** Attacking or guarding: the stick decays and the gear does not steer. */
		constexpr uint32 NoSteering = 1u << 8;
		constexpr uint32 ReleaseHeld = 1u << 10;
		constexpr uint32 AnimationEnded = 1u << 11;
		constexpr uint32 RestartAnimation = 1u << 12;
		constexpr uint32 DashInput = 1u << 15;
		constexpr uint32 LastDashInput = 1u << 16;
		/** The camera side this fighter's controls last followed. */
		constexpr uint32 CameraSide = 1u << 17;
		constexpr uint32 OnGround = 1u << 18;
		/** Moving backward on the ground (dust at the feet). */
		constexpr uint32 BackDashDust = 1u << 19;
		/** Three bits alternating the hit reactions, from bit 20 (1 high, 2 low, 3 reset). */
		constexpr uint32 HitAlternation = 7u << 20;
		constexpr uint32 KnockedOut = 1u << 23;
		constexpr uint32 StickAway = 1u << 24;
		constexpr uint32 NoBlend = 1u << 25;
		constexpr uint32 AttackLive = 1u << 26;
		/** Player 2's record (pad 2). */
		constexpr uint32 Side = 1u << 27;
		constexpr uint32 RestartLoop = 1u << 28;
		/** Two bits of the ground under the gear (EBattlingGround), from bit 29. */
		constexpr uint32 Ground = 3u << 29;
	}

	/** A fighter's second word of flags (D4h). */
	namespace BattlingFlags2
	{
		/** The movement sound: 0 none, 1 walking, 2 dashing, 3 attacking. */
		constexpr uint32 MovementSound = 3u << 0;
		constexpr uint32 LastMovementSound = 3u << 2;
		constexpr uint32 Bounced = 1u << 4;
		/** The last hit was a projectile's or was guarded: the HUD's flash. */
		constexpr uint32 HitFlash = 1u << 5;
		constexpr uint32 DashRefused = 1u << 6;
	}

	/** The fighter's movement (C4h): bit 0 moving, bit 1 in the air, or one of the two states below. */
	namespace BattlingMovement
	{
		constexpr uint8 Moving = 1;
		constexpr uint8 InAir = 2;
		constexpr uint8 Down = 4;
		constexpr uint8 KnockedOut = 5;
	}

	/** What the fighter is doing (C5h). */
	namespace BattlingAction
	{
		constexpr uint8 Free = 0;
		constexpr uint8 Attacking = 2;
		constexpr uint8 BackDashing = 4;
	}

	/** An attack's swing, tested against the opponent on the tick after the one it is set on. */
	struct FBattlingHitVolume
	{
		FBattlingPoint A{};
		FBattlingPoint PreviousA{};
		FBattlingPoint B{};
		FBattlingPoint PreviousB{};
		/** The gear's sound index its hit plays, 0 a random hit sound; nothing sets it. */
		int16 Sound = 0;
		uint8 Joint = 0;
		/** 1 a new swing (no previous points), 2 one point. */
		uint8 Flags = 0;
		/** 2 set this tick, 1 live, 0 free. */
		uint8 State = 0;
		uint8 Kind = 0;
		uint8 Power = 0;
		/** The low byte of the pass it was last set on. */
		uint8 Pass = 0;
		/** Its event's side, joint and vertex, which a later frame of the same swing finds it by. */
		uint32 Id = 0;
		/** The chain state of the move it belongs to, -1 for the round's first. */
		int8 MoveState = -1;
	};

	struct FBattlingProjectile
	{
		FBattlingPoint Position{};
		FBattlingPoint Previous{};
		std::array<int16, 3> Velocity{};
		/** Its heading, a unit vector (4096 = 1). */
		std::array<int16, 3> Direction{};
		bool bActive = false;
		uint8 Speed = 0;
		uint8 Look = 0;
		uint8 SpeedMode = 0;
		/** To the opponent's centre, this tick. */
		int32 Distance = 0;
		uint32 Id = 0;
		int16 Power = 0;
		int16 Homing = 0;
		int16 Life = 0;
	};

	/** What a fight tick records of a fighter for the replay. */
	struct FBattlingReplayRecord
	{
		int16 X = 0;
		int16 Y = 0;
		int16 Z = 0;
		/** The yaw (12 bits), 1000h an animation started, 2000h dashing, 4000h moving backward, 8000h an Ether Bullet fired. */
		uint16 YawFlags = 0;
		uint8 Animation = 0;
		uint8 Frames = 0;
		uint8 Blend = 0;
		bool bGuard = false;
	};

	/** A gear in the arena: the program's fighter record. */
	struct FBattlingFighter
	{
		static constexpr int32 VolumeCount = 16;
		static constexpr int32 ProjectileCount = 8;
		static constexpr int32 QueueSize = 32;
		static constexpr int32 RecordCount = 256;

		FBattlingPoint Position{};
		FBattlingPoint Velocity{};
		FBattlingPoint KnockBack{};
		/** An attack's forward push this tick; its y is never set, so it stays 0. */
		FBattlingPoint LungePush{};
		int32 GroundSpeed = 0;
		int32 Lunge = 0;
		/** 0-100h: how far the human's or the computer's stick leans. */
		int32 Stick = 0;
		uint8 Animation = 0;
		/** FFh forces the animation to start again. */
		uint8 LastAnimation = 0xFF;
		uint8 AnimationSpeed = 0;
		uint16 EventFrame = 0;
		uint8 SpeedBonus = 0;
		int32 Yaw = 0;
		/** Relative to the opponent's direction: 0 toward. */
		int32 WantedDirection = 0;
		FBattlingPoint PreviousPosition{};
		/** The stick's four axes: left, up, right, down (0-100h each). */
		std::array<int32, 4> Axes{};
		int32 Acceleration = 0;
		int32 Deceleration = 0;
		/** Perfect Works Build's arena's: the gear's speed scale for its running, and a dash tick's heat (the gear table's). */
		int16 RunScale = 0;
		int32 DashHeat = 0;
		int32 GroundHeight = 0;
		int16 Hp = 0;
		int16 Heat = 0;
		int16 DrawnHp = 0;
		int16 OverheatDamage = 0;
		int16 MaxHp = 0;
		int16 EtherHeat = 0;
		uint8 HpGauge = 0;
		uint8 PreviousHpGauge = 0;
		uint8 GaugeFlash = 0;
		uint8 BackDashTimer = 0;
		uint8 Movement = 0;
		uint8 Action = 0;
		uint8 LastMovement = 0;
		int16 ReactionTimer = 0;
		int16 KnockDownTimer = 0;
		/** The camera side's steering offset: player 1 400h, player 2 -400h. */
		int16 CameraOffset = 0;
		int16 SteerOffset = 0;
		uint32 Flags = 0;
		uint32 Flags2 = 0;
		int32 Stun = 0;
		/** Perfect Works Build's Basic arena's: the last hit taken was a swing's, which knocks down at the fifth in a row, not the fourth. */
		uint8 SwingHit = 0;
		int16 HitsInRow = 0;
		int16 RoundsWon = 0;
		int32 Steer = 0;
		/** The hit's kind + 1, 0 none. */
		int32 HitRequest = 0;
		std::array<FBattlingHitVolume, VolumeCount> Volumes{};
		int32 AttackPower = 0;
		int32 BaseYaw = 0;
		std::array<FBattlingProjectile, ProjectileCount> Projectiles{};
		int32 NearestDistance = 0;
		/** The projectile NearestDistance was measured to, -1 none yet. */
		int8 NearestProjectile = -1;
		uint8 Gear = 0;
		/** Bit 0 no boost dust, 1 aquatic, 2 desert. */
		uint8 Kind = 0;
		uint8 SandDust = 0;
		int16 AttackYaw = 0;
		uint8 HeldAnimation = 0;
		uint8 AlarmTimer = 0;
		uint8 FarMoves = 0;
		uint8 NearMoves = 0;
		int16 Cooldown = 0;
		int16 DownStun = 0;
		uint8 HitFlash = 0;
		FBattlingPoint HitPoint{};
		/** The hit flash the last frame's light on the gear was drawn with: the flash before the frame faded it. */
		uint8 HitLight = 0;
		FBattlingPoint Top{};
		FBattlingPoint Base{};
		FBattlingPoint Centre{};
		FBattlingPoint PreviousBase{};
		/** The previous tick's heights of the second and the first foot. */
		std::array<int16, 2> FootHeights{};
		int32 Damage = 0;
		FBattlingPoint Source{};
		int16 FrameBefore = 0;
		int16 FramesStepped = 0;
		int16 Blend = 0;
		uint16 Clock = 0;
		std::array<uint8, QueueSize> Queue{};
		uint8 QueueWrite = 0;
		uint8 QueueRead = 0;
		uint8 QueueCount = 0;
		uint8 Chain = 0;
		std::array<FBattlingReplayRecord, RecordCount> Records{};
		/** The tick's record, and the one being replayed. */
		uint8 Record = 0;
		uint8 Replayed = 0;
		std::array<uint8, 3> Colour{};
		uint16 MoveScale = 0;
		uint16 SpeedScale = 0;
		uint16 GearSpeedScale = 0;
		/** The chain state of the move that started last, -1 for the round's first. */
		int8 MoveState = -1;
		int32 OverheatFromEther = 0;
		int32 OverheatFromDash = 0;
		int32 R1Presses = 0;
		int32 AttackPresses = 0;
		int16 HitsTaken = 0;

		const FBattlingGearFile* File = nullptr;
		const FBattlingGearStats* Stats = nullptr;
		const FBattlingSoundSet* SoundSet = nullptr;
		FBattlingSkeleton Skeleton;
		/**
		 * Each animation's own playback, kept while other animations play (an animation resumed without a restart goes on from
		 * where it was), and one more for the numbers past the gear's animations.
		 */
		std::vector<FBattlingAnimationPlayer> Players;

		FBattlingAnimationPlayer& Player(uint8 Number) { return Number < Players.size() ? Players[Number] : Players.back(); }

		int32 SideIndex() const { return (Flags & BattlingFlags::Side) != 0 ? 1 : 0; }
		bool IsComputer() const { return (Flags & BattlingFlags::Computer) != 0; }
		EBattlingGround Ground() const { return static_cast<EBattlingGround>((Flags >> 29) & 3); }
		/** The move the current attack follows: its trail flag decides the attack's trail. */
		WELTALLCORE_API FBattlingMove CurrentMove() const;
	};
}
