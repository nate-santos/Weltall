#include "FieldDrawBuilder.h"

#include "Graphics/TextureUpscale.h"

namespace
{
	/** Adds a stretch of the order to the runs, joined to the last when it continues it with the same state. */
	void AddRun(TArray<FFieldDrawRun>& Runs, const FFieldDrawRun& Run, bool bJoinable)
	{
		if (bJoinable && !Runs.IsEmpty())
		{
			FFieldDrawRun& Last = Runs.Last();
			if (Last.Pass == Run.Pass && Last.Blend == Run.Blend && Last.Depth == Run.Depth && Last.bDepthWrite == Run.bDepthWrite && Last.Rule == Run.Rule
				&& Last.FirstVertex + Last.VertexCount == Run.FirstVertex)
			{
				Last.VertexCount += Run.VertexCount;
				return;
			}
		}
		Runs.Add(Run);
	}

	Weltall::FFieldDrawItem ToItem(uint32 Polygon)
	{
		return {Polygon, Weltall::EFieldDrawKind::Scenery};
	}

	Weltall::FFieldDrawItem ToItem(const Weltall::FFieldDrawItem& Item)
	{
		return Item;
	}

	constexpr uint32 SemiTransparentCode = 0x2000000;
}

FFieldDrawBuilder::FItem FFieldDrawBuilder::Describe(const Weltall::FFieldDrawItem& Item) const
{
	FItem Described = DescribeShape(Item);
	Described.Kinds = TexelKindsOf(Described);
	return Described;
}

uint8 FFieldDrawBuilder::TexelKindsOf(const FItem& Item) const
{
	if (TexelKinds == nullptr || !Item.bSemiTransparent || !Item.bTextured || Item.CornerCount == 0)
	{
		return Weltall::TexelKinds::Any;
	}
	const uint32 MeshCorners = Mesh.Corners();
	const uint32 Polygon = Item.FirstCorner < MeshCorners ? Mesh.CornerPolygon(Item.FirstCorner) : Draw.DynamicCorners[Item.FirstCorner - MeshCorners].Polygon;
	const FFieldScenePolygonState& State = Polygon < Mesh.Polygons() ? Mesh.States()[Polygon] : Draw.DynamicPolygons[Polygon - Mesh.Polygons()];
	if (State.Texture == Weltall::NoTextureView)
	{
		return Weltall::TexelKinds::Any;
	}
	const EFieldTextureFilter Filter = (State.Flags & FieldPolygonFlags::Sprite) != 0 ? Draw.SpriteFilter : Draw.Filter;
	const Weltall::FTexelRect Rect{static_cast<uint8>(State.TexelMin), static_cast<uint8>(State.TexelMin >> 8), static_cast<uint8>(State.TexelMax),
		static_cast<uint8>(State.TexelMax >> 8)};
	// A window's texels wrap: only its CLUT tells.
	return TexelKinds->Kinds(State.Texture, Rect, Filter == EFieldTextureFilter::Upscaled ? Weltall::TextureUpscale::Reach : 0, (State.Flags & FieldPolygonFlags::Window) != 0);
}

FFieldDrawBuilder::FItem FFieldDrawBuilder::DescribeShape(const Weltall::FFieldDrawItem& Item) const
{
	if (Item.Kind == Weltall::EFieldDrawKind::Gear)
	{
		return DescribeGear(Item);
	}
	if (Item.Kind == Weltall::EFieldDrawKind::SpriteModel)
	{
		return DescribeSpriteModel(Item);
	}
	FItem Described;
	if (Item.Kind == Weltall::EFieldDrawKind::Character)
	{
		const Weltall::FFieldCharacterQuad& Entry = Quads[Item.Index];
		const Weltall::FSpriteQuad& Quad = Entry.Quad;
		Described.FirstCorner = Mesh.Corners() + Item.Index * QuadCorners;
		Described.CornerCount = QuadCorners;
		Described.bSemiTransparent = (Quad.ColorCode & SemiTransparentCode) != 0;
		Described.bTextured = true;
		Described.bSprite = !Characters[Entry.Character].bOnFloor;
		Described.bShadow = !Described.bSprite;
		Described.PageBlend = Weltall::FTexturePage::FromAttribute(Quad.TexturePage).Blend;
		return Described;
	}
	if (Item.Kind == Weltall::EFieldDrawKind::Backdrop)
	{
		// Every packet of the panorama is opaque: one stretch of corners, drawn behind the rest as the background is.
		Described.FirstCorner = Mesh.Corners() + Draw.BackdropCorners;
		Described.CornerCount = Draw.BackdropCornerCount;
		Described.bTextured = true;
		Described.bBackground = true;
		return Described;
	}
	if (Item.Kind == Weltall::EFieldDrawKind::Particle)
	{
		Described.FirstCorner = Mesh.Corners() + Draw.ParticleCorners + Item.Index * QuadCorners;
		Described.CornerCount = QuadCorners;
		Described.bSemiTransparent = true;
		Described.bTextured = true;
		Described.PageBlend = Weltall::FTexturePage::FromAttribute(Particles[Item.Index].TexturePage).Blend;
		return Described;
	}
	const Weltall::FFieldScenePolygon& Polygon = Scene.Polygons[Item.Index];
	const bool bRedirected = Draw.Redirects.IsValidIndex(static_cast<int32>(Item.Index)) && Draw.Redirects[Item.Index] != FFieldSceneDraw::NoRedirect;
	Described.FirstCorner = bRedirected ? Mesh.Corners() + Draw.Redirects[Item.Index] : Mesh.FirstCorner(Item.Index);
	Described.CornerCount = Mesh.CornerCount(Item.Index);
	Described.bSemiTransparent = (Polygon.Packet.Code & 2) != 0;
	Described.bTextured = Scene.PrimitiveOf(Polygon).Traits().bTextured;
	Described.bBackground = Scene.Objects[Polygon.Object].bBackground;
	Described.bScenery = !Described.bBackground;
	Described.PageBlend = Polygon.Page.Blend;
	return Described;
}

