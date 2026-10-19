#include "Battle/Show/BattleShowScreenWave.h"

#include <catch_amalgamated.hpp>

using namespace Weltall;

TEST_CASE("The screen wave steps a frame every two vertical blanks, then goes black until its frame goes on", "[Battle]")
{
	FBattleScreenWave Wave;
	CHECK_FALSE(Wave.IsRunning());
	Wave.Start();
	REQUIRE(Wave.IsRunning());
	CHECK(Wave.Mesh().Kind() == EBattleScreenMesh::ScreenWave);
	CHECK(Wave.Mesh().Steps() == 0);

	// Each frame's wait of two blanks comes before its step.
	Wave.Blank();
	CHECK(Wave.Mesh().Steps() == 0);
	Wave.Blank();
	CHECK(Wave.Mesh().Steps() == 1);
	while (Wave.BlanksPassed() < 2 * FBattleScreenWave::FrameCount)
	{
		Wave.Blank();
	}
	CHECK(Wave.Mesh().Steps() == FBattleScreenWave::FrameCount);
	CHECK_FALSE(Wave.IsBlack());

	// Then three waits of two, the screen cleared after the second; no frame steps in them.
	FBattleScreenMesh Alone;
	Alone.Create(EBattleScreenMesh::ScreenWave);
	for (int32 Frame = 0; Frame < FBattleScreenWave::FrameCount; ++Frame)
	{
		Alone.Step((Frame + 1) & 1);
	}
	while (Wave.BlanksPassed() < FBattleScreenWave::BlackFrom)
	{
		CHECK_FALSE(Wave.IsBlack());
		Wave.Blank();
	}
	CHECK(Wave.IsBlack());
	CHECK(Wave.IsRunning());
	Wave.Blank();
	Wave.Blank();
	CHECK_FALSE(Wave.IsRunning());
	CHECK(Wave.BlanksPassed() == FBattleScreenWave::Blanks);
	CHECK(Wave.Mesh().Steps() == FBattleScreenWave::FrameCount);
	CHECK(Wave.Mesh().Brightness() == Alone.Brightness());
	CHECK(Wave.Mesh().Phase() == Alone.Phase());
	CHECK(Wave.Mesh().Amplitude() == Alone.Amplitude());
	CHECK(Wave.Mesh().Depth() == Alone.Depth());

	// A blank past its end changes nothing; a frame shown without its blanks ends it at once.
	Wave.Blank();
	CHECK(Wave.BlanksPassed() == FBattleScreenWave::Blanks);
	Wave.Start();
	Wave.Stop();
	CHECK_FALSE(Wave.IsRunning());
}

TEST_CASE("The screen wave darkens from its hundred and first frame, to black by its last", "[Battle]")
{
	FBattleScreenMesh Mesh;
	Mesh.Create(EBattleScreenMesh::ScreenWave);
	for (int32 Frame = 1; Frame <= 0x64; ++Frame)
	{
		Mesh.Step(Frame & 1);
	}
	CHECK(Mesh.Brightness() == 0x80);
	Mesh.Step(1);
	CHECK(Mesh.Brightness() == 0x7C);
	for (int32 Frame = 0x66; Frame <= FBattleScreenWave::FrameCount; ++Frame)
	{
		Mesh.Step(Frame & 1);
	}
	// Every corner's colour is the brightness plus its ripple's, clamped: none is left above black.
	std::vector<FBattleMeshTriangle> Triangles;
	Mesh.Draw(0, 0, Mesh.Columns(), Triangles);
	int32 Lit = 0;
	for (const FBattleMeshTriangle& Triangle : Triangles)
	{
		for (const uint8 Color : Triangle.Colors)
		{
			Lit += Color != 0;
		}
	}
	CHECK(Lit == 0);
}
