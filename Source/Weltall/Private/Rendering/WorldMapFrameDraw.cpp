#include "Rendering/WorldMapRendering.h"

#include "FieldDrawBuilder.h"
#include "Rendering/VramTexture.h"
#include "ScenePolygonOrder.h"
#include "WorldMap/WorldMapScreenWaver.h"

namespace
{
	/** The world map's screen: 216 lines, drawn by the field's passes, which take 224 about the middle. */
	constexpr float WorldMapScreenLines = 216.0f;
	constexpr float FieldScreenLines = 224.0f;
	/** The view depth of one of the sprites' ordering-table entries, as the scene shader stands a sprite at its entry. */
	constexpr float WorldMapEntryDepth = 16.0f;

	/**
	 * What the scenery sorted by depth takes in: the ground, the place models, the trees, and the clouds, which have no
	 * opaque texels: a cloud is hidden where the scenery stands before it, and covered as the order has it by what is drawn
	 * after it (a peak cuts what of it floats in front of the slope, a cloud behind it seen).
	 */
	bool IsScenery(Weltall::EWorldMapLayer Layer)
	{
		return Layer == Weltall::EWorldMapLayer::Terrain || Layer == Weltall::EWorldMapLayer::Model || Layer == Weltall::EWorldMapLayer::Billboard
			|| Layer == Weltall::EWorldMapLayer::Cloud;
	}

	uint32 PackTexel(int32 U, int32 V)
	{
		return static_cast<uint32>(FMath::Clamp(U, 0, 255)) | static_cast<uint32>(FMath::Clamp(V, 0, 255)) << 8;
	}

	uint32 PackColor(const Weltall::FDrawColor& Color)
	{
		return static_cast<uint32>(Color.R | Color.G << 8 | Color.B << 16);
	}

	using FCorners = std::array<std::array<float, 3>, 4>;
	using FTexCoords = std::array<std::array<float, 2>, 4>;

	/** A piece of a polygon as drawn: its corners, their texels, and the texels it may sample. */
	struct FPolygonPiece
	{
		FCorners View{};
		FTexCoords TexCoords{};
		std::array<uint8, 2> TexelMin{};
		std::array<uint8, 2> TexelMax{};
	};

	bool IsWindowed(const Weltall::FWorldMapPolygon& Polygon)
	{
		return Polygon.WindowWidth != 0 && Polygon.CornerCount == 4;
	}

	/**
	 * A quad whose texels repeat in a window across u (the horizon's band, scrolled by Scroll): split where u wraps, each
	 * piece sampling within the window. How many pieces (none when it spans no texels).
	 */
	int32 WindowPieces(const Weltall::FWorldMapPolygon& Polygon, float Scroll, std::array<FPolygonPiece, 2>& Pieces)
	{
		const float Width = Polygon.WindowWidth;
		const float Start = Polygon.TexCoords[0][0] + Scroll;
		float Wrapped = FMath::Fmod(Start, Width);
		if (Wrapped < 0.0f)
		{
			Wrapped += Width;
		}
		const float Across = Polygon.TexCoords[1][0] - Polygon.TexCoords[0][0];
		if (Across <= 0.0f)
		{
			return 0;
		}
		const float Split = FMath::Clamp((Width - Wrapped) / Across, 0.0f, 1.0f);
		const auto Lerp = [](const std::array<float, 3>& A, const std::array<float, 3>& B, float T) {
			return std::array<float, 3>{A[0] + (B[0] - A[0]) * T, A[1] + (B[1] - A[1]) * T, A[2] + (B[2] - A[2]) * T};
		};
		const std::array<float, 3> Top = Lerp(Polygon.View[0], Polygon.View[1], Split);
		const std::array<float, 3> Bottom = Lerp(Polygon.View[2], Polygon.View[3], Split);
		const float V0 = Polygon.TexCoords[0][1];
		const float V1 = Polygon.TexCoords[2][1];
		Pieces[0] = {FCorners{Polygon.View[0], Top, Polygon.View[2], Bottom}, FTexCoords{{{Wrapped, V0}, {Width, V0}, {Wrapped, V1}, {Width, V1}}},
			{static_cast<uint8>(Wrapped), Polygon.TexelMin[1]}, {static_cast<uint8>(Polygon.WindowWidth - 1), Polygon.TexelMax[1]}};
		if (Split >= 1.0f)
		{
			return 1;
		}
		const float Rest = Wrapped + Across - Width;
		Pieces[1] = {FCorners{Top, Polygon.View[1], Bottom, Polygon.View[3]}, FTexCoords{{{0.0f, V0}, {Rest, V0}, {0.0f, V1}, {Rest, V1}}},
			{0, Polygon.TexelMin[1]}, {static_cast<uint8>(FMath::Max(Rest - 1.0f, 0.0f)), Polygon.TexelMax[1]}};
		return 2;
	}

