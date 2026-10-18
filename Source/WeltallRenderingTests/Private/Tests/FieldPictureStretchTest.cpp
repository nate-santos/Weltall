#include "Misc/AutomationTest.h"
#include "Rendering/FieldSceneRendering.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldPictureStretchTest, "Weltall.Rendering.ScenePicturesStretchAcrossTheViewAndCarryTheSceneSideways",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	void AddRows(TArray<FVector4f>& Objects, const FVector4f& Across, const FVector4f& Down, const FVector4f& Along)
	{
		Objects.Add(Across);
		Objects.Add(Down);
		Objects.Add(Along);
	}
}

bool FFieldPictureStretchTest::RunTest(const FString&)
{
	// The original's screen fills a target 10:7 wide; a wider one stretches the picture as far as 21:9 and cuts it there.
	TestEqual(TEXT("10:7: as drawn"), FieldPictureStretchFactor(FIntPoint(1440, 1008)), 1.0f);
	TestEqual(TEXT("4:3: as drawn"), FieldPictureStretchFactor(FIntPoint(1440, 1080)), 1.0f);
	TestTrue(TEXT("16:9: 16/9 over 10/7"), FMath::IsNearlyEqual(FieldPictureStretchFactor(FIntPoint(1920, 1080)), (16.0f / 9.0f) / (10.0f / 7.0f), 1e-4f));
	TestTrue(TEXT("21:9: 21/9 over 10/7"), FMath::IsNearlyEqual(FieldPictureStretchFactor(FIntPoint(2520, 1080)), (21.0f / 9.0f) / (10.0f / 7.0f), 1e-4f));
	TestEqual(TEXT("32:9: no further than 21:9"), FieldPictureStretchFactor(FIntPoint(3840, 1080)), FieldPictureStretchFactor(FIntPoint(2520, 1080)));
	TestTrue(TEXT("16:9: the whole target"), FieldPictureArea(FIntPoint(1920, 1080)) == FIntRect(0, 0, 1920, 1080));
	TestTrue(TEXT("32:9: 21:9 of it about its middle"), FieldPictureArea(FIntPoint(3840, 1080)) == FIntRect(660, 0, 3180, 1080));

	// Object 0 the picture, 1 a model, 2 the identity the view-space corners are drawn by, 3 a view turned a quarter about y.
	FFieldSceneDraw Draw;
	AddRows(Draw.Objects, FVector4f(1, 0, 0, 10), FVector4f(0, 1, 0, 0), FVector4f(0, 0, 1, 500));
	AddRows(Draw.Objects, FVector4f(1, 0, 0, 100), FVector4f(0, 1, 0, 0), FVector4f(0, 0, 1, 500));
	AddRows(Draw.Objects, FVector4f(1, 0, 0, 0), FVector4f(0, 1, 0, 0), FVector4f(0, 0, 1, 0));
	AddRows(Draw.Objects, FVector4f(0, 0, 1, 5), FVector4f(0, 1, 0, 0), FVector4f(-1, 0, 0, 300));
	const auto AddCorner = [&Draw](const FVector3f& Position, uint32 Object, const FVector3f& Anchor, bool bMoves)
	{
		FFieldSceneCorner& Corner = Draw.DynamicCorners.AddDefaulted_GetRef();
		Corner.Position = Position;
		Corner.Object = Object;
		Draw.PictureStretch.CornerAnchors.Add(Anchor);
		Draw.PictureStretch.CornerMoves.Add(bMoves ? 1 : 0);
	};
	AddCorner(FVector3f(50, 0, 400), 2, FVector3f(40, 0, 400), true);
	AddCorner(FVector3f(50, 0, 400), 1, FVector3f::ZeroVector, false);
	AddCorner(FVector3f(10, 0, 20), 3, FVector3f(10, 0, 20), true);
	FFieldPictureStretch& Stretch = Draw.PictureStretch;
	Stretch.Pictures = {0};
	Stretch.ObjectAnchors = {FFieldPictureStretch::NoAnchor, 1, FFieldPictureStretch::NoAnchor, FFieldPictureStretch::NoAnchor};

	TArray<FVector4f> Objects;
	TArray<FFieldSceneCorner> Corners;
	TestFalse(TEXT("Not staged: nothing to stretch"), StretchFieldPictureScene(Draw, 1.5f, Objects, Corners));
	Stretch.bStaged = true;
	TestFalse(TEXT("No wider than the screen: nothing to stretch"), StretchFieldPictureScene(Draw, 1.0f, Objects, Corners));
	if (!TestTrue(TEXT("Half as wide again"), StretchFieldPictureScene(Draw, 1.5f, Objects, Corners)))
	{
		return false;
	}
	TestTrue(TEXT("The picture's points drawn half as far across again"), Objects[0].Equals(FVector4f(1.5f, 0, 0, 15)));
	TestTrue(TEXT("The picture's depth as it was"), Objects[2].Equals(Draw.Objects[2]));
	TestTrue(TEXT("The model moved by half its place across, its shape kept"), Objects[3].Equals(FVector4f(1, 0, 0, 150)));
	TestTrue(TEXT("The identity as it was"), Objects[6].Equals(Draw.Objects[6]));
	TestTrue(TEXT("A view-space corner moved by half its anchor's place across"), Corners[0].Position.Equals(FVector3f(70, 0, 400)));
	TestTrue(TEXT("A corner of the model moves with the model's rows"), Corners[1].Position.Equals(FVector3f(50, 0, 400)));
	// Under the turned view the corner's place across is 20 + 5: it moves 12.5 across the view, along that view's first row.
	TestTrue(TEXT("A corner under a turned view moved along it"), Corners[2].Position.Equals(FVector3f(10, 0, 32.5f)));
	return true;
}

#endif
