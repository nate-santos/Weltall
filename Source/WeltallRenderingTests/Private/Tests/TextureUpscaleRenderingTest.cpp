#include "Graphics/TextureUpscale.h"
#include "Misc/AutomationTest.h"
#include "RHIGPUReadback.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "RenderingThread.h"
#include "Rendering/VramTexture.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureUpscaleRenderingTest, "Weltall.Rendering.TexturesUpscaledOnTheGpuAreTheCoresBitForBit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** Units whose nibbles take few values, so that neighbours often agree and the rule has steps to find. */
	Weltall::FVramImage NoisyImage(uint32& Noise, int16 X, int16 Y, int16 Width, int16 Height, bool bSeparate = false)
	{
		Weltall::FVramImage Image{{X, Y, Width, Height}, std::vector<uint16>(static_cast<std::size_t>(Width * Height)), bSeparate};
		for (uint16& Unit : Image.Units)
		{
			uint16 Value = 0;
			for (int32 Nibble = 0; Nibble < 4; ++Nibble)
			{
				Noise = Noise * 1664525u + 1013904223u;
				Value = static_cast<uint16>(Value | (Noise >> 30) % 3 << (Nibble * 4));
			}
			Unit = Value;
		}
		return Image;
	}

	/** The upscaled texels of both depths as the GPU has them now, row by row. */
	TArray<TArray<uint8>> ReadUpscaled(const FVramTexture& Texture)
	{
		TArray<TArray<uint8>> Read;
		Read.SetNum(2);
		ENQUEUE_RENDER_COMMAND(ReadUpscaledTexels)([&Texture, &Read](FRHICommandListImmediate& RHICmdList)
		{
			FRHIGPUTextureReadback Readbacks[2] = {FRHIGPUTextureReadback(TEXT("TestUpscaled4")), FRHIGPUTextureReadback(TEXT("TestUpscaled8"))};
			FIntPoint Sizes[2];
			{
				FRDGBuilder GraphBuilder(RHICmdList);
				const FVramTexture::FUpscaled Upscaled = Texture.Upscaled(GraphBuilder);
				if (Upscaled.Indexed4 == nullptr)
				{
					return;
				}
				const FRDGTextureRef Textures[2] = {Upscaled.Indexed4, Upscaled.Indexed8};
				for (int32 Depth = 0; Depth < 2; ++Depth)
				{
					Sizes[Depth] = Textures[Depth]->Desc.Extent;
					AddEnqueueCopyPass(GraphBuilder, &Readbacks[Depth], Textures[Depth]);
				}
				GraphBuilder.Execute();
			}
			RHICmdList.SubmitAndBlockUntilGPUIdle();
			for (int32 Depth = 0; Depth < 2; ++Depth)
			{
				int32 RowPitch = 0;
				const uint8* Data = static_cast<const uint8*>(Readbacks[Depth].Lock(RowPitch));
				Read[Depth].SetNumUninitialized(Sizes[Depth].X * Sizes[Depth].Y);
				for (int32 Row = 0; Row < Sizes[Depth].Y; ++Row)
				{
					FMemory::Memcpy(&Read[Depth][Row * Sizes[Depth].X], Data + static_cast<std::size_t>(Row) * RowPitch, Sizes[Depth].X);
				}
				Readbacks[Depth].Unlock();
			}
		});
		FlushRenderingCommands();
		return Read;
	}
}

bool FTextureUpscaleRenderingTest::RunTest(const FString&)
{
	uint32 Noise = 61;
	Weltall::FVram Vram;
	Vram.Upload(NoisyImage(Noise, 0, 0, 1024, 512));
	Vram.Upload(NoisyImage(Noise, 300, 100, 20, 30, true));
	FVramTexture Texture;
	BeginInitResource(&Texture);
	Texture.SetUpscaled(true);
	const auto Compare = [this, &Texture, &Vram](const TCHAR* When)
	{
		Texture.Follow(Vram);
		const TArray<TArray<uint8>> Read = ReadUpscaled(Texture);
		const Weltall::ETextureDepth Depths[2] = {Weltall::ETextureDepth::Indexed4, Weltall::ETextureDepth::Indexed8};
		for (int32 Depth = 0; Depth < 2; ++Depth)
		{
			const Weltall::FTextureIndices Expected = Weltall::TextureUpscale::Upscale(Vram, Depths[Depth]);
			if (!TestEqual(FString::Printf(TEXT("%s: the %d-bit texels were read back"), When, Depth == 0 ? 4 : 8), Read[Depth].Num(),
					Expected.Width * Expected.Height))
			{
				continue;
			}
			int32 Differing = 0;
			FString First;
			for (int32 At = 0; At < Read[Depth].Num(); ++At)
			{
				if (Read[Depth][At] != Expected.Indices[static_cast<std::size_t>(At)] && Differing++ == 0)
				{
					First = FString::Printf(TEXT("(%d, %d): GPU %d, core %d"), At % Expected.Width, At / Expected.Width, Read[Depth][At],
						Expected.Indices[static_cast<std::size_t>(At)]);
				}
			}
			TestEqual(FString::Printf(TEXT("%s: %d-bit texels unlike the core's, the first %s"), When, Depth == 0 ? 4 : 8, *First), Differing, 0);
		}
	};
	Compare(TEXT("Made whole"));
	// Inside a tile, a separate texture across tiles, and across VRAM's corner: made again only about them.
	Vram.Upload(NoisyImage(Noise, 70, 20, 5, 3));
	Vram.Upload(NoisyImage(Noise, 500, 200, 90, 40, true));
	Vram.Upload(NoisyImage(Noise, 1022, 510, 4, 4));
	Compare(TEXT("Made again where VRAM changed"));
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

#endif
