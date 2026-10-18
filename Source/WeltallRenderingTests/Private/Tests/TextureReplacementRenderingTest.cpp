#include "Field/FieldScene.h"
#include "Graphics/Texture.h"
#include "Graphics/TexturePictures.h"
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
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/TextureModLibrary.h"
#include "Rendering/TextureModSources.h"
#include "Rendering/VramTexture.h"
#include "RHICommandList.h"
#include "RenderingThread.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace
{
	/** Blue pixels, then the given ones. */
	struct FTestPng
	{
		int32 Width = 0;
		int32 Height = 0;
		TArray<uint8> Pixels;

		FTestPng(int32 InWidth, int32 InHeight) : Width(InWidth), Height(InHeight)
		{
			Pixels.Init(0, Width * Height * 4);
			for (int32 Pixel = 0; Pixel < Width * Height; ++Pixel) Set(Pixel % Width, Pixel / Width, FColor::Blue);
		}

		void Set(int32 X, int32 Y, FColor Color)
		{
			uint8* At = &Pixels[(Y * Width + X) * 4];
			At[0] = Color.R;
			At[1] = Color.G;
			At[2] = Color.B;
			At[3] = Color.A;
		}

		bool Save(const FString& File) const
		{
			IImageWrapperModule& Module = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const auto Png = Module.CreateImageWrapper(EImageFormat::PNG);
			return Png && Png->SetRaw(Pixels.GetData(), Pixels.Num(), Width, Height, ERGBFormat::RGBA, 8) && FFileHelper::SaveArrayToFile(Png->GetCompressed(), *File);
		}
	};

	FString PngName(const Weltall::FVram& Vram, const Weltall::FTextureRegion& Region)
	{
		return FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(Weltall::TextureRegionKey(Vram, Region)).c_str())) + TEXT(".png");
	}

	bool Near(const FColor& Pixel, int32 R, int32 G, int32 B)
	{
		return FMath::Abs(Pixel.R - R) <= 2 && FMath::Abs(Pixel.G - G) <= 2 && FMath::Abs(Pixel.B - B) <= 2;
	}

	/** Follows the regions with their replacements read: the frame that asks for them, and once they have all arrived the next. */
	void FollowRead(FVramTexture& Texture, const Weltall::FVram& Vram, std::span<const Weltall::FTextureRegion> Regions)
	{
		Texture.Follow(Vram, Regions);
		FTextureModLibrary::WaitForDecodes();
		Texture.Follow(Vram, Regions);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureReplacementRenderingTest, "Weltall.Rendering.PngReplacementsKeepHighResolutionAndOriginalBlendClasses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTextureReplacementRenderingTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureReplacement"));
	const FString Captured = Folder / FTextureModLibrary::OriginalsFolder / TEXT("captured");
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	Weltall::FVram Vram;
	Weltall::FVramImage Source{{0, 0, 256, 256}, std::vector<Weltall::uint16>(256 * 256, 0x001F)};
	Source.Units[0] = 0;
	Source.Units[1] = 0x83E0;
	Vram.Upload(Source);
	const Weltall::FTexturePage Page{0, 0, Weltall::ETextureDepth::Direct15};
	const Weltall::FClutPosition Clut{};
	const uint32 Key = FVramTexture::Pack(Page, Clut);
	const Weltall::FTextureRegion Whole = Weltall::WholeTextureView(Key);
	FTestPng Picture(512, 512);
	// Texel 0 is transparent: its first subtexel stays a hole, its second is drawn. Then two subtexels of one texel, a
	// refined edge, and a texel in a tile far from the first.
	Picture.Set(0, 0, FColor(0, 0, 0, 0));
	Picture.Set(4, 0, FColor::Yellow);
	Picture.Set(5, 0, FColor::Magenta);
	Picture.Set(6, 0, FColor(0, 0, 255, 0));
	Picture.Set(300, 300, FColor::Green);
	if (!TestTrue(TEXT("The synthetic PNG is saved"), Picture.Save(Folder / PngName(Vram, Whole)))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	FollowRead(Texture, Vram, std::span(&Whole, 1));
	Weltall::FDrawList List;
	const Weltall::FDrawColor Grey{64, 64, 64};
	List.Primitives.push_back(Weltall::MakeGouraudRect({0, 0, 256, 256}, {Grey, Grey, Grey, Grey}));
	auto Sprite = Weltall::MakeSprite(0, 0, 256, 256, {0, Page, Clut}, 0, 0);
	Sprite.Blend = Weltall::EDrawBlend::SemiTransparent;
	List.Primitives.push_back(Sprite);
	const auto Render = [&Texture, &List]()
	{
		return RenderPassesOffscreen({512, 512}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
		{
			AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 512, 512), {256, 256});
		});
	};
	const auto Actual = Render();
	if (TestEqual(TEXT("The replacement frame is read"), Actual.Num(), 512 * 512))
	{
		TestTrue(TEXT("A transparent subtexel over a transparent texel is a hole"), Near(Actual[0], 64, 64, 64));
		TestTrue(TEXT("An opaque subtexel over a transparent texel is drawn"), Near(Actual[1], 0, 0, 255));
		TestTrue(TEXT("Original semi-transparent texels still average"), Near(Actual[2], 32, 32, 159));
		TestTrue(TEXT("The first high-resolution subtexel is yellow"), Near(Actual[4], 255, 255, 0));
		TestTrue(TEXT("The next subtexel within the same original texel is magenta"), Near(Actual[5], 255, 0, 255));
		TestTrue(TEXT("PNG alpha refines an opaque source texel"), Near(Actual[6], 64, 64, 64));
		TestTrue(TEXT("The adjacent high-resolution texel stays blue"), Near(Actual[7], 0, 0, 255));
		TestEqual(TEXT("A tile far from the first is read"), Actual[300 * 512 + 300], FColor::Green);
	}
	// The same GPU resources serve the 3D scene shader used by the field, battle and world map.
	const FFieldSceneMesh Mesh(Weltall::FFieldScene{});
	FFieldSceneDraw Draw;
	Draw.ProjectionDistance = 500;
	Draw.Background = FLinearColor(64.0f / 255.0f, 64.0f / 255.0f, 64.0f / 255.0f);
	Draw.Objects = {FVector4f(1, 0, 0, 0), FVector4f(0, 1, 0, 0), FVector4f(0, 0, 1, 0)};
	Draw.Colors = {FUintVector4(0x808080, 0x808080, 0x808080, 0x808080)};
	Draw.DynamicPolygons = {FFieldScenePolygonState{Key, 1, 0, 0xFFFF}};
	Draw.DynamicCorners = {
		FFieldSceneCorner{FVector3f(-224, -224, 1000), 0, FVector2f(0, 0), 0, 0},
		FFieldSceneCorner{FVector3f(224, -224, 1000), 0, FVector2f(256, 0), 0, 1},
		FFieldSceneCorner{FVector3f(-224, 224, 1000), 0, FVector2f(0, 256), 0, 2},
		FFieldSceneCorner{FVector3f(224, 224, 1000), 0, FVector2f(256, 256), 0, 3}};
	Draw.Order = {0, 1, 2, 1, 3, 2};
	Draw.Runs.Add(FFieldDrawRun{0, 6});
	const auto ScenePixels = RenderFieldSceneOffscreen(Mesh, Texture, Draw, {512, 512});
	if (TestEqual(TEXT("The replacement scene is read"), ScenePixels.Num(), 512 * 512))
	{
		TestEqual(TEXT("The 3D scene shader samples the first high-resolution subtexel"), ScenePixels[4], FColor::Yellow);
		TestEqual(TEXT("The 3D scene shader samples the next subtexel"), ScenePixels[5], FColor::Magenta);
		TestEqual(TEXT("The 3D scene shader samples a far tile"), ScenePixels[300 * 512 + 300], FColor::Green);
	}
	Texture.Follow(Vram);
	const auto UploadOnly = Render();
	if (TestEqual(TEXT("An upload-only frame is read"), UploadOnly.Num(), 512 * 512))
	{
		TestEqual(TEXT("Uploading CPU VRAM preserves the bound replacements"), UploadOnly[4], FColor::Yellow);
	}
	Texture.Follow(Vram, std::span<const Weltall::FTextureRegion>{});
	const auto EmptySources = Render();
	if (TestEqual(TEXT("An untextured source set is read"), EmptySources.Num(), 512 * 512))
	{
		TestEqual(TEXT("An explicit empty source set releases the previous scene's replacements"), EmptySources[4], FColor::Red);
	}
	Texture.Follow(Vram, std::span(&Whole, 1));
	FTextureModLibrary::Configure(Folder, true, true);
	const Weltall::FVramRect FeedbackRegion{0, 0, 256, 256};
	Texture.SetReplacementExclusions(std::span(&FeedbackRegion, 1));
	Texture.Follow(Vram, std::span(&Whole, 1));
	// Rendered feedback changes GPU VRAM while the CPU retains the old source pixels.
	ENQUEUE_RENDER_COMMAND(WriteSyntheticFeedback)([&Texture](FRHICommandListImmediate& RHICmdList)
	{
		const TArray<uint16> Feedback = [] { TArray<uint16> Made; Made.Init(0x03E0, 256 * 256); return Made; }();
		RHICmdList.UpdateTexture2D(Texture.Texture(), 0, FUpdateTextureRegion2D(0, 0, 0, 0, 256, 256), 256 * sizeof(uint16),
			reinterpret_cast<const uint8*>(Feedback.GetData()));
	});
	const auto FeedbackPixels = Render();
	if (TestEqual(TEXT("The live feedback frame is read"), FeedbackPixels.Num(), 512 * 512))
	{
		TestEqual(TEXT("GPU-only feedback keeps its current texels instead of the stale PNG"), FeedbackPixels[4], FColor::Green);
	}
	FTextureModLibrary::WaitForCaptures();
	TestFalse(TEXT("GPU-only feedback is not captured from stale CPU pixels"), IFileManager::Get().FileExists(*(Captured / PngName(Vram, Whole))));
	Vram.Upload(Source);
	Texture.SetReplacementExclusions({});
	FollowRead(Texture, Vram, std::span(&Whole, 1));
	const auto Restored = Render();
	if (TestEqual(TEXT("The restored source frame is read"), Restored.Num(), 512 * 512))
	{
		TestEqual(TEXT("An original CPU upload permits PNG replacements again"), Restored[4], FColor::Yellow);
	}
	Texture.Follow(Vram, std::span(&Whole, 1));
	FTextureModLibrary::WaitForCaptures();
	TestTrue(TEXT("The restored original texture is captured once it holds still"), IFileManager::Get().FileExists(*(Captured / PngName(Vram, Whole))));
	const auto CheckExcluded = [this, &Captured, &Texture, &Vram](const Weltall::FTextureRegion& Region, const Weltall::FVramRect& Written, const TCHAR* Name)
	{
		Texture.SetReplacementExclusions(std::span(&Written, 1));
		Texture.Follow(Vram, std::span(&Region, 1));
		FTextureModLibrary::WaitForCaptures();
		TestFalse(Name, IFileManager::Get().FileExists(*(Captured / PngName(Vram, Region))));
	};
	CheckExcluded(Weltall::WholeTextureView(FVramTexture::Pack({960, 0, Weltall::ETextureDepth::Direct15}, {})), {0, 0, 1, 1},
		TEXT("A page wrapping across VRAM's right edge is excluded"));
	CheckExcluded(Weltall::WholeTextureView(FVramTexture::Pack({0, 0, Weltall::ETextureDepth::Indexed4}, {0, 256})), {0, 256, 1, 1},
		TEXT("A palette written by feedback excludes its indexed page"));
	Texture.SetReplacementExclusions({});
	FTextureModLibrary::Configure(Folder, false, false);
	Texture.Follow(Vram);
	const auto Disabled = Render();
	if (TestEqual(TEXT("The unmodified frame is read"), Disabled.Num(), 512 * 512)) TestEqual(TEXT("Disabling replacements restores the source"), Disabled[4], FColor::Red);
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureRegionReplacementTest, "Weltall.Rendering.ReplacementsFollowTheirRegionsAndTheOriginalsColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTextureRegionReplacementTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureRegionReplacement"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// A 4-bit page: index 1 white everywhere, its CLUT at (0, 480).
	Weltall::FVram Vram;
	Vram.Upload({{0, 0, 64, 256}, std::vector<Weltall::uint16>(64 * 256, 0x1111)});
	std::vector<Weltall::uint16> Colors(16, 0x7FFF);
	Colors[0] = 0;
	Vram.Upload({{0, 480, 16, 1}, Colors});
	const uint32 View = FVramTexture::Pack({0, 0, Weltall::ETextureDepth::Indexed4}, {0, 480});
	const Weltall::FTextureRegion Left{View, 0, 0, 8, 8};
	const Weltall::FTextureRegion Right{View, 8, 0, 8, 8};
	FTestPng Grey(8, 8);
	for (int32 Pixel = 0; Pixel < 64; ++Pixel) Grey.Set(Pixel % 8, Pixel / 8, FColor(128, 128, 128));
	// Both regions show the same picture, so one PNG stands for both.
	if (!Grey.Save(Folder / PngName(Vram, Left))) return false;
	TestEqual(TEXT("Two places of one picture share its key"), PngName(Vram, Left), PngName(Vram, Right));
	FTextureModLibrary::Configure(Folder, true, false);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	const Weltall::FTextureRegion Regions[] = {Left};
	FollowRead(Texture, Vram, Regions);
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 16, 8, {0, {0, 0, Weltall::ETextureDepth::Indexed4}, {0, 480}}, 0, 0));
	const auto Render = [&Texture, &List]()
	{
		return RenderPassesOffscreen({16, 8}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
		{
			AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 16, 8), {16, 8});
		});
	};
	const auto Bound = Render();
	if (TestEqual(TEXT("The region frame is read"), Bound.Num(), 16 * 8))
	{
		TestTrue(TEXT("The bound region shows its picture"), Near(Bound[0], 128, 128, 128));
		TestEqual(TEXT("Texels outside every bound region keep the original"), Bound[8], FColor::White);
	}
	// Drawn twice as large, a picture is blended between its pixels: a quarter of the way from white to black.
	FTestPng Edge(8, 8);
	for (int32 Pixel = 0; Pixel < 64; ++Pixel) Edge.Set(Pixel % 8, Pixel / 8, Pixel % 8 < 4 ? FColor::White : FColor::Black);
	FTextureModLibrary::Configure(Folder, false, false);
	if (!Edge.Save(Folder / TEXT("edge") / PngName(Vram, Left))) return false;
	if (!IFileManager::Get().Delete(*(Folder / PngName(Vram, Left)))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FollowRead(Texture, Vram, Regions);
	const auto Magnified = RenderPassesOffscreen({32, 16}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 32, 16), {16, 8});
	});
	if (TestEqual(TEXT("The magnified frame is read"), Magnified.Num(), 32 * 16))
	{
		TestTrue(TEXT("A magnified picture is filtered between its pixels"), Near(Magnified[7], 191, 191, 191));
		TestEqual(TEXT("Far from an edge the picture's own pixel is drawn"), Magnified[2], FColor::White);
	}
	// A picture of another size is stretched over its region each way: one and a half times as wide, half white, half
	// black, its black begins at the region's middle.
	FTestPng Wide(12, 8);
	for (int32 Pixel = 0; Pixel < 96; ++Pixel) Wide.Set(Pixel % 12, Pixel / 12, Pixel % 12 < 6 ? FColor::White : FColor::Black);
	FTextureModLibrary::Configure(Folder, false, false);
	if (!Wide.Save(Folder / TEXT("edge") / PngName(Vram, Left))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FollowRead(Texture, Vram, Regions);
	const auto Stretched = Render();
	if (TestEqual(TEXT("The stretched frame is read"), Stretched.Num(), 16 * 8))
	{
		TestEqual(TEXT("A wider picture's first half covers the region's first half"), Stretched[3], FColor::White);
		TestEqual(TEXT("A wider picture's second half covers the region's second half"), Stretched[4], FColor::Black);
	}
	FTextureModLibrary::Configure(Folder, false, false);
	if (!Grey.Save(Folder / TEXT("edge") / PngName(Vram, Left))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FollowRead(Texture, Vram, Regions);
	// Half the white: the picture's grey darkens by the same ratio.
	Colors[1] = 0x3DEF;
	Vram.Upload({{0, 480, 16, 1}, Colors});
	Texture.Follow(Vram, Regions);
	const auto Faded = Render();
	if (TestEqual(TEXT("The faded frame is read"), Faded.Num(), 16 * 8))
	{
		TestTrue(TEXT("A faded CLUT keeps the picture and fades it with the original"), Near(Faded[0], 61, 61, 61));
		TestTrue(TEXT("The original fades outside the picture"), Near(Faded[8], 123, 123, 123));
	}
	// A picture of its own for the new colours takes over from the faded one.
	FTestPng Red(8, 8);
	for (int32 Pixel = 0; Pixel < 64; ++Pixel) Red.Set(Pixel % 8, Pixel / 8, FColor::Red);
	if (!Red.Save(Folder / TEXT("faded") / PngName(Vram, Left))) return false;
	FTextureModLibrary::Configure(Folder, false, false);
	FTextureModLibrary::Configure(Folder, true, false);
	FollowRead(Texture, Vram, Regions);
	const auto Swapped = Render();
	if (TestEqual(TEXT("The swapped frame is read"), Swapped.Num(), 16 * 8))
	{
		TestEqual(TEXT("The colours' own picture is drawn as it is"), Swapped[0], FColor::Red);
	}
	// The same indices through a CLUT of their own, as a fog level or a colour variant draws them, take the grey picture
	// matched through the white CLUT, darkened as their colour is.
	FTextureModLibrary::Configure(Folder, false, false);
	IFileManager::Get().DeleteDirectory(*(Folder / TEXT("faded")), false, true);
	Colors[1] = 0x7FFF;
	Vram.Upload({{0, 480, 16, 1}, Colors});
	Colors[1] = 0x3DEF;
	Vram.Upload({{0, 481, 16, 1}, Colors});
	FTextureModLibrary::Configure(Folder, true, false);
	const uint32 Variant = FVramTexture::Pack({0, 0, Weltall::ETextureDepth::Indexed4}, {0, 481});
	const Weltall::FTextureRegion Both[] = {Weltall::FTextureRegion{Variant, 0, 0, 8, 8}, Left};
	FollowRead(Texture, Vram, Both);
	Weltall::FDrawList VariantList;
	VariantList.Primitives.push_back(Weltall::MakeSprite(0, 0, 16, 8, {0, {0, 0, Weltall::ETextureDepth::Indexed4}, {0, 481}}, 0, 0));
	const auto VariantPixels = RenderPassesOffscreen({16, 8}, [&Texture, &VariantList](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&VariantList}, Color, FIntRect(0, 0, 16, 8), {16, 8});
	});
	if (TestEqual(TEXT("The variant frame is read"), VariantPixels.Num(), 16 * 8))
	{
		TestTrue(TEXT("Another CLUT's view of the same indices takes the matched picture in its colours"), Near(VariantPixels[0], 61, 61, 61));
	}
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureModelSeedsTest, "Weltall.Rendering.ModelsTeachTheirReplacementsTheColoursTheyWereLoadedWith",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTextureModelSeedsTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureModelSeeds"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// A model's 4-bit region of index 1, loaded with colour 1 white; by the time it is drawn its CLUT has been made half
	// as bright (a battle's tint or flash), so only its colours at the load name the replacement.
	Weltall::FVram Vram;
	Vram.Upload({{64, 0, 2, 8}, std::vector<Weltall::uint16>(2 * 8, 0x1111)});
	std::vector<Weltall::uint16> Loaded(16, 0x7FFF);
	Loaded[0] = 0;
	std::vector<Weltall::uint16> Drawn = Loaded;
	Drawn[1] = 0x3DEF;
	Vram.Upload({{0, 480, 16, 1}, Drawn});
	const uint32 View = FVramTexture::Pack({64, 0, Weltall::ETextureDepth::Indexed4}, {0, 480});
	const Weltall::FTexturePictureSeed Seed{{View, 0, 0, 8, 8}, std::make_shared<const std::vector<Weltall::uint16>>(Loaded)};
	const uint64 LoadedKey = Weltall::TextureRegionKeyWith(Weltall::ReadTextureRegionTexels(Vram, Seed.Region), Loaded);
	FTestPng Grey(8, 8);
	for (int32 Pixel = 0; Pixel < 64; ++Pixel) Grey.Set(Pixel % 8, Pixel / 8, FColor(128, 128, 128));
	if (!Grey.Save(Folder / (FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(LoadedKey).c_str())) + TEXT(".png")))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 8, 8, {0, {64, 0, Weltall::ETextureDepth::Indexed4}, {0, 480}}, 0, 0));
	FollowTextureModSources(Texture, Vram, nullptr, {}, {}, std::span(&Seed, 1));
	FTextureModLibrary::WaitForDecodes();
	FollowTextureModSources(Texture, Vram, nullptr, {}, {}, std::span(&Seed, 1));
	const auto Pixels = RenderPassesOffscreen({8, 8}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 8, 8), {8, 8});
	});
	if (TestEqual(TEXT("The model's frame is read"), Pixels.Num(), 8 * 8))
	{
		TestTrue(TEXT("The model takes the replacement of its loaded colours, following the drawn ones"), Near(Pixels[3 * 8 + 3], 61, 61, 61));
	}
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureModelAnimationsTest, "Weltall.Rendering.ModelsKeepTheirReplacementsThroughTheirOwnTexelAnimations",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTextureModelAnimationsTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureModelAnimations"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// A model's 4-bit region loaded as index 1, which an animation of its own (a stage's blend) has rewritten as index 2
	// by the time it is drawn: its key at the load names the replacement, its texels as drawn none.
	Weltall::FVram Vram;
	Vram.Upload({{64, 0, 2, 8}, std::vector<Weltall::uint16>(2 * 8, 0x1111)});
	std::vector<Weltall::uint16> Colors(16, 0x7FFF);
	Colors[0] = 0;
	Vram.Upload({{0, 480, 16, 1}, Colors});
	const uint32 View = FVramTexture::Pack({64, 0, Weltall::ETextureDepth::Indexed4}, {0, 480});
	const auto Palette = std::make_shared<const std::vector<Weltall::uint16>>(Colors);
	const Weltall::FTextureRegion Region{View, 0, 0, 8, 8};
	const uint64 LoadedKey = Weltall::TextureRegionKeyWith(Weltall::ReadTextureRegionTexels(Vram, Region), Colors);
	Vram.Upload({{64, 0, 2, 8}, std::vector<Weltall::uint16>(2 * 8, 0x2222)});
	FTestPng Grey(8, 8);
	for (int32 Pixel = 0; Pixel < 64; ++Pixel) Grey.Set(Pixel % 8, Pixel / 8, FColor(128, 128, 128));
	if (!Grey.Save(Folder / (FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(LoadedKey).c_str())) + TEXT(".png")))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 8, 8, {0, {64, 0, Weltall::ETextureDepth::Indexed4}, {0, 480}}, 0, 0));
	const auto Draw = [this, &Vram, &List](const Weltall::FTexturePictureSeed& Seed)
	{
		FVramTexture Texture;
		BeginInitResource(&Texture);
		FollowTextureModSources(Texture, Vram, nullptr, {}, {}, std::span(&Seed, 1));
		FTextureModLibrary::WaitForDecodes();
		FollowTextureModSources(Texture, Vram, nullptr, {}, {}, std::span(&Seed, 1));
		const auto Pixels = RenderPassesOffscreen({8, 8}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
		{
			AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 8, 8), {8, 8});
		});
		BeginReleaseResource(&Texture);
		FlushRenderingCommands();
		TestEqual(TEXT("The model's frame is read"), Pixels.Num(), 8 * 8);
		return Pixels.Num() == 8 * 8 ? Pixels[3 * 8 + 3] : FColor::Black;
	};
	TestTrue(TEXT("Keyed by its texels as drawn, the animated picture has no replacement: its white original is drawn"), Draw({Region, Palette}).R > 200);
	TestTrue(TEXT("Named by its picture as the load left it, the model keeps its replacement through the animation"),
		Near(Draw({Region, Palette, LoadedKey}), 128, 128, 128));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTexturePartsTest,"Weltall.Rendering.PartsOfALargerPictureTakeItsReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTexturePartsTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureParts"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// An 8-bit picture 320 texels across at (768, 256), as the field's dungeon maps lie, its replacement red on its left
	// half and green on its right; drawn from the page at 832, which begins 128 texels into it.
	Weltall::FVram Vram;
	std::vector<Weltall::uint16> Colors(256, 0x7FFF);
	Colors[0] = 0;
	Weltall::FVramImage Picture{{768, 256, 160, 8}, std::vector<Weltall::uint16>(160 * 8, 0x0101)};
	Picture.Picture = Weltall::FVramPictureForm{1, std::make_shared<const std::vector<Weltall::uint16>>(Colors)};
	Vram.Upload(Picture);
	Vram.Upload({{0, 480, 256, 1}, Colors});
	std::vector<Weltall::FTexturePictureSeed> Seeds;
	Weltall::AddPictureSeeds(Vram.Pictures(), Seeds);
	if (!TestEqual(TEXT("The picture is one seed"), static_cast<int32>(Seeds.size()), 1)) return false;
	FTestPng Halves(320, 8);
	for (int32 Pixel = 0; Pixel < 320 * 8; ++Pixel) Halves.Set(Pixel % 320, Pixel / 320, Pixel % 320 < 160 ? FColor::Red : FColor::Green);
	const uint64 Key = Weltall::TextureRegionKeyWith(Weltall::ReadTextureRegionTexels(Vram, Seeds[0].Region), Colors);
	if (!Halves.Save(Folder / (FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(Key).c_str())) + TEXT(".png")))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 64, 8, {0, {832, 256, Weltall::ETextureDepth::Indexed8}, {0, 480}}, 0, 0));
	FollowTextureModSources(Texture, Vram, nullptr, {}, {&List});
	FTextureModLibrary::WaitForDecodes();
	FollowTextureModSources(Texture, Vram, nullptr, {}, {&List});
	const auto Pixels = RenderPassesOffscreen({64, 8}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 64, 8), {64, 8});
	});
	if (TestEqual(TEXT("The part's frame is read"), Pixels.Num(), 64 * 8))
	{
		TestEqual(TEXT("The page's first texels show the picture's left half from its 128th pixel"), Pixels[4 * 64 + 10], FColor::Red);
		TestEqual(TEXT("The page's texels past the picture's middle show its right half"), Pixels[4 * 64 + 40], FColor::Green);
	}
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureAtlasGrowthTest,"Weltall.Rendering.ReplacementAtlasGrowsKeepingItsPictures",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTextureAtlasGrowthTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureAtlasGrowth"));
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// Two direct pages of their own colours, replaced by a small red picture and by a green one eight times the page's
	// size, which takes more tiles than the atlas first has.
	Weltall::FVram Vram;
	Vram.Upload({{0, 0, 256, 256}, std::vector<Weltall::uint16>(256 * 256, 0x7C00)});
	Vram.Upload({{256, 0, 256, 256}, std::vector<Weltall::uint16>(256 * 256, 0x7C1F)});
	const uint32 SmallPage = FVramTexture::Pack({0, 0, Weltall::ETextureDepth::Direct15}, {});
	const uint32 LargePage = FVramTexture::Pack({256, 0, Weltall::ETextureDepth::Direct15}, {});
	const Weltall::FTextureRegion Small = Weltall::WholeTextureView(SmallPage);
	const Weltall::FTextureRegion Both[] = {Small, Weltall::WholeTextureView(LargePage)};
	FTestPng Red(64, 64);
	FTestPng Green(2048, 2048);
	for (int32 Pixel = 0; Pixel < 64 * 64; ++Pixel) Red.Set(Pixel % 64, Pixel / 64, FColor::Red);
	for (int32 Pixel = 0; Pixel < 2048 * 2048; ++Pixel) Green.Set(Pixel % 2048, Pixel / 2048, FColor::Green);
	if (!Red.Save(Folder / PngName(Vram, Small)) || !Green.Save(Folder / PngName(Vram, Both[1]))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 256, 256, {0, {0, 0, Weltall::ETextureDepth::Direct15}, {}}, 0, 0));
	List.Primitives.push_back(Weltall::MakeSprite(256, 0, 256, 256, {0, {256, 0, Weltall::ETextureDepth::Direct15}, {}}, 0, 0));
	const auto Render = [&Texture, &List]()
	{
		return RenderPassesOffscreen({512, 256}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
		{
			AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 512, 256), {512, 256});
		});
	};
	FollowRead(Texture, Vram, std::span(&Small, 1));
	const auto First = Render();
	if (TestEqual(TEXT("The first frame is read"), First.Num(), 512 * 256))
	{
		TestEqual(TEXT("A picture smaller than its region is stretched over it"), First[128 * 512 + 128], FColor::Red);
	}
	FollowRead(Texture, Vram, Both);
	const auto Grown = Render();
	if (TestEqual(TEXT("The grown frame is read"), Grown.Num(), 512 * 256))
	{
		TestEqual(TEXT("A picture uploaded before the atlas grew is still drawn"), Grown[128 * 512 + 128], FColor::Red);
		TestEqual(TEXT("A picture larger than the first atlas is drawn"), Grown[255 * 512 + 511], FColor::Green);
	}
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTextureModSourcesTest,"Weltall.Rendering.TextureSourcesTakeSpriteCellsAndWholePages",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTextureModSourcesTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TextureModSources"));
	const FString Captured = Folder / FTextureModLibrary::OriginalsFolder / TEXT("captured");
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	Weltall::FVram Vram;
	Vram.Upload({{0, 0, 256, 256}, std::vector<Weltall::uint16>(256 * 256, 0x001F)});
	Vram.Upload({{256, 0, 256, 256}, std::vector<Weltall::uint16>(256 * 256, 0x03E0)});
	const uint32 SpritePage = FVramTexture::Pack({0, 0, Weltall::ETextureDepth::Direct15}, {});
	const uint32 ModelPage = FVramTexture::Pack({256, 0, Weltall::ETextureDepth::Direct15}, {});
	FFieldSceneDraw Draw;
	// A sprite's cell (texels 8-23 by 4-11) and a model's polygon.
	Draw.DynamicPolygons = {FFieldScenePolygonState{SpritePage, 4, 8 | 4 << 8, 23 | 11 << 8}, FFieldScenePolygonState{ModelPage, 0, 0, 10 | 10 << 8}};
	FTextureModLibrary::Configure(Folder, false, true);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	FollowTextureModSources(Texture, Vram, nullptr, {&Draw}, {});
	FTextureModLibrary::WaitForCaptures();
	TestFalse(TEXT("Nothing is captured before its texels have held still for a frame"),
		IFileManager::Get().FileExists(*(Captured / PngName(Vram, Weltall::WholeTextureView(ModelPage)))));
	FollowTextureModSources(Texture, Vram, nullptr, {&Draw}, {});
	FTextureModLibrary::WaitForCaptures();
	TestTrue(TEXT("A sprite's cell is captured as its own picture"),
		IFileManager::Get().FileExists(*(Captured / PngName(Vram, Weltall::FTextureRegion{SpritePage, 8, 4, 16, 8}))));
	TestTrue(TEXT("A model's page is captured whole"), IFileManager::Get().FileExists(*(Captured / PngName(Vram, Weltall::WholeTextureView(ModelPage)))));
	TestFalse(TEXT("A sprite's page is not captured whole"), IFileManager::Get().FileExists(*(Captured / PngName(Vram, Weltall::WholeTextureView(SpritePage)))));
	// A model whose texels the game moves every frame, as it scrolls water, has no still picture to save.
	for (Weltall::uint16 Frame = 1; Frame < 4; ++Frame)
	{
		Vram.Upload({{256, 0, 1, 1}, {Frame}});
		FollowTextureModSources(Texture, Vram, nullptr, {&Draw}, {});
	}
	FTextureModLibrary::WaitForCaptures();
	TArray<FString> Saved;
	IFileManager::Get().FindFiles(Saved, *(Captured / TEXT("*.png")), true, false);
	TestEqual(TEXT("A picture changing every frame is not captured"), Saved.Num(), 2);
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTexturePictureReplacementTest, "Weltall.Rendering.PicturesTakeTheReplacementsOfTheirOwnColours",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FTexturePictureReplacementTest::RunTest(const FString&)
{
	const FString Folder = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation/TexturePictureReplacement"));
	const FString Captured = Folder / FTextureModLibrary::OriginalsFolder / TEXT("captured");
	IFileManager::Get().DeleteDirectory(*Folder, false, true);
	IFileManager::Get().MakeDirectory(*Folder, true);
	ON_SCOPE_EXIT
	{
		FTextureModLibrary::Configure(Folder, false, false);
		IFileManager::Get().DeleteDirectory(*Folder, false, true);
	};
	// A 4-bit page of index 1, and over its first 8 x 8 texels a picture whose own colour 1 is white; the game draws
	// both through a CLUT of its own in which it is half as bright.
	Weltall::FVram Vram;
	Vram.Upload({{64, 0, 4, 8}, std::vector<Weltall::uint16>(4 * 8, 0x1111)});
	std::vector<Weltall::uint16> Own(16, 0x7FFF);
	Own[0] = 0;
	Weltall::FVramImage Picture{{64, 0, 2, 8}, std::vector<Weltall::uint16>(2 * 8, 0x1111)};
	Picture.Picture = Weltall::FVramPictureForm{0, std::make_shared<const std::vector<Weltall::uint16>>(Own)};
	Vram.Upload(Picture);
	std::vector<Weltall::uint16> Drawn = Own;
	Drawn[1] = 0x3DEF;
	Vram.Upload({{0, 480, 16, 1}, Drawn});
	std::vector<Weltall::FTexturePictureSeed> Seeds;
	Weltall::AddPictureSeeds(Vram.Pictures(), Seeds);
	if (!TestEqual(TEXT("The picture is one seed"), static_cast<int32>(Seeds.size()), 1)) return false;
	const uint64 OwnKey = Weltall::TextureRegionKeyWith(Weltall::ReadTextureRegionTexels(Vram, Seeds[0].Region), Own);
	const FString OwnName = FString(UTF8_TO_TCHAR(Weltall::AssetKeyName(OwnKey).c_str())) + TEXT(".png");
	FTestPng Grey(8, 8);
	for (int32 Pixel = 0; Pixel < 64; ++Pixel) Grey.Set(Pixel % 8, Pixel / 8, FColor(128, 128, 128));
	if (!TestTrue(TEXT("The picture's PNG is saved"), Grey.Save(Folder / OwnName))) return false;
	FTextureModLibrary::Configure(Folder, true, false);
	FVramTexture Texture;
	BeginInitResource(&Texture);
	Weltall::FDrawList List;
	List.Primitives.push_back(Weltall::MakeSprite(0, 0, 16, 8, {0, {64, 0, Weltall::ETextureDepth::Indexed4}, {0, 480}}, 0, 0));
	FollowTextureModSources(Texture, Vram, nullptr, {}, {&List});
	FTextureModLibrary::WaitForDecodes();
	FollowTextureModSources(Texture, Vram, nullptr, {}, {&List});
	const auto Pixels = RenderPassesOffscreen({16, 8}, [&Texture, &List](FRDGBuilder& GraphBuilder, FRDGTextureRef Color)
	{
		AddDrawListPasses(GraphBuilder, Texture, FDrawListFrame{&List}, Color, FIntRect(0, 0, 16, 8), {16, 8});
	});
	if (TestEqual(TEXT("The picture frame is read"), Pixels.Num(), 16 * 8))
	{
		TestTrue(TEXT("The picture takes the replacement of its own colours, following the drawn ones"), Near(Pixels[0], 61, 61, 61));
		TestTrue(TEXT("The page beside the picture keeps the original"), Near(Pixels[8], 123, 123, 123));
	}
	// Captured, the picture is saved in its own colours as well as in the drawn ones, and the page beside it not at all.
	FTextureModLibrary::Configure(Folder, false, false);
	IFileManager::Get().Delete(*(Folder / OwnName));
	FTextureModLibrary::Configure(Folder, false, true);
	FollowTextureModSources(Texture, Vram, nullptr, {}, {&List});
	FollowTextureModSources(Texture, Vram, nullptr, {}, {&List});
	FTextureModLibrary::WaitForCaptures();
	const uint32 View = FVramTexture::Pack({64, 0, Weltall::ETextureDepth::Indexed4}, {0, 480});
	TestTrue(TEXT("The picture is captured in its own colours"), IFileManager::Get().FileExists(*(Captured / OwnName)));
	TestTrue(TEXT("The picture is captured as drawn"), IFileManager::Get().FileExists(*(Captured / PngName(Vram, Weltall::FTextureRegion{View, 0, 0, 8, 8}))));
	TestFalse(TEXT("A page holding a picture is not captured whole"), IFileManager::Get().FileExists(*(Captured / PngName(Vram, Weltall::WholeTextureView(View)))));
	BeginReleaseResource(&Texture);
	FlushRenderingCommands();
	return true;
}

#endif
