#include "BattlingFightOps.h"
#include "BattlingMath.h"

namespace Weltall::BattlingFightOps
{
	namespace
	{
		using namespace BattlingMath;

		constexpr int32 ArenaCentre = 0x3F80;
		constexpr int32 ArenaRadius = 16000;
		constexpr int32 Gravity = 10;
		constexpr uint8 SandDustTicks = 15;
		constexpr int32 SoundBounce = 0xD;

		enum : uint16
		{
			ButtonL1 = 0x04,
			ButtonR1 = 0x08,
			ButtonTriangle = 0x10,
			ButtonCircle = 0x20,
			ButtonCross = 0x40,
			ButtonSquare = 0x80,
			ButtonUp = 0x1000,
			ButtonRight = 0x2000,
			ButtonDown = 0x4000,
			ButtonLeft = 0x8000,
			ButtonsDpad = 0xF000,
		};

		int32 Decay(int32 Value, int32 Step)
		{
			Value -= Step;
			return Value < 0 ? 0 : Value;
		}

		int32 Grow(int32 Value, int32 Step, int32 Cap)
		{
			Value += Step;
			return Cap < Value ? Cap : Value;
		}

		/** Which side of the line through A and B a point lies on, as -1, 0 or 1. */
		int32 SideOf(const std::array<int32, 2>& A, const std::array<int32, 2>& B, int32 X, int32 Z)
		{
			const int32 Across = static_cast<int32>(static_cast<uint32>(A[1] - B[1]) * static_cast<uint32>(X) + static_cast<uint32>(B[0] - A[0]) * static_cast<uint32>(Z)
				+ static_cast<uint32>(A[0]) * static_cast<uint32>(B[1]) - static_cast<uint32>(B[0]) * static_cast<uint32>(A[1]));
			return Across < 0 ? -1 : (Across > 0 ? 1 : 0);
		}

		/**
		 * Which end of a fighter's body (top or base) crossed the line through A and B since the last tick, or lies nearer it:
		 * the point the program pushes the two gears apart by.
		 */
		const FBattlingPoint& NearEnd(const std::array<int32, 2>& A, const std::array<int32, 2>& B, const FBattlingFighter& Fighter)
		{
			const int32 Before = SideOf(A, B, Fighter.PreviousPosition[0], Fighter.PreviousPosition[2]);
			const int32 Top = SideOf(A, B, Fighter.Top[0], Fighter.Top[2]);
			const int32 Base = SideOf(A, B, Fighter.Base[0], Fighter.Base[2]);
			const FBattlingPoint Line{A[0], 0, A[1]};
			if (Top * Before < 0)
			{
				if (Base * Before < 0)
				{
					return DistanceXZ(Fighter.Base, Line) < DistanceXZ(Fighter.Top, Line) ? Fighter.Top : Fighter.Base;
				}
				return Fighter.Top;
			}
			if (Base * Before < 0)
			{
				return Fighter.Base;
			}
			return DistanceXZ(Fighter.Top, Line) < DistanceXZ(Fighter.Base, Line) ? Fighter.Top : Fighter.Base;
		}
	}

	int32 TerrainHeight(FBattlingFight& Fight, const FBattlingPoint& Point, bool bWithBed)
	{
		if (!FBattlingTerrain::Covers(Point[0], Point[2]))
		{
			Fight.Fault = EBattlingFault::OutsideHeightfield;
			return 0;
		}
		return Fight.Terrain.Height(Point[0], Point[1], Point[2], bWithBed);
	}

	uint32 TerrainCell(FBattlingFight& Fight, const FBattlingPoint& Point)
	{
		if (!FBattlingTerrain::Covers(Point[0], Point[2]))
		{
			Fight.Fault = EBattlingFault::OutsideHeightfield;
			return 0;
		}
		return Fight.Terrain.Cell(Point[0], Point[2]);
	}

	bool InLakeZone(FBattlingFight& Fight, const FBattlingPoint& Point)
	{
		if (Point[0] - ArenaCentre < 1 || Point[2] - ArenaCentre < 1)
		{
			return ((TerrainCell(Fight, Point) >> 24) & 3) == static_cast<uint32>(EBattlingGround::Water);
		}
		return true;
	}

