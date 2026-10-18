#include "Field/FieldFrame.h"
#include "Field/FieldSceneFrame.h"
#include "Field/FieldSceneJoins.h"
#include "Graphics/Vram.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "RenderingThread.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/VramTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

// A door set a unit off its frame: drawn where the scene joins it, no pixel of what lies behind shows between the
// two; the hairline is there without the joins, and again while the door stands away from where the map built it.

namespace
{
	constexpr uint8 JoinsFlatQuad = 0x28;
	/** Four pixels to each of the original's: a unit of the scene, half an original pixel at this distance, is two. */
	constexpr int32 JoinsScale = 4;
	const FIntPoint JoinsSize(320 * JoinsScale, 224 * JoinsScale);
	const FColor JoinsBackground(64, 64, 64);

	/** A wall about a doorway and its door, each a model of flat quads, seen straight on 1000 units ahead with H = 500. */
	class FDoorScene
	{
	public:
		FDoorScene()
		{
			View.View = Weltall::FGteMatrix::Identity();
			View.View.T = {0, 0, 1000};
			View.Scene = View.View;
			View.ScreenRotation = Weltall::FGteMatrix::Identity();
			View.Billboard = Weltall::FGteMatrix::Identity();
			View.ProjectionDistance = 500;
			Scene.State.BackgroundColor = {JoinsBackground.R, JoinsBackground.G, JoinsBackground.B};
			// The jambs and the lintel in red, the doorway 76 across and 116 high about the screen's middle.
			Wall = Add({{-100, -58, 0}, {-38, -58, 0}, {-100, 58, 0}, {-38, 58, 0}, {38, -58, 0}, {100, -58, 0}, {38, 58, 0}, {100, 58, 0}, {-100, -100, 0}, {100, -100, 0}},
				{{0, 1, 2, 3}, {4, 5, 6, 7}, {8, 9, 0, 5}}, {255, 0, 0}, {});
			// The door in blue, a unit to the right of its place: a unit short of the left jamb, a unit over the right one.
			Door = Add({{-38, -58, 0}, {38, -58, 0}, {-38, 58, 0}, {38, 58, 0}}, {{0, 1, 2, 3}}, {0, 0, 255}, {1, 0, 0});
			Scene.Joins = Weltall::BuildFieldSceneJoins(Scene);
		}

		/** The scene drawn as a frame of the field, the tick before's frame given when it is drawn from one. */
		TArray<FColor> Render(const FVramTexture& Vram, const Weltall::FFieldFrame* Previous = nullptr)
		{
			Frame = {};
			Frame.bSceneDrawn = true;
			Frame.Background = Scene.State.BackgroundColor;
			Frame.View = View;
			Frame.Tick = Previous != nullptr ? Previous->Tick + 1 : 0;
			Weltall::EvaluateFieldScene(Scene, View, Weltall::EFieldCulling::InRange, Frame.Scenery);
			Frame.SceneryWorld.assign(Scene.Objects.size(), {});
			for (const uint32 Polygon : Frame.Scenery.DrawOrder)
			{
				Frame.Order.push_back({Polygon, Weltall::EFieldDrawKind::Scenery});
			}
			const FFieldSceneMesh Mesh(Scene);
			FFieldRenderOptions Options;
			Options.AntiAliasing = EFieldAntiAliasing::None;
			Options.Sorting = EFieldSorting::PaintersOrder;
			FFieldSceneDraw Draw;
			BuildFieldFrameDraw(Scene, Frame, Previous, 1.0f, Mesh, Options, Draw);
			return RenderFieldSceneOffscreen(Mesh, Vram, Draw, JoinsSize);
		}

		Weltall::FFieldScene Scene;
		Weltall::FFieldView View;
		Weltall::FFieldFrame Frame;
		uint16 Wall = 0;
		uint16 Door = 0;

