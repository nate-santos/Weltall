#include "BattlingFightOps.h"
#include "BattlingMath.h"

namespace Weltall::BattlingFightOps
{
	namespace
	{
		using namespace BattlingMath;

		constexpr int32 SoundProjectileHit = 0xC;
		constexpr int32 SoundGuarded = 0xF;

		/** The segment's unit vector and length on the ground (4096 = 1), or a length of 0. */
		struct FSegmentFrame
		{
			int32 Length = 0;
			int32 UnitX = 0;
			int32 UnitZ = 0;
		};

		FSegmentFrame Frame(int32 X0, int32 Z0, int32 X1, int32 Z1)
		{
			FSegmentFrame Result;
			const int32 Dx = X1 - X0;
			const int32 Dz = Z1 - Z0;
			Result.Length = Length2(Dx, Dz);
			if (Result.Length != 0)
			{
				Result.UnitX = (Dx << 12) / Result.Length;
				Result.UnitZ = (Dz << 12) / Result.Length;
			}
			return Result;
		}

		/** Whether the segment (X0, Z0)-(X1, Z1) passes within Radius of (Cx, Cz), as the program tests it on the ground. */
		bool SegmentHitsCircle(int32 X0, int32 Z0, int32 X1, int32 Z1, int32 Cx, int32 Cz, int32 Radius, int32* Along = nullptr, int32* Inside = nullptr,
			FSegmentFrame* Out = nullptr)
		{
			const FSegmentFrame Segment = Frame(X0, Z0, X1, Z1);
			if (Segment.Length == 0)
			{
				return false;
			}
			const int32 Forward = Over4096((Cx - X0) * Segment.UnitX + (Cz - Z0) * Segment.UnitZ);
			const int32 Side = Over4096((Cz - Z0) * Segment.UnitX - (Cx - X0) * Segment.UnitZ);
			const int32 Within = Side * Side - GteSquare(Radius);
			if (Along != nullptr)
			{
				*Along = Forward;
				*Inside = Within;
				*Out = Segment;
			}
			return Within < 0 && Forward >= 0 && Forward <= Segment.Length + Radius;
		}

		/** The segment test that also places the contact: where the segment first enters the circle. */
		bool SegmentHitsCircleAt(FBattlingFight& Fight, int32 X0, int32 Z0, int32 X1, int32 Z1, int32 Cx, int32 Cz, int32 Radius)
		{
			int32 Forward = 0;
			int32 Within = 0;
			FSegmentFrame Segment;
			if (!SegmentHitsCircle(X0, Z0, X1, Z1, Cx, Cz, Radius, &Forward, &Within, &Segment))
			{
				return false;
			}
			const int32 Back = SquareRoot0(Within < 0 ? -Within : Within);
			Fight.Contact[0] = Over4096((Forward - Back) * Segment.UnitX) + X0;
			Fight.Contact[1] = Over4096((Forward - Back) * Segment.UnitZ) + Z0;
			return true;
		}

		/** The quad a two-point swing sweeps (both points' moves, the segment, the previous segment) against the circle. */
		bool QuadHitsCircle(FBattlingFight& Fight, const FBattlingHitVolume& Volume, int32 Cx, int32 Cz, int32 Radius)
		{
			Fight.Contact = {Cx, Cz};
			return SegmentHitsCircle(Volume.PreviousA[0], Volume.PreviousA[2], Volume.A[0], Volume.A[2], Cx, Cz, Radius)
				|| SegmentHitsCircle(Volume.PreviousB[0], Volume.PreviousB[2], Volume.B[0], Volume.B[2], Cx, Cz, Radius)
				|| SegmentHitsCircle(Volume.A[0], Volume.A[2], Volume.B[0], Volume.B[2], Cx, Cz, Radius)
				|| SegmentHitsCircle(Volume.PreviousA[0], Volume.PreviousA[2], Volume.PreviousB[0], Volume.PreviousB[2], Cx, Cz, Radius);
		}

