#include "Menu/MenuButtonIcons.h"

#include "Input/KernelPad.h"
#include "Menu/MenuPrimitive.h"
#include "Menu/MenuSpriteSheet.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr uint16 L1Role = 0x04;
		constexpr uint16 R1Role = 0x08;
		/** The pictures' pages: 15-bit, 256 texels square, four across and two down VRAM. */
		constexpr int32 PageSize = 256;
		constexpr int32 PagesAcross = FVram::Width / PageSize;
		constexpr int32 PageCount = PagesAcross * (FVram::Height / PageSize);
		constexpr int32 DirectDepth = 2;

		/** The sheet's own frame of a PlayStation symbol in a slot of the shape, 0 where it has none. */
		int32 SheetFrame(std::span<const FSheetButtonPicture> Own, std::string_view Icon, EMenuIconSlot Slot)
		{
			for (const FSheetButtonPicture& Picture : Own)
			{
				if (Picture.Slot == Slot && Picture.Icon == Icon)
				{
					return Picture.Frame;
				}
			}
			return 0;
		}

		/** A slot's size in the menu's pixels. */
		constexpr int32 SlotWidth(EMenuIconSlot Slot) { return Slot == EMenuIconSlot::Hint ? 16 : 12; }
		constexpr int32 SlotHeight(EMenuIconSlot Slot) { return Slot == EMenuIconSlot::Hint ? 9 : 12; }

		/**
		 * Moves a picture's texels to the rectangle at (U, V), keeping which way round its corners take them. The texels
		 * the picture named as its own (one drawn at another size does) were the old picture's: the new one's lie
		 * between its corners.
		 */
		void MoveTexels(FDrawPrimitive& Drawn, float U, float V, float Width, float Height)
		{
			Drawn.Texels.reset();
			float MinU = Drawn.Corners[0].U;
			float MinV = Drawn.Corners[0].V;
			for (const FDrawVertex& Corner : Drawn.Corners)
			{
				MinU = std::min(MinU, Corner.U);
				MinV = std::min(MinV, Corner.V);
			}
			for (FDrawVertex& Corner : Drawn.Corners)
			{
				Corner.U = Corner.U == MinU ? U : U + Width;
				Corner.V = Corner.V == MinV ? V : V + Height;
			}
		}

		/** The glyph-cache key of a button named in text: past every menu role. */
		constexpr uint16 TextButtonKey = 0x1000;
		/** A text's button glyph is a face button's width. */
		constexpr float TextGlyphPixels = 12.0f;

		/** The glyph-cache key of the directional buttons named in text. */
		constexpr uint16 TextDirectionsKey = TextButtonKey + 0x100;

		/**
		 * The PlayStation icon the text shows a button as: the font's symbol of a face button, a shoulder button's name, the
		 * D-pad for the directional buttons, and the original Start and Select names.
		 */
		std::string_view WrittenIcon(const FTextButtonCell& Cell)
		{
			if (Cell.bDirections)
			{
				return "DPad";
			}
			switch (Cell.Button)
			{
			case EPadButton::Circle:
				return "Circle";
			case EPadButton::Square:
				return "Square";
			case EPadButton::Triangle:
				return "Triangle";
			case EPadButton::Cross:
				return "Cross";
			case EPadButton::L1:
				return "L1";
			case EPadButton::R1:
				return "R1";
			case EPadButton::L2:
				return "L2";
			case EPadButton::R2:
				return "R2";
			case EPadButton::Start:
				return "Start";
			case EPadButton::Select:
				return "Select";
			default:
				return {};
			}
		}

		/** Shifts a primitive across the screen. */
		void MoveAcross(FDrawPrimitive& Drawn, float By)
		{
			for (FDrawVertex& Corner : Drawn.Corners)
			{
				Corner.X += By;
			}
		}

		/** The part of a sprite from Left to Right across the screen, its texels cut with it. */
		FDrawPrimitive CutAcross(const FDrawPrimitive& Sprite, float Left, float Right)
		{
			FDrawPrimitive Piece = Sprite;
			float MinX = Sprite.Corners[0].X;
			float MinU = Sprite.Corners[0].U;
			for (const FDrawVertex& Corner : Sprite.Corners)
			{
				MinX = std::min(MinX, Corner.X);
				MinU = std::min(MinU, Corner.U);
			}
			for (FDrawVertex& Corner : Piece.Corners)
			{
				const float X = Corner.X == MinX ? Left : Right;
				Corner.U = MinU + (X - MinX);
				Corner.X = X;
			}
			return Piece;
		}

		/** Widens (or narrows) a picture about its centre by a factor across and down. */
		void ScaleAboutCentre(FDrawPrimitive& Drawn, float Across, float Down)
		{
			float CentreX = 0.0f;
			float CentreY = 0.0f;
			for (const FDrawVertex& Corner : Drawn.Corners)
			{
				CentreX += Corner.X / 4.0f;
				CentreY += Corner.Y / 4.0f;
			}
			for (FDrawVertex& Corner : Drawn.Corners)
			{
				Corner.X = CentreX + (Corner.X - CentreX) * Across;
				Corner.Y = CentreY + (Corner.Y - CentreY) * Down;
			}
		}
	}

	EMenuIconSlot MenuIconSlotOf(uint16 Role)
	{
		return Role == L1Role || Role == R1Role ? EMenuIconSlot::Hint : EMenuIconSlot::FaceButton;
	}

	EPadButton MenuRoleButton(uint16 Role)
	{
		for (uint8 Button = 0; Button < 16; ++Button)
		{
			const FPadState Held{FPadState::Bit(static_cast<EPadButton>(Button))};
			if ((FKernelPad::LogicalButtons(FKernelPad::ReportWord(Held)) & Role) != 0)
			{
				return static_cast<EPadButton>(Button);
			}
		}
		return EPadButton::Select;
	}

	void FMenuButtonIcons::Apply(FDrawList& List, std::span<const FMenuDrawnIcon> Icons, const IButtonGlyphs& Glyphs, const FMenuSpriteSheet& Sheet,
		std::span<const FSheetButtonPicture> Own, std::span<const FButtonGlyph> Keys)
	{
		for (const FMenuDrawnIcon& Icon : Icons)
		{
			FDrawPrimitive& Drawn = List.Primitives[Icon.Primitive];
			if (!Drawn.Texture)
			{
				continue;
			}
			if (Icon.Role >= MenuPictureRole)
			{
				const FMenuSheetPicture* SheetPicture = Sheet.Picture(Icon.Role - MenuPictureRole);
				const FPlaced* Picture = SheetPicture != nullptr ? PlacePicture(*SheetPicture) : nullptr;
				if (Picture != nullptr)
				{
					const EBlendMode Blend = Drawn.Texture->Page.Blend;
					Drawn.Texture = FDrawTexture{PicturesVram, FTexturePage::FromAttribute(MenuGpu::TexturePage(DirectDepth, 0, Picture->PageX, Picture->PageY)), {}};
					Drawn.Texture->Page.Blend = Blend;
					MoveTexels(Drawn, static_cast<float>(Picture->U), static_cast<float>(Picture->V), static_cast<float>(Picture->Width),
						static_cast<float>(Picture->Height));
				}
				continue;
			}
			EButtonIconSet Set = EButtonIconSet::Keyboard;
			std::string_view Symbol;
			if (Icon.Role >= MenuKeyRole)
			{
				const std::size_t Named = Icon.Role - MenuKeyRole;
				if (Named >= Keys.size())
				{
					continue;
				}
				Set = Keys[Named].Set;
				Symbol = Keys[Named].Icon;
			}
			else
			{
				const FRoleGlyph& Glyph = GlyphOf(Icon.Role, Glyphs);
				if (Glyph.Icon.empty())
				{
					continue;
				}
				Set = Glyph.Set;
				Symbol = Glyph.Icon;
			}
			// The page's blend is the drawn picture's: a dimmed or fading one stays so.
			const EBlendMode Blend = Drawn.Texture->Page.Blend;
			if (Set == EButtonIconSet::PlayStation)
			{
				if (const int32 Frame = SheetFrame(Own, Symbol, Icon.Slot))
				{
					// The game's own picture of the symbol: its frame's texels in the drawn one's place.
					const std::span<const FMenuSpritePart> Parts = Sheet.Frame(Frame);
					if (!Parts.empty())
					{
						const FMenuSpritePart& Part = Parts.back();
						Drawn.Texture->Page = FTexturePage::FromAttribute(MenuGpu::TexturePage(Part.Depth, 0, Part.ImageX, Part.ImageY));
						Drawn.Texture->Page.Blend = Blend;
						Drawn.Texture->Clut = FClutPosition::FromAttribute(MenuGpu::Clut(Part.ClutX, Part.ClutY));
						MoveTexels(Drawn, Part.U, Part.V, Part.Width, Part.Height);
					}
					continue;
				}
			}
			const FPlaced* Picture = Place(Set, Symbol, Icon.Slot);
			if (Picture == nullptr)
			{
				continue;
			}
			ScaleAboutCentre(Drawn, static_cast<float>(Picture->Width) / static_cast<float>(SlotWidth(Icon.Slot) * MenuIconTexelsPerPixel),
				static_cast<float>(Picture->Height) / static_cast<float>(SlotHeight(Icon.Slot) * MenuIconTexelsPerPixel));
			Drawn.Texture = FDrawTexture{PicturesVram, FTexturePage::FromAttribute(MenuGpu::TexturePage(DirectDepth, 0, Picture->PageX, Picture->PageY)), {}};
			Drawn.Texture->Page.Blend = Blend;
			MoveTexels(Drawn, static_cast<float>(Picture->U), static_cast<float>(Picture->V), static_cast<float>(Picture->Width),
				static_cast<float>(Picture->Height));
		}
	}

	void FMenuButtonIcons::ApplyToText(FDrawList& List, std::span<const FTextButtonCell> Cells, const IButtonGlyphs& Glyphs, std::vector<int32>* Matches,
		uint32* Mark)
	{
		// From the last sprite and its rightmost glyph back, so the pieces a cut adds move nothing still to cut.
		TextCells.assign(Cells.begin(), Cells.end());
		TextCuts.clear();
		uint32 LineEnd = 0;
		std::size_t LineGrown = 0;
		std::sort(TextCells.begin(), TextCells.end(), [](const FTextButtonCell& A, const FTextButtonCell& B)
		{
			return A.Primitive != B.Primitive ? A.Primitive > B.Primitive : A.OffsetX > B.OffsetX;
		});
		for (const FTextButtonCell& Cell : TextCells)
		{
			if (Cell.Primitive >= List.Primitives.size())
			{
				continue;
			}
			if (Cell.LineEnd != LineEnd)
			{
				LineEnd = Cell.LineEnd;
				LineGrown = 0;
			}
			const FRoleGlyph& Glyph = Cell.bDirections ? DirectionsGlyph(Glyphs)
													   : GlyphOfButton(static_cast<uint16>(TextButtonKey + static_cast<uint16>(Cell.Button)), Cell.Button, Glyphs);
			if (Glyph.Icon.empty() || (Glyph.Set == EButtonIconSet::PlayStation && Glyph.Icon == WrittenIcon(Cell)))
			{
				continue;
			}
			const FPlaced* Picture = Place(Glyph.Set, Glyph.Icon, EMenuIconSlot::FaceButton);
			const FDrawPrimitive Line = List.Primitives[Cell.Primitive];
			if (Picture == nullptr || !Line.Texture)
			{
				continue;
			}
			float MinX = Line.Corners[0].X;
			float MaxX = MinX;
			float MinY = Line.Corners[0].Y;
			for (const FDrawVertex& Corner : Line.Corners)
			{
				MinX = std::min(MinX, Corner.X);
				MaxX = std::max(MaxX, Corner.X);
				MinY = std::min(MinY, Corner.Y);
			}
			const float GlyphLeft = MinX + static_cast<float>(Cell.OffsetX);
			const float GlyphRight = std::min(GlyphLeft + static_cast<float>(Cell.Width), MaxX);
			// The picture starts where the cell does, and what follows in the line moves by as much as the picture is wider
			// than the cell, so the words after keep their space.
			const float PictureWidth = static_cast<float>(Picture->Width) / static_cast<float>(MenuIconTexelsPerPixel);
			const float Shift = GlyphLeft + PictureWidth - GlyphRight;
			TextPieces.clear();
			if (GlyphLeft > MinX)
			{
				TextPieces.push_back(CutAcross(Line, MinX, GlyphLeft));
			}
			FDrawPrimitive& Drawn = TextPieces.emplace_back(MakeSprite(static_cast<int32>(GlyphLeft), static_cast<int32>(MinY), static_cast<int32>(TextGlyphPixels),
				static_cast<int32>(TextGlyphPixels), *Line.Texture, 0, 0));
			Drawn.Blend = Line.Blend;
			Drawn.bModulate = false;
			const EBlendMode Blend = Line.Texture->Page.Blend;
			ScaleAboutCentre(Drawn, static_cast<float>(Picture->Width) / static_cast<float>(SlotWidth(EMenuIconSlot::FaceButton) * MenuIconTexelsPerPixel),
				static_cast<float>(Picture->Height) / static_cast<float>(SlotHeight(EMenuIconSlot::FaceButton) * MenuIconTexelsPerPixel));
			MoveAcross(Drawn, (PictureWidth - TextGlyphPixels) / 2.0f);
			Drawn.Texture = FDrawTexture{PicturesVram, FTexturePage::FromAttribute(MenuGpu::TexturePage(DirectDepth, 0, Picture->PageX, Picture->PageY)), {}};
			Drawn.Texture->Page.Blend = Blend;
			MoveTexels(Drawn, static_cast<float>(Picture->U), static_cast<float>(Picture->V), static_cast<float>(Picture->Width),
				static_cast<float>(Picture->Height));
			if (GlyphRight < MaxX)
			{
				MoveAcross(TextPieces.emplace_back(CutAcross(Line, GlyphRight, MaxX)), Shift);
			}
			const auto At = List.Primitives.begin() + Cell.Primitive;
			List.Primitives.erase(At);
			List.Primitives.insert(List.Primitives.begin() + Cell.Primitive, TextPieces.begin(), TextPieces.end());
			const std::size_t Added = TextPieces.size() - 1;
			// The line's later sprites and the pieces cut from it further right.
			const std::size_t LineLast = Cell.LineEnd == 0 ? Cell.Primitive + 1 : Cell.LineEnd + LineGrown;
			for (std::size_t Later = Cell.Primitive + TextPieces.size(); Later < std::min(LineLast + Added, List.Primitives.size()); ++Later)
			{
				MoveAcross(List.Primitives[Later], Shift);
			}
			LineGrown += Added;
			TextCuts.emplace_back(Cell.Primitive, static_cast<uint32>(Added));
			if (Cell.LineRest != 0)
			{
				// The line's sprite that lies apart, where the cuts so far have left it, with the pieces cut from it.
				const uint32 Rest = Cell.LineRest - 1;
				std::size_t RestAt = Rest;
				std::size_t Count = 1;
				for (const auto& [Cut, Grown] : TextCuts)
				{
					RestAt += Cut < Rest ? Grown : 0;
					Count += Cut == Rest ? Grown : 0;
				}
				for (std::size_t Later = RestAt; Later < std::min(RestAt + Count, List.Primitives.size()); ++Later)
				{
					MoveAcross(List.Primitives[Later], Shift);
				}
			}
			if (Matches != nullptr && Cell.Primitive < Matches->size())
			{
				(*Matches)[Cell.Primitive] = -1;
				Matches->insert(Matches->begin() + Cell.Primitive, Added, -1);
			}
			if (Mark != nullptr && *Mark > Cell.Primitive)
			{
				*Mark += static_cast<uint32>(Added);
			}
		}
	}

	const FMenuButtonIcons::FRoleGlyph& FMenuButtonIcons::DirectionsGlyph(const IButtonGlyphs& Glyphs)
	{
		FRoleGlyph& Made = GlyphOfButton(TextDirectionsKey, EPadButton::Up, Glyphs);
		if (Made.Directions == RolesRevision + 1)
		{
			return Made;
		}
		Made.Directions = RolesRevision + 1;
		if (Made.Set == EButtonIconSet::Keyboard)
		{
			// The four keys together, as one key cap: up, left, down, right.
			std::string Keys;
			for (const EPadButton Direction : {EPadButton::Up, EPadButton::Left, EPadButton::Down, EPadButton::Right})
			{
				const std::string Icon = Glyphs.Glyph(Direction).Icon;
				if (Icon.empty())
				{
					return Made;
				}
				Keys += (Keys.empty() ? "" : std::string(1, MenuKeysSeparator)) + Icon;
			}
			Made.Icon = Keys;
		}
		else if (Made.Icon.starts_with("DPad_"))
		{
			Made.Icon = "DPad";
		}
		else if (const std::size_t Under = Made.Icon.find('_'); Under != std::string::npos)
		{
			// A stick's direction: the stick.
			Made.Icon.resize(Under);
		}
		return Made;
	}

	const FMenuButtonIcons::FRoleGlyph& FMenuButtonIcons::GlyphOf(uint16 Role, const IButtonGlyphs& Glyphs)
	{
		return GlyphOfButton(Role, MenuRoleButton(Role), Glyphs);
	}

	FMenuButtonIcons::FRoleGlyph& FMenuButtonIcons::GlyphOfButton(uint16 Key, EPadButton Button, const IButtonGlyphs& Glyphs)
	{
		const uint16 Role = Key;
		if (RolesOf != &Glyphs || RolesRevision != Glyphs.Revision())
		{
			RolesOf = &Glyphs;
			RolesRevision = Glyphs.Revision();
			RolesKnown = 0;
		}
		for (int32 Known = 0; Known < RolesKnown; ++Known)
		{
			if (Roles[static_cast<std::size_t>(Known)].Role == Role)
			{
				return Roles[static_cast<std::size_t>(Known)];
			}
		}
		// Six roles at most, the text's four face buttons, six named buttons and the directional ones: another (none is drawn) takes the last place.
		FRoleGlyph& Made = Roles[static_cast<std::size_t>(std::min(RolesKnown, static_cast<int32>(Roles.size()) - 1))];
		RolesKnown = std::min(RolesKnown + 1, static_cast<int32>(Roles.size()));
		FButtonGlyph Glyph = Glyphs.Glyph(Button);
		Made.Role = Role;
		Made.Set = Glyph.Set;
		Made.Icon = std::move(Glyph.Icon);
		Made.Directions = 0;
		return Made;
	}

	const FMenuButtonIcons::FPlaced* FMenuButtonIcons::Place(EButtonIconSet Set, std::string_view Icon, EMenuIconSlot Slot)
	{
		for (const FPlaced& Known : Placed)
		{
			if (Known.Set == Set && Known.Slot == Slot && Known.Icon == Icon)
			{
				return &Known;
			}
		}
		FPlaced* Made = Shelve(DrawMenuButtonIcon(Set, Icon, Slot));
		if (Made != nullptr)
		{
			Made->Set = Set;
			Made->Icon = std::string(Icon);
			Made->Slot = Slot;
		}
		return Made;
	}

	const FMenuButtonIcons::FPlaced* FMenuButtonIcons::PlacePicture(const FMenuSheetPicture& Own)
	{
		for (const FPlaced& Known : Placed)
		{
			if (Known.bSheetPicture && Known.PictureKey == Own.Key)
			{
				return &Known;
			}
		}
		FPlaced* Made = Shelve({Own.Width, Own.Height, Own.Texels});
		if (Made != nullptr)
		{
			Made->bSheetPicture = true;
			Made->PictureKey = Own.Key;
		}
		return Made;
	}

	FMenuButtonIcons::FPlaced* FMenuButtonIcons::Shelve(FMenuIconPicture Picture)
	{
		if (Picture.Width > PageSize || Picture.Height > PageSize)
		{
			return nullptr;
		}
		if (ShelfX + Picture.Width > PageSize)
		{
			ShelfY += ShelfHeight;
			ShelfX = 0;
			ShelfHeight = 0;
		}
		if (ShelfY + Picture.Height > PageSize)
		{
			++Page;
			ShelfX = 0;
			ShelfY = 0;
			ShelfHeight = 0;
		}
		if (Page >= PageCount)
		{
			return nullptr;
		}
		FPlaced& Made = Placed.emplace_back();
		Made.PageX = Page % PagesAcross * PageSize;
		Made.PageY = Page / PagesAcross * PageSize;
		Made.U = ShelfX;
		Made.V = ShelfY;
		Made.Width = Picture.Width;
		Made.Height = Picture.Height;
		PictureVram.Upload({{static_cast<int16>(Made.PageX + Made.U), static_cast<int16>(Made.PageY + Made.V), static_cast<int16>(Picture.Width),
			static_cast<int16>(Picture.Height)}, std::move(Picture.Texels)});
		ShelfX += Made.Width;
		ShelfHeight = std::max(ShelfHeight, Made.Height);
		return &Made;
	}
}