	void ClampToArena(const FBattlingPoint& Position, FBattlingPoint& Velocity, int32 Radius, int32 StaleY)
	{
		const FGteLongVector Outward{Position[0] + Velocity[0] - ArenaCentre, StaleY, Position[2] + Velocity[2] - ArenaCentre};
		if (Length2(Outward[0], Outward[2]) <= Radius)
		{
			return;
		}
		// The velocity in the frame of the arena's radius there, its outward part dropped, then pulled in 8 units past the
		// edge at a time until the gear is inside.
		const FGteVector Normal = VectorNormalS(Outward);
		FGteMatrix Frame;
		Frame.M[0] = {Normal[2], 0, static_cast<int16>(-Normal[0])};
		Frame.M[1] = {0, 0x1000, 0};
		Frame.M[2] = {Normal[0], 0, Normal[2]};
		std::array<int32, 3> Local = ApplyMatrixLV(Frame, Velocity);
		const FGteMatrix Back = Transpose(Frame);
		Local[2] = 0;
		for (;;)
		{
			Velocity = ApplyMatrixLV(Back, Local);
			const int32 Reach = Length2(Position[0] + Velocity[0] - ArenaCentre, Position[2] + Velocity[2] - ArenaCentre);
			if (Reach <= Radius)
			{
				break;
			}
			Local[2] = Local[2] + 8 - (Reach - Radius);
		}
	}