		/** The body's axis at height Y, which must lie strictly between the top and the base. */
		FBattlingPoint AxisAt(const FBattlingFighter& Fighter, int32 Y)
		{
			const FBattlingPoint& Top = Fighter.Top;
			const FBattlingPoint& Base = Fighter.Base;
			const int32 T = ((Y - Base[1]) * 0x1000) / (Top[1] - Base[1]);
			return {(T * (Top[0] - Base[0]) >> 12) + Base[0], (T * (Top[1] - Base[1]) >> 12) + Base[1], (T * (Top[2] - Base[2]) >> 12) + Base[2]};
		}
	}

	namespace
	{
		/**
		 * A projectile's damage in Perfect Works' arena: a downed gear takes it whole (Expert: 3/4) without a sound or a guard;
		 * one from behind half as much again; the guard halves it (Expert: before any test of where it came from, Basic: after).
		 * Expert counts each unguarded one in the hits in a row.
		 */
		int32 ArenaProjectileDamage(FBattlingFight& Fight, FBattlingFighter& Fighter, const FBattlingProjectile& Projectile, const FBattlingPoint& Point,
			uint32& Kind)
		{
			const bool bExpert = ExpertRules(Fight);
			const uint16 Power = static_cast<uint16>(Projectile.Power);
			if (Fighter.Movement == BattlingMovement::Down)
			{
				return bExpert ? static_cast<int32>(static_cast<uint64>(Power) * 0xC0000000u >> 32) : Power;
			}
			const bool bGuarded = (Fighter.Flags & BattlingFlags::GuardActive) != 0;
			if (bExpert && bGuarded)
			{
				PlayFighterSound(Fight, Fighter, SoundGuarded, &Point, EBattlingSoundPlace::Fixed);
				Fighter.HitFlash = 0x80;
				return Power >> 1;
			}
			const int32 Heading = GteRatan2(Projectile.Velocity[0], Projectile.Velocity[2], Fight.Tables.Arctangents);
			int32 Damage = Projectile.Power;
			if (static_cast<uint32>((Heading - Fighter.Yaw) & 0xFFF) - 0x601 < 0x3FF)
			{
				Damage += Damage >> 1;
				Kind = 3;
			}
			if (bExpert || !bGuarded)
			{
				PlayFighterSound(Fight, Fighter, SoundProjectileHit, &Point, EBattlingSoundPlace::Fixed);
				if (bExpert)
				{
					++Fighter.HitsInRow;
				}
				return Damage;
			}
			PlayFighterSound(Fight, Fighter, SoundGuarded, &Point, EBattlingSoundPlace::Fixed);
			Fighter.HitFlash = 0x80;
			return (Damage + (Projectile.Power < 0 ? 1 : 0)) >> 1;
		}
	}

	void AgeVolumes(FBattlingFighter& Fighter)
	{
		for (FBattlingHitVolume& Volume : Fighter.Volumes)
		{
			Volume.State = Volume.State == 2 ? 1 : 0;
		}
	}

