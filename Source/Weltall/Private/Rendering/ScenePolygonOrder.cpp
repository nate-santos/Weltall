#include "ScenePolygonOrder.h"

#include "FieldDrawBuilder.h"
#include "Graphics/TextureRegion.h"
#include "Graphics/TextureUpscale.h"

namespace
{
	void AddScenePolygonRun(TArray<FFieldDrawRun>& Runs, const FFieldDrawRun& Run, bool bJoinable)
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

	EFieldDepthRule RuleOf(const FScenePolygonItem& Item)
	{
		return Item.bModel ? EFieldDepthRule::Model : Item.bScenery ? EFieldDepthRule::Scenery : EFieldDepthRule::Entry;
	}

	/** Whether it has texels drawn opaque, which a prepass can take the depth of. */
	bool HasOpaqueTexels(const FScenePolygonItem& Item)
	{
		return (Item.bTextured && (Item.Kinds & Weltall::TexelKinds::Plain) != 0) || !Item.bSemiTransparent;
	}

	/** Whether it has texels that blend: a semi-transparent polygon's, but where its texture has none. */
	bool HasBlendedTexels(const FScenePolygonItem& Item)
	{
		return Item.bSemiTransparent && (!Item.bTextured || (Item.Kinds & Weltall::TexelKinds::SemiTransparent) != 0);
	}

	/** The pass that draws its opaque texels: all of a polygon's that has no blended ones. */
	EFieldPass PlainPass(const FScenePolygonItem& Item)
	{
		return HasBlendedTexels(Item) ? EFieldPass::PlainTexels : EFieldPass::Opaque;
	}

	uint32 AddScenePolygonToOrder(FFieldSceneDraw& Draw, const FScenePolygonItem& Item, EFieldPass Pass, Weltall::EBlendMode Blend, EFieldRunDepth Depth,
		bool bDepthWrite, bool bJoinable, EFieldDepthRule Rule = EFieldDepthRule::Entry)
	{
		const uint32 First = static_cast<uint32>(Draw.Order.Num());
		for (uint32 Corner = 0; Corner < Item.CornerCount; ++Corner)
		{
			Draw.Order.Add(Item.FirstCorner + Corner);
		}
		AddScenePolygonRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(Pass), Blend, Depth, bDepthWrite, Rule}, bJoinable);
		return First;
	}

	/** As the GPU draws a polygon: opaque; a semi-transparent textured one's plain texels, then its blended ones; an untextured one blended. */
	void AddLayeredScenePolygon(FFieldSceneDraw& Draw, const FScenePolygonItem& Item, Weltall::EBlendMode UntexturedBlend, EFieldRunDepth Depth, bool bDepthWrite,
		EFieldDepthRule Rule = EFieldDepthRule::Entry)
	{
		if (!HasBlendedTexels(Item))
		{
			if (HasOpaqueTexels(Item))
			{
				AddScenePolygonToOrder(Draw, Item, EFieldPass::Opaque, Weltall::EBlendMode::Average, Depth, bDepthWrite, true, Rule);
			}
		}
		else if (Item.bTextured && !HasOpaqueTexels(Item))
		{
			// Its blended texels alone, which join the blended texels drawn just before them.
			AddScenePolygonToOrder(Draw, Item, EFieldPass::SemiTransparentTexels, Item.PageBlend, Depth, false, true, Rule);
		}
		else if (Item.bTextured)
		{
			const uint32 First = AddScenePolygonToOrder(Draw, Item, EFieldPass::PlainTexels, Weltall::EBlendMode::Average, Depth, bDepthWrite, false, Rule);
			AddScenePolygonRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(EFieldPass::SemiTransparentTexels), Item.PageBlend, Depth, false, Rule}, false);
		}
		else
		{
			AddScenePolygonToOrder(Draw, Item, EFieldPass::BlendAll, UntexturedBlend, Depth, false, true, Rule);
		}
	}
}

void AddScenePolygonsInPaintersOrder(FFieldSceneDraw& Draw, TConstArrayView<FScenePolygonItem> Items)
{
	// An untextured semi-transparent polygon blends as the last textured one drawn before it set the GPU.
	Weltall::EBlendMode Current = Weltall::EBlendMode::Average;
	for (const FScenePolygonItem& Item : Items)
	{
		if (Item.bTextured || Item.bSetsBlend)
		{
			Current = Item.PageBlend;
		}
		AddLayeredScenePolygon(Draw, Item, Current, EFieldRunDepth::None, false);
	}
}

