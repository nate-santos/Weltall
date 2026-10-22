#include "Graphics/Texture.h"
#include "Graphics/Vram.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "OffscreenRender.h"
#include "Presentation/DrawList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "Rendering/DrawListRendering.h"
#include "Rendering/VramTexture.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSeparateTranslucentBatchTest, "Weltall.Rendering.SeparateTranslucentBatchesPreservePixelOrder",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FSeparateTranslucentBatchTest::RunTest(const FString&)
{
	using namespace Weltall;
	IConsoleVariable* Toggle = IConsoleManager::Get().FindConsoleVariable(TEXT("weltall.BatchSeparateTranslucent"));
	if (!TestNotNull(TEXT("Batch comparison switch"), Toggle)) return false;
	const int32 PreviousToggle = Toggle->GetInt();
	ON_SCOPE_EXIT { Toggle->Set(PreviousToggle, ECVF_SetByCode); };
	FVram Vram;
	Vram.Upload({{0, 0, 4, 8}, std::vector<uint16>(32, 0x3210)});
	Vram.Upload({{0, 256, 4, 1}, {0, 0x001F, 0x83E0, 0xFC00}});
	FVramTexture Texture;
	BeginInitResource(&Texture);
	Texture.Follow(Vram);
	ON_SCOPE_EXIT { BeginReleaseResource(&Texture); FlushRenderingCommands(); };
	FDrawList List;
	List.Primitives.push_back(MakeGouraudRect({0, 0, 320, 224}, {FDrawColor{64, 64, 64}, {64, 64, 64}, {64, 64, 64}, {64, 64, 64}}));
	auto Sprite = [&](int16 X, int16 Y, Weltall::EBlendMode Mode)
	{
		auto Draw = MakeSprite(X, Y, 16, 8, {0, {0, 0, ETextureDepth::Indexed4, Mode}, {0, 256}}, 0, 0);
		Draw.Blend = EDrawBlend::SemiTransparent;
		List.Primitives.push_back(Draw);
	};
	for (const auto Mode : {Weltall::EBlendMode::Average, Weltall::EBlendMode::Add, Weltall::EBlendMode::Subtract, Weltall::EBlendMode::AddQuarter})
	{
		const int16 Y = 16 + static_cast<int16>(List.Primitives.size()) * 2;
		// Adjacent glyph-sized shapes can share both passes; overlaps and mode changes must split.
		Sprite(10, Y, Mode); Sprite(26, Y, Mode); Sprite(42, Y, Mode);
		Sprite(45, Y, Mode); Sprite(61, Y, Mode);
		List.Primitives.back().Clip = {64, Y, 8, 8};
		Sprite(80, Y, Weltall::EBlendMode::Average);
	}
	FDrawList Before = List;
	std::vector<int32> Matches(List.Primitives.size());
	for (std::size_t Index = 0; Index < Matches.size(); ++Index) Matches[Index] = static_cast<int32>(Index);
	// These shapes are disjoint in the latest tick but overlap while interpolating.
	for (auto& Corner : Before.Primitives[2].Corners) Corner.X -= 24;
	const FIntPoint Size(640, 448);
	auto Render = [&](const FDrawListFrame& Frame, bool bBatch)
	{
		Toggle->Set(bBatch ? 1 : 0, ECVF_SetByCode);
		return RenderPassesOffscreen(Size, [&](FRDGBuilder& Graph, FRDGTextureRef Output)
		{
			AddClearRenderTargetPass(Graph, Output, FLinearColor::Black);
			AddDrawListPasses(Graph, Texture, Frame, Output, FIntRect(12, 8, 628, 440), {320, 224});
		});
	};
	for (float Alpha : {0.0f, 0.5f, 1.0f})
	{
		const FDrawListFrame Frame{&List, &Before, &Matches, Alpha};
		const TArray<FColor> Individual = Render(Frame, false);
		const TArray<FColor> Batched = Render(Frame, true);
		TestEqual(TEXT("Complete GPU readback"), Batched.Num(), Size.X * Size.Y);
		TestTrue(TEXT("Batched pixels match ordered individual passes"), Batched == Individual);
		TestTrue(TEXT("The test actually draws colored texels"), Batched.Contains(FColor::Red));
	}
	return true;
}
#endif
