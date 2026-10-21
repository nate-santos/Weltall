#include "Algo/Count.h"
#include "Field/FieldScene.h"
#include "Graphics/TexelKinds.h"
#include "Graphics/Vram.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/VramTexture.h"
#include "Rendering/WorldMapRendering.h"
#include "WorldMap/WorldMapFrame.h"

#include <array>
#include <utility>

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 ScreenWidth = 320;
	constexpr int32 ScreenHeight = 216;
	constexpr int32 Distance = 256;
	/** A 4-bit texture page at VRAM (0, 0) and its CLUT at (0, 256). */
	constexpr uint16 Page = 0;
	constexpr uint16 Clut = 256 << 6;

	/** Two rows of a 4-bit texture: 16 texels of colour 1 then 16 of colour 2 across 32; the CLUT: transparent, red, green. */
	Weltall::FVram MakeVram()
	{
		Weltall::FVram Vram;
		Vram.Upload({{0, 0, 8, 1}, {0x1111, 0x1111, 0x1111, 0x1111, 0x2222, 0x2222, 0x2222, 0x2222}});
		Vram.Upload({{0, 1, 8, 1}, {0x1111, 0x1111, 0x1111, 0x1111, 0x2222, 0x2222, 0x2222, 0x2222}});
		Vram.Upload({{0, 256, 3, 1}, {0x0000, 0x001F, 0x03E0}});
		return Vram;
	}

	/** A quad over the original's screen pixels [X0, X1) x [Y0, Y1), at a view depth, for a screen centred at (CenterX, CenterY). */
	Weltall::FWorldMapPolygon ScreenQuad(int32 X0, int32 Y0, int32 X1, int32 Y1, float Depth, int32 CenterY, int32 CenterX = 160)
	{
		Weltall::FWorldMapPolygon Polygon;
		Polygon.CornerCount = 4;
		for (std::size_t Corner = 0; Corner < 4; ++Corner)
		{
			const float X = static_cast<float>((Corner & 1) != 0 ? X1 : X0);
			const float Y = static_cast<float>((Corner & 2) != 0 ? Y1 : Y0);
			Polygon.View[Corner] = {(X - static_cast<float>(CenterX)) * Depth / Distance, (Y - static_cast<float>(CenterY)) * Depth / Distance, Depth};
		}
		return Polygon;
	}

	TArray<FColor> Render(const Weltall::FWorldMapFrame& Frame, const FVramTexture& Vram, float ColumnScale = 1.0f,
		EFieldSorting Sorting = EFieldSorting::PaintersOrder)
	{
		const FFieldSceneMesh NoMesh{Weltall::FFieldScene{}};
		FWorldMapRenderOptions Options;
		Options.Sorting = Sorting;
		Options.AntiAliasing = EFieldAntiAliasing::None;
		FFieldSceneDraw Draw;
		BuildWorldMapFrameDraw(Frame, nullptr, Options, Draw, static_cast<float>(ScreenHeight), ColumnScale);
		return RenderFieldSceneOffscreen(NoMesh, Vram, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapRenderingTest, "Weltall.Rendering.WorldMapFollowsTheOriginalsScreen",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// On foot the centre is 140 lines down; a flat blue quad where the original's GTE would put it.
	Weltall::FWorldMapFrame Walking;
	Walking.ScreenCenterY = 140;
	Walking.ProjectionDistance = Distance;
	Weltall::FWorldMapPolygon& Flat = Walking.Polygons.emplace_back(ScreenQuad(40, 20, 100, 60, 1000.0f, 140));
	Flat.Colors.fill({0, 0, 255});
	// The band's texture window: 32 texels of the window's 32 from texel 8, which wrap after 24: red 8-15, green 16-31, red 0-7.
	Weltall::FWorldMapPolygon& Band = Walking.Polygons.emplace_back(ScreenQuad(160, 100, 224, 120, 2000.0f, 140));
	Band.bTextured = true;
	Band.bRaw = true;
	Band.TexturePage = Page;
	Band.Clut = Clut;
	Band.WindowWidth = 32;
	for (std::size_t Corner = 0; Corner < 4; ++Corner)
	{
		Band.TexCoords[Corner] = {(Corner & 1) != 0 ? 40.0f : 8.0f, (Corner & 2) != 0 ? 2.0f : 0.0f};
	}
	Band.TexelMin = {0, 0};
	Band.TexelMax = {31, 1};

	// Flying the centre is 120: the same view-space quad lands 20 lines higher.
	Weltall::FWorldMapFrame Flying = Walking;
	Flying.ScreenCenterY = 120;
	Flying.Polygons.resize(1);
	Flying.Polygons[0] = ScreenQuad(40, 20, 100, 60, 1000.0f, 140);
	Flying.Polygons[0].Colors.fill({0, 0, 255});

	// A screen of 640 columns across the same width (the arena's winner screen): a quad over its columns 380-440 covers 190-220.
	Weltall::FWorldMapFrame Wide = Walking;
	Wide.Polygons.resize(1);
	Wide.Polygons[0] = ScreenQuad(380, 20, 440, 60, 1000.0f, 140, 320);
	Wide.Polygons[0].Colors.fill({0, 0, 255});

	const TArray<FColor> OnFoot = Render(Walking, Units);
	const TArray<FColor> InFlight = Render(Flying, Units);
	const TArray<FColor> Halved = Render(Wide, Units, 0.5f);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), OnFoot.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("both"), InFlight.Num(), OnFoot.Num())
		|| !TestEqual(TEXT("all three"), Halved.Num(), OnFoot.Num()))
	{
		return false;
	}
	const auto Near = [this](const TArray<FColor>& Pixels, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Pixels[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(OnFoot, TEXT("inside the quad"), 42, 22, 0, 0, 255);
	Near(OnFoot, TEXT("inside the quad's far corner"), 97, 57, 0, 0, 255);
	Near(OnFoot, TEXT("above the quad"), 70, 17, 0, 0, 0);
	Near(OnFoot, TEXT("below the quad"), 70, 63, 0, 0, 0);
	Near(OnFoot, TEXT("the window's texels 8-15 first"), 164, 110, 255, 0, 0);
	Near(OnFoot, TEXT("then 16-31"), 190, 110, 0, 255, 0);
	Near(OnFoot, TEXT("then wrapped to 0-7"), 220, 110, 255, 0, 0);
	Near(InFlight, TEXT("20 lines higher flying"), 70, 5, 0, 0, 255);
	Near(InFlight, TEXT("and clear below"), 70, 45, 0, 0, 0);
	Near(Halved, TEXT("the wide screen's quad at half its columns"), 192, 30, 0, 0, 255);
	Near(Halved, TEXT("to its right edge"), 218, 30, 0, 0, 255);
	Near(Halved, TEXT("clear left of it"), 187, 30, 0, 0, 0);
	Near(Halved, TEXT("clear right of it"), 223, 30, 0, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapTexelKindsRenderingTest, "Weltall.Rendering.WorldMapLeavesOutPassesWithNoTexelsToDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapTexelKindsRenderingTest::RunTest(const FString&)
{
	// Beside the strip's CLUT (transparent, red, green), one of blended colours only and one of plain and blended.
	Weltall::FVram Vram = MakeVram();
	Vram.Upload({{16, 256, 3, 1}, {0x0000, 0x801F, 0x83E0}});
	Vram.Upload({{32, 256, 3, 1}, {0x0000, 0x001F, 0x83E0}});
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// Semi-transparent quads, added: two of blended texels overlapping, one of both kinds, one of plain texels, over a
	// gear's opaque quad.
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 108;
	Frame.ProjectionDistance = Distance;
	const auto Quad = [&Frame](int32 X0, int32 Y0, int32 X1, int32 Y1, float Depth, uint16 ClutColumn, bool bSemiTransparent)
	{
		Weltall::FWorldMapPolygon& Polygon = Frame.Polygons.emplace_back(ScreenQuad(X0, Y0, X1, Y1, Depth, 108));
		Polygon.Layer = bSemiTransparent ? Weltall::EWorldMapLayer::Model : Weltall::EWorldMapLayer::Gear;
		Polygon.bTextured = true;
		Polygon.bRaw = true;
		Polygon.bSemiTransparent = bSemiTransparent;
		Polygon.TexturePage = static_cast<uint16>(Page | 1 << 5);
		Polygon.Clut = static_cast<uint16>(Clut | ClutColumn);
		for (std::size_t Corner = 0; Corner < 4; ++Corner)
		{
			Polygon.TexCoords[Corner] = {(Corner & 1) != 0 ? 32.0f : 0.0f, (Corner & 2) != 0 ? 2.0f : 0.0f};
		}
		Polygon.TexelMin = {0, 0};
		Polygon.TexelMax = {31, 1};
	};
	Quad(100, 20, 220, 200, 1500.0f, 0, false);
	Quad(20, 20, 140, 60, 1000.0f, 1, true);
	Quad(60, 40, 180, 80, 1000.0f, 1, true);
	Quad(20, 100, 140, 140, 1000.0f, 2, true);
	Quad(180, 100, 300, 140, 1000.0f, 0, true);
	const FFieldSceneMesh NoMesh{Weltall::FFieldScene{}};
	const auto Passes = [](const FFieldSceneDraw& Draw, uint8 Pass)
	{
		return Algo::CountIf(Draw.Runs, [Pass](const FFieldDrawRun& Run) { return Run.Pass == Pass; });
	};
	for (const EFieldSorting Sorting : {EFieldSorting::PaintersOrder, EFieldSorting::DepthBufferWithScenery})
	{
		for (const EFieldAntiAliasing AntiAliasing : {EFieldAntiAliasing::None, EFieldAntiAliasing::Msaa})
		{
			const FString Case = FString::Printf(TEXT("sorting %d, anti-aliasing %d"), static_cast<int32>(Sorting), static_cast<int32>(AntiAliasing));
			FWorldMapRenderOptions Options;
			Options.Sorting = Sorting;
			Options.AntiAliasing = AntiAliasing;
			FFieldSceneDraw Both;
			BuildWorldMapFrameDraw(Frame, nullptr, Options, Both, static_cast<float>(ScreenHeight));
			Weltall::FTexelKindCache Kinds;
			Kinds.Follow(Vram);
			FFieldSceneDraw Needed;
			BuildWorldMapFrameDraw(Frame, nullptr, Options, Needed, static_cast<float>(ScreenHeight), 1.0f, nullptr, &Kinds);
			if (Sorting == EFieldSorting::PaintersOrder)
			{
				// Every semi-transparent quad drawn twice; then the mixed one alone, the blended pair as one run, the plain one opaque.
				TestEqual(Case + TEXT(": plain passes without kinds"), Passes(Both, 1), 4);
				TestEqual(Case + TEXT(": plain passes"), Passes(Needed, 1), 1);
				TestEqual(Case + TEXT(": blended runs"), Passes(Needed, 2), Passes(Both, 2) - 2);
			}
			// The depth rule adds the scenery's prepass, of the opaque texels only.
			TestTrue(Case + TEXT(": fewer runs"), Needed.Runs.Num() < Both.Runs.Num());
			const TArray<FColor> Expected = RenderFieldSceneOffscreen(NoMesh, Units, Both, FIntPoint(ScreenWidth, ScreenHeight) * 2);
			const TArray<FColor> Drawn = RenderFieldSceneOffscreen(NoMesh, Units, Needed, FIntPoint(ScreenWidth, ScreenHeight) * 2);
			TestTrue(Case + TEXT(": drawn"), Expected.Num() == ScreenWidth * ScreenHeight * 4 && Drawn.Num() == Expected.Num());
			TestTrue(Case + TEXT(": the same pixels"), Drawn == Expected);
		}
	}
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapPortRowsRenderingTest, "Weltall.Rendering.WorldMapGearsTakeTheirTexelsFromThePortsRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapPortRowsRenderingTest::RunTest(const FString&)
{
	// The console's page at (0, 0) is red under its CLUT; the page 512 rows below it, in the port's rows, colour 3 under a CLUT there: blue.
	Weltall::FVram Vram = MakeVram();
	Vram.UploadToPortRows({{0, 512, 8, 1}, {0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333}});
	Vram.UploadToPortRows({{0, 513, 8, 1}, {0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333, 0x3333}});
	Vram.UploadToPortRows({{0, 768, 4, 1}, {0x0000, 0x0000, 0x0000, 0x7C00}});
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// A model's polygon whose page and CLUT carry the port's high y bits, and the same read as the console reads them.
	constexpr uint16 PortPage = 0x800;
	constexpr uint16 PortClut = 0x8000;
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 140;
	Frame.ProjectionDistance = Distance;
	const auto Textured = [&Frame](int32 X0, int32 X1, bool bPortRows)
	{
		Weltall::FWorldMapPolygon& Polygon = Frame.Polygons.emplace_back(ScreenQuad(X0, 40, X1, 80, 1000.0f, 140));
		Polygon.Layer = Weltall::EWorldMapLayer::Gear;
		Polygon.bTextured = true;
		Polygon.bRaw = true;
		Polygon.bPortRows = bPortRows;
		Polygon.TexturePage = static_cast<uint16>(Page | PortPage);
		Polygon.Clut = static_cast<uint16>(Clut | PortClut);
		for (std::size_t Corner = 0; Corner < 4; ++Corner)
		{
			Polygon.TexCoords[Corner] = {(Corner & 1) != 0 ? 8.0f : 0.0f, (Corner & 2) != 0 ? 2.0f : 0.0f};
		}
		Polygon.TexelMin = {0, 0};
		Polygon.TexelMax = {7, 1};
	};
	Textured(40, 100, true);
	Textured(140, 200, false);
	const TArray<FColor> Pixels = Render(Frame, Units);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), ScreenWidth * ScreenHeight))
	{
		return false;
	}
	const auto Near = [this, &Pixels](const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Pixels[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(TEXT("the port's rows' texels under the port's CLUT"), 70, 60, 0, 0, 255);
	Near(TEXT("the console's page under its own CLUT"), 170, 60, 255, 0, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapOneSidedRenderingTest, "Weltall.Rendering.WorldMapOneSidedPolygonsFaceTheEye",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapOneSidedRenderingTest::RunTest(const FString&)
{
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(MakeVram());

	// Four blue quads in a row. A quad's corners wind clockwise on the screen; turned over, the other way.
	const auto TurnedOver = [](Weltall::FWorldMapPolygon Polygon)
	{
		std::swap(Polygon.View[0], Polygon.View[1]);
		std::swap(Polygon.View[2], Polygon.View[3]);
		return Polygon;
	};
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 108;
	Frame.ProjectionDistance = Distance;
	Frame.Polygons.push_back(ScreenQuad(20, 40, 60, 80, 1000.0f, 108));
	Frame.Polygons.push_back(TurnedOver(ScreenQuad(80, 40, 120, 80, 1000.0f, 108)));
	Frame.Polygons.push_back(ScreenQuad(140, 40, 180, 80, 1000.0f, 108));
	Frame.Polygons.push_back(TurnedOver(ScreenQuad(200, 40, 240, 80, 1000.0f, 108)));
	for (Weltall::FWorldMapPolygon& Polygon : Frame.Polygons)
	{
		Polygon.Colors.fill({0, 0, 255});
	}
	// The last two leave their facing to the display.
	Frame.Polygons[2].bOneSided = true;
	Frame.Polygons[3].bOneSided = true;

	const TArray<FColor> Pixels = Render(Frame, Units);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), ScreenWidth * ScreenHeight))
	{
		return false;
	}
	const auto Blue = [&Pixels](int32 X, int32 Y) { return static_cast<int32>(Pixels[Y * ScreenWidth + X].B); };
	TestEqual(TEXT("A polygon is drawn facing the eye"), Blue(40, 60), 255);
	TestEqual(TEXT("and facing away, as the frame gave it"), Blue(100, 60), 255);
	TestEqual(TEXT("A one-sided polygon is drawn facing the eye, both its triangles"), Blue(150, 50), 255);
	TestEqual(TEXT("its second triangle"), Blue(170, 70), 255);
	TestEqual(TEXT("and not facing away"), Blue(210, 50), 0);
	TestEqual(TEXT("neither triangle"), Blue(230, 70), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapWaverRenderingTest, "Weltall.Rendering.WorldMapWaverMovesItsRows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapWaverRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// A blue quad over x 40-99, y 20-59; the waver draws the screen's middle (x 64-255) again, row 30 moved 8 pixels
	// right and row 31 8 left, the rest where they are.
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 140;
	Frame.ProjectionDistance = Distance;
	Weltall::FWorldMapPolygon& Flat = Frame.Polygons.emplace_back(ScreenQuad(40, 20, 100, 60, 1000.0f, 140));
	Flat.Colors.fill({0, 0, 255});
	Frame.WaverShifts.assign(192, 0);
	Frame.WaverShifts[30] = 8;
	Frame.WaverShifts[31] = -8;
	const TArray<FColor> Pixels = Render(Frame, Units);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), ScreenWidth * ScreenHeight))
	{
		return false;
	}
	const auto Near = [this, &Pixels](const TCHAR* What, int32 X, int32 Y, int32 B)
	{
		const FColor& Pixel = Pixels[Y * ScreenWidth + X];
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (0, 0, %d)"), What, Pixel.R, Pixel.G, Pixel.B, B),
			Pixel.R <= 2 && Pixel.G <= 2 && FMath::Abs(Pixel.B - B) <= 2);
	};
	Near(TEXT("a still row keeps the quad"), 95, 40, 255);
	Near(TEXT("and its end"), 103, 40, 0);
	Near(TEXT("the row moved right carries the quad past its end"), 103, 30, 255);
	Near(TEXT("and leaves what lies left of the middle as it was"), 50, 30, 255);
	Near(TEXT("the row moved left ends the quad early"), 95, 31, 0);
	Near(TEXT("the row above the two is still"), 103, 29, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapSceneryDepthRenderingTest, "Weltall.Rendering.WorldMapSceneryIsSortedByDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapSceneryDepthRenderingTest::RunTest(const FString&)
{
	// A cloud's texels: index 1 of the CLUT at (0, 257), blue and semi-transparent; its page adds them.
	constexpr uint16 CloudPage = 1 << 5;
	constexpr uint16 CloudClut = 257 << 6;
	Weltall::FVram Vram = MakeVram();
	Vram.Upload({{0, 257, 2, 1}, {0x0000, 0xFC00}});
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// The grey ground 1000 away over the whole screen; a green model 2 units behind it, a decal as a town's plate lies a
	// hair under the ground; a blue model 500 behind it and a red one 300 before it, the order linking the red first, then
	// the ground, the decal and the blue. A cloud 200 before the ground linked before it, and one 200 behind it after it:
	// clouds have no opaque texels, so what is drawn after one covers it in the order still. Last the party's yellow ship 100
	// behind the ground, as its hull rides under the sea: it keeps the game's order whatever the sorting.
	const auto Place = [](Weltall::EWorldMapLayer Layer, int32 X0, int32 Y0, int32 X1, int32 Y1, float Depth, Weltall::FDrawColor Color)
	{
		Weltall::FWorldMapPolygon Polygon = ScreenQuad(X0, Y0, X1, Y1, Depth, 108);
		Polygon.Layer = Layer;
		Polygon.Colors.fill(Color);
		return Polygon;
	};
	const auto Cloud = [&Place](int32 X0, float Depth)
	{
		Weltall::FWorldMapPolygon Polygon = Place(Weltall::EWorldMapLayer::Cloud, X0, 120, X0 + 40, 160, Depth, {128, 128, 128});
		Polygon.bTextured = true;
		Polygon.bRaw = true;
		Polygon.bSemiTransparent = true;
		Polygon.TexturePage = CloudPage;
		Polygon.Clut = CloudClut;
		for (std::size_t Corner = 0; Corner < 4; ++Corner)
		{
			Polygon.TexCoords[Corner] = {(Corner & 1) != 0 ? 16.0f : 0.0f, (Corner & 2) != 0 ? 1.0f : 0.0f};
		}
		Polygon.TexelMin = {0, 0};
		Polygon.TexelMax = {15, 0};
		return Polygon;
	};
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 108;
	Frame.ProjectionDistance = Distance;
	Frame.Polygons.push_back(Place(Weltall::EWorldMapLayer::Model, 200, 50, 240, 80, 700.0f, {255, 0, 0}));
	Frame.Polygons.push_back(Cloud(150, 800.0f));
	Frame.Polygons.push_back(Place(Weltall::EWorldMapLayer::Terrain, 0, 0, ScreenWidth, ScreenHeight, 1000.0f, {128, 128, 128}));
	Frame.Polygons.push_back(Place(Weltall::EWorldMapLayer::Model, 20, 20, 60, 60, 1002.0f, {0, 255, 0}));
	Frame.Polygons.push_back(Place(Weltall::EWorldMapLayer::Model, 90, 50, 140, 80, 1500.0f, {0, 0, 255}));
	Frame.Polygons.push_back(Cloud(250, 1200.0f));
	Frame.Polygons.push_back(Place(Weltall::EWorldMapLayer::Model, 20, 120, 60, 160, 1100.0f, {255, 255, 0}));
	Frame.Polygons.back().bVehicle = true;

	const TArray<FColor> Buffered = Render(Frame, Units, 1.0f, EFieldSorting::DepthBuffer);
	const TArray<FColor> Sorted = Render(Frame, Units, 1.0f, EFieldSorting::DepthBufferWithScenery);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Buffered.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("both"), Sorted.Num(), Buffered.Num()))
	{
		return false;
	}
	const auto Near = [this](const TArray<FColor>& Pixels, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Pixels[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(Buffered, TEXT("in the order: the far model linked last over the ground"), 115, 65, 0, 0, 255);
	Near(Buffered, TEXT("in the order: the ground over the near model linked first"), 220, 65, 128, 128, 128);
	Near(Buffered, TEXT("in the order: the ground over the near cloud linked before it"), 170, 140, 128, 128, 128);
	Near(Buffered, TEXT("in the order: the far cloud linked after the ground adds its blue"), 270, 140, 128, 128, 255);
	Near(Sorted, TEXT("by depth: the ground in front of the far model"), 115, 65, 128, 128, 128);
	Near(Sorted, TEXT("by depth: the near model in front of the ground"), 220, 65, 255, 0, 0);
	Near(Sorted, TEXT("by depth: within the margin the order decides, the plate over its ground"), 40, 40, 0, 255, 0);
	Near(Sorted, TEXT("by depth: the ground drawn after the near cloud still covers it"), 170, 140, 128, 128, 128);
	Near(Sorted, TEXT("by depth: the far cloud is behind the ground"), 270, 140, 128, 128, 128);
	Near(Sorted, TEXT("by depth: the ground elsewhere"), 160, 190, 128, 128, 128);
	Near(Buffered, TEXT("in the order: the ship linked last over the ground"), 40, 140, 255, 255, 0);
	Near(Sorted, TEXT("the ship keeps the order: over the ground it rides under"), 40, 140, 255, 255, 0);
	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArenaDepthRenderingTest, "Weltall.Rendering.ArenaGearsAreSortedAndStandInFrontOfTheirShadows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FArenaDepthRenderingTest::RunTest(const FString&)
{
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(MakeVram());

	// The arena's frame: its gears stand under the ground drawn. The eye 100 units above a grey floor. A brown piece of the
	// ground 800 away before a blue gear 1000 away, linked before it; a green gear 1500 away linked after the blue, an
	// effect 1100 away after it too, and the blue gear's shadow on the floor from 800 to 1400 ahead, linked after it.
	const auto Place = [](Weltall::EWorldMapLayer Layer, const std::array<std::array<float, 3>, 4>& View, Weltall::FDrawColor Color)
	{
		Weltall::FWorldMapPolygon Polygon;
		Polygon.Layer = Layer;
		Polygon.CornerCount = 4;
		Polygon.View = View;
		Polygon.Colors.fill(Color);
		return Polygon;
	};
	const auto Standing = [&Place](Weltall::EWorldMapLayer Layer, float Half, float Top, float Bottom, float Depth, Weltall::FDrawColor Color)
	{
		return Place(Layer, {{{-Half, Top, Depth}, {Half, Top, Depth}, {-Half, Bottom, Depth}, {Half, Bottom, Depth}}}, Color);
	};
	const auto Lying = [&Place](Weltall::EWorldMapLayer Layer, float Half, float Near, float Far, Weltall::FDrawColor Color)
	{
		return Place(Layer, {{{-Half, 100.0f, Far}, {Half, 100.0f, Far}, {-Half, 100.0f, Near}, {Half, 100.0f, Near}}}, Color);
	};
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 108;
	Frame.ProjectionDistance = Distance;
	Frame.bGearsUnderGround = true;
	Frame.Polygons.push_back(Lying(Weltall::EWorldMapLayer::Terrain, 3000.0f, 400.0f, 4000.0f, {128, 128, 128}));
	Frame.Polygons.push_back(Standing(Weltall::EWorldMapLayer::Terrain, 40.0f, 0.0f, 100.0f, 800.0f, {128, 64, 0}));
	Frame.Polygons.push_back(Standing(Weltall::EWorldMapLayer::Gear, 100.0f, -200.0f, 100.0f, 1000.0f, {0, 0, 255}));
	Frame.Polygons.push_back(Standing(Weltall::EWorldMapLayer::Gear, 60.0f, -150.0f, -50.0f, 1500.0f, {0, 255, 0}));
	Frame.Polygons.push_back(Standing(Weltall::EWorldMapLayer::Effect, 30.0f, -150.0f, -100.0f, 1100.0f, {255, 0, 0}));
	Weltall::FWorldMapPolygon& Shadow = Frame.Polygons.emplace_back(Lying(Weltall::EWorldMapLayer::Shadow, 300.0f, 800.0f, 1400.0f, {0, 0, 0}));
	Shadow.bSemiTransparent = true;

	const auto Render = [&](EFieldSorting Sorting)
	{
		const FFieldSceneMesh NoMesh{Weltall::FFieldScene{}};
		FWorldMapRenderOptions Options;
		Options.Sorting = Sorting;
		Options.AntiAliasing = EFieldAntiAliasing::None;
		FFieldSceneDraw Draw;
		BuildWorldMapFrameDraw(Frame, nullptr, Options, Draw, static_cast<float>(ScreenHeight));
		// As the arena's view: the floor's up, the world's -y.
		Draw.FloorUp = FVector3f(0.0f, -1.0f, 0.0f);
		return RenderFieldSceneOffscreen(NoMesh, Units, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	};
	const TArray<FColor> Painted = Render(EFieldSorting::PaintersOrder);
	const TArray<FColor> Buffered = Render(EFieldSorting::DepthBuffer);
	const TArray<FColor> Sorted = Render(EFieldSorting::DepthBufferWithScenery);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	for (const TArray<FColor>* Pixels : {&Painted, &Buffered, &Sorted})
	{
		if (!TestEqual(TEXT("The frames were read back"), Pixels->Num(), ScreenWidth * ScreenHeight))
		{
			return false;
		}
	}
	const auto Near = [this](const TArray<FColor>& Pixels, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = Pixels[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	Near(Painted, TEXT("in the order: the far gear linked last over the near one"), 160, 95, 0, 255, 0);
	Near(Painted, TEXT("in the order: the shadow over the gear's legs"), 180, 127, 0, 0, 127);
	for (const auto& [Pixels, Name] : {std::pair(&Buffered, TEXT("per pixel")), std::pair(&Sorted, TEXT("scenery too"))})
	{
		Near(*Pixels, *FString::Printf(TEXT("%s: the near gear in front of the far one"), Name), 160, 95, 0, 0, 255);
		Near(*Pixels, *FString::Printf(TEXT("%s: the ground keeps the order, the gear over it"), Name), 160, 120, 0, 0, 255);
		Near(*Pixels, *FString::Printf(TEXT("%s: the effect keeps the order, over the gear"), Name), 160, 78, 255, 0, 0);
		Near(*Pixels, *FString::Printf(TEXT("%s: the gear's legs, 26 units above the floor, in front of its shadow"), Name), 180, 127, 0, 0, 255);
		Near(*Pixels, *FString::Printf(TEXT("%s: the gear's sole, 2 units above the floor, in its shadow"), Name), 180, 133, 0, 0, 127);
		Near(*Pixels, *FString::Printf(TEXT("%s: the shadow on the floor beside the gear"), Name), 200, 130, 64, 64, 64);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FWorldMapDrawingMoveTest, "Weltall.Rendering.WorldMapDrawingMovesBetweenTicks",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FWorldMapDrawingMoveTest::RunTest(const FString&)
{
	// Two anchored quads and the horizon's band, whose 32 texels from texel 8 wrap in a window of 32.
	Weltall::FWorldMapFrame Frame;
	Frame.ScreenCenterY = 140;
	Frame.ProjectionDistance = Distance;
	Frame.Anchors.resize(2);
	for (uint32 Anchor = 0; Anchor < 2; ++Anchor)
	{
		Weltall::FWorldMapPolygon& Quad = Frame.Polygons.emplace_back(ScreenQuad(40, 20 + 50 * static_cast<int32>(Anchor), 100, 60 + 50 * static_cast<int32>(Anchor), 1000.0f, 140));
		Quad.Anchor = Anchor;
		Quad.Colors.fill({0, 0, 255});
	}
	Weltall::FWorldMapPolygon& Band = Frame.Polygons.emplace_back(ScreenQuad(160, 100, 224, 120, 2000.0f, 140));
	Band.Layer = Weltall::EWorldMapLayer::Horizon;
	Band.bTextured = true;
	Band.TexturePage = Page;
	Band.Clut = Clut;
	Band.WindowWidth = 32;
	for (std::size_t Corner = 0; Corner < 4; ++Corner)
	{
		Band.TexCoords[Corner] = {(Corner & 1) != 0 ? 40.0f : 8.0f, (Corner & 2) != 0 ? 2.0f : 0.0f};
	}
	Band.TexelMax = {31, 1};

	const auto BlendOf = [](float Shift, float Projection, float CenterY, float Scroll)
	{
		Weltall::FWorldMapFrameBlend Blend;
		for (int32 Anchor = 0; Anchor < 2; ++Anchor)
		{
			Weltall::FWorldMapViewMove Move{{{1.0f, 0.0f, 0.0f, Shift * static_cast<float>(Anchor + 1)}, {0.0f, 1.0f, 0.0f, -Shift}, {0.0f, 0.0f, 1.0f, Shift}}};
			Blend.Moves.push_back(Move);
		}
		Blend.ProjectionDistance = Projection;
		Blend.ScreenCenterY = CenterY;
		Blend.HorizonScroll = Scroll;
		return Blend;
	};
	// Built a tick's moment, moved to a later one: as built at that moment. Texel 8 scrolled by 4 still wraps once; by 24 it
	// starts the window and wraps no more.
	const Weltall::FWorldMapFrameBlend Built = BlendOf(0.0f, 256.0f, 140.0f, 0.0f);
	const Weltall::FWorldMapFrameBlend Later = BlendOf(3.5f, 270.0f, 132.0f, 4.0f);
	const Weltall::FWorldMapFrameBlend Unwrapped = BlendOf(7.0f, 280.0f, 128.0f, 24.0f);
	FWorldMapRenderOptions Options;
	FFieldSceneDraw Moved;
	FWorldMapDrawLayout Layout;
	BuildWorldMapFrameDraw(Frame, &Built, Options, Moved, static_cast<float>(ScreenHeight), 1.0f, &Layout);
	if (!TestEqual(TEXT("The band's place is kept"), Layout.Bands.Num(), 1) || !TestEqual(TEXT("split in two"), Layout.Bands[0].Pieces, 2))
	{
		return false;
	}
	TestTrue(TEXT("Moved within its pieces"), MoveWorldMapFrameDraw(Frame, &Later, Layout, Moved, static_cast<float>(ScreenHeight)));
	FFieldSceneDraw Expected;
	BuildWorldMapFrameDraw(Frame, &Later, Options, Expected, static_cast<float>(ScreenHeight));
	TestEqual(TEXT("The projection"), Moved.ProjectionDistance, Expected.ProjectionDistance);
	TestTrue(TEXT("The anchors' rows"), Moved.Objects == Expected.Objects);
	const bool bSameCorners = Moved.DynamicCorners.Num() == Expected.DynamicCorners.Num()
		&& FMemory::Memcmp(Moved.DynamicCorners.GetData(), Expected.DynamicCorners.GetData(), Moved.DynamicCorners.Num() * sizeof(FFieldSceneCorner)) == 0;
	TestTrue(TEXT("The corners, the band's pieces with them"), bSameCorners);
	const bool bSameStates = Moved.DynamicPolygons.Num() == Expected.DynamicPolygons.Num()
		&& FMemory::Memcmp(Moved.DynamicPolygons.GetData(), Expected.DynamicPolygons.GetData(), Moved.DynamicPolygons.Num() * sizeof(FFieldScenePolygonState)) == 0;
	TestTrue(TEXT("The polygons' states, the band's texels with them"), bSameStates);
	TestEqual(TEXT("The runs"), Moved.Runs.Num(), Expected.Runs.Num());
	TestFalse(TEXT("A scroll that wraps the band otherwise asks for a build"), MoveWorldMapFrameDraw(Frame, &Unwrapped, Layout, Moved, static_cast<float>(ScreenHeight)));
	return true;
}

#endif
