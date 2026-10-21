#pragma once

#include "CoreMinimal.h"
#include "Field/FieldFrame.h"
#include "Graphics/TexelKinds.h"
#include "Rendering/FieldSceneRendering.h"

#include <array>
#include <span>

/** The passes of FieldScene.usf. */
enum class EFieldPass : uint8
{
	Opaque = 0,
	PlainTexels = 1,
	SemiTransparentTexels = 2,
	BlendAll = 3,
};

/** Flags of FFieldScenePolygonState; the values match FieldScene.usf. */
namespace FieldPolygonFlags
{
	constexpr uint32 Raw = 1;
	/** Pixel art of the characters and effects, filtered as the sprites are. */
	constexpr uint32 Sprite = 4;
	/**
	 * Its texels wrap within a texture window, as a draw mode sets one: its polygon state's TexelMin holds the window's
	 * offset and TexelMax the bits of a texel coordinate the window replaces (as the texel packs: u, then v).
	 */
	constexpr uint32 Window = 8;
	/** A gear's floor shadow, broader than a character's: the ground under it may rise further before what stands there hides it. */
	constexpr uint32 BroadShadow = 16;
	/** Drawn only where its corners wind clockwise on the screen as they are drawn: a triangle facing away is given no area. */
	constexpr uint32 OneSided = 32;
	/**
	 * From this bit, a sprite's or particle's ordering-table entry plus one: against the depth buffer
	 * it stands at the far edge of that entry, all its quads alike, so it sorts against the others of
	 * its kind as the field sorts them and against the scenery by the scenery's own depth.
	 */
	constexpr uint32 EntryShift = 8;

	/** The flags that stand a polygon at its ordering-table entry's depth. */
	inline uint32 AtEntry(int32 OtIndex)
	{
		return static_cast<uint32>(FMath::Clamp(OtIndex, 0, 0xFFFFFF - 1) + 1) << EntryShift;
	}
}

/** Flags in the top byte of a polygon's first colour, for what changes from frame to frame; the values match FieldScene.usf. */
namespace FieldColorFlags
{
	/** Its texels are modulated by its colour even when its packet draws them raw (depth-cued in fog). */
	constexpr uint32 Modulated = 1u << 24;
}

/** The corners a quad is drawn with in the dynamic corners: two triangles, 0-1-2 and 1-3-2. */
constexpr uint32 QuadCorners = 6;
constexpr int32 QuadTriangleCorners[QuadCorners] = {0, 1, 2, 1, 3, 2};

/** Moved further than this between ticks (whole world units, along any axis), a thing jumped: it is not drawn in between. */
constexpr int32 FieldJumpDistance = 256;

/** Whether two places in the world lie within Limit of each other along every axis. */
bool IsNearInWorld(const std::array<int32, 3>& A, const std::array<int32, 3>& B, int32 Limit);

/** A model's matrix as three rows, Alpha of the way from one tick's to the next: each column's direction and length, and the translation. */
void AddBlendedFieldObject(TArray<FVector4f>& Objects, const Weltall::FGteMatrix& From, const Weltall::FGteMatrix& To, float Alpha);
/** The same for matrices kept as real numbers. */
void AddBlendedFieldObject(TArray<FVector4f>& Objects, const Weltall::FAffine& From, const Weltall::FAffine& To, float Alpha);

/**
 * Lays a field frame's corners out in drawing order, in runs of one state: the scenery's polygons,
 * the characters' quads, the particles, the panorama and the gears' polygons, in the original's
 * painter's order or against a depth buffer. Given the VRAM the frame is drawn from (TexelKinds), a
 * semi-transparent polygon's pass that has no texels to draw is left out, and a polygon with only one
 * kind of texel joins its neighbours as an opaque one does.
 */
