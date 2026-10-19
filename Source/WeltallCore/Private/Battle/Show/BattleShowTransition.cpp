#include "Battle/Show/BattleShowTransition.h"

#include "Game/PsxRandom.h"
#include "Graphics/GteMath.h"
#include "Graphics/GteVectors.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr int32 CellTexels = 16;
		/** A screen pixel is 32 units of the shards' view at the start. */
		constexpr int32 UnitsPerPixel = 0x20;
		/** Each side's shard x is its column's pixel less this, y its row's start plus 200h a row. */
		constexpr std::array<int32, FBattleShatter::Sides> ColumnBias{0x9B, 0x95};
		constexpr std::array<int32, FBattleShatter::Sides> FirstRowY{-0xD60, -0xCA0};
		constexpr int32 RowStep = 0x200;
		constexpr int32 StartDepth = 0x4000;
		/** The launch's length is 500 to 1499 whole units along the view axis, 16.16. */
		constexpr int32 LaunchBase = 500;
		constexpr int32 LaunchSpread = 1000;
		constexpr int32 FallBase = 0x70800;
		constexpr int32 FallSpread = 0x640;
		constexpr int32 FallScale = 0x100;
		constexpr int32 Fade = 6;
		constexpr int32 BackgroundFade = 12;

		uint8 Darken(uint8 Value, int32 By)
		{
			return static_cast<uint8>(std::clamp(Value - By, 0, 255));
		}

		/** A cell's triangle as the transition makes it, with its seven random numbers. */
		FBattleScreenShard MakeShard(int32 Side, int32 Row, int32 Column, FPsxRandom& Random, const FGteArctangents& Arctangents)
		{
			// The distance that makes a shard's tilt reach 800h: the screen's half diagonal in units.
			const int32 Reach = SquareRoot0(0x9500) << 10;
			FBattleScreenShard Shard;
			Shard.Position = {(Column * CellTexels - ColumnBias[static_cast<std::size_t>(Side)]) * UnitsPerPixel,
				FirstRowY[static_cast<std::size_t>(Side)] + Row * RowStep, StartDepth};
			const int32 Launch = -(LaunchBase + Random.Next() % LaunchSpread) * 0x10000;
			const int32 Distance = SquareRoot0(GteSquare(Shard.Position[0]) + GteSquare(Shard.Position[1]));
			Shard.Delay = Distance >> 10;
			const int32 Heading = GteRatan2(Shard.Position[1], Shard.Position[0], Arctangents);
			const int32 Turn = Random.Next();
			const int32 Tilt = (Distance << 11) / Reach;
			const int32 Wobble = Random.Next();
			const FGteVector Angles{0, static_cast<int16>(Tilt - 0x20 + (Wobble & 0x3F)), static_cast<int16>(Heading + 0x600 + (Turn & 0x3FF))};
			Shard.Velocity = ApplyMatrixLV(RotationZyx(Angles), {0, 0, Launch});
			Shard.Fall = (Random.Next() % FallSpread) * -FallScale + FallBase;
			Shard.Spin[0] = static_cast<int16>((Random.Next() & 0xFF) - 0x7F);
			Shard.Spin[1] = static_cast<int16>((Random.Next() & 0xFF) - 0x7F);
			Shard.Spin[2] = static_cast<int16>((Random.Next() & 0x1FF) - 0xFF);
			Shard.Colors = {{{0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}}};
			return Shard;
		}

		void StepShard(FBattleScreenShard& Shard, int32 Buffer)
		{
			if (Shard.Delay != 0)
			{
				--Shard.Delay;
				return;
			}
			std::array<uint8, 3>& Color = Shard.Colors[static_cast<std::size_t>(Buffer)];
			for (uint8& Channel : Color)
			{
				Channel = Darken(Channel, Fade);
			}
			for (std::size_t Axis = 0; Axis < 3; ++Axis)
			{
				// Only each velocity's whole units move the shard.
				Shard.Position[Axis] += Shard.Velocity[Axis] >> 16;
				Shard.Rotation[Axis] = static_cast<int16>(Shard.Rotation[Axis] + Shard.Spin[Axis]);
			}
			for (int32& Speed : Shard.Velocity)
			{
				Speed -= Speed / 16;
			}
			Shard.Velocity[1] += Shard.Fall;
		}
	}

	void FBattleShatter::Create(FPsxRandom& Random, const FGteArctangents& Arctangents)
	{
		StepCount = 0;
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			for (int32 Row = 0; Row < Rows; ++Row)
			{
				for (int32 Column = 0; Column < Columns; ++Column)
				{
					Shards[static_cast<std::size_t>(Index(Side, Row, Column))] = MakeShard(Side, Row, Column, Random, Arctangents);
				}
			}
		}
	}

	void FBattleShatter::Step(int32 Buffer)
	{
		++StepCount;
		for (FBattleScreenShard& Shard : Shards)
		{
			StepShard(Shard, Buffer);
		}
	}

	FGteMatrix FBattleShatter::DrawMatrix(const FBattleScreenShard& Shard)
	{
		FGteMatrix Matrix = RotationXyz(Shard.Rotation);
		Matrix.T = Shard.Position;
		return Matrix;
	}

	int32 FBattleShatter::DrawEntry(const FBattleScreenShard& Shard, int32 Side)
	{
		const FGteMatrix Matrix = DrawMatrix(Shard);
		const FGteVector& Corner = Corners[static_cast<std::size_t>(Side)][2];
		const int32 Depth = ((Matrix.M[2][0] * Corner[0] + Matrix.M[2][1] * Corner[1] + Matrix.M[2][2] * Corner[2]) >> 12) + Matrix.T[2];
		return (std::clamp(Depth, 0, 0xFFFF) >> 2) >> 6;
	}

	std::array<std::array<int32, 2>, 3> FBattleShatter::PicturePixels(int32 Side, int32 Row, int32 Column)
	{
		const int32 Left = Column * CellTexels;
		const int32 Right = Left + CellTexels;
		const int32 Top = Row * CellTexels;
		const int32 Bottom = Top + CellTexels;
		if (Side == 0)
		{
			return {{{Left, Top}, {Right, Top}, {Left, Bottom}}};
		}
		return {{{Right, Top}, {Right, Bottom}, {Left, Bottom}}};
	}

	void FBattleShatterWings::Create(FPsxRandom Random, const FGteArctangents& Arctangents)
	{
		Shards.assign(static_cast<std::size_t>(FBattleShatter::Sides * FBattleShatter::Rows * 2 * ColumnsEachSide), {});
		for (int32 Side = 0; Side < FBattleShatter::Sides; ++Side)
		{
			for (int32 Row = 0; Row < FBattleShatter::Rows; ++Row)
			{
				for (int32 Column = FirstColumn; Column < EndColumn; ++Column)
				{
					if (Column < 0 || Column >= FBattleShatter::Columns)
					{
						Shards[static_cast<std::size_t>(Index(Side, Row, Column))] = MakeShard(Side, Row, Column, Random, Arctangents);
					}
				}
			}
		}
	}

	void FBattleShatterWings::Step(int32 Buffer)
	{
		for (FBattleScreenShard& Shard : Shards)
		{
			StepShard(Shard, Buffer);
		}
	}

	std::array<std::array<uint8, 2>, 3> FBattleShatter::Texels(int32 Side, int32 Row, int32 Column)
	{
		const uint8 Left = static_cast<uint8>((Column * CellTexels) & 0x3F);
		const uint8 Right = static_cast<uint8>(Left + CellTexels);
		const uint8 Top = static_cast<uint8>(Row * CellTexels);
		const uint8 Bottom = static_cast<uint8>(Top + CellTexels);
		if (Side == 0)
		{
			return {{{Left, Top}, {Right, Top}, {Left, Bottom}}};
		}
		return {{{Right, Top}, {Right, Bottom}, {Left, Bottom}}};
	}

	uint16 FBattleShatter::TexturePage(int32 Column)
	{
		// GetTPage(2, 1, x, 100h): 16-bit texels, blend mode 1, the page holding the cell's column.
		const int32 X = PictureX + Column * CellTexels;
		return static_cast<uint16>(2 << 7 | 1 << 5 | (PictureY & 0x100) >> 4 | (X & 0x3FF) >> 6);
	}

	namespace
	{
		/** Both meshes start this far ahead of the eye: 16 units a pixel of the screen. */
		constexpr int32 MeshDepth = 0x2000;
		constexpr int32 MeshUnitsPerPixel = 0x10;
		constexpr int32 TriangleCell = 32;
		constexpr int32 WaveCell = 16;
		/** The triangles: each side's place in the first cell, and the corners about a triangle's place (the shatter's). */
		constexpr std::array<std::array<int32, 2>, FBattleScreenMesh::Sides> TrianglePlace{{{-0x960, -0x660}, {-0x8A0, -0x5A0}}};
		constexpr int32 TriangleDim = 3;
		constexpr int32 TriangleApproach = 0x40;
		constexpr int32 TriangleNearest = 0x3F;
		constexpr uint8 NeutralColor = 0x80;
		/** The wave: each side's place in the first cell, and its corners about it. */
		constexpr std::array<std::array<int32, 2>, FBattleScreenMesh::Sides> WavePlace{{{-0x9B0, -0x6B0}, {-0x950, -0x650}}};
		constexpr std::array<std::array<std::array<int16, 2>, 3>, FBattleScreenMesh::Sides> WaveCorners{{
			{{{-0x50, -0x50}, {0xB0, -0x50}, {-0x50, 0xB0}}},
			{{{0x50, -0xB0}, {0x50, 0x50}, {-0xB0, 0x50}}},
		}};
		/** A triangle the GTE's sum for a screen coordinate fell below its range for is not linked. */
		constexpr uint32 ScreenSumBelowRange = 1u << 15;

		/** The picture's pixels a cell's triangle shows: side 0 the cell's upper left, side 1 its lower right. */
		std::array<std::array<int32, 2>, 3> CellPixels(int32 Side, int32 Row, int32 Column, int32 Cell)
		{
			const int32 Left = Column * Cell;
			const int32 Right = Left + Cell;
			const int32 Top = Row * Cell;
			const int32 Bottom = Top + Cell;
			if (Side == 0)
			{
				return {{{Left, Top}, {Right, Top}, {Left, Bottom}}};
			}
			return {{{Right, Top}, {Right, Bottom}, {Left, Bottom}}};
		}
	}

	void FBattleScreenMesh::Create(EBattleScreenMesh InKind)
	{
		Transition = InKind;
		Count = 0;
		Distance = MeshDepth;
		Colors = {NeutralColor, NeutralColor};
		Light = NeutralColor;
		Approach = 0x40;
		WaveHeight = InKind == EBattleScreenMesh::WaveSine ? 0x400 : 0;
		WavePhase = InKind == EBattleScreenMesh::WaveSine ? 0x700 : 0;
	}

	void FBattleScreenMesh::Step(int32 Buffer)
	{
		++Count;
		switch (Transition)
		{
		case EBattleScreenMesh::Triangles:
			Distance -= TriangleApproach;
			[[fallthrough]];
		case EBattleScreenMesh::TrianglesStill:
			Colors[static_cast<std::size_t>(Buffer)] = Darken(Colors[static_cast<std::size_t>(Buffer)], TriangleDim);
			break;
		case EBattleScreenMesh::Wave:
			Approach += 10;
			WaveHeight += 0x600;
			Distance -= Approach;
			if (Count >= 0x19)
			{
				Light -= 0x14;
			}
			break;
		case EBattleScreenMesh::WaveSine:
			Approach += 1;
			WavePhase += 0xA0;
			Distance -= 0x3C;
			if (Count < 0x43)
			{
				WaveHeight += 0x80;
			}
			else
			{
				Light -= 0x18;
			}
			break;
		case EBattleScreenMesh::ScreenWave:
			// The phase runs faster as the count it takes a quarter of grows.
			Approach += 1;
			WavePhase += 0x80 + (Approach >> 2);
			WaveHeight += 0x40;
			Distance -= 0x1E;
			if (Count >= 0x65)
			{
				Light -= 4;
			}
			break;
		}
	}

	void FBattleScreenMesh::Draw(int32 Buffer, int32 FirstColumn, int32 EndColumn, std::vector<FBattleMeshTriangle>& Triangles) const
	{
		Triangles.clear();
		const bool bWave = IsWave();
		const int32 Cell = bWave ? WaveCell : TriangleCell;
		const int32 CellUnits = Cell * MeshUnitsPerPixel;
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const std::array<int32, 2>& Place = (bWave ? WavePlace : TrianglePlace)[static_cast<std::size_t>(Side)];
			for (int32 Row = 0; Row < Rows(); ++Row)
			{
				for (int32 Column = FirstColumn; Column < EndColumn; ++Column)
				{
					FBattleMeshTriangle& Triangle = Triangles.emplace_back();
					Triangle.Pixels = CellPixels(Side, Row, Column, Cell);
					Triangle.Matrix = FGteMatrix::Identity();
					const int32 X = Place[0] + Column * CellUnits;
					const int32 Y = Place[1] + Row * CellUnits;
					if (!bWave)
					{
						// Each triangle at its own place, all as far ahead; the buffer's packet holds its colour.
						Triangle.Matrix.T = {X, Y, Distance};
						Triangle.Corners = FBattleShatter::Corners[static_cast<std::size_t>(Side)];
						Triangle.Colors.fill(Colors[static_cast<std::size_t>(Buffer)]);
						Triangle.bDrawn = Distance > TriangleNearest;
						continue;
					}
					Triangle.Matrix.T = {0, 0, Distance};
					for (std::size_t Corner = 0; Corner < 3; ++Corner)
					{
						const std::array<int16, 2>& About = WaveCorners[static_cast<std::size_t>(Side)][Corner];
						const int16 CornerX = static_cast<int16>(About[0] + X);
						const int16 CornerY = static_cast<int16>(About[1] + Y);
						// How far the corner lies from the middle sets where on the wave it rides.
						const int32 FromMiddle = SquareRoot0(GteSquare(CornerX) + GteSquare(CornerY));
						int32 Height = Transition == EBattleScreenMesh::Wave ? GteCos(FromMiddle * 3 / 5) * WaveHeight
							: GteSin(WavePhase + (FromMiddle << 1)) * WaveHeight;
						if (Height < 0)
						{
							Height += 0xFFF;
						}
						Triangle.Corners[Corner] = {CornerX, CornerY, static_cast<int16>(Height >> 14)};
						Triangle.Colors[Corner] = static_cast<uint8>(std::clamp((Height >> 19) + Light, 0, 255));
					}
				}
			}
		}
	}

	FBattleMeshLink FBattleScreenMesh::Link(const FBattleMeshTriangle& Triangle) const
	{
		FGte Gte;
		Gte.Transform = Triangle.Matrix;
		Gte.OffsetX = ScreenCentreX << 16;
		Gte.OffsetY = ScreenCentreY << 16;
		Gte.ProjectionDistance = ProjectionDistance;
		FBattleMeshLink Result;
		uint32 Flags = 0;
		for (std::size_t Corner = 0; Corner < 3; ++Corner)
		{
			const FGteProjection Projected = Gte.Rtps(Triangle.Corners[Corner]);
			Result.Screen[Corner] = {Projected.ScreenX, Projected.ScreenY};
			Flags |= Projected.Flag;
			// A quarter of the last corner's depth, shifted down by six.
			Result.Entry = (Projected.ScreenZ >> 2) >> 6;
		}
		Result.bLinked = Triangle.bDrawn && (!IsWave() || (Flags & ScreenSumBelowRange) == 0);
		return Result;
	}

	void FBattleDefaultTransition::Start(FPsxRandom& Random, const FGteArctangents& Arctangents)
	{
		FramesLeft = FrameCount;
		Stage = 1;
		DrawBuffer = 0;
		BackgroundColors = {{{0xFF, 0xFF, 0xFF}, {0xFF, 0xFF, 0xFF}}};
		Pieces.Create(Random, Arctangents);
		// The wings draw from where the game's generator stands, without moving it.
		WingPieces.Create(Random, Arctangents);
	}

	int32 FBattleDefaultTransition::Frame()
	{
		if (FramesLeft > 0)
		{
			--FramesLeft;
		}
		DrawBuffer = 1 - DrawBuffer;
		int32 Ran = 0;
		if (Stage < LastStage)
		{
			Ran = Stage++;
		}
		Pieces.Step(DrawBuffer);
		WingPieces.Step(DrawBuffer);
		for (uint8& Channel : BackgroundColors[static_cast<std::size_t>(DrawBuffer)])
		{
			Channel = Darken(Channel, BackgroundFade);
		}
		return Ran;
	}
}