	void ReadPad(FBattlingFight& Fight, FBattlingFighter& Fighter, const std::array<FBattlingPad, 2>& Pads)
	{
		const FBattlingPad& Pad = Pads[static_cast<std::size_t>(Fighter.SideIndex())];
		const bool bAnalog = static_cast<uint32>(Pad.Type - 3) < 2;
		// The port's stick also holds directions; an analog pad's holds none, so the bout steers by its tilt.
		const uint32 Held = bAnalog ? Pad.Held & ~static_cast<uint32>(Pad.StickHeld) : Pad.Held;
		const uint32 Pressed = Pad.Pressed;
		uint8 StickX = Pad.LeftX;
		uint8 StickY = Pad.LeftY;
		if (!bAnalog)
		{
			StickX = Fight.Tables.DpadX[(Held >> 12) & 15];
			StickY = Fight.Tables.DpadY[(Held >> 12) & 15];
		}
		if (Fighter.IsComputer())
		{
			return;
		}
		if (Fight.CameraSide != ((Fighter.Flags >> 17) & 1))
		{
			Fighter.SteerOffset = Fight.CameraSide == 0 ? Fighter.CameraOffset : static_cast<int16>(-Fighter.CameraOffset);
			Fighter.Flags = (Fighter.Flags & ~BattlingFlags::CameraSide) | (static_cast<uint32>(Fight.CameraSide & 1) << 17);
			Fighter.BaseYaw += 0x800;
			Fighter.Steer += 0x800;
		}
		if ((Pressed & ButtonTriangle) != 0)
		{
			QueuePush(Fighter, 1);
			++Fighter.AttackPresses;
		}
		if ((Pressed & ButtonCircle) != 0)
		{
			QueuePush(Fighter, 2);
			++Fighter.AttackPresses;
		}
		if ((Pressed & ButtonR1) != 0)
		{
			QueuePush(Fighter, 3);
			++Fighter.R1Presses;
		}
		if ((Pressed & ButtonSquare) != 0)
		{
			QueueClear(Fighter);
			QueuePush(Fighter, 4);
		}
		if ((Held & ButtonCross) != 0)
		{
			Fighter.Flags |= BattlingFlags::DashInput;
		}
		// Perfect Works' arena cancels as the guard is pressed too, and lets the queue be while it is held.
		const bool bArena = ArenaRules(Fight);
		if ((Pressed & (bArena ? ButtonCross | ButtonL1 : ButtonCross)) != 0)
		{
			QueueClear(Fighter);
		}
		int32 Down = Fighter.Deceleration;
		if ((Held & ButtonL1) == 0)
		{
			Fighter.Flags &= ~BattlingFlags::GuardCounter;
		}
		else
		{
			Fighter.Flags |= BattlingFlags::GuardInput;
			if (!bArena)
			{
				QueueClear(Fighter);
			}
			Down <<= 2;
			Fighter.Flags |= BattlingFlags::NoSteering;
		}
		const EBattlingGround Ground = Fighter.Ground();
		const bool bDesert = (Fighter.Kind & 4) != 0;
		int32 Up = Fighter.Acceleration;
		if (Ground == EBattlingGround::Sand && !bDesert)
		{
			Up = Fighter.Acceleration / 2;
		}
		if (Ground == EBattlingGround::Water && bDesert)
		{
			Up = Fighter.Acceleration / 2;
		}
		const bool bFrozen = (Fighter.Flags & BattlingFlags::NoSteering) != 0;
		if (bFrozen)
		{
			Down = (Down << 1) / 3;
		}

		const int32 Dy = (StickY - 0x80) * 2;
		const int32 Dx = (StickX - 0x80) * 2;
		uint32 StickAngle = static_cast<uint32>(GteRatan2(Dy, Dx, Fight.Tables.Arctangents) + Fighter.SteerOffset);
		std::array<int32, 4>& Axes = Fighter.Axes;
		if ((Held & ButtonsDpad) == 0)
		{
			if (SquareRoot0(Dy * Dy + Dx * Dx) < 0x30)
			{
				StickAngle = 0;
			}
			Axes[1] = (Dy >= 0 || bFrozen) ? Decay(Axes[1], Down) : Grow(Axes[1], Up, -Dy);
			Axes[3] = (Dy < 1 || bFrozen) ? Decay(Axes[3], Down) : Grow(Axes[3], Up, Dy);
			Axes[0] = (Dx >= 0 || bFrozen) ? Decay(Axes[0], Down) : Grow(Axes[0], Up, -Dx);
			Axes[2] = (Dx < 1 || bFrozen) ? Decay(Axes[2], Down) : Grow(Axes[2], Up, Dx);
		}
		else
		{
			Axes[1] = ((Held & ButtonUp) == 0 || bFrozen) ? Decay(Axes[1], Down) : Grow(Axes[1], Up, 0x100);
			Axes[3] = ((Held & ButtonDown) == 0 || bFrozen) ? Decay(Axes[3], Down) : Grow(Axes[3], Up, 0x100);
			Axes[0] = ((Held & ButtonLeft) == 0 || bFrozen) ? Decay(Axes[0], Down) : Grow(Axes[0], Up, 0x100);
			Axes[2] = ((Held & ButtonRight) == 0 || bFrozen) ? Decay(Axes[2], Down) : Grow(Axes[2], Up, 0x100);
		}
		const int32 Along = Axes[1] - Axes[3];
		const int32 Across = Axes[2] - Axes[0];
		const uint32 MoveAngle = static_cast<uint32>(GteRatan2(Across, Along, Fight.Tables.Arctangents));
		const int32 Size = SquareRoot0(Along * Along + Across * Across);
		int32 Magnitude = Size > 0x100 ? 0x100 : Size;
		if (Size < 0x30)
		{
			Magnitude = 0;
		}
		Fighter.Stick = Magnitude;
		Fighter.WantedDirection = Magnitude == 0 ? 0 : static_cast<int32>(MoveAngle) + Fighter.SteerOffset;
		if ((StickAngle & 0xFFF) - 0x201 < 0x3FF)
		{
			BackDashTry(Fight, Fighter);
			Fighter.Flags |= BattlingFlags::StickAway;
		}
		else
		{
			Fighter.Flags &= ~BattlingFlags::StickAway;
		}
		// Meant to clear the back-dash dust once the stick turns from the move; it sets it instead, without effect.
		if ((Fighter.Flags & BattlingFlags::BackDashDust) != 0 && Fighter.Stick != 0)
		{
			const int32 Raw = GteRatan2(StickY - 0x80, StickX - 0x80, Fight.Tables.Arctangents);
			int32 Turn = static_cast<int32>(MoveAngle & 0xFFF) - static_cast<int32>((static_cast<uint32>(Raw) + 0x400) & 0xFFF);
			if (Turn < 0)
			{
				Turn = -Turn;
			}
			if (0x200 < Turn)
			{
				Fighter.Flags |= BattlingFlags::BackDashDust;
			}
		}
	}

