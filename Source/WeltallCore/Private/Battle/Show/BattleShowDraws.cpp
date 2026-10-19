#include "Battle/Show/BattleShow.h"

#include "BattleShowDrawing.h"
#include "Graphics/GteVectors.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		using namespace BattleShowDrawing;

		constexpr int32 FadeEntry = 2;
		constexpr int32 NumberEntry = 0;
		/** Kinds of spawned sprites and how the kernel draws them. */
		constexpr int32 DotKind = 8;
		constexpr int32 TileKind = 9;
		constexpr int32 LitModelKind = 15;
		constexpr uint32 ScreenSpace = 0x1000000;
		constexpr uint32 FarEntry = 0x2000000;
		constexpr uint32 ParentsEntry = 0x20000000;
		constexpr uint32 ShadowFlag = 0x4;
		/** Where the running total stands on the screen, and how its bursts' ghosts shrink and turn one from the next. */
		constexpr int32 TotalX = 0xA0;
		constexpr int32 TotalY = 0x46;
		constexpr int32 BurstGhosts = 6;
		constexpr int32 GhostShrink = 0x330;
		constexpr int16 GhostTurn = 5;
		/** The numbers' screen matrix: the identity 512 units ahead, where the projection draws a unit a pixel. */
		constexpr int32 ScreenMatrixDepth = 0x200;
		constexpr uint32 NumberKeys = 0x40000000;
		constexpr uint32 BurstKeys = 0x50000000;
		constexpr uint32 TotalKey = 0x60000000;
		constexpr uint32 TotalPanelKey = 0x68000000;
		/** The running total's panel in the HUD's sheet; a sheet's frame drawn by the kernel is raw, its page's blend bits clear. */
		constexpr int32 TotalPanelFrame = 0x81;
		constexpr uint8 RawQuadCode = 0x2D;
		constexpr uint16 PageBlendBits = 0x60;
		constexpr uint32 ShadowKeys = 0x20000000;
		constexpr uint32 ExtensionKeys = 0x30000000;
		constexpr uint32 MarkerKeys = 0x70000000;
		/** A marker's triangle is not drawn when a corner's x fell off the GTE's screen range (FLAG bit 14). */
		constexpr uint32 ScreenXSaturated = 0x4000;
		/** The nearest entry a marker's triangle is linked into. */
		constexpr int32 NearestMarkerEntry = 5;
		/** A streak reaches back along the sprite's velocity shifted down by this and its length word. */
		constexpr uint32 StreakShift = 8;
	}

	void FBattleShow::RunDraws()
	{
		for (const FKernelTaskEntry& Entry : Sprites.Tasks())
		{
			if (Entry.Sprite)
			{
				DrawSprite(*Entry.Sprite);
				continue;
			}
			const IKernelTask* Task = Entry.Task;
			if (Task == &Fade)
			{
				Shown.Fade = {Fade.Fade.Mode, Fade.Fade.Shown, Fade.Fade.bFromEffect};
				Links.push_back({EBattleShowLink::Fade, 0, FadeEntry});
			}
			else if (Task == &TintFade)
			{
				TintFade.Fade.Draw(Tints);
			}
			else if (Task == &Numbers.Total)
			{
				DrawTotal();
			}
			else if (const FBattleMarker* Marker = Markers.Find(Task))
			{
				DrawMarker(*Marker);
			}
			else if (const std::optional<FBattleSprites::FTrailDrawing> Trail = Battle.TrailDrawing(Task))
			{
				DrawTrail(*Trail);
			}
			else if (const FBattleShards* Shards = Extensions.Shards(Task))
			{
				ModelDrawing.DrawShards(*Shards);
			}
			else if (const FBattleWave* Wave = Extensions.Wave(Task))
			{
				DrawWave(*Wave);
			}
			else if (const FBattleScroll* Scroll = Extensions.Scroll(Task))
			{
				DrawScroll(*Scroll);
			}
			else
			{
				for (const FBattleNumber& Number : Numbers.Numbers)
				{
					if (Task == &Number)
					{
						DrawNumber(Number);
					}
				}
				for (const FBattleTotalBurst& Burst : Numbers.Bursts)
				{
					if (Task == &Burst)
					{
						DrawBurst(Burst);
					}
				}
			}
		}
	}

	int32 FBattleShow::SpriteEntry(const FSprite& Sprite, const FGteMatrix& View)
	{
		return BattleShowDrawing::SpriteEntry(Gte, Sprite, View);
	}

	void FBattleShow::DrawSprite(FSpriteTask& Task)
	{
		if (!Task.bDrawn)
		{
			return;
		}
		FSprite& Sprite = Task.Sprite;
		switch (Battle.EndRoutine(Task))
		{
		case EBattleSpriteRoutine::Combatant:
		{
			// A combatant's sprite.
			if (Battle.bCombatantsHidden)
			{
				return;
			}
			const int32 Entry = SpriteEntry(Sprite, Camera.View);
			Sprite.OtIndex = static_cast<int16>(Entry);
			if (InTable(Entry))
			{
				DrawSpriteParts(Task, Entry);
			}
			return;
		}
		case EBattleSpriteRoutine::Proxy:
			// A 3D combatant's proxy: the object's height kept for the scripts, the sprite itself not drawn.
			Sprite.FrameTop = static_cast<int16>(Objects.Height(FBattleSprites::SlotOf(Sprite)));
			Sprite.FrameHalfHeight = static_cast<int16>(static_cast<uint16>(Sprite.FrameTop) >> 1);
			Sprite.OtIndex = static_cast<int16>(SpriteEntry(Sprite, Camera.View));
			return;
		case EBattleSpriteRoutine::Controller:
			return;
		default:
			break;
		}
		switch (Task.DrawRole)
		{
		case BattleSpriteDraws::Streak:
		case BattleSpriteDraws::LineToParent:
		{
			// A streak, or a line to the parent: a line from the sprite, linked at its place's
			// entry, while its frame is 0. A streak in screen space goes through the identity: its place is the view's.
			if (Sprite.Frame != 0)
			{
				return;
			}
			const bool bStreak = Task.DrawRole == BattleSpriteDraws::Streak;
			const FGteMatrix Matrix = bStreak && (Sprite.DrawFlags & ScreenSpace) != 0 ? FGteMatrix::Identity() : Camera.View;
			Gte.Transform = Matrix;
			const FGteVector From = Place(Sprite);
			const FGteProjection Point = Gte.Rtps(From);
			const int32 Entry = Point.ScreenZ >> 2 >> EntryShift;
			Sprite.OtIndex = static_cast<int16>(Entry);
			if (bStreak)
			{
				// Back along its motion: each axis's velocity shifted down by 8 and the word a part sprite keeps its frame's top in.
				const uint32 Shift = (static_cast<uint16>(Sprite.FrameTop) + StreakShift) & 0x1F;
				AddLine(Task, Matrix,
					{static_cast<int16>(From[0] - static_cast<int16>(Sprite.Velocity[0] >> Shift)), static_cast<int16>(From[1] - static_cast<int16>(Sprite.Velocity[1] >> Shift)),
						static_cast<int16>(From[2] - static_cast<int16>(Sprite.Velocity[2] >> Shift))},
					Entry);
				return;
			}
			if (Sprite.Parent == nullptr)
			{
				ReportFault(EBattleShowFault::EndedTask);
				return;
			}
			AddLine(Task, Matrix, Place(*Sprite.Parent), Entry);
			return;
		}
		case BattleSpriteDraws::Kernel:
			DrawSpriteModel(Task, true);
			return;
		default:
			break;
		}
		switch (Sprite.Kind())
		{
		case 0:
		case 5:
		case 6:
		case 14:
		{
			// The sprite's parts.
			if ((Sprite.NextWord & SpriteNextFlags::KeepTaskMarks) != 0 && Battle.bCombatantsHidden)
			{
				return;
			}
			int32 Entry = SpriteEntry(Sprite, Camera.View);
			Sprite.OtIndex = static_cast<int16>(Entry);
			if ((Sprite.DrawFlags & ScreenSpace) == 0)
			{
				if ((Sprite.DrawFlags & ParentsEntry) != 0)
				{
					if (Sprite.Parent == nullptr)
					{
						ReportFault(EBattleShowFault::EndedTask);
						return;
					}
					Entry = Sprite.Parent->OtIndex;
				}
				if (InTable(Entry))
				{
					DrawSpriteParts(Task, Entry);
				}
				return;
			}
			// A sprite in screen space: its own matrix at its place, without the view.
			UpdateMatrix(Sprite);
			Entry = (Sprite.DrawFlags & FarEntry) != 0 ? static_cast<int32>(LastEntry) : Sprite.OtBias;
			if (InTable(Entry))
			{
				PartScratch.clear();
				Weltall::DrawSpriteParts(Sprite, SpriteModelMatrix(Sprite), Entry, &PartScratch);
				AddQuads(Task, EBattleShowDrawer::Sprite);
			}
			return;
		}
		case 2:
		case 7:
			// The kernel's model kind draws a part of its own models, which no battle script attaches; a battle model
			// under it is read as one.
			UpdateMatrix(Sprite);
			if (Sprite.ModelFile != nullptr)
			{
				ReportFault(EBattleShowFault::Model);
			}
			return;
		case LitModelKind:
			DrawSpriteModel(Task, false);
			return;
		case DotKind:
		case TileKind:
		{
			// A dot or a tile: a point or a small square at the sprite's place, linked at its depth's entry
			// whatever that is, when its frame is 0.
			if (Sprite.Frame != 0)
			{
				return;
			}
			Gte.Transform = Camera.View;
			const FGteProjection Point = Gte.Rtps(Place(Sprite));
			const int32 Entry = Point.ScreenZ >> 2 >> EntryShift;
			Sprite.OtIndex = static_cast<int16>(Entry);
			AddTile(Task, Sprite.Kind() == DotKind, Entry);
			return;
		}
		default:
			return;
		}
	}

	void FBattleShow::DrawSpriteParts(FSpriteTask& Task, int32 OtIndex)
	{
		// The kernel's sprite drawing: in battle the sprite's own matrix faces the screen; its place goes through the view.
		FSprite& Sprite = Task.Sprite;
		UpdateMatrix(Sprite);
		PartScratch.clear();
		Weltall::DrawSpriteParts(Sprite, SpriteDrawMatrix(Sprite, SpriteModelMatrix(Sprite), Camera.View), OtIndex, &PartScratch);
		AddQuads(Task, EBattleShowDrawer::Sprite);
		if ((Sprite.DrawFlags & ShadowFlag) != 0)
		{
			PartScratch.clear();
			DrawSpriteShadow(Sprite, Camera.View, OtIndex, PartScratch);
			AddQuads(Task, EBattleShowDrawer::Shadow);
		}
	}

	void FBattleShow::AddQuads(const FSpriteTask& Task, EBattleShowDrawer Kind)
	{
		if (PartScratch.empty())
		{
			return;
		}
		const uint16 Drawer = static_cast<uint16>(Shown.Drawers.size());
		// A sprite's shadow blends with its own last shadow, and an extension's quads over it with its last: the sprite's key, marked.
		const uint32 Mark = Kind == EBattleShowDrawer::Shadow ? ShadowKeys : Kind == EBattleShowDrawer::Extension ? ExtensionKeys : 0;
		Shown.Drawers.push_back({Task.Id | Mark, Kind, Place(Task.Sprite)});
		for (const FSpriteQuad& Quad : PartScratch)
		{
			Links.push_back({EBattleShowLink::Quad, static_cast<uint32>(Shown.Quads.size()), Quad.OtIndex});
			Shown.Quads.push_back({Quad, Drawer});
		}
	}

	FGteMatrix FBattleShow::GlyphMatrix(const FGteVector& Rotation, const std::array<int32, 3>& Translation, const std::array<int32, 3>& Scale) const
	{
		// The rotation, TransMatrix, then the screen matrix before it and the scale's columns after.
		FGteMatrix Local = RotationXyz(Rotation);
		Local.T = Translation;
		FGteMatrix Screen = FGteMatrix::Identity();
		Screen.T = {0, 0, ScreenMatrixDepth};
		return ScaleColumns(CompMatrix(Screen, Local), Scale);
	}

	void FBattleShow::AddGlyphs(uint32 Key, EBattleShowDrawer Kind, const FGteVector& World, const FGteMatrix& Matrix, const std::vector<FBattleSheetPart>& Parts,
		uint32 Color)
	{
		// Each glyph a quad in the matrix's plane, its texels edge to edge as the packet holds them.
		if (Parts.empty())
		{
			return;
		}
		const uint16 Drawer = static_cast<uint16>(Shown.Drawers.size());
		Shown.Drawers.push_back({Key, Kind, World});
		for (const FBattleSheetPart& Part : Parts)
		{
			FSpriteQuad Quad;
			Quad.Matrix = Matrix;
			const int16 Right = static_cast<int16>(Part.X + Part.Width);
			const int16 Bottom = static_cast<int16>(Part.Y + Part.Height);
			Quad.Corners = {FGteVector{Part.X, Part.Y, 0}, FGteVector{Right, Part.Y, 0}, FGteVector{Part.X, Bottom, 0}, FGteVector{Right, Bottom, 0}};
			Quad.U0 = Part.U;
			Quad.U1 = static_cast<uint8>(Part.U + Part.Width);
			Quad.V0 = Part.V;
			Quad.V1 = static_cast<uint8>(Part.V + Part.Height);
			Quad.TexturePage = Part.TexturePage;
			Quad.Clut = Part.Clut;
			Quad.ColorCode = Color;
			Quad.OtIndex = NumberEntry;
			Links.push_back({EBattleShowLink::Quad, static_cast<uint32>(Shown.Quads.size()), NumberEntry});
			Shown.Quads.push_back({Quad, Drawer});
		}
	}

	void FBattleShow::DrawNumber(const FBattleNumber& Number)
	{
		// At the combatant's place on the screen, twice as far out from the centre, H ahead.
		const FGteVector World{static_cast<int16>(Number.Position[0] >> 16), static_cast<int16>(Number.Position[1] >> 16), static_cast<int16>(Number.Position[2] >> 16)};
		Gte.Transform = Camera.View;
		const FGteProjection Point = Gte.Rtps(World);
		const std::array<int32, 3> Translation{(Point.ScreenX - (Gte.OffsetX >> 16)) * 2, (Point.ScreenY - (Gte.OffsetY >> 16)) * 2, Gte.ProjectionDistance};
		AddGlyphs(NumberKeys | Number.Serial, EBattleShowDrawer::Number, World, GlyphMatrix(Number.Rotation, Translation, Number.Scale), Number.Parts,
			ColorCode(Number.Color, Number.Code));
	}

	void FBattleShow::DrawBurst(const FBattleTotalBurst& Burst)
	{
		// Six ghosts at the total's place, each turned back and smaller than the one before.
		const std::array<int32, 3> Translation{(TotalX - (Gte.OffsetX >> 16)) * 2, (TotalY - (Gte.OffsetY >> 16)) * 2, Gte.ProjectionDistance};
		FGteVector Rotation = Burst.Rotation;
		std::array<int32, 3> Scale = Burst.Scale;
		for (int32 Ghost = 0; Ghost < BurstGhosts; ++Ghost)
		{
			AddGlyphs(BurstKeys | Burst.Serial << 3 | static_cast<uint32>(Ghost), EBattleShowDrawer::TotalBurst, {}, GlyphMatrix(Rotation, Translation, Scale), Burst.Parts,
				ColorCode(Burst.Color, Burst.Code));
			Rotation[2] = static_cast<int16>(Rotation[2] - GhostTurn);
			for (int32& Axis : Scale)
			{
				Axis -= GhostShrink;
			}
		}
	}

	void FBattleShow::DrawMarker(const FBattleMarker& Marker)
	{
		if (MarkerModel == nullptr)
		{
			ReportFault(EBattleShowFault::File);
			return;
		}
		FGteMatrix Matrix = RotationXyz(Marker.Rotation);
		Matrix.T = Marker.Position;
		Matrix = CompMatrix(Camera.View, Matrix);
		// Scaled by its depth to keep one size on the screen.
		int32 Size = int32{Gte.ProjectionDistance} << 12;
		if (Matrix.T[2] != 0)
		{
			Size /= Matrix.T[2];
		}
		if (Size == 0)
		{
			ReportFault(EBattleShowFault::MarkerTooFar);
			return;
		}
		const int32 Scale = 0x1000000 / Size / 2;
		Matrix = ScaleColumns(Matrix, {Scale, Scale, Scale});
		Gte.Transform = Matrix;
		const uint16 Drawer = static_cast<uint16>(Shown.Drawers.size());
		Shown.Drawers.push_back({MarkerKeys | Marker.Serial, EBattleShowDrawer::Marker,
			{static_cast<int16>(Marker.Position[0]), static_cast<int16>(Marker.Position[1]), static_cast<int16>(Marker.Position[2])}});
		for (const FBattleMarkerModel::FTriangle& Triangle : MarkerModel->Triangles)
		{
			// An unlit triangle of the model: the corners through RTPT, the back faces dropped, linked by their average depth.
			std::array<FGteProjection, 3> Corners;
			uint32 Flag = 0;
			for (std::size_t Corner = 0; Corner < 3; ++Corner)
			{
				Corners[Corner] = Gte.Rtps(MarkerModel->Vertices[Triangle.Vertices[Corner]]);
				Flag |= Corners[Corner].Flag;
			}
			if ((Flag & ScreenXSaturated) != 0
				|| Nclip(Corners[0].ScreenX, Corners[0].ScreenY, Corners[1].ScreenX, Corners[1].ScreenY, Corners[2].ScreenX, Corners[2].ScreenY) < 1)
			{
				continue;
			}
			const int32 Entry = std::max(Gte.AverageDepth3(Corners[0].ScreenZ, Corners[1].ScreenZ, Corners[2].ScreenZ) >> EntryShift, NearestMarkerEntry);
			if (Entry > static_cast<int32>(LastEntry))
			{
				continue;
			}
			FBattleShowTriangle& Drawn = Shown.Triangles.emplace_back();
			Drawn.Matrix = Matrix;
			Drawn.Corners = {MarkerModel->Vertices[Triangle.Vertices[0]], MarkerModel->Vertices[Triangle.Vertices[1]], MarkerModel->Vertices[Triangle.Vertices[2]]};
			for (std::size_t Corner = 0; Corner < 3; ++Corner)
			{
				Drawn.ColorCodes[Corner] = ColorCode(Triangle.Colors[Corner], Corner == 0 ? static_cast<uint8>(Triangle.Code | 1) : 0);
			}
			Drawn.Drawer = Drawer;
			Links.push_back({EBattleShowLink::Triangle, static_cast<uint32>(Shown.Triangles.size() - 1), Entry});
		}
	}

	void FBattleShow::DrawTotal()
	{
		// The total: the panel's frame from the HUD's sheet at its place on the screen, as the kernel draws a sheet's
		// frame (raw texels, its page without a blend mode); then the digits at the total's place.
		const FBattleTotalPanel& Total = Numbers.Total;
		SheetScratch.clear();
		if (Numbers.Sheet == nullptr || !Numbers.Sheet->BuildParts(TotalPanelFrame, Total.Place[0], Total.Place[1], SheetScratch))
		{
			ReportFault(EBattleShowFault::File);
			return;
		}
		for (FBattleSheetPart& Part : SheetScratch)
		{
			Part.TexturePage = static_cast<uint16>(Part.TexturePage & ~PageBlendBits);
		}
		FGteMatrix OnScreen = FGteMatrix::Identity();
		OnScreen.T = {-(Gte.OffsetX >> 16), -(Gte.OffsetY >> 16), Gte.ProjectionDistance};
		AddGlyphs(TotalPanelKey, EBattleShowDrawer::Total, {}, OnScreen, SheetScratch, ColorCode({0x80, 0x80, 0x80}, RawQuadCode));
		const std::array<int32, 3> Translation{(TotalX - (Gte.OffsetX >> 16)) * 2, (TotalY - (Gte.OffsetY >> 16)) * 2, Gte.ProjectionDistance};
		AddGlyphs(TotalKey, EBattleShowDrawer::Total, {}, GlyphMatrix(Total.Rotation, Translation, Total.Scale), Total.Parts, ColorCode(Total.Color, Total.Code));
	}
}
