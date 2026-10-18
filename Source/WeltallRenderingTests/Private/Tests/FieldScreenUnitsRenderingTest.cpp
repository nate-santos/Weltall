#include "Graphics/Vram.h"
#include "Misc/AutomationTest.h"
#include "OffscreenRender.h"
#include "Presentation/DrawList.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/DrawListRendering.h"
#include "Rendering/FieldScreenUnitsRendering.h"
#include "Rendering/VramTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

// Pixels of a picture of the screen written into VRAM's units, then drawn from VRAM as a direct-colour
// texture: the colour's 5 bits a channel, the copy's brightness, and black where there is no picture.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFieldScreenUnitsRenderingTest, "Weltall.Rendering.FieldScreenPixelsGoIntoVramAsTheirUnits",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FFieldScreenUnitsRenderingTest::RunTest(const FString&)
{
	constexpr int32 Width = 320;
	constexpr int32 Height = 224;
	FVramTexture Units;
	BeginInitResource(&Units);
	Units.Follow(Weltall::FVram{});
	ENQUEUE_RENDER_COMMAND(WriteTestScreenUnits)([&Units](FRHICommandListImmediate& RHICmdList)
	{
		FRDGBuilder GraphBuilder(RHICmdList);
		FRDGTextureRef Picture = GraphBuilder.CreateTexture(FRDGTextureDesc::Create2D(FIntPoint(Width, Height), PF_B8G8R8A8, FClearValueBinding::Black,
			TexCreate_RenderTargetable | TexCreate_ShaderResource), TEXT("TestScreenPicture"));
		AddClearRenderTargetPass(GraphBuilder, Picture, FLinearColor(1.0f, 0.5f, 0.0f));
		FRDGTextureRef Vram = GraphBuilder.RegisterExternalTexture(CreateRenderTarget(Units.Texture(), TEXT("TestVram")));
		AddFieldScreenUnitsPass(GraphBuilder, Picture, Vram, {FIntRect(0, 0, 8, 2), FIntPoint(0, 0), 128});
		AddFieldScreenUnitsPass(GraphBuilder, Picture, Vram, {FIntRect(8, 0, 16, 2), FIntPoint(0, 0), 64});
		AddFieldScreenUnitsPass(GraphBuilder, nullptr, Vram, {FIntRect(16, 0, 24, 2), FIntPoint(0, 0), 128});
		GraphBuilder.SetTextureAccessFinal(Vram, ERHIAccess::SRVMask);
		GraphBuilder.Execute();
	});
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeGouraudRect({0, 0, Width, Height}, {Weltall::FDrawColor{64, 64, 64}, {64, 64, 64}, {64, 64, 64}, {64, 64, 64}}));
	const Weltall::FDrawTexture Direct{0, {0, 0, Weltall::ETextureDepth::Direct15, Weltall::EBlendMode::Average}, {}};
	Weltall::FDrawPrimitive Sprite = Weltall::MakeSprite(0, 0, 24, 2, Direct, 0, 0);
	Sprite.bModulate = false;
	List.Primitives.push_back(Sprite);
	const TArray<FColor> Pixels = RenderPassesOffscreen(FIntPoint(Width, Height), [&Units, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Output)
	{
		const FDrawListFrame Frame{&List};
		AddDrawListPasses(GraphBuilder, Units, Frame, Output, FIntRect(0, 0, Width, Height), Weltall::FScreenSize{Width, Height});
	});
	BeginReleaseResource(&Units);
	FlushRenderingCommands();
	if (!TestEqual(TEXT("The frame was read back"), Pixels.Num(), Width * Height))
	{
		return false;
	}
	const auto Check = [this, &Pixels](const TCHAR* What, int32 X, int32 LowR, int32 HighR, int32 LowG, int32 HighG, int32 HighB)
	{
		const FColor& Pixel = Pixels[X];
		TestTrue(FString::Printf(TEXT("%s: (%d, %d, %d)"), What, Pixel.R, Pixel.G, Pixel.B),
			Pixel.R >= LowR && Pixel.R <= HighR && Pixel.G >= LowG && Pixel.G <= HighG && Pixel.B <= HighB);
	};
	// 31, 15 or 16 (the clear's half, 127 or 128 in 8 bits), 0 in 5 bits a channel.
	Check(TEXT("the picture's colour"), 2, 248, 255, 120, 134, 4);
	// At half brightness: 15, 7 or 8, 0.
	Check(TEXT("at half brightness"), 10, 118, 126, 54, 68, 4);
	// No picture: black units, texel 0, which lets the grey through.
	Check(TEXT("without a picture"), 18, 62, 66, 62, 66, 66);
	return true;
}

#endif