	void UpdateVelocity(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		Fighter.PreviousPosition = Fighter.Position;
		Fighter.PreviousBase = Fighter.Base;
		int32 Speed = Fighter.GroundSpeed;
		const int32 Toward = (Fighter.Flags & BattlingFlags::Side) != 0 ? Fight.FightAngle + 0x800 : Fight.FightAngle;
		if (Fighter.Movement == 0)
		{
			Fighter.BaseYaw = Fighter.Steer == 0 ? TurnAngle(Fighter.BaseYaw, Toward, 0x100, Fight.Random) : Toward;
			if (4 < Fight.CameraEase)
			{
				Fight.CameraEase = 4;
			}
		}
		else if (Fighter.Movement == BattlingMovement::Moving)
		{
			if (0x100 < Fight.DistanceXZ)
			{
				Fighter.BaseYaw = Fighter.Steer == 0 ? TurnAngle(Fighter.BaseYaw, Toward, 0x100, Fight.Random) : Toward;
			}
			if (2 < Fight.CameraEase)
			{
				Fight.CameraEase = 2;
			}
		}
		if (Fighter.Action == BattlingAction::BackDashing)
		{
			Speed = static_cast<int32>(static_cast<uint32>(Fighter.BackDashTimer) * static_cast<uint32>(-0x50)) / 3;
			Fighter.Yaw = Toward;
			Fighter.BaseYaw = Toward;
			Fighter.Steer = 0;
			Fighter.GroundSpeed = 0;
			Fighter.Axes = {};
		}
		else
		{
			Fighter.Yaw = Fighter.BaseYaw + Fighter.Steer;
		}
		if (Fighter.Action == BattlingAction::Attacking)
		{
			if ((Fighter.Flags & BattlingFlags::ActionStarted) != 0 || (Fighter.Movement & BattlingMovement::InAir) != 0)
			{
				Fighter.AttackYaw = static_cast<int16>(Toward);
			}
			Fighter.Flags = (Fighter.Flags & ~BattlingFlags::BackDashDust) | BattlingFlags::NoSteering;
			Fighter.Yaw = Fighter.AttackYaw;
		}
		else
		{
			Fighter.Flags &= ~BattlingFlags::NoSteering;
		}
		if ((Fighter.Flags & BattlingFlags::OnGround) == 0)
		{
			Fighter.Flags &= ~BattlingFlags::BackDashDust;
		}
		else
		{
			Fighter.Velocity[0] = -(GteSin(Fighter.BaseYaw + Fighter.Steer) * Speed) >> 12;
			Fighter.Velocity[2] = -(GteCos(Fighter.BaseYaw + Fighter.Steer) * Speed) >> 12;
		}
		Fighter.Velocity[1] += Gravity;
		if (Fighter.Lunge != 0 && Fighter.Position[1] == Fighter.GroundHeight)
		{
			Fighter.Lunge = Over256(Fighter.Lunge * 0xA0);
			Fighter.LungePush[0] = -(GteSin(Toward) * Fighter.Lunge) >> 12;
			Fighter.LungePush[2] = -(GteCos(Toward) * Fighter.Lunge) >> 12;
			Fighter.Velocity[0] += Fighter.LungePush[0];
			Fighter.Velocity[1] += Fighter.LungePush[1];
			Fighter.Velocity[2] += Fighter.LungePush[2];
		}
		if (0x280 < Fight.Distance && Fight.Settings.bRubberBand)
		{
			const FBattlingFighter& Other = Opponent(Fight, Fighter);
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				Fighter.Velocity[Axis] += (Other.Position[Axis] - Fighter.Position[Axis]) / 0x30;
			}
		}
		int32 Friction = 0x80;
		switch (Fighter.Ground())
		{
		case EBattlingGround::Hard:
			Friction = 0xFF;
			break;
		case EBattlingGround::Water:
		case EBattlingGround::Sand:
			Friction = 0;
			break;
		default:
			break;
		}
		if (Fighter.Movement == BattlingMovement::Down)
		{
			Friction = 0xFF;
		}
		if ((Fighter.Flags & BattlingFlags::OnGround) != 0)
		{
			Fighter.KnockBack[0] = Over256(Fighter.KnockBack[0] * Friction);
			Fighter.KnockBack[2] = Over256(Fighter.KnockBack[2] * Friction);
		}
		for (std::size_t Axis = 0; Axis < 3; ++Axis)
		{
			Fighter.Velocity[Axis] += Fighter.KnockBack[Axis];
		}
	}

	void Move(FBattlingFight& Fight, FBattlingFighter& Fighter)
	{
		if (!FBattlingTerrain::Covers(Fighter.Position[0], Fighter.Position[2]))
		{
			Fight.Fault = EBattlingFault::OutsideHeightfield;
			return;
		}
		const FBattlingTerrainSample Ground = Fight.Terrain.Sample(Fighter.Position[0], Fighter.Position[1], Fighter.Position[2], true);
		Fighter.GroundHeight = Ground.Height;
		const uint32 Type = (TerrainCell(Fight, Fighter.Position) >> 24) & 3;
		Fighter.Flags = (Fighter.Flags & ~BattlingFlags::Ground) | (Type << 29);
		if (Type == static_cast<uint32>(EBattlingGround::Sand))
		{
			Fighter.SandDust = SandDustTicks;
		}
		// The clamp normalizes a vector whose height it never sets: the stack there still holds the scaled normal's height
		// the ground's triangle left.
		ClampToArena(Fighter.Position, Fighter.Velocity, ArenaRadius, Ground.ScaledNormalY);
		Fighter.Flags &= ~BattlingFlags::OnGround;
		if (Fighter.GroundHeight < Fighter.Position[1] + Fighter.Velocity[1])
		{
			Fighter.Flags |= BattlingFlags::OnGround;
			if (Fighter.Movement == BattlingMovement::Down)
			{
				if ((Fighter.Flags2 & BattlingFlags2::Bounced) == 0)
				{
					PlayFighterSound(Fight, Fighter, SoundBounce, &Fighter.Position, EBattlingSoundPlace::Following);
					Vibrate(Fight, Fighter, 6);
				}
				Fighter.Flags2 |= BattlingFlags2::Bounced;
				Fighter.Velocity[1] = Fighter.Velocity[1] < 0x40 ? 0 : -Fighter.Velocity[1] / 3;
			}
			else
			{
				Fighter.Velocity[1] = 0;
			}
			Fighter.Position[1] = Fighter.GroundHeight;
		}
		for (std::size_t Axis = 0; Axis < 3; ++Axis)
		{
			Fighter.Position[Axis] += Fighter.Velocity[Axis];
		}
	}

	void Separate(FBattlingFight& Fight)
	{
		if (0x300 < Fight.DistanceXZ)
		{
			return;
		}
		FBattlingFighter& One = Fight.Fighters[0];
		FBattlingFighter& Two = Fight.Fighters[1];
		const int32 OneBase = One.Base[1];
		const int32 OneTop = One.Top[1];
		const int32 TwoTop = Two.Top[1];
		const int32 TwoBase = Two.Base[1];
		const bool bOverlap = (OneBase < TwoTop && TwoBase < OneBase) || (TwoTop < OneTop && OneTop < TwoBase) || (TwoBase < OneTop && OneBase < TwoBase)
			|| (OneTop < TwoTop && TwoTop < OneBase);
		if (!bOverlap)
		{
			return;
		}
		const std::array<int32, 2> Middle{(One.PreviousPosition[0] + Two.PreviousPosition[0]) / 2, (One.PreviousPosition[2] + Two.PreviousPosition[2]) / 2};
		const std::array<int32, 2> Across{(One.PreviousPosition[2] - Two.PreviousPosition[2]) + Middle[0],
			(Two.PreviousPosition[0] - One.PreviousPosition[0]) + Middle[1]};
		const FBattlingPoint& OneEnd = NearEnd(Middle, Across, One);
		const FBattlingPoint& TwoEnd = NearEnd(Middle, Across, Two);
		FBattlingPoint A{};
		FBattlingPoint B{};
		for (std::size_t Axis = 0; Axis < 3; ++Axis)
		{
			A[Axis] = OneEnd[Axis] - One.PreviousPosition[Axis] + One.Position[Axis];
			B[Axis] = TwoEnd[Axis] - Two.PreviousPosition[Axis] + Two.Position[Axis];
		}
		int32 Apart = DistanceXZ(A, B);
		const int32 Reach = One.File->PushRadius + Two.File->PushRadius;
		if (Reach <= Apart)
		{
			return;
		}
		if (DistanceXZ(One.PreviousPosition, A) <= DistanceXZ(One.PreviousPosition, B))
		{
			Apart = -Apart;
		}
		const int32 Overlap = Apart + Reach;
		const int32 Angle = GteRatan2(One.PreviousPosition[0] - Two.PreviousPosition[0], One.PreviousPosition[2] - Two.PreviousPosition[2],
			Fight.Tables.Arctangents);
		int32 SpeedOne = Length2(One.Velocity[0], One.Velocity[2]);
		int32 SpeedTwo = Length2(Two.Velocity[0], Two.Velocity[2]);
		int32 Total = SpeedOne + SpeedTwo;
		int32 ShareTwo = SpeedOne * Overlap;
		if (Total == 0)
		{
			SpeedTwo = 1;
			Total = 2;
			ShareTwo = Overlap;
		}
		const int32 PushTwo = (ShareTwo << 8) / Total;
		const int32 PushOne = (SpeedTwo * Overlap * 0x100) / Total;
		One.Position[0] += GteSin(Angle) * PushOne >> 20;
		One.Position[2] += GteCos(Angle) * PushOne >> 20;
		Two.Position[0] -= GteSin(Angle) * PushTwo >> 20;
		Two.Position[2] -= GteCos(Angle) * PushTwo >> 20;
	}
}