class FFieldDrawBuilder
{
public:
	FFieldDrawBuilder(const Weltall::FFieldScene& InScene, const Weltall::FFieldSceneFrame& InFrame, std::span<const Weltall::FFieldCharacterView> InCharacters,
		std::span<const Weltall::FFieldCharacterQuad> InQuads, std::span<const Weltall::FFieldParticleQuad> InParticles, const FFieldSceneMesh& InMesh,
		FFieldSceneDraw& InDraw, const Weltall::FFieldGearsFrame* InGears = nullptr, const Weltall::FFieldSpriteModelsFrame* InSpriteModels = nullptr,
		Weltall::FTexelKindCache* InTexelKinds = nullptr)
		: Scene(InScene), Frame(InFrame), Characters(InCharacters), Quads(InQuads), Particles(InParticles), Mesh(InMesh), Draw(InDraw), Gears(InGears),
		  SpriteModels(InSpriteModels), TexelKinds(InTexelKinds)
	{
	}

	/** Everything in the order given, as the GPU draws it. */
	void PaintersOrder(std::span<const Weltall::FFieldDrawItem> Order);
	/** The scenery alone, in the frame's own order. */
	void PaintersOrder();

	/**
	 * The painter's order, but where the characters' sprites and the 3D models (the gears, the children's models)
	 * are: their opaque texels mark the pixels they cover, and there alone the depth decides where it differs clearly
	 * from theirs (a sprite's: the entry the field sorts it in; a model's: its own): what the order draws before one
	 * hides it only when nearer than that by the margin, what it draws after covers it unless farther by as much.
	 * Elsewhere the scenery is exactly as the original draws it, unless bSceneryByDepth: then the scenery's polygons
	 * are sorted by depth too, the order deciding only within the decals' margin of the nearest surface.
	 */
	void DepthBuffer(std::span<const Weltall::FFieldDrawItem> Order, bool bSceneryByDepth = false);
	void DepthBuffer(bool bSceneryByDepth = false);

private:
	struct FItem
	{
		uint32 FirstCorner = 0;
		uint32 CornerCount = 0;
		bool bSemiTransparent = false;
		bool bTextured = false;
		bool bBackground = false;
		/** A quad of a character's sprite, or of its shadow lying on its floor (a gear's floor shadow too). */
		bool bSprite = false;
		bool bShadow = false;
		/** A polygon of a 3D model standing at its own depth: a gear's (not its floor shadow) or cloth triangle, a child's model's. */
		bool bModel = false;
		/** A polygon of the scenery's placed models (not the background's). */
		bool bScenery = false;
		Weltall::EBlendMode PageBlend = Weltall::EBlendMode::Average;
		/** The depth rule its runs follow (DepthBuffer's). */
		EFieldDepthRule Rule = EFieldDepthRule::Entry;
		/** A semi-transparent textured polygon's kinds of texel (Weltall::TexelKinds), Any where they are not known. */
		uint8 Kinds = Weltall::TexelKinds::Any;
	};

	/** Whether it has texels drawn opaque, which a prepass can take the depth of: any but a wholly blended polygon's. */
	static bool HasOpaqueTexels(const FItem& Item) { return (Item.bTextured && (Item.Kinds & Weltall::TexelKinds::Plain) != 0) || !Item.bSemiTransparent; }
	/** Whether it has texels that blend: a semi-transparent polygon's, but where its texture has none. */
	static bool HasBlendedTexels(const FItem& Item) { return Item.bSemiTransparent && (!Item.bTextured || (Item.Kinds & Weltall::TexelKinds::SemiTransparent) != 0); }
	/** The pass that draws its opaque texels: all of a polygon's that has no blended ones. */
	static EFieldPass PlainPass(const FItem& Item) { return HasBlendedTexels(Item) ? EFieldPass::PlainTexels : EFieldPass::Opaque; }
	/** Whether it marks the pixels of its opaque texels, sorted there by depth: a sprite, or a model with any. */
	static bool IsMarking(const FItem& Item) { return Item.bSprite || (Item.bModel && HasOpaqueTexels(Item)); }

