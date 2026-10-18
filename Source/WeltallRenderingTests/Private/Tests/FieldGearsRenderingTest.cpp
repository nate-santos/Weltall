#include "Field/FieldFrame.h"
#include "Field/FieldGearDrawing.h"
#include "Field/FieldScene.h"
#include "Gears/GearPackets.h"
#include "Graphics/Vram.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/VramTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	constexpr int32 ScreenWidth = 320;
	constexpr int32 ScreenHeight = 224;
	/** A gear's texture: one row of 4-bit texels at VRAM (0, 0), its CLUT at (0, 256). */
	constexpr uint16 PartClut = 256 << 6;
	constexpr uint8 ModulatedQuad = 0x2C;
	constexpr uint8 FlatTriangle = 0x20;
	constexpr uint8 FlatQuad = 0x28;
	constexpr int32 Depth = 1000;
	constexpr uint16 Projection = 500;

	/**
	 * The part's texels (transparent, red, semi-transparent green, white) and the sprites' shadow blob
	 * where the gears' floor shadow samples it: the page at (640, 256), its rows from 224, its CLUT at
	 * (256, 243), every texel white and semi-transparent.
	 */
	Weltall::FVram MakeVram()
	{
		Weltall::FVram Vram;
		Vram.Upload({{0, 0, 1, 1}, {0x3210}});
		Vram.Upload({{0, 256, 4, 1}, {0x0000, 0x001F, 0x83E0, 0x7FFF}});
		Vram.Upload({{640, 480, 4, 16}, std::vector<uint16>(64, 0x1111)});
		Vram.Upload({{256, 243, 2, 1}, {0x0000, 0xFFFF}});
		return Vram;
	}

	/** A point on the screen as model space at Depth under an identity matrix with H 500. */
	int16 ModelAt(int32 Screen, int32 Center)
	{
		return static_cast<int16>((Screen - Center) * Depth / Projection);
	}

	/**
	 * A gear of one bone whose part holds a textured quad over the screen's [40, 80) x [40, 80) and a
	 * flat triangle over (100, 40), (140, 40), (100, 80), and its floor shadow: a quad of half-sizes 40
	 * turned to face the screen at its centre.
	 */
	struct FTestGear
	{
		FTestGear()
		{
			for (const int32 Y : {40, 80})
			{
				for (const int32 X : {40, 80})
				{
					Part.Vertices.push_back({ModelAt(X, 160), ModelAt(Y, 112), 0});
				}
			}
			Part.Vertices.push_back({ModelAt(100, 160), ModelAt(40, 112), 0});
			Part.Vertices.push_back({ModelAt(140, 160), ModelAt(40, 112), 0});
			Part.Vertices.push_back({ModelAt(100, 160), ModelAt(80, 112), 0});
			Weltall::FMdlPrimitive Quad;
			Quad.Type = Weltall::EMdlPrimitiveType::TexturedQuad;
			Quad.Code = ModulatedQuad;
			Quad.Vertices = {0, 1, 2, 3};
			Quad.Uvs = {Weltall::FMdlUv{0, 0}, Weltall::FMdlUv{4, 0}, Weltall::FMdlUv{0, 0}, Weltall::FMdlUv{4, 0}};
			Part.Primitives.push_back(Quad);
			Weltall::FMdlPrimitive Triangle;
			Triangle.Type = Weltall::EMdlPrimitiveType::FlatTriangle;
			Triangle.Code = FlatTriangle;
			Triangle.Vertices = {4, 5, 6, 0};
			Part.Primitives.push_back(Triangle);
			Packets.Bone = 1;
			Packets.Packets.resize(2);
			Packets.Packets[0].Code = ModulatedQuad;
			Packets.Packets[1].Code = FlatTriangle;
			Packets.Textures = {{0, PartClut}, {}};
			Layout.push_back({0, nullptr, nullptr, 40, 40, 0, 1});
			Layout.push_back({0, &Packets, &Part, 0, 0, 1, 2});
		}

		/** A tick's frame with the part's object at X view units across. */
		Weltall::FFieldFrame Frame(uint32 Tick, int32 X) const
		{
			Weltall::FFieldFrame Frame;
			Frame.Tick = Tick;
			Frame.bSceneDrawn = true;
			Frame.Background = {200, 200, 200};
			Frame.View.View = Weltall::FGteMatrix::Identity();
			Frame.View.ProjectionDistance = Projection;
			Weltall::FFieldGearsFrame& Gears = Frame.Gears;
			Gears.Layout = 1;
			Weltall::FFieldGearObject& Shadow = Gears.Objects.emplace_back();
			Shadow.Matrix.M = {{{4096, 0, 0}, {0, 0, 4096}, {0, 4096, 0}}};
			Shadow.Matrix.T = {0, 0, Depth};
			Shadow.bDrawn = true;
			Weltall::FFieldGearObject& Bone = Gears.Objects.emplace_back();
			Bone.Matrix = Weltall::FGteMatrix::Identity();
			Bone.Matrix.T = {X, 0, Depth};
			Bone.bDrawn = true;
			const auto Polygon = [&Gears](Weltall::FMdlColor Color, uint16 Bucket)
			{
				Weltall::FFieldGearPolygon& Drawn = Gears.Polygons.emplace_back();
				Drawn.Colors.fill(Color);
				Drawn.Bucket = Bucket;
				Drawn.bDrawn = true;
			};
			Polygon({0x40, 0x40, 0x40}, 60);
			Polygon({64, 64, 64}, 50);
			Polygon({0, 0, 255}, 50);
			for (uint32 Index = 0; Index < 3; ++Index)
			{
				Gears.Links.push_back({Weltall::EFieldGearDraw::Polygon, Index, Gears.Polygons[Index].Bucket});
				Frame.Order.push_back({Index, Weltall::EFieldDrawKind::Gear, Weltall::EFieldGearDraw::Polygon});
			}
			return Frame;
		}

		Weltall::FMdlPart Part;
		Weltall::FGearBonePackets Packets;
		std::vector<Weltall::FFieldGearLayoutItem> Layout;
	};

	/**
	 * A gear standing on a floor 100 view units below the eye: one bone whose part holds a blue flat triangle 900 units
	 * ahead, from 110 units above the floor to its tip a little into it, and its floor shadow lying on that floor from
	 * 800 to 1200 units ahead, linked after the part, as the original sorts a shadow by its nearest corner.
	 */
	struct FStandingGear
	{
		FStandingGear()
		{
			Part.Vertices = {{-60, 0, 0}, {60, 0, 0}, {0, 110, 0}};
			Weltall::FMdlPrimitive Triangle;
			Triangle.Type = Weltall::EMdlPrimitiveType::FlatTriangle;
			Triangle.Code = FlatTriangle;
			Triangle.Vertices = {0, 1, 2, 0};
			Part.Primitives.push_back(Triangle);
			Packets.Bone = 1;
			Packets.Packets.resize(1);
			Packets.Packets[0].Code = FlatTriangle;
			Packets.Textures = {{}};
			Layout.push_back({0, nullptr, nullptr, 40, 40, 0, 1});
			Layout.push_back({0, &Packets, &Part, 0, 0, 1, 1});
		}

		Weltall::FFieldFrame Frame() const
		{
			constexpr int16 FiveTimes = 5 * 4096;
			Weltall::FFieldFrame Frame;
			Frame.Tick = 1;
			Frame.bSceneDrawn = true;
			Frame.Background = {200, 200, 200};
			Frame.View.View = Weltall::FGteMatrix::Identity();
			Frame.View.ProjectionDistance = Projection;
			Weltall::FFieldGearsFrame& Gears = Frame.Gears;
			Gears.Layout = 1;
			Weltall::FFieldGearObject& Shadow = Gears.Objects.emplace_back();
			Shadow.Matrix.M = {{{FiveTimes, 0, 0}, {0, FiveTimes, 0}, {0, 0, FiveTimes}}};
			Shadow.Matrix.T = {0, 100, 1000};
			Shadow.bDrawn = true;
			Weltall::FFieldGearObject& Bone = Gears.Objects.emplace_back();
			Bone.Matrix = Weltall::FGteMatrix::Identity();
			Bone.Matrix.T = {0, 0, 900};
			Bone.bDrawn = true;
			const auto Polygon = [&Gears](Weltall::FMdlColor Color, uint16 Bucket)
			{
				Weltall::FFieldGearPolygon& Drawn = Gears.Polygons.emplace_back();
				Drawn.Colors.fill(Color);
				Drawn.Bucket = Bucket;
				Drawn.bDrawn = true;
			};
			Polygon({0x40, 0x40, 0x40}, 800 / 4);
			Polygon({0, 0, 255}, 900 / 4);
			for (const uint32 Index : {1u, 0u})
			{
				Gears.Links.push_back({Weltall::EFieldGearDraw::Polygon, Index, Gears.Polygons[Index].Bucket});
				Frame.Order.push_back({Index, Weltall::EFieldDrawKind::Gear, Weltall::EFieldGearDraw::Polygon});
			}
			return Frame;
		}

		Weltall::FMdlPart Part;
		Weltall::FGearBonePackets Packets;
		std::vector<Weltall::FFieldGearLayoutItem> Layout;
	};

	/**
	 * A gear of one bone whose part holds two flat triangles over the screen's centre, a red one 900 view units ahead and
	 * a blue one Behind units farther, the blue linked after the red, as the original's buckets can link a far part's
	 * polygon after a near one's.
	 */
	struct FLayeredGear
	{
		explicit FLayeredGear(int16 Behind)
		{
			for (const int16 Z : {int16{0}, Behind})
			{
				const uint16 First = static_cast<uint16>(Part.Vertices.size());
				Part.Vertices.push_back({-100, -100, Z});
				Part.Vertices.push_back({100, -100, Z});
				Part.Vertices.push_back({0, 100, Z});
				Weltall::FMdlPrimitive Triangle;
				Triangle.Type = Weltall::EMdlPrimitiveType::FlatTriangle;
				Triangle.Code = FlatTriangle;
				Triangle.Vertices = {First, static_cast<uint16>(First + 1), static_cast<uint16>(First + 2), 0};
				Part.Primitives.push_back(Triangle);
			}
			Packets.Bone = 1;
			Packets.Packets.resize(2);
			Packets.Packets[0].Code = FlatTriangle;
			Packets.Packets[1].Code = FlatTriangle;
			Packets.Textures = {{}, {}};
			Layout.push_back({0, &Packets, &Part, 0, 0, 0, 2});
		}

		Weltall::FFieldFrame Frame() const
		{
			Weltall::FFieldFrame Frame;
			Frame.Tick = 1;
			Frame.bSceneDrawn = true;
			Frame.Background = {200, 200, 200};
			Frame.View.View = Weltall::FGteMatrix::Identity();
			Frame.View.ProjectionDistance = Projection;
			Weltall::FFieldGearsFrame& Gears = Frame.Gears;
			Gears.Layout = 1;
			Weltall::FFieldGearObject& Bone = Gears.Objects.emplace_back();
			Bone.Matrix = Weltall::FGteMatrix::Identity();
			Bone.Matrix.T = {0, 0, 900};
			Bone.bDrawn = true;
			for (const Weltall::FMdlColor Color : {Weltall::FMdlColor{255, 0, 0}, Weltall::FMdlColor{0, 0, 255}})
			{
				Weltall::FFieldGearPolygon& Drawn = Gears.Polygons.emplace_back();
				Drawn.Colors.fill(Color);
				Drawn.Bucket = 225;
				Drawn.bDrawn = true;
			}
			for (const uint32 Index : {0u, 1u})
			{
				Gears.Links.push_back({Weltall::EFieldGearDraw::Polygon, Index, Gears.Polygons[Index].Bucket});
				Frame.Order.push_back({Index, Weltall::EFieldDrawKind::Gear, Weltall::EFieldGearDraw::Polygon});
			}
			return Frame;
		}

		Weltall::FMdlPart Part;
		Weltall::FGearBonePackets Packets;
		std::vector<Weltall::FFieldGearLayoutItem> Layout;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldGearsRenderingTest, "Weltall.Rendering.FieldGearsFollowTheGpusRules",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldGearsRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	const FTestGear Gear;
	const Weltall::FFieldScene Scene;
	const FFieldSceneMesh Mesh(Scene, Gear.Layout, 1);
	FFieldRenderOptions Options;
	Options.AntiAliasing = EFieldAntiAliasing::None;
	const Weltall::FFieldFrame Latest = Gear.Frame(1, 0);
	FFieldSceneDraw Draw;
	BuildFieldFrameDraw(Scene, Latest, nullptr, 1.0f, Mesh, Options, Draw);
	const TArray<FColor> Pixels = RenderFieldSceneOffscreen(Mesh, Units, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	// The bone's part 40 view units (20 pixels) further right in the tick before: half way between
	// the ticks it stands 10 pixels right of its latest place.
	const Weltall::FFieldFrame Before = Gear.Frame(0, 40);
	FFieldSceneDraw Between;
	BuildFieldFrameDraw(Scene, Latest, &Before, 0.5f, Mesh, Options, Between);
	const TArray<FColor> Moving = RenderFieldSceneOffscreen(Mesh, Units, Between, FIntPoint(ScreenWidth, ScreenHeight));
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Pixels.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("both"), Moving.Num(), Pixels.Num()))
	{
		return false;
	}
	const auto NearIn = [this](const TArray<FColor>& In, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = In[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	NearIn(Pixels, TEXT("background"), 300, 200, 200, 200, 200);
	// The quad's four texels are 10 pixels wide each, modulated by the part's colour at half.
	NearIn(Pixels, TEXT("texel 0 is transparent"), 45, 60, 200, 200, 200);
	NearIn(Pixels, TEXT("red at half"), 55, 60, 127, 0, 0);
	NearIn(Pixels, TEXT("a semi-transparent texel of an opaque polygon"), 65, 60, 0, 127, 0);
	NearIn(Pixels, TEXT("white at half"), 75, 60, 127, 127, 127);
	NearIn(Pixels, TEXT("the flat triangle"), 105, 45, 0, 0, 255);
	// The floor shadow subtracts its blob at colour 40h: white at a half.
	NearIn(Pixels, TEXT("the shadow subtracts"), 160, 112, 73, 73, 73);
	NearIn(Pixels, TEXT("outside the shadow"), 160, 140, 200, 200, 200);
	NearIn(Moving, TEXT("between two ticks: texel 0 where the latest has red"), 55, 60, 200, 200, 200);
	NearIn(Moving, TEXT("between two ticks: red"), 65, 60, 127, 0, 0);
	NearIn(Moving, TEXT("between two ticks: the shadow stays"), 160, 112, 73, 73, 73);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldGearShadowRenderingTest, "Weltall.Rendering.FieldGearsStandInFrontOfTheirFloorShadows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldGearShadowRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	const FStandingGear Gear;
	const Weltall::FFieldScene Scene;
	const FFieldSceneMesh Mesh(Scene, Gear.Layout, 1);
	const Weltall::FFieldFrame Latest = Gear.Frame();
	const auto Render = [&](EFieldSorting Sorting)
	{
		FFieldRenderOptions Options;
		Options.AntiAliasing = EFieldAntiAliasing::None;
		Options.Sorting = Sorting;
		FFieldSceneDraw Draw;
		BuildFieldFrameDraw(Scene, Latest, nullptr, 1.0f, Mesh, Options, Draw);
		return RenderFieldSceneOffscreen(Mesh, Units, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	};
	const TArray<FColor> Painted = Render(EFieldSorting::PaintersOrder);
	const TArray<FColor> Buffered = Render(EFieldSorting::DepthBuffer);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Painted.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("both"), Buffered.Num(), Painted.Num()))
	{
		return false;
	}
	const auto NearIn = [this](const TArray<FColor>& In, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = In[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	// Row 155 shows the triangle 23 units above its floor, and behind it the floor 1163 units ahead, in the shadow.
	NearIn(Painted, TEXT("painter's order: the shadow linked later subtracts from the gear"), 160, 155, 0, 0, 128);
	NearIn(Buffered, TEXT("depth buffer: the gear stands in front of its shadow"), 160, 155, 0, 0, 255);
	// Row 166, 3 units above the floor: the gear's sole still lies in its shadow.
	NearIn(Buffered, TEXT("depth buffer: the shadow still under the gear's sole"), 160, 166, 0, 0, 128);
	NearIn(Buffered, TEXT("depth buffer: the shadow on the floor beside the gear"), 220, 165, 73, 73, 73);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldGearPartsDepthRenderingTest, "Weltall.Rendering.FieldGearPartsAreSortedByDepth",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldGearPartsDepthRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	const Weltall::FFieldScene Scene;
	const auto Render = [&](const FLayeredGear& Gear, EFieldSorting Sorting)
	{
		const FFieldSceneMesh Mesh(Scene, Gear.Layout, 1);
		const Weltall::FFieldFrame Latest = Gear.Frame();
		FFieldRenderOptions Options;
		Options.AntiAliasing = EFieldAntiAliasing::None;
		Options.Sorting = Sorting;
		FFieldSceneDraw Draw;
		BuildFieldFrameDraw(Scene, Latest, nullptr, 1.0f, Mesh, Options, Draw);
		return RenderFieldSceneOffscreen(Mesh, Units, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	};
	const FLayeredGear Apart(300);
	const FLayeredGear Close(4);
	const TArray<FColor> ApartPainted = Render(Apart, EFieldSorting::PaintersOrder);
	const TArray<FColor> ApartBuffered = Render(Apart, EFieldSorting::DepthBuffer);
	const TArray<FColor> CloseBuffered = Render(Close, EFieldSorting::DepthBuffer);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	for (const TArray<FColor>* Pixels : {&ApartPainted, &ApartBuffered, &CloseBuffered})
	{
		if (!TestEqual(TEXT("The frames were read back"), Pixels->Num(), ScreenWidth * ScreenHeight))
		{
			return false;
		}
	}
	const auto NearIn = [this](const TArray<FColor>& In, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = In[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	NearIn(ApartPainted, TEXT("painter's order: the far triangle linked last covers the near one"), 160, 112, 0, 0, 255);
	NearIn(ApartBuffered, TEXT("depth buffer: beside the gear the background"), 300, 200, 200, 200, 200);
	NearIn(ApartBuffered, TEXT("depth buffer: the near triangle stands in front of the far one"), 160, 112, 255, 0, 0);
	NearIn(CloseBuffered, TEXT("depth buffer: within the margin the order decides, as on a detail laid on a surface"), 160, 112, 0, 0, 255);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldGearSceneryDepthRenderingTest, "Weltall.Rendering.FieldGearsAreSortedAgainstTheScenery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldGearSceneryDepthRenderingTest::RunTest(const FString&)
{
	const Weltall::FVram Vram = MakeVram();
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	// The layered gear, and after it a green quad of the scenery 1100 units ahead over the screen's centre: behind the
	// gear's red triangle, before its blue one.
	const FLayeredGear Gear(300);
	Weltall::FFieldScene Scene;
	Weltall::FMdlPart& Part = Scene.Models.emplace_back().Parts.emplace_back();
	Part.Vertices = {{-150, -150, 0}, {150, -150, 0}, {-150, 150, 0}, {150, 150, 0}};
	Weltall::FMdlPrimitive Quad;
	Quad.Type = Weltall::EMdlPrimitiveType::StaticFlatQuad;
	Quad.Code = FlatQuad;
	Quad.Color = {0, 255, 0};
	Quad.Vertices = {0, 1, 2, 3};
	Part.Primitives.push_back(Quad);
	Weltall::FFieldScenePolygon Polygon;
	Polygon.Packet.Code = FlatQuad;
	Polygon.Packet.Colors[0] = Quad.Color;
	Scene.Polygons.push_back(Polygon);
	Weltall::FFieldSceneObject& Object = Scene.Objects.emplace_back();
	Object.Placement = Weltall::FGteMatrix::Identity();
	Object.Placement.T = {0, 0, 1100};
	Object.Bounds = {{0, 0, 0}, 5000};
	Object.PolygonCount = 1;
	Weltall::FFieldFrame Latest = Gear.Frame();
	Latest.View.Scene = Latest.View.View;
	Latest.View.ScreenRotation = Weltall::FGteMatrix::Identity();
	Latest.View.Billboard = Weltall::FGteMatrix::Identity();
	Weltall::EvaluateFieldScene(Scene, Latest.View, Weltall::EFieldCulling::InRange, Latest.Scenery);
	Latest.SceneryWorld.assign(Scene.Objects.size(), {});
	for (const uint32 Drawn : Latest.Scenery.DrawOrder)
	{
		Latest.Order.push_back({Drawn, Weltall::EFieldDrawKind::Scenery});
	}

	const FFieldSceneMesh Mesh(Scene, Gear.Layout, 1);
	const auto Render = [&](EFieldSorting Sorting)
	{
		FFieldRenderOptions Options;
		Options.AntiAliasing = EFieldAntiAliasing::None;
		Options.Sorting = Sorting;
		FFieldSceneDraw Draw;
		BuildFieldFrameDraw(Scene, Latest, nullptr, 1.0f, Mesh, Options, Draw);
		return RenderFieldSceneOffscreen(Mesh, Units, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	};
	const TArray<FColor> Painted = Render(EFieldSorting::PaintersOrder);
	const TArray<FColor> Buffered = Render(EFieldSorting::DepthBuffer);
	const TArray<FColor> Sorted = Render(EFieldSorting::DepthBufferWithScenery);
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Painted.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("all"), Buffered.Num(), Painted.Num())
		|| !TestEqual(TEXT("all"), Sorted.Num(), Painted.Num()))
	{
		return false;
	}
	const auto NearIn = [this](const TArray<FColor>& In, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = In[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	NearIn(Painted, TEXT("painter's order: the scenery linked last covers the gear"), 160, 112, 0, 255, 0);
	NearIn(Buffered, TEXT("depth buffer: the gear's near triangle stands in front of the scenery"), 160, 112, 255, 0, 0);
	NearIn(Sorted, TEXT("the scenery sorted too: the same"), 160, 112, 255, 0, 0);
	return true;
}

namespace
{
	/**
	 * Where a placed gear's textures lie in VRAM's port rows, as the stand-ins' ninth model's: a page in the port's second
	 * band, at (64, 768), its CLUT at (256, 529).
	 */
	constexpr int16 PortPageX = 0x40;
	constexpr int16 PortPageY = Weltall::FVram::Height + 0x100;
	constexpr int16 PortClutX = 0x100;
	constexpr int16 PortClutY = Weltall::FVram::Height + 17;
	/** The page and CLUT with libgpu's high y bits, as a load at such a place gives them to its parts. */
	constexpr uint16 PortPage = 0x800 | 0x10 | PortPageX / 64;
	constexpr uint16 PortClut = static_cast<uint16>(PortClutY << 6 | PortClutX / 16);

	/**
	 * A gear its host places: one bone whose part holds FTestGear's textured quad, its texels in the port's rows, the
	 * model's own matrix putting it over the screen's [40, 80) x [40, 80) and its placement drawing it at half the size
	 * about the screen's centre, X view units across.
	 */
	struct FPlacedGear
	{
		FPlacedGear()
		{
			for (const int32 Y : {40, 80})
			{
				for (const int32 X : {40, 80})
				{
					Part.Vertices.push_back({ModelAt(X, 160), ModelAt(Y, 112), 0});
				}
			}
			Weltall::FMdlPrimitive Quad;
			Quad.Type = Weltall::EMdlPrimitiveType::TexturedQuad;
			Quad.Code = ModulatedQuad;
			Quad.Vertices = {0, 1, 2, 3};
			Quad.Uvs = {Weltall::FMdlUv{0, 0}, Weltall::FMdlUv{4, 0}, Weltall::FMdlUv{0, 0}, Weltall::FMdlUv{4, 0}};
			Part.Primitives.push_back(Quad);
			Packets.Bone = 1;
			Packets.Packets.resize(1);
			Packets.Packets[0].Code = ModulatedQuad;
			Packets.Textures = {{PortPage, PortClut}};
			Layout.push_back({10, &Packets, &Part, 0, 0, 0, 1, true});
		}

		Weltall::FFieldFrame Frame(uint32 Tick, float X) const
		{
			Weltall::FFieldFrame Frame;
			Frame.Tick = Tick;
			Frame.bSceneDrawn = true;
			Frame.Background = {200, 200, 200};
			Frame.View.View = Weltall::FGteMatrix::Identity();
			Frame.View.ProjectionDistance = Projection;
			Weltall::FFieldGearsFrame& Gears = Frame.Gears;
			Gears.Layout = 1;
			Weltall::FFieldGearObject& Bone = Gears.Objects.emplace_back();
			Bone.Matrix = Weltall::FGteMatrix::Identity();
			Bone.Matrix.T = {0, 0, Depth};
			Bone.bDrawn = true;
			Bone.bFine = true;
			Bone.Fine.M = {{{0.5f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 0.5f}}};
			Bone.Fine.T = {X, 0.0f, static_cast<float>(Depth)};
			Weltall::FFieldGearPolygon& Drawn = Gears.Polygons.emplace_back();
			Drawn.Colors.fill({64, 64, 64});
			Drawn.Bucket = 50;
			Drawn.bDrawn = true;
			Gears.Links.push_back({Weltall::EFieldGearDraw::Polygon, 0, Drawn.Bucket});
			Frame.Order.push_back({0, Weltall::EFieldDrawKind::Gear, Weltall::EFieldGearDraw::Polygon});
			return Frame;
		}

		Weltall::FMdlPart Part;
		Weltall::FGearBonePackets Packets;
		std::vector<Weltall::FFieldGearLayoutItem> Layout;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldPlacedGearRenderingTest, "Weltall.Rendering.PlacedGearsDrawFromThePortsRowsWhereTheirHostPutsThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldPlacedGearRenderingTest::RunTest(const FString&)
{
	// The same four texels as the gears' test, in the port's rows; the console's rows hold nothing where the same
	// page and CLUT words would read without their high bits.
	Weltall::FVram Vram;
	Vram.UploadToPortRows({{PortPageX, PortPageY, 1, 1}, {0x3210}});
	Vram.UploadToPortRows({{PortClutX, PortClutY, 4, 1}, {0x0000, 0x001F, 0x83E0, 0x7FFF}});
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Vram);

	const FPlacedGear Gear;
	const Weltall::FFieldScene Scene;
	const FFieldSceneMesh Mesh(Scene, Gear.Layout, 1);
	FFieldRenderOptions Options;
	Options.AntiAliasing = EFieldAntiAliasing::None;
	const Weltall::FFieldFrame Latest = Gear.Frame(1, 0.0f);
	FFieldSceneDraw Draw;
	BuildFieldFrameDraw(Scene, Latest, nullptr, 1.0f, Mesh, Options, Draw);
	const TArray<FColor> Pixels = RenderFieldSceneOffscreen(Mesh, Units, Draw, FIntPoint(ScreenWidth, ScreenHeight));
	// The gear 40 view units (20 pixels) further right in the tick before: half way between the ticks, 10 pixels.
	const Weltall::FFieldFrame Before = Gear.Frame(0, 40.0f);
	FFieldSceneDraw Between;
	BuildFieldFrameDraw(Scene, Latest, &Before, 0.5f, Mesh, Options, Between);
	const TArray<FColor> Moving = RenderFieldSceneOffscreen(Mesh, Units, Between, FIntPoint(ScreenWidth, ScreenHeight));
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frames were read back"), Pixels.Num(), ScreenWidth * ScreenHeight) || !TestEqual(TEXT("both"), Moving.Num(), Pixels.Num()))
	{
		return false;
	}
	const auto NearIn = [this](const TArray<FColor>& In, const TCHAR* What, int32 X, int32 Y, int32 R, int32 G, int32 B)
	{
		const FColor& Pixel = In[Y * ScreenWidth + X];
		const bool bNear = FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d) near (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B, R, G, B), bNear);
	};
	// Its own matrix would draw the quad over [40, 80) x [40, 80); the placement draws it at half the size about the
	// centre, over [100, 120) x [76, 96), its four texels 5 pixels wide each.
	NearIn(Pixels, TEXT("nothing where its own matrix has it"), 55, 60, 200, 200, 200);
	NearIn(Pixels, TEXT("texel 0 is transparent"), 102, 86, 200, 200, 200);
	NearIn(Pixels, TEXT("red from the port's rows, at half"), 107, 86, 127, 0, 0);
	NearIn(Pixels, TEXT("green"), 112, 86, 0, 127, 0);
	NearIn(Pixels, TEXT("white"), 117, 86, 127, 127, 127);
	NearIn(Pixels, TEXT("past the quad"), 122, 86, 200, 200, 200);
	NearIn(Pixels, TEXT("under the quad"), 107, 98, 200, 200, 200);
	NearIn(Moving, TEXT("between two ticks: texel 0 where the latest has red"), 107, 86, 200, 200, 200);
	NearIn(Moving, TEXT("between two ticks: red ten pixels on"), 117, 86, 127, 0, 0);
	return true;
}

#endif