	private:
		uint16 Add(const std::vector<Weltall::FGteVector>& Vertices, const std::vector<std::array<uint16, 4>>& Quads, Weltall::FMdlColor Color,
			const std::array<int32, 3>& Position)
		{
			Weltall::FMdlPart& Part = Scene.Models.emplace_back().Parts.emplace_back();
			Part.Vertices = Vertices;
			Weltall::FFieldSceneObject Object;
			Object.Model = static_cast<uint16>(Scene.Models.size() - 1);
			Object.Placement = Weltall::FGteMatrix::Identity();
			Object.Placement.T = Position;
			Object.Built = Object.Placement;
			Object.Bounds = {{0, 0, 0}, 5000};
			Object.FirstPolygon = static_cast<uint32>(Scene.Polygons.size());
			Object.PolygonCount = static_cast<uint32>(Quads.size());
			for (const std::array<uint16, 4>& Quad : Quads)
			{
				Weltall::FMdlPrimitive Primitive;
				Primitive.Type = Weltall::EMdlPrimitiveType::StaticFlatQuad;
				Primitive.Code = JoinsFlatQuad;
				Primitive.Color = Color;
				Primitive.Vertices = Quad;
				Weltall::FFieldScenePolygon Polygon;
				Polygon.Object = static_cast<uint16>(Scene.Objects.size());
				Polygon.Primitive = static_cast<uint16>(Part.Primitives.size());
				Polygon.Packet.Code = JoinsFlatQuad;
				Polygon.Packet.Colors[0] = Color;
				Part.Primitives.push_back(Primitive);
				Scene.Polygons.push_back(Polygon);
			}
			Scene.Objects.push_back(Object);
			return static_cast<uint16>(Scene.Objects.size() - 1);
		}
	};

	/** How many pixels of the rows through the doorway's middle show the background between the wall's outer edges. */
	int32 CountShowingThrough(const TArray<FColor>& Pixels)
	{
		int32 Count = 0;
		for (int32 Y = 112 * JoinsScale - 40; Y < 112 * JoinsScale + 40; ++Y)
		{
			for (int32 X = (160 - 45) * JoinsScale; X < (160 + 45) * JoinsScale; ++X)
			{
				const FColor& Pixel = Pixels[Y * JoinsSize.X + X];
				Count += FMath::Abs(Pixel.R - JoinsBackground.R) <= 2 && FMath::Abs(Pixel.G - JoinsBackground.G) <= 2 && FMath::Abs(Pixel.B - JoinsBackground.B) <= 2 ? 1 : 0;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldSceneJoinsRenderingTest, "Weltall.Rendering.FieldSceneryPiecesMeetWhereJoined",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldSceneJoinsRenderingTest::RunTest(const FString&)
{
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Weltall::FVram());

	FDoorScene Joined;
	const TArray<FColor> Meeting = Joined.Render(Units);

	FDoorScene Apart;
	Apart.Scene.Joins.clear();
	const TArray<FColor> Gapped = Apart.Render(Units);

	// The door swung a little on its hinge, as a script opens it: the joins let go of it.
	FDoorScene Swung;
	Swung.Scene.Objects[Swung.Door].Placement.T = {1, 0, 4};
	const TArray<FColor> Released = Swung.Render(Units);

	// Back in its place this tick, but drawn from a tick in which it was not: let go still. Then at rest again.
	const Weltall::FFieldFrame Moving = Swung.Frame;
	Swung.Scene.Objects[Swung.Door].Placement = Swung.Scene.Objects[Swung.Door].Built;
	const TArray<FColor> Settling = Swung.Render(Units, &Moving);
	const Weltall::FFieldFrame Settled = Swung.Frame;
	const TArray<FColor> AtRest = Swung.Render(Units, &Settled);

	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	const int32 Expected = JoinsSize.X * JoinsSize.Y;
	if (!TestEqual(TEXT("The frames were read back"), Meeting.Num(), Expected) || Gapped.Num() != Expected || Released.Num() != Expected
		|| Settling.Num() != Expected || AtRest.Num() != Expected)
	{
		return false;
	}
	TestEqual(TEXT("The scene joins the door's four corners to the frame's"), static_cast<int32>(Joined.Scene.Joins.size()), 4);
	TestEqual(TEXT("Joined, nothing shows between the door and its frame"), CountShowingThrough(Meeting), 0);
	// A unit is two pixels across here, down 80 rows.
	TestEqual(TEXT("Without the joins the hairline beside the door shows what lies behind"), CountShowingThrough(Gapped), 160);
	TestEqual(TEXT("A door away from its built place is drawn at its own"), CountShowingThrough(Released), 160);
	TestEqual(TEXT("And still while it is drawn from a tick in which it was away"), CountShowingThrough(Settling), 160);
	TestEqual(TEXT("At rest again it meets its frame"), CountShowingThrough(AtRest), 0);
	const FColor& InDoor = Meeting[(112 * JoinsScale) * JoinsSize.X + 160 * JoinsScale];
	TestTrue(TEXT("The door is drawn"), InDoor.B > 250 && InDoor.R < 5);
	const FColor& AtSeam = Meeting[(112 * JoinsScale) * JoinsSize.X + (160 - 19) * JoinsScale + 1];
	TestTrue(TEXT("The door reaches the jamb's edge"), AtSeam.B > 250 && AtSeam.R < 5);
	return true;
}

#endif
