#include "Foundation/Crc32.h"
#include "Graphics/BeetleTextureNames.h"
#include "Graphics/TextureRegion.h"
#include "HAL/FileManager.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "Modules/ModuleManager.h"
#include "OffscreenRender.h"
#include "Presentation/DrawList.h"
#include "Rendering/DrawListRendering.h"
#include "Rendering/TextureModLibrary.h"
#include "Rendering/VramTexture.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	bool SavePng(const FString& File, int32 Width, int32 Height, TFunctionRef<FColor(int32, int32)> Pixel)
	{
		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(Width * Height * 4);
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				const FColor Color = Pixel(X, Y);
				uint8* At = &Pixels[(Y * Width + X) * 4];
				At[0] = Color.R;
				At[1] = Color.G;
				At[2] = Color.B;
				At[3] = Color.A;
			}
		}
		IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		const auto Png = Module.CreateImageWrapper(EImageFormat::PNG);
		return Png && Png->SetRaw(Pixels.GetData(), Pixels.Num(), Width, Height, ERGBFormat::RGBA, 8) && FFileHelper::SaveArrayToFile(Png->GetCompressed(), *File);
	}

	FString PackName(std::span<const Weltall::uint16> Upload, std::span<const Weltall::uint16> Palette)
	{
		return FString(UTF8_TO_TCHAR(Weltall::BeetleTextureFileName({Weltall::Crc32OfUnits(Upload), Weltall::Crc32OfUnits(Palette)}).c_str())) + TEXT(".png");
	}

	bool Near(const FColor& Pixel, int32 R, int32 G, int32 B)
	{
		return FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeetleTexturePacksTest, "Weltall.Rendering.BeetlePacksDrawTheUploadsTheyNameWithTheirKinds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBeetleTexturePacksTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/BeetleTexturePacks"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// A 4-bit picture of 4 x 8 units (16 x 8 texels) through index 1, opaque green; a later upload over its last column of
	// units through index 2, semi-transparent red. Index 3 is never drawn here.
	Weltall::FVram Vram;
	const Weltall::FVramImage Picture{{64, 0, 4, 8}, std::vector<Weltall::uint16>(32, 0x1111)};
	Vram.Upload(Picture);
	Vram.Upload({{67, 0, 1, 8}, std::vector<Weltall::uint16>(8, 0x2222)});
	Weltall::FVramImage Clut{{0, 480, 16, 1}, std::vector<Weltall::uint16>(16, 0x7C00)};
	Clut.Units[1] = 0x03E0;
	Clut.Units[2] = 0x801F;
	Vram.Upload(Clut);
	Weltall::FTexturePage Page;
	Page.X = 64;
	Page.Depth = Weltall::ETextureDepth::Indexed4;
	const Weltall::FClutPosition Position{0, 480};
	const uint32 View = FVramTexture::Pack(Page, Position);
	const Weltall::FTextureRegion Region{View, 0, 0, 16, 8};

	// The pack's picture of the first upload at twice its texels: a hole, an opaque yellow, a semi-transparent magenta, then blue.
	const FString Pack = Folder / TEXT("Xenogears-texture-replacements");
	const bool bSaved = SavePng(Pack / PackName(Picture.Units, Clut.Units), 32, 16, [](int32 X, int32 Y)
	{
		if (Y == 0 && X == 0) return FColor(0, 0, 0, 0);
		if (Y == 0 && X == 1) return FColor(255, 255, 0, 255);
		if (Y == 0 && X == 2) return FColor(255, 0, 255, 100);
		return FColor(0, 0, 255, 255);
	});
	// An alternative of the same name in a folder within the pack, which Beetle never reads, though its path sorts first.
	const bool bAlternative = SavePng(Pack / TEXT("- alternate") / PackName(Picture.Units, Clut.Units), 32, 16, [](int32, int32) { return FColor::White; });
	if (!TestTrue(TEXT("The pack's pictures are saved"), bSaved && bAlternative)) return false;
	FTextureModLibrary::Configure(Folder, true, false);

	FVramTexture Texture;
	BeginInitResource(&Texture);
	ON_SCOPE_EXIT { BeginReleaseResource(&Texture); FlushRenderingCommands(); };
	Texture.Follow(Vram, std::span(&Region, 1));
	FTextureModLibrary::WaitForDecodes();
	Texture.Follow(Vram, std::span(&Region, 1));
	Weltall::FDrawList List;
	const Weltall::FDrawColor Grey{64, 64, 64};
	List.Primitives.push_back(Weltall::MakeGouraudRect({0, 0, 256, 256}, {Grey, Grey, Grey, Grey}));
	auto Sprite = Weltall::MakeSprite(0, 0, 16, 8, {0, Page, Position}, 0, 0);
	Sprite.Blend = Weltall::EDrawBlend::SemiTransparent;
	List.Primitives.push_back(Sprite);
	const auto Render = [&Texture, &List]()
	{
		return RenderPassesOffscreen({512, 512}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
		{
			AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 512, 512), {256, 256});
		});
	};
	const auto Pixels = Render();
	if (TestEqual(TEXT("The frame is read"), Pixels.Num(), 512 * 512))
	{
		TestTrue(TEXT("A pixel of no alpha is a hole"), Near(Pixels[0], 64, 64, 64));
		TestTrue(TEXT("A pixel of full alpha draws opaque in a semi-transparent sprite"), Near(Pixels[1], 255, 255, 0));
		TestTrue(TEXT("A pixel of partial alpha blends as a texel with bit 15 does"), Near(Pixels[2], 159, 32, 159));
		TestTrue(TEXT("The rest of the picture is drawn"), Near(Pixels[3], 0, 0, 255));
		TestTrue(TEXT("The picture is the pack's own, not the alternative within it"), Near(Pixels[512 + 8], 0, 0, 255));
		TestTrue(TEXT("Texels of an upload the pack has no picture of are the original's, blending by their bit 15"), Near(Pixels[24], 159, 32, 32));
	}

	// Weltall's own replacement of the region comes first.
	const FString Own = Folder / (FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(Weltall::TextureRegionKey(Vram, Region)).c_str())) + TEXT(".png"));
	if (!TestTrue(TEXT("Weltall's own picture is saved"), SavePng(Own, 16, 8, [](int32, int32) { return FColor::Cyan; }))) return false;
	FTextureModLibrary::Configure(Folder, false, false);
	FTextureModLibrary::Configure(Folder, true, false);
	Texture.Follow(Vram, std::span(&Region, 1));
	FTextureModLibrary::WaitForDecodes();
	Texture.Follow(Vram, std::span(&Region, 1));
	const auto WithOwn = Render();
	if (TestEqual(TEXT("The frame with Weltall's own picture is read"), WithOwn.Num(), 512 * 512))
	{
		TestTrue(TEXT("Weltall's own replacement is drawn rather than the pack's"), Near(WithOwn[4], 0, 255, 255));
	}

	// A pack made with Beetle's Reduce Palette Range names an upload through the colours from its least index to its greatest.
	const Weltall::FVramImage Third{{80, 0, 2, 2}, {0x3211, 0x1231, 0x1123, 0x2213}};
	Vram.Upload(Third);
	if (!TestTrue(TEXT("The reduced palette's picture is saved"),
		SavePng(Pack / PackName(Third.Units, std::span(Clut.Units).subspan(1, 3)), 16, 4, [](int32, int32) { return FColor::Green; })))
	{
		return false;
	}
	FTextureModLibrary::Configure(Folder, false, false);
	FTextureModLibrary::Configure(Folder, true, false);
	Page.X = 80 / 64 * 64;
	const Weltall::FTextureRegion Reduced{FVramTexture::Pack(Page, Position), static_cast<Weltall::uint8>((80 - Page.X) * 4), 0, 8, 2};
	const uint64 ReducedKey = Weltall::TextureRegionKey(Vram, Reduced);
	TestTrue(TEXT("A packed picture is made in the background"), FTextureModLibrary::FindPacked(Vram, Reduced, ReducedKey, 1 << 20).bPending);
	FTextureModLibrary::WaitForDecodes();
	const FTextureModLookup Found = FTextureModLibrary::FindPacked(Vram, Reduced, ReducedKey, 1 << 20);
	if (TestTrue(TEXT("The reduced palette's picture is found"), Found.Image.IsValid()))
	{
		TestEqual(TEXT("At the pack's scale"), Found.Image->Width, 16);
		TestTrue(TEXT("Its pixels' kinds are its alpha's"), Found.Image->bKindsFromAlpha);
		TestTrue(TEXT("Its pixels are the pack's"), Found.Image->Rgba[1] == 255 && Found.Image->Rgba[3] == Weltall::TexelKindAlpha::Plain);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBeetleTexturePacksWholePageTest, "Weltall.Rendering.BeetlePacksCoverOnlyTheirUploadsOfAWholePage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBeetleTexturePacksWholePageTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/BeetleTexturePacksWholePage"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// As the memory cards' screen has them: a save icon of 16 x 16 texels through a palette whose colour 0 is opaque white,
	// and beside it in the same page a line of text typed at run time, drawn through the text's own palette (0 clear, 1 red).
	Weltall::FVram Vram;
	const Weltall::FVramImage Icon{{64, 0, 4, 16}, std::vector<Weltall::uint16>(64, 0x1111)};
	Vram.Upload(Icon);
	std::vector<Weltall::uint16> Line(4 * 13, 0);
	Line[0] = 0x0011;
	Vram.Upload({{72, 0, 4, 13}, Line});
	Weltall::FVramImage IconClut{{0, 481, 16, 1}, std::vector<Weltall::uint16>(16, 0x03E0)};
	IconClut.Units[0] = 0x7FFF;
	Vram.Upload(IconClut);
	Weltall::FVramImage TextClut{{0, 480, 16, 1}, std::vector<Weltall::uint16>(16, 0x001F)};
	TextClut.Units[0] = 0;
	Vram.Upload(TextClut);
	Weltall::FTexturePage Page;
	Page.X = 64;
	Page.Depth = Weltall::ETextureDepth::Indexed4;
	const Weltall::FClutPosition IconPosition{0, 481};
	const Weltall::FClutPosition TextPosition{0, 480};

	const FString Pack = Folder / TEXT("Xenogears-texture-replacements");
	if (!TestTrue(TEXT("The pack's icon is saved"), SavePng(Pack / PackName(Icon.Units, IconClut.Units), 64, 64, [](int32, int32) { return FColor::Blue; })))
	{
		return false;
	}
	FTextureModLibrary::Configure(Folder, true, false);
	// The views as a menu leaves them, no picture known in either: each its whole page, the icon's pack part a region of its own.
	TArray<Weltall::FTextureRegion> Regions{Weltall::WholeTextureView(FVramTexture::Pack(Page, IconPosition)),
		Weltall::WholeTextureView(FVramTexture::Pack(Page, TextPosition))};
	FTextureModLibrary::AddPackedPartRegions(Vram, Regions[0], Regions);
	FTextureModLibrary::AddPackedPartRegions(Vram, Regions[1], Regions);
	if (!TestEqual(TEXT("The icon's palette has its one part as a region"), Regions.Num(), 3)) return false;
	TestTrue(TEXT("The part is the icon's upload"), Regions[2] == Weltall::FTextureRegion{Regions[0].View, 0, 0, 16, 16});

	FVramTexture Texture;
	BeginInitResource(&Texture);
	ON_SCOPE_EXIT { BeginReleaseResource(&Texture); FlushRenderingCommands(); };
	const std::span<const Weltall::FTextureRegion> Drawn(Regions.GetData(), Regions.Num());
	Texture.Follow(Vram, Drawn);
	FTextureModLibrary::WaitForDecodes();
	Texture.Follow(Vram, Drawn);
	Texture.Follow(Vram, Drawn);
	Weltall::FDrawList List;
	const Weltall::FDrawColor Grey{64, 64, 64};
	List.Primitives.push_back(Weltall::MakeGouraudRect({0, 0, 256, 256}, {Grey, Grey, Grey, Grey}));
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 16, 16, {0, Page, IconPosition}, 0, 0));
	List.Primitives.push_back(Weltall::MakeSprite(0, 32, 16, 13, {0, Page, TextPosition}, 32, 0));
	const auto Pixels = RenderPassesOffscreen({512, 512}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 512, 512), {256, 256});
	});
	if (TestEqual(TEXT("The frame is read"), Pixels.Num(), 512 * 512))
	{
		TestTrue(TEXT("The icon is the pack's picture"), Near(Pixels[4 * 512 + 4], 0, 0, 255));
		TestTrue(TEXT("The text's glyph is its own"), Near(Pixels[64 * 512], 255, 0, 0));
		TestTrue(TEXT("The text's background stays clear, not the icon palette's white"), Near(Pixels[64 * 512 + 8], 64, 64, 64));
	}
	return true;
}

#endif