void AddScenePolygonsUnderDepthRule(FFieldSceneDraw& Draw, TConstArrayView<FScenePolygonItem> Items)
{
	// The sprites' and the models' opaque texels mark their pixels; the scenery sorted by depth writes the depth of its own,
	// what is drawn over the models once they have marked theirs, beside them.
	for (const FScenePolygonItem& Item : Items)
	{
		if ((Item.bSprite || Item.bModel || (Item.bScenery && !Item.bOverModels)) && HasOpaqueTexels(Item))
		{
			AddScenePolygonToOrder(Draw, Item, PlainPass(Item), Weltall::EBlendMode::Average, EFieldRunDepth::Mask, true, true, RuleOf(Item));
		}
	}
	for (const FScenePolygonItem& Item : Items)
	{
		if (Item.bScenery && Item.bOverModels && HasOpaqueTexels(Item))
		{
			const EFieldPass Plain = PlainPass(Item);
			const EFieldRunDepth Depth = Item.bGround ? EFieldRunDepth::MaskBesideMarks : EFieldRunDepth::MaskBesideModels;
			AddScenePolygonToOrder(Draw, Item, Plain, Weltall::EBlendMode::Average, Depth, true, true, EFieldDepthRule::Scenery);
		}
	}
	Weltall::EBlendMode Current = Weltall::EBlendMode::Average;
	for (const FScenePolygonItem& Item : Items)
	{
		if (Item.bTextured || Item.bSetsBlend)
		{
			Current = Item.PageBlend;
		}
		if (Item.bSprite && !HasOpaqueTexels(Item))
		{
			// Nothing of it marks or stands at its depth: its blended texels alone.
			AddScenePolygonToOrder(Draw, Item, EFieldPass::SemiTransparentTexels, Item.PageBlend, EFieldRunDepth::Masked, false, false);
		}
		else if (Item.bSprite)
		{
			const EFieldPass Plain = PlainPass(Item);
			const uint32 First = AddScenePolygonToOrder(Draw, Item, Plain, Weltall::EBlendMode::Average, EFieldRunDepth::InsideMask, false, false);
			if (HasBlendedTexels(Item))
			{
				AddScenePolygonRun(Draw.Runs,
					{First, Item.CornerCount, static_cast<uint8>(EFieldPass::SemiTransparentTexels), Item.PageBlend, EFieldRunDepth::Masked, false}, false);
			}
			AddScenePolygonRun(Draw.Runs, {First, Item.CornerCount, static_cast<uint8>(Plain), Weltall::EBlendMode::Average, EFieldRunDepth::PushedBack, true},
				false);
		}
		else if (Item.bModel && HasOpaqueTexels(Item))
		{
			// A model's opaque texels against its prepass's depth, which within the margin lets the order decide among the models.
			const EFieldPass Plain = PlainPass(Item);
			const uint32 First = AddScenePolygonToOrder(Draw, Item, Plain, Weltall::EBlendMode::Average, EFieldRunDepth::InsideMask, false,
				!HasBlendedTexels(Item), EFieldDepthRule::Model);
			if (HasBlendedTexels(Item))
			{
				AddScenePolygonRun(Draw.Runs,
					{First, Item.CornerCount, static_cast<uint8>(EFieldPass::SemiTransparentTexels), Item.PageBlend, EFieldRunDepth::Masked, false,
						EFieldDepthRule::Model},
					false);
			}
		}
		else if (Item.bModel && Item.bTextured && Item.bSemiTransparent)
		{
			// A model's polygon of blended texels alone, against the depth as its others' are.
			AddScenePolygonToOrder(Draw, Item, EFieldPass::SemiTransparentTexels, Item.PageBlend, EFieldRunDepth::Masked, false, false, EFieldDepthRule::Model);
		}
		else
		{
			const EFieldRunDepth Depth = Item.bShadow ? EFieldRunDepth::OutsideMask
				: Item.bBackground || Item.bInOrder	  ? EFieldRunDepth::None
				: Item.bGround						  ? EFieldRunDepth::GroundOverModels
				: Item.bOverModels					  ? EFieldRunDepth::OverModels
													  : EFieldRunDepth::Masked;
			AddLayeredScenePolygon(Draw, Item, Current, Depth, true, RuleOf(Item));
		}
	}
}

void FindScenePolygonTexelKinds(const FFieldSceneDraw& Draw, TArrayView<FScenePolygonItem> Items, Weltall::FTexelKindCache& TexelKinds)
{
	for (FScenePolygonItem& Item : Items)
	{
		if (!Item.bSemiTransparent || !Item.bTextured || Item.CornerCount == 0)
		{
			continue;
		}
		const FFieldScenePolygonState& State = Draw.DynamicPolygons[static_cast<int32>(Draw.DynamicCorners[static_cast<int32>(Item.FirstCorner)].Polygon)];
		if (State.Texture == Weltall::NoTextureView)
		{
			continue;
		}
		const EFieldTextureFilter Filter = (State.Flags & FieldPolygonFlags::Sprite) != 0 ? Draw.SpriteFilter : Draw.Filter;
		const Weltall::FTexelRect Rect{static_cast<uint8>(State.TexelMin), static_cast<uint8>(State.TexelMin >> 8), static_cast<uint8>(State.TexelMax),
			static_cast<uint8>(State.TexelMax >> 8)};
		// A window's texels wrap: only its CLUT tells.
		Item.Kinds = TexelKinds.Kinds(State.Texture, Rect, Filter == EFieldTextureFilter::Upscaled ? Weltall::TextureUpscale::Reach : 0, (State.Flags & FieldPolygonFlags::Window) != 0);
	}
}