	/** A drawn corner's place and texel: the piece's corner Corner. */
	void PlaceCorner(FFieldSceneCorner& Out, const FPolygonPiece& Piece, int32 Corner)
	{
		const std::array<float, 3>& Point = Piece.View[static_cast<std::size_t>(Corner)];
		const std::array<float, 2>& Texel = Piece.TexCoords[static_cast<std::size_t>(Corner)];
		Out.Position = FVector3f(Point[0], Point[1], Point[2]);
		Out.TexCoord = FVector2f(Texel[0], Texel[1]);
	}

	void SetTexels(FFieldScenePolygonState& State, const FPolygonPiece& Piece)
	{
		State.TexelMin = PackTexel(Piece.TexelMin[0], Piece.TexelMin[1]);
		State.TexelMax = PackTexel(Piece.TexelMax[0], Piece.TexelMax[1]);
	}

	/** The corners a piece of CornerCount corners is drawn as: its triangle, or a quad's two. */
	std::span<const int32> DrawnCorners(int32 CornerCount)
	{
		static constexpr int32 TriangleCorners[3] = {0, 1, 2};
		return CornerCount == 3 ? std::span<const int32>(TriangleCorners) : std::span<const int32>(QuadTriangleCorners);
	}

	/** The polygons' corners and states for the passes, a polygon at a time. */
	class FWorldMapDrawBuilder
	{
	public:
		FWorldMapDrawBuilder(FFieldSceneDraw& InDraw, uint32 InUnanchored, float InHorizonScroll, int32 InEntriesBack, bool bInSceneryByDepth,
			bool bInGroundInOrder, FWorldMapDrawLayout* InLayout)
			: Draw(InDraw), Unanchored(InUnanchored), HorizonScroll(InHorizonScroll), EntriesBack(InEntriesBack), bSceneryByDepth(bInSceneryByDepth),
			  bGroundInOrder(bInGroundInOrder), Layout(InLayout)
		{
		}

		void AddPolygon(const Weltall::FWorldMapPolygon& Polygon, uint32 Number)
		{
			if (!IsWindowed(Polygon))
			{
				AddCorners(Polygon, {Polygon.View, Polygon.TexCoords, Polygon.TexelMin, Polygon.TexelMax});
				return;
			}
			const bool bHorizon = Polygon.Layer == Weltall::EWorldMapLayer::Horizon;
			if (bHorizon && Layout != nullptr)
			{
				Layout->Bands.Add({Number, Draw.DynamicCorners.Num(), Draw.DynamicPolygons.Num(), 0});
			}
			std::array<FPolygonPiece, 2> Pieces;
			const int32 Count = WindowPieces(Polygon, bHorizon ? HorizonScroll : 0.0f, Pieces);
			for (int32 Piece = 0; Piece < Count; ++Piece)
			{
				AddCorners(Polygon, Pieces[static_cast<std::size_t>(Piece)]);
			}
			if (bHorizon && Layout != nullptr)
			{
				Layout->Bands.Last().Pieces = Count;
			}
		}

		TArray<FScenePolygonItem>& Items() { return AddedItems; }

