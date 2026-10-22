#include "Graphics/Vram.h"
#include "Graphics/Texture.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "Presentation/DrawList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/DrawListRendering.h"
#include "Rendering/VramTexture.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPartialVramUploadTest, "Weltall.Rendering.PartialVramUploadsKeepConsolePaletteAndPortPixels",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FPartialVramUploadTest::RunTest(const FString&)
{
	using namespace Weltall;
	FVram Vram;
	Vram.Upload({{0, 0, 32, 16}, std::vector<uint16>(32 * 16, 0x001F)});
	Vram.UploadToPortRows({{0, 512, 32, 16}, std::vector<uint16>(32 * 16, 0x03E0)});
	Vram.Upload({{128, 0, 1, 1}, {0x1111}});
	Vram.Upload({{0, 256, 2, 1}, {0, 0x001F}});
	const FVram Original = Vram;
	FDrawList List;
	List.Primitives.push_back(MakeSprite(0, 0, 32, 16, {0, {0, 0, ETextureDepth::Direct15, Weltall::EBlendMode::Average}, {}}, 0, 0));
	List.Primitives.push_back(MakeSprite(40, 0, 32, 16, {0, {0, 512, ETextureDepth::Direct15, Weltall::EBlendMode::Average}, {}}, 0, 0));
	List.Primitives.push_back(MakeSprite(80, 0, 4, 1, {0, {128, 0, ETextureDepth::Indexed4, Weltall::EBlendMode::Average}, {0, 256}}, 0, 0));
	FVramTexture Incremental;
	BeginInitResource(&Incremental);
	const FIntPoint Size(320, 224);
	auto Render = [&](const FVramTexture& Texture)
	{
		return RenderPassesOffscreen(Size, [&](FRDGBuilder& Graph, FRDGTextureRef Output)
		{
			AddClearRenderTargetPass(Graph, Output, FLinearColor::Black);
			AddDrawListPasses(Graph, Texture, {&List}, Output, FIntRect(FIntPoint::ZeroValue, Size), {320, 224});
		});
	};
	auto Compare = [&](const FVram& State, const TCHAR* Label)
	{
		FVramTexture Full;
		BeginInitResource(&Full);
		Full.Follow(State);
		Incremental.Follow(State);
		const TArray<FColor> Actual = Render(Incremental);
		const TArray<FColor> Expected = Render(Full);
		BeginReleaseResource(&Full);
		FlushRenderingCommands();
		TestEqual(TEXT("Readback size"), Actual.Num(), Size.X * Size.Y);
		TestTrue(Label, Actual == Expected);
		return Actual;
	};
	const TArray<FColor> Initial = Compare(Vram, TEXT("Initial complete upload"));
	if (Initial.Num() == Size.X * Size.Y)
	{
		TestEqual(TEXT("Console texture actually drawn"), Initial[5 * Size.X + 5], FColor::Red);
		TestEqual(TEXT("Port texture actually drawn"), Initial[5 * Size.X + 45], FColor::Green);
	}
	Vram.Upload({{3, 5, 1, 1}, {0x7C00}});
	Compare(Vram, TEXT("Small offset update retains other console and port texels"));
	Vram.Upload({{15, 7, 2, 2}, {0x7FFF, 0x03E0, 0x7C00, 0x001F}});
	Compare(Vram, TEXT("Update across both tile boundaries"));
	Vram.Upload({{1, 256, 1, 1}, {0x7C00}});
	Compare(Vram, TEXT("Palette change reaches indexed texture"));
	Vram.UploadToPortRows({{7, 517, 1, 1}, {0x7C00}});
	Compare(Vram, TEXT("Port-only change bypasses the console mask"));
	Vram.Upload({{2, 2, 1, 1}, {0x03E0}});
	Vram.UploadToPortRows({{8, 518, 1, 1}, {0x7FFF}});
	Compare(Vram, TEXT("Console and port writes together"));
	Compare(Original, TEXT("Rollback within one lineage takes complete contents"));
	FVram ConsoleBranch = Original;
	ConsoleBranch.Upload({{0, 0, 1, 1}, {0x7FFF}});
	Compare(ConsoleBranch, TEXT("Console branch after snapshot"));
	FVram PortBranch = Original;
	PortBranch.UploadToPortRows({{0, 512, 1, 1}, {0x7C00}});
	Compare(PortBranch, TEXT("Equal total revisions with a different port revision synchronize fully"));
	FVram Other;
	Other.Upload({{0, 0, 32, 16}, std::vector<uint16>(32 * 16, 0x7C00)});
	Other.UploadToPortRows({{0, 512, 32, 16}, std::vector<uint16>(32 * 16, 0x7FFF)});
	Compare(Other, TEXT("Different lineage replaces retained content"));
	Incremental.SetUpscaled(true);
	Compare(Vram, TEXT("Enabling upscaling retains complete unit contents"));
	BeginReleaseResource(&Incremental);
	FlushRenderingCommands();
	return true;
}
#endif