	void CheckHits(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		FBattlingFighter& Other = Opponent(Fight, Fighter);
		const int32 Radius = Fighter.File->BodyRadius;
		const int32 TopY = Fighter.Top[1];
		const int32 BaseY = Fighter.Base[1];
		int32 Hits = 0;
		int32 Damage = 0;
		uint32 Kind = 0;
		FBattlingPoint Point{};
		FBattlingPoint Source{};

		for (FBattlingProjectile& Projectile : Other.Projectiles)
		{
			if (!Projectile.bActive)
			{
				continue;
			}
			bool bHit = false;
			if (TopY < Projectile.Position[1] && Projectile.Position[1] < BaseY)
			{
				Point = AxisAt(Fighter, Projectile.Position[1]);
				bHit = SegmentHitsCircleAt(Fight, Projectile.Previous[0], Projectile.Previous[2], Projectile.Position[0], Projectile.Position[2], Point[0],
					Point[2], Radius);
			}
			else if (Projectile.Distance < Radius)
			{
				// Beyond the body's span but within reach: a hit on the end nearer the projectile's height before its step.
				const int32 Before = Projectile.Previous[1];
				const int32 ToTop = std::abs(TopY - Before);
				const int32 ToBase = std::abs(BaseY - Before);
				const int32 Step = std::abs(Projectile.Position[1] - Before);
				if (ToTop < ToBase)
				{
					if (ToTop < Step)
					{
						bHit = true;
						Point = Fighter.Top;
					}
				}
				else if (ToBase < Step)
				{
					bHit = true;
					Point = Fighter.Base;
				}
			}
			if (!bHit)
			{
				continue;
			}
			Fighter.HitFlash = 0xFF;
			Fighter.Flags2 |= BattlingFlags2::HitFlash;
			if (ArenaRules(Fight))
			{
				Damage = ArenaProjectileDamage(Fight, Fighter, Projectile, Point, Kind);
			}
			else
			{
				const int32 Heading = GteRatan2(Projectile.Velocity[0], Projectile.Velocity[2], Fight.Tables.Arctangents);
				Damage = Projectile.Power;
				if (static_cast<uint32>((Heading - Fighter.Yaw) & 0xFFF) - 0x601 < 0x3FF)
				{
					Damage <<= 1;
					Kind = 3;
					PlayFighterSound(Fight, Fighter, SoundProjectileHit, &Point, EBattlingSoundPlace::Fixed);
				}
				else if ((Fighter.Flags & BattlingFlags::GuardActive) == 0)
				{
					PlayFighterSound(Fight, Fighter, SoundProjectileHit, &Point, EBattlingSoundPlace::Fixed);
				}
				else
				{
					PlayFighterSound(Fight, Fighter, SoundGuarded, &Point, EBattlingSoundPlace::Fixed);
					Damage = Damage / 2;
					Fighter.HitFlash = 0x80;
				}
			}
			const int32 Stun = (Damage << 1) / 3;
			Fighter.Stun += Stun;
			// Perfect Works' Expert arena adds the whole damage to the knock-down stun.
			Fighter.DownStun = static_cast<int16>(Fighter.DownStun + (ExpertRules(Fight) ? Damage : Stun));
			Fighter.HitPoint = Point;
			Hits = Damage != 0 ? 1 : 0;
			Fight.Effects.SparkSource = Point;
			Fight.Effects.SparkRequest = 0x10;
			Fighter.ReactionTimer = static_cast<int16>(Damage / 3 + 12);
			Source = Point;
			Projectile.bActive = false;
			break;
		}

		for (FBattlingHitVolume& Volume : Other.Volumes)
		{
			if (Volume.State != 1 || (Volume.Flags & 1) != 0)
			{
				continue;
			}
			bool bHit = false;
			FBattlingPoint Axis{};
			if ((Volume.Flags & 2) == 0)
			{
				int32 Height = Volume.A[1] + Volume.PreviousA[1] + Volume.B[1] + Volume.PreviousB[1];
				Height = (Height < 0 ? Height + 3 : Height) >> 2;
				if (TopY < Height && Height < BaseY)
				{
					Axis = AxisAt(Fighter, Height);
					bHit = QuadHitsCircle(Fight, Volume, Axis[0], Axis[2], Radius);
				}
			}
			else if (TopY < Volume.A[1] && Volume.A[1] < BaseY)
			{
				Axis = AxisAt(Fighter, Volume.A[1]);
				bHit = SegmentHitsCircleAt(Fight, Volume.PreviousA[0], Volume.PreviousA[2], Volume.A[0], Volume.A[2], Axis[0], Axis[2], Radius);
			}
			if (!bHit)
			{
				continue;
			}
			const FBattlingPoint Contact{Fight.Contact[0], Axis[1], Fight.Contact[1]};
			Other.Flags &= ~BattlingFlags::AttackLive;
			const bool bArena = ArenaRules(Fight);
			bool bGuarded = (Fighter.Flags & BattlingFlags::GuardActive) != 0;
			if (bArena)
			{
				// Perfect Works' arena: a swing takes 4/5 of its power from a downed gear, which cannot guard, and keeps it so.
				Fight.Effects.SparkSource = Contact;
				if (!ExpertRules(Fight))
				{
					Fighter.SwingHit = 1;
				}
				if (Fighter.Movement == BattlingMovement::Down)
				{
					Volume.Power = static_cast<uint8>(Volume.Power * 4 / 5);
					bGuarded = false;
				}
			}
			uint32 Power = Volume.Power;
			if (!bGuarded)
			{
				PlayFighterSound(Fight, Fighter, Volume.Sound, &Contact, EBattlingSoundPlace::Fixed);
				SpawnEffect(Fight, Contact, 0);
				Fight.Effects.SparkSource = Contact;
				Fight.Effects.SparkRequest = 0x10;
				Fighter.HitFlash = 0xFF;
				Fighter.Flags2 &= ~BattlingFlags2::HitFlash;
			}
			else
			{
				PlayFighterSound(Fight, Fighter, SoundGuarded, &Contact, EBattlingSoundPlace::Fixed);
				Fight.Effects.SparkSource = Contact;
				Fight.Effects.SparkRequest = 4;
				Fighter.HitFlash = 0xC0;
				Power >>= 1;
				Fighter.Flags2 |= BattlingFlags2::HitFlash;
			}
			const uint32 Stun = (Power * 6) / 5;
			if (bArena && !ExpertRules(Fight))
			{
				// Perfect Works' Basic arena: the reaction lasts as long as the stun it adds up to, stored as a halfword.
				const int32 Stunned = Fighter.Stun + static_cast<int32>(Stun);
				Fighter.ReactionTimer = static_cast<int16>(Stunned);
				Fighter.DownStun = static_cast<int16>(Fighter.DownStun + static_cast<int16>(Stun));
				Fighter.Stun = static_cast<int32>((static_cast<uint32>(Fighter.Stun) & 0xFFFF0000u) | (static_cast<uint32>(Stunned) & 0xFFFFu));
			}
			else
			{
				Fighter.ReactionTimer = static_cast<int16>(Power);
				// Perfect Works' Expert arena adds the power to the knock-down stun too.
				Fighter.DownStun = static_cast<int16>(Fighter.DownStun + static_cast<int16>(Stun) + (bArena ? static_cast<int16>(Power) : int16{0}));
				Fighter.Stun += static_cast<int32>(Stun);
			}
			Fighter.HitPoint = Contact;
			if (Power != 0)
			{
				++Hits;
			}
			if (Damage < static_cast<int32>(Power))
			{
				Source = Volume.A;
				Kind = Volume.Kind;
				Damage = static_cast<int32>(Power);
			}
			AgeVolumes(Other);
			break;
		}

		if (Hits == 0)
		{
			Fighter.HitRequest = 0;
			return;
		}
		Fighter.HitRequest = static_cast<int32>(Kind + 1);
		Fighter.Damage = Damage;
		Fighter.Source = Source;
		++Fighter.HitsTaken;
	}