	private:
		void AddCorners(const Weltall::FWorldMapPolygon& Polygon, const FPolygonPiece& Piece)
		{
			const uint32 Index = static_cast<uint32>(Draw.DynamicPolygons.Num());
			FScenePolygonItem& Item = AddedItems.AddDefaulted_GetRef();
			Item.FirstCorner = static_cast<uint32>(Draw.DynamicCorners.Num());
			Item.bSemiTransparent = Polygon.bSemiTransparent;
			Item.bTextured = Polygon.bTextured;
			Item.bBackground = Polygon.Layer == Weltall::EWorldMapLayer::Sky || Polygon.Layer == Weltall::EWorldMapLayer::Horizon;
			Item.bSprite = Polygon.Layer == Weltall::EWorldMapLayer::Sprite;
			Item.bShadow = Polygon.Layer == Weltall::EWorldMapLayer::Shadow;
			Item.bInOrder = Polygon.Layer == Weltall::EWorldMapLayer::Effect || (bGroundInOrder && Polygon.Layer == Weltall::EWorldMapLayer::Terrain);
			// The party's ship keeps the game's order against the scenery: drawn whole over the sand and the sea it rides in.
			Item.bScenery = bSceneryByDepth && !Item.bInOrder && !Polygon.bVehicle && IsScenery(Polygon.Layer);
			Item.bModel = Polygon.Layer == Weltall::EWorldMapLayer::Gear;
			const std::span<const int32> Corners = DrawnCorners(Polygon.CornerCount);
			for (const int32 Corner : Corners)
			{
				FFieldSceneCorner& Out = Draw.DynamicCorners.AddDefaulted_GetRef();
				PlaceCorner(Out, Piece, Corner);
				Out.Polygon = Index;
				Out.Object = Polygon.Anchor == Weltall::FWorldMapPolygon::NoAnchor ? Unanchored : Polygon.Anchor;
				Out.Corner = Polygon.bGouraud ? static_cast<uint32>(Corner) : 0;
			}
			Item.CornerCount = static_cast<uint32>(Corners.size());
			FFieldScenePolygonState& State = Draw.DynamicPolygons.AddDefaulted_GetRef();
			State.Texture = FVramTexture::NoTexture;
			State.Flags = Polygon.bOneSided ? FieldPolygonFlags::OneSided : 0;
			// A shadow lies on uneven ground: where the drawing knows the floor (the arena's), what was drawn before it hides it
			// only standing as high above the floor as a gear's broad shadow allows.
			if (Polygon.Layer == Weltall::EWorldMapLayer::Shadow)
			{
				State.Flags |= FieldPolygonFlags::BroadShadow;
			}
			if (Polygon.bTextured)
			{
				// A page and CLUT in VRAM's port rows are read with their high y bits.
				const Weltall::FTexturePage Page = Polygon.bPortRows ? Weltall::FTexturePage::FromPortAttribute(Polygon.TexturePage)
																	 : Weltall::FTexturePage::FromAttribute(Polygon.TexturePage);
				State.Texture = FVramTexture::Pack(Page, Polygon.bPortRows ? Weltall::FClutPosition::FromPortAttribute(Polygon.Clut)
																		   : Weltall::FClutPosition::FromAttribute(Polygon.Clut));
				State.Flags |= Polygon.bRaw ? FieldPolygonFlags::Raw : 0;
				// The sprites' and the trees' pixel art takes the sprites' filter; a sprite stands whole at its ordering-table entry.
				if (Polygon.Layer == Weltall::EWorldMapLayer::Sprite || Polygon.Layer == Weltall::EWorldMapLayer::Billboard)
				{
					State.Flags |= FieldPolygonFlags::Sprite;
				}
				if (Polygon.Layer == Weltall::EWorldMapLayer::Sprite)
				{
					State.Flags |= FieldPolygonFlags::AtEntry(Polygon.Bucket + EntriesBack);
				}
				SetTexels(State, Piece);
				Item.PageBlend = Page.Blend;
			}
			Draw.Colors.Emplace(PackColor(Polygon.Colors[0]), PackColor(Polygon.Colors[1]), PackColor(Polygon.Colors[2]), PackColor(Polygon.Colors[3]));
		}

		FFieldSceneDraw& Draw;
		/** The object of the polygons that move with no anchor. */
		uint32 Unanchored = 0;
		float HorizonScroll = 0.0f;
		/** How many entries farther a sprite stands than the one it was sorted into (the frame's SortBack). */
		int32 EntriesBack = 0;
		bool bSceneryByDepth = false;
		bool bGroundInOrder = false;
		FWorldMapDrawLayout* Layout = nullptr;
		TArray<FScenePolygonItem> AddedItems;
	};