FFieldDrawBuilder::FItem FFieldDrawBuilder::DescribeForDepth(const Weltall::FFieldDrawItem& Item, bool bSceneryByDepth) const
{
	FItem Described = Describe(Item);
	if (Described.bModel)
	{
		Described.Rule = EFieldDepthRule::Model;
	}
	else if (Described.bScenery && bSceneryByDepth)
	{
		Described.Rule = EFieldDepthRule::Scenery;
	}
	return Described;
}

FFieldDrawBuilder::FItem FFieldDrawBuilder::DescribeSpriteModel(const Weltall::FFieldDrawItem& Item) const
{
	FItem Described;
	if (SpriteModels == nullptr || !Draw.SpriteModelCorners.IsValidIndex(static_cast<int32>(Item.Index))
		|| Draw.SpriteModelCorners[Item.Index] == FFieldSceneDraw::NoRedirect)
	{
		return Described;
	}
	const Weltall::FFieldSpriteModelPolygon& Polygon = SpriteModels->Polygons[Item.Index];
	Described.FirstCorner = Mesh.Corners() + Draw.SpriteModelCorners[Item.Index];
	Described.CornerCount = Polygon.CornerCount == 4 ? QuadCorners : 3;
	Described.bSemiTransparent = (Polygon.Code & 2) != 0;
	Described.bTextured = Weltall::MdlPrimitiveTraits(Polygon.Type).bTextured;
	Described.bModel = true;
	Described.PageBlend = Weltall::FTexturePage::FromAttribute(Polygon.TexturePage).Blend;
	return Described;
}

uint32 FFieldDrawBuilder::Add(const FItem& Item, EFieldPass Pass, Weltall::EBlendMode Blend, EFieldRunDepth Depth, bool bDepthWrite, bool bJoinable)
{
	const uint32 First = static_cast<uint32>(Draw.Order.Num());
	if (Item.CornerCount == 0)
	{
		return First;
	}
	for (uint32 Corner = 0; Corner < Item.CornerCount; ++Corner)
	{
		Draw.Order.Add(Item.FirstCorner + Corner);
	}
	// Nothing joins across the screen wave: its runs are drawn in a pass of their own.
	AddRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(Pass), Blend, Depth, bDepthWrite, Item.Rule}, bJoinable && Draw.Runs.Num() > Draw.WaveRuns);
	return First;
}

void FFieldDrawBuilder::AddLayered(const FItem& Item, Weltall::EBlendMode UntexturedBlend, EFieldRunDepth Depth, bool bDepthWrite)
{
	// As the GPU draws them: a semi-transparent textured polygon's plain texels, then its blended ones.
	if (Item.CornerCount == 0)
	{
		return;
	}
	if (!HasBlendedTexels(Item))
	{
		if (HasOpaqueTexels(Item))
		{
			Add(Item, EFieldPass::Opaque, Weltall::EBlendMode::Average, Depth, bDepthWrite, true);
		}
	}
	else if (Item.bTextured)
	{
		if (!HasOpaqueTexels(Item))
		{
			// Its blended texels alone, which join the blended texels drawn just before them.
			Add(Item, EFieldPass::SemiTransparentTexels, Item.PageBlend, Depth, false, true);
			return;
		}
		const uint32 First = Add(Item, EFieldPass::PlainTexels, Weltall::EBlendMode::Average, Depth, bDepthWrite, false);
		AddRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(EFieldPass::SemiTransparentTexels), Item.PageBlend, Depth, false, Item.Rule}, false);
	}
	else
	{
		Add(Item, EFieldPass::BlendAll, UntexturedBlend, Depth, false, true);
	}
}