	void SpawnProjectile(FBattlingFight& Fight, FBattlingFighter& Fighter, const FBattlingPoint& From, const FBattlingPoint* Toward, int32 KindNumber,
		int16 Power, uint32 Id)
	{
		FBattlingProjectile* Free = nullptr;
		for (FBattlingProjectile& Projectile : Fighter.Projectiles)
		{
			if (!Projectile.bActive)
			{
				Free = &Projectile;
				break;
			}
		}
		if (Free == nullptr)
		{
			return;
		}
		const FBattlingFighter& Other = Opponent(Fight, Fighter);
		FBattlingPoint Heading{Other.Position[0] - From[0], Other.Position[1] - From[1] - 0x90, Other.Position[2] - From[2]};
		if (Toward != nullptr)
		{
			Heading = {From[0] - (*Toward)[0], From[1] - (*Toward)[1], From[2] - (*Toward)[2]};
		}
		const FBattlingProjectileKind& Kind = Fight.Tables.ProjectileKinds[static_cast<std::size_t>(KindNumber)];
		Free->Direction = NormalizeToShort(Heading);
		Free->Homing = Kind.Homing;
		Free->Speed = Kind.Speed;
		Free->Life = Kind.Life;
		Free->Look = Kind.Look;
		Free->Id = Id;
		Free->Power = Power;
		Free->SpeedMode = Kind.SpeedMode;
		if (Kind.Sound != 0)
		{
			PlayFighterSound(Fight, Fighter, Kind.Sound, &Free->Position, EBattlingSoundPlace::Following);
		}
		Free->Velocity = ScaleVector(Free->Direction, Free->Speed);
		Free->Position = From;
		Free->Previous = From;
		Free->bActive = true;
	}