	/**
	 * The screen waver's rows as the wave's triangles: each row of the frame's middle drawn again moved across by its
	 * shift, on the passes' 224-line screen, which the world map's 216 lines fill about its middle.
	 */
	void AddWaverRows(const std::vector<int16>& Shifts, TArray<FFieldWaveCorner>& Grid)
	{
		constexpr int32 Triangles[6] = {0, 1, 2, 1, 3, 2};
		constexpr float MiddleX = 160.0f;
		constexpr float Scale = FieldScreenLines / WorldMapScreenLines;
		const auto OnFieldScreen = [](float X, float Y)
		{
			return FVector2f(MiddleX + (X - MiddleX) * Scale, FieldScreenLines / 2.0f + (Y - WorldMapScreenLines / 2.0f) * Scale);
		};
		const float Left = static_cast<float>(Weltall::FWorldMapScreenWaver::Left);
		const float Width = static_cast<float>(Weltall::FWorldMapScreenWaver::Width);
		for (std::size_t Row = 0; Row < Shifts.size(); ++Row)
		{
			const float Shift = static_cast<float>(Shifts[Row]);
			for (const int32 Corner : Triangles)
			{
				const float X = Left + ((Corner & 1) != 0 ? Width : 0.0f);
				const float Y = static_cast<float>(Row) + ((Corner & 2) != 0 ? 1.0f : 0.0f);
				Grid.Add({OnFieldScreen(X + Shift, Y), OnFieldScreen(X, Y)});
			}
		}
	}

	constexpr Weltall::FWorldMapViewMove Unmoved{{{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}}};

	/** An object's rows: Move, then each point moved down in proportion to its depth (Lower per unit of depth) and across scaled by ColumnScale. */
	void AddObject(TArray<FVector4f>& Objects, const Weltall::FWorldMapViewMove& Move, float Lower, float ColumnScale)
	{
		const auto Row = [&Move](std::size_t Index) { return FVector4f(Move[Index][0], Move[Index][1], Move[Index][2], Move[Index][3]); };
		Objects.Add(Row(0) * ColumnScale);
		Objects.Add(Row(1) + Row(2) * Lower);
		Objects.Add(Row(2));
	}

	bool IsBlended(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend)
	{
		return Blend != nullptr && Blend->Moves.size() == Frame.Anchors.size();
	}

	/** What a blend changes of the drawing besides the horizon: each anchor's rows and the projection. */
	void PlaceWorldMapObjects(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend, FFieldSceneDraw& Draw, float ScreenLines,
		float ColumnScale)
	{
		const bool bBlended = IsBlended(Frame, Blend);
		const float Distance = FMath::Max(bBlended ? Blend->ProjectionDistance : static_cast<float>(Frame.ProjectionDistance), 1.0f);
		const float CenterY = bBlended ? Blend->ScreenCenterY : static_cast<float>(Frame.ScreenCenterY);
		// The passes project about the middle of 224 lines: the frame's own lines fill them at a longer distance, and its centre
		// (the world map's is below the middle) is reached by moving each point down in proportion to its depth.
		Draw.ProjectionDistance = Distance * FieldScreenLines / ScreenLines;
		const float Lower = (CenterY - ScreenLines / 2.0f) / Distance;
		Draw.Objects.Reset();
		for (std::size_t Anchor = 0; Anchor < Frame.Anchors.size(); ++Anchor)
		{
			AddObject(Draw.Objects, bBlended ? Blend->Moves[Anchor] : Unmoved, Lower, ColumnScale);
		}
		AddObject(Draw.Objects, Unmoved, Lower, ColumnScale);
	}

	float HorizonScroll(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend)
	{
		return IsBlended(Frame, Blend) ? Blend->HorizonScroll : 0.0f;
	}
}

