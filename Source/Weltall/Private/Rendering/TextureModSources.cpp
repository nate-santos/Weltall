#include "Rendering/TextureModSources.h"

#include "FieldDrawBuilder.h"
#include "Graphics/TexturePictures.h"
#include "Graphics/TextureRegion.h"
#include "Presentation/DrawList.h"
#include "Rendering/FieldSceneRendering.h"
#include "Rendering/TextureModLibrary.h"
#include "Rendering/VramTexture.h"
#include "Algo/BinarySearch.h"
#include "Algo/Unique.h"

void FollowTextureModSources(FVramTexture& Texture, const Weltall::FVram& Vram, const FFieldSceneMesh* Mesh,
	std::initializer_list<const FFieldSceneDraw*> Draws, std::initializer_list<const Weltall::FDrawList*> Lists,
	std::span<const Weltall::FTexturePictureSeed> Models, std::span<const Weltall::FTextureRegion> Sheets)
{
	if (!FTextureModLibrary::IsActive()) return;
	static TArray<Weltall::FTextureRegion> Regions;
	static TArray<uint32> RegionViews;
	static TArray<uint32> Pages;
	static std::vector<Weltall::FTextureRegion> InPictures;
	Regions.Reset();
	RegionViews.Reset();
	Pages.Reset();
	if (Mesh != nullptr) Regions.Append(Mesh->TextureRegions().GetData(), Mesh->TextureRegions().Num());
	for (const Weltall::FTexturePictureSeed& Model : Models) Regions.Add(Model.Region);
	Regions.Append(Sheets.data(), static_cast<int32>(Sheets.size()));
	for (const FFieldSceneDraw* Draw : Draws)
	{
		if (Draw == nullptr) continue;
		Regions.Append(Draw->SpriteCells);
		Regions.Append(Draw->BackdropRegions);
	}
	for (const FFieldSceneDraw* Draw : Draws)
	{
		if (Draw == nullptr) continue;
		for (const FFieldScenePolygonState& State : Draw->DynamicPolygons)
		{
			if (State.Texture == FVramTexture::NoTexture) continue;
			if ((State.Flags & FieldPolygonFlags::Sprite) == 0 || (State.Flags & FieldPolygonFlags::Window) != 0)
			{
				Pages.Add(State.Texture);
				continue;
			}
			// A sprite's or particle's quad reads exactly its cell, unless it is some rows of a cell already taken whole.
			const Weltall::FTextureRegion Quad = Weltall::TextureRegionBetween(State.Texture, static_cast<uint8>(State.TexelMin),
				static_cast<uint8>(State.TexelMin >> 8), static_cast<uint8>(State.TexelMax), static_cast<uint8>(State.TexelMax >> 8));
			const bool bInCell = Draw->SpriteCells.ContainsByPredicate([&Quad](const Weltall::FTextureRegion& Cell)
			{
				return Cell.View == Quad.View && Cell.Contains(Quad.U, Quad.V) && Cell.Contains(Quad.U + Quad.Width - 1, Quad.V + Quad.Height - 1);
			});
			if (!bInCell) Regions.Add(Quad);
		}
	}
	for (const Weltall::FDrawList* List : Lists)
	{
		if (List == nullptr) continue;
		for (const auto& Primitive : List->Primitives)
		{
			if (Primitive.Texture) Pages.Add(Weltall::PackTextureView(Primitive.Texture->Page, Primitive.Texture->Clut));
		}
	}
	// Whatever else is drawn from a view no region is known on takes the pictures VRAM holds there, or else its whole page as one.
	for (const Weltall::FTextureRegion& Region : Regions) RegionViews.Add(Region.View);
	RegionViews.Sort();
	Pages.Sort();
	Pages.SetNum(Algo::Unique(Pages), EAllowShrinking::No);
	for (const uint32 Page : Pages)
	{
		if (Algo::BinarySearch(RegionViews, Page) != INDEX_NONE) continue;
		InPictures.clear();
		Weltall::AddPictureRegionsInView(Vram.Pictures(), Page, InPictures);
		if (InPictures.empty()) Regions.Add(Weltall::WholeTextureView(Page));
		else Regions.Append(InPictures.data(), static_cast<int32>(InPictures.size()));
	}
	// A whole page holds what the game draws through other CLUTs too, such as the menus' text beside the save icons: a pack's
	// pictures cover only their own uploads in it, as in Beetle, each a region of its own after the page's (which Weltall's
	// own picture of the page, where it has one, comes before).
	for (int32 Index = 0, Count = Regions.Num(); Index < Count; ++Index)
	{
		const Weltall::FTextureRegion Whole = Regions[Index];
		if (Whole == Weltall::WholeTextureView(Whole.View)) FTextureModLibrary::AddPackedPartRegions(Vram, Whole, Regions);
	}
	Texture.Follow(Vram, std::span<const Weltall::FTextureRegion>(Regions.GetData(), Regions.Num()), Models);
}