	void UpdateProjectiles(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		const FBattlingFighter& Other = Opponent(Fight, Fighter);
		Fighter.NearestDistance = 0x10000;
		Fight.Effects.Colour = Fighter.Colour;
		const int32 Side = Fighter.SideIndex();
		for (int32 Slot = 0; Slot < FBattlingFighter::ProjectileCount; ++Slot)
		{
			FBattlingProjectile& Projectile = Fighter.Projectiles[static_cast<std::size_t>(Slot)];
			if (!Projectile.bActive)
			{
				continue;
			}
			Projectile.Life = static_cast<int16>(Projectile.Life - 1);
			if (Projectile.Life == -1)
			{
				Projectile.bActive = false;
				continue;
			}
			if (TerrainHeight(Fight, Projectile.Position, false) < Projectile.Position[1])
			{
				Projectile.bActive = false;
				SpawnEffect(Fight, Projectile.Position, 1);
				continue;
			}
			Projectile.Previous = Projectile.Position;
			const FBattlingPoint ToTarget{Other.Centre[0] - Projectile.Position[0], Other.Centre[1] - Projectile.Position[1],
				Other.Centre[2] - Projectile.Position[2]};
			const int32 Distance = Length3(ToTarget[0], ToTarget[1], ToTarget[2]);
			Projectile.Distance = Distance;
			if (Distance < Fighter.NearestDistance)
			{
				Fighter.NearestDistance = Distance;
				Fighter.NearestProjectile = static_cast<int8>(Slot);
			}
			const FGteVector Unit = VectorNormalS({ToTarget[0], ToTarget[1], ToTarget[2]});
			Projectile.Direction = AverageShort12({Unit[0], Unit[1], Unit[2]}, Projectile.Direction, Projectile.Homing, 0x1000 - Projectile.Homing);
			Projectile.Velocity = ScaleVector(Projectile.Direction, Projectile.Speed);
			const auto Advance = [&Projectile]() {
				for (std::size_t Axis = 0; Axis < 3; ++Axis)
				{
					Projectile.Position[Axis] += Projectile.Velocity[Axis];
				}
			};
			const auto RandomShade = [&Fight]() {
				const int32 Value = Fight.Random.Next();
				return static_cast<uint8>(Value % 0xBF + 0x40);
			};
			switch (Projectile.Look)
			{
			case 0:
			{
				std::array<int32, 3> Half{};
				for (std::size_t Axis = 0; Axis < 3; ++Axis)
				{
					const int32 Value = Projectile.Velocity[Axis];
					Half[Axis] = static_cast<int16>((Value - (Value >> 31)) >> 1);
				}
				for (int32 Step = 0; Step < 2; ++Step)
				{
					for (std::size_t Axis = 0; Axis < 3; ++Axis)
					{
						Projectile.Position[Axis] += Half[Axis];
					}
					SpawnEffect(Fight, Projectile.Position, 10);
				}
				break;
			}
			case 1:
				Advance();
				SpawnEffect(Fight, Projectile.Position, 2);
				break;
			case 2:
				Advance();
				AddLine(Fight, Projectile.Previous, Projectile.Position, {0xFF, static_cast<uint8>((static_cast<uint8>(Fight.Pass) + Slot) * 0x40 - 1), 0x40});
				break;
			case 3:
				Advance();
				AddTrail(Fight, Side, Projectile.Position, Projectile.Id, 2);
				break;
			case 4:
			{
				const uint8 Shade = RandomShade();
				Advance();
				AddLine(Fight, Projectile.Previous, Projectile.Position, {Shade, Shade, 0xFF});
				break;
			}
			case 5:
			{
				const uint8 Shade = RandomShade();
				Advance();
				AddLine(Fight, Projectile.Previous, Projectile.Position, {Shade, Shade, 0xFF});
				AddTrail(Fight, Side, Projectile.Position, Projectile.Id, 2);
				break;
			}
			default:
				break;
			}
			switch (Projectile.SpeedMode)
			{
			case 1:
				if (Projectile.Speed < 0x70)
				{
					Projectile.Speed = static_cast<uint8>(Projectile.Speed + 0x10);
				}
				break;
			case 2:
				Projectile.Homing = static_cast<int16>(Projectile.Life * -0x30 + 0x600);
				break;
			case 3:
				Projectile.Homing = 0;
				break;
			default:
				break;
			}
		}
	}
}