void BuildWorldMapFrameDraw(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend, const FWorldMapRenderOptions& Options,
	FFieldSceneDraw& Draw, float ScreenLines, float ColumnScale, FWorldMapDrawLayout* Layout, Weltall::FTexelKindCache* TexelKinds)
{
	Draw.Objects.Reset();
	Draw.Colors.Reset();
	Draw.DynamicCorners.Reset();
	Draw.DynamicPolygons.Reset();
	Draw.SpriteCells.Reset();
	Draw.Order.Reset();
	Draw.Runs.Reset();
	Draw.WaveGrid.Reset();
	Draw.bWave = false;
	Draw.WaveRuns = 0;
	Draw.ParticleCorners = 0;
	Draw.BackdropCorners = 0;
	Draw.BackdropCornerCount = 0;
	Draw.BackdropQuads.clear();
	Draw.Redirects.Reset();
	Draw.bGearsDrawn = false;
	Draw.bClipped = false;
	Draw.FloorUp = FVector3f::ZeroVector;
	Draw.Background = FLinearColor::Black;
	Draw.Filter = Options.Filter;
	Draw.SpriteFilter = Options.SpriteFilter;
	Draw.AntiAliasing = Options.AntiAliasing;
	Draw.bDepthBuffer = Options.Sorting != EFieldSorting::PaintersOrder;
	PlaceWorldMapObjects(Frame, Blend, Draw, ScreenLines, ColumnScale);
	if (Layout != nullptr)
	{
		Layout->Bands.Reset();
	}
	const bool bSceneryByDepth = Options.Sorting == EFieldSorting::DepthBufferWithScenery;
	FWorldMapDrawBuilder Builder(Draw, static_cast<uint32>(Frame.Anchors.size()), HorizonScroll(Frame, Blend),
		FMath::RoundToInt(static_cast<float>(Frame.SortBack) / WorldMapEntryDepth), bSceneryByDepth, Frame.bGearsUnderGround, Layout);
	for (std::size_t Number = 0; Number < Frame.Polygons.size(); ++Number)
	{
		Builder.AddPolygon(Frame.Polygons[Number], static_cast<uint32>(Number));
	}
	if (TexelKinds != nullptr)
	{
		FindScenePolygonTexelKinds(Draw, Builder.Items(), *TexelKinds);
	}
	if (Draw.bDepthBuffer)
	{
		AddScenePolygonsUnderDepthRule(Draw, Builder.Items());
	}
	else
	{
		AddScenePolygonsInPaintersOrder(Draw, Builder.Items());
	}
	if (!Frame.WaverShifts.empty())
	{
		// A cutscene's waver: the whole scene drawn, then its middle again through the rows.
		Draw.bWave = true;
		Draw.WaveRuns = Draw.Runs.Num();
		AddWaverRows(Frame.WaverShifts, Draw.WaveGrid);
	}
}

bool MoveWorldMapFrameDraw(const Weltall::FWorldMapFrame& Frame, const Weltall::FWorldMapFrameBlend* Blend, const FWorldMapDrawLayout& Layout,
	FFieldSceneDraw& Draw, float ScreenLines, float ColumnScale)
{
	PlaceWorldMapObjects(Frame, Blend, Draw, ScreenLines, ColumnScale);
	const float Scroll = HorizonScroll(Frame, Blend);
	for (const FWorldMapDrawLayout::FBand& Band : Layout.Bands)
	{
		std::array<FPolygonPiece, 2> Pieces;
		if (WindowPieces(Frame.Polygons[Band.Polygon], Scroll, Pieces) != Band.Pieces)
		{
			return false;
		}
		const std::span<const int32> Corners = DrawnCorners(Frame.Polygons[Band.Polygon].CornerCount);
		for (int32 Piece = 0; Piece < Band.Pieces; ++Piece)
		{
			const FPolygonPiece& Drawn = Pieces[static_cast<std::size_t>(Piece)];
			const int32 First = Band.FirstCorner + Piece * static_cast<int32>(Corners.size());
			for (std::size_t Corner = 0; Corner < Corners.size(); ++Corner)
			{
				PlaceCorner(Draw.DynamicCorners[First + static_cast<int32>(Corner)], Drawn, Corners[Corner]);
			}
			if (Frame.Polygons[Band.Polygon].bTextured)
			{
				SetTexels(Draw.DynamicPolygons[Band.FirstState + Piece], Drawn);
			}
		}
	}
	return true;
}