	FItem Describe(const Weltall::FFieldDrawItem& Item) const;
	/** The item before its kinds of texel are known. */
	FItem DescribeShape(const Weltall::FFieldDrawItem& Item) const;
	/** The kinds of texel a semi-transparent textured item's polygon reads, from the VRAM: Any without one. */
	uint8 TexelKindsOf(const FItem& Item) const;
	/** A gear's polygon or cloth triangle; none of its corners when the gears are not drawn. */
	FItem DescribeGear(const Weltall::FFieldDrawItem& Item) const;
	/** A polygon of a child's model. */
	FItem DescribeSpriteModel(const Weltall::FFieldDrawItem& Item) const;
	template <typename TItems>
	void Painters(const TItems& Items);
	template <typename TItems>
	void Depth(const TItems& Items, bool bSceneryByDepth);
	/** The item as DepthBuffer draws it: described, with the depth rule its runs follow. */
	FItem DescribeForDepth(const Weltall::FFieldDrawItem& Item, bool bSceneryByDepth) const;
	uint32 Add(const FItem& Item, EFieldPass Pass, Weltall::EBlendMode Blend, EFieldRunDepth Depth, bool bDepthWrite, bool bJoinable);
	void AddLayered(const FItem& Item, Weltall::EBlendMode UntexturedBlend, EFieldRunDepth Depth, bool bDepthWrite);

	const Weltall::FFieldScene& Scene;
	const Weltall::FFieldSceneFrame& Frame;
	std::span<const Weltall::FFieldCharacterView> Characters;
	std::span<const Weltall::FFieldCharacterQuad> Quads;
	std::span<const Weltall::FFieldParticleQuad> Particles;
	const FFieldSceneMesh& Mesh;
	FFieldSceneDraw& Draw;
	const Weltall::FFieldGearsFrame* Gears;
	const Weltall::FFieldSpriteModelsFrame* SpriteModels;
	Weltall::FTexelKindCache* TexelKinds;
};

/** How far a character's quads are drawn from where the latest tick put them: back toward the tick before's place by 1 - Alpha. */
FVector3f FieldCharacterOffset(const Weltall::FFieldCharacterView& Character, const Weltall::FFieldFrame* Previous, float Alpha);

/** The characters' quads as dynamic corners, their states and colours, each moved back toward the tick before's place by 1 - Alpha. */
void AddFieldCharacterQuads(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw);

/** The particles' quads as dynamic corners, their states and colours, each drawn between its two ticks' corners and colours. */
void AddFieldParticleQuads(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw);

/**
 * The polygons the children's models draw, as dynamic corners in view space: each drawn between its two
 * ticks' corners when the same child's polygon was drawn in the tick before and its sprite did not jump.
 */
void AddFieldSpriteModels(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw);

/**
 * The polygons of models whose vertices a MIMe animation moves (drawn between the two ticks'
 * vertices) or whose texels the scripts scroll, as dynamic corners in their models' spaces, the
 * mesh's corners redirected to them. A scrolled model keeps the scene's joins that hold.
 */
void AddFieldChangedModels(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha,
	const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw);

/**
 * The scenery's joined polygons whose joins do not all hold in the frame drawn (a model moved from where the map
 * built it: a door swung open), as dynamic corners: each corner its own model's again unless its own join holds.
 */
void AddFieldReleasedJoins(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw);

/**
 * The panorama's packets as dynamic corners far behind everything the field draws, where they cover
 * the screen as the packets say: across the original's screen and on past it for wider views, turned
 * and moved Alpha of the way from the tick before's place.
 */
void AddFieldBackdrop(const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha, const FFieldSceneMesh& Mesh,
	FFieldSceneDraw& Draw);

/**
 * What a frame of a scene staged before a picture stretches and moves (FFieldPictureStretch), once every object's rows and
 * dynamic corner is in Draw: the scene's pictures, every other object by its own place (a gear's by its floor shadow's),
 * and the corners drawn in view space by their character's origin, their particle's middle or their child's model's
 * origin, a cloth's by its gear's place. Nothing for a scene without a picture.
 */
void AddFieldPictureStretch(const Weltall::FFieldScene& Scene, const Weltall::FFieldFrame& Latest, const Weltall::FFieldFrame* Previous, float Alpha,
	const FFieldSceneMesh& Mesh, FFieldSceneDraw& Draw);