template <typename TItems>
void FFieldDrawBuilder::Painters(const TItems& Items)
{
	// An untextured semi-transparent polygon blends as the last textured primitive drawn before it set the GPU.
	Weltall::EBlendMode Current = Weltall::EBlendMode::Average;
	for (const auto& Entry : Items)
	{
		const FItem Item = Describe(ToItem(Entry));
		if (Item.bTextured)
		{
			Current = Item.PageBlend;
		}
		AddLayered(Item, Current, EFieldRunDepth::None, false);
	}
}

template <typename TItems>
void FFieldDrawBuilder::Depth(const TItems& Items, bool bSceneryByDepth)
{
	// First the depth where the opaque texels are of the characters' sprites and the 3D models, marking those pixels,
	// and of the scenery when it is sorted by depth too.
	for (const auto& Entry : Items)
	{
		const FItem Item = DescribeForDepth(ToItem(Entry), bSceneryByDepth);
		if ((IsMarking(Item) || Item.Rule == EFieldDepthRule::Scenery) && HasOpaqueTexels(Item))
		{
			Add(Item, PlainPass(Item), Weltall::EBlendMode::Average, EFieldRunDepth::Mask, true, true);
		}
	}
	// Then everything in the painter's order: outside the marked pixels exactly as the original draws
	// it (the scenery's decals staying on the ground they were drawn over), inside the marks against the
	// depth, so the sprites and the models are sorted against the scenery, the effects and each other by
	// depth. A sprite or a model draws its own opaque texels there only; a shadow, lying under them, never
	// over a sprite. The scenery sorted by depth is drawn against the prepasses everywhere: the order decides
	// only within the decals' margin.
	Weltall::EBlendMode Current = Weltall::EBlendMode::Average;
	for (const auto& Entry : Items)
	{
		const FItem Item = DescribeForDepth(ToItem(Entry), bSceneryByDepth);
		if (Item.bTextured)
		{
			Current = Item.PageBlend;
		}
		const EFieldPass Plain = PlainPass(Item);
		if (Item.bSprite && !HasOpaqueTexels(Item))
		{
			// Nothing of it marks or stands at its depth: its blended texels alone.
			Add(Item, EFieldPass::SemiTransparentTexels, Item.PageBlend, EFieldRunDepth::Masked, false, false);
		}
		else if (Item.bSprite)
		{
			// Against the depth, then its own pushed back by the margin, so what the order draws after covers it unless clearly behind.
			const uint32 First = Add(Item, Plain, Weltall::EBlendMode::Average, EFieldRunDepth::InsideMask, false, false);
			if (HasBlendedTexels(Item))
			{
				AddRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(EFieldPass::SemiTransparentTexels), Item.PageBlend, EFieldRunDepth::Masked, false},
					false);
			}
			AddRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(Plain), Weltall::EBlendMode::Average, EFieldRunDepth::PushedBack, true}, false);
		}
		else if (IsMarking(Item))
		{
			// A model's opaque texels against its prepass's depth, which within the margin lets the order decide among the models.
			const uint32 First = Add(Item, Plain, Weltall::EBlendMode::Average, EFieldRunDepth::InsideMask, false, !HasBlendedTexels(Item));
			if (HasBlendedTexels(Item))
			{
				AddRun(Draw.Runs,
					{First, Item.CornerCount, static_cast<uint8>(EFieldPass::SemiTransparentTexels), Item.PageBlend, EFieldRunDepth::Masked, false, Item.Rule},
					false);
			}
		}
		else
		{
			AddLayered(Item, Current, Item.bShadow ? EFieldRunDepth::OutsideMask : Item.bBackground ? EFieldRunDepth::None : EFieldRunDepth::Masked, true);
		}
	}
}

void FFieldDrawBuilder::PaintersOrder(std::span<const Weltall::FFieldDrawItem> Order)
{
	Painters(Order);
}

void FFieldDrawBuilder::PaintersOrder()
{
	Painters(Frame.DrawOrder);
}

void FFieldDrawBuilder::DepthBuffer(std::span<const Weltall::FFieldDrawItem> Order, bool bSceneryByDepth)
{
	Depth(Order, bSceneryByDepth);
}

void FFieldDrawBuilder::DepthBuffer(bool bSceneryByDepth)
{
	Depth(Frame.DrawOrder, bSceneryByDepth);
}
