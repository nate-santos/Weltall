#include "Game/TitleMarkDrawing.h"

#include "Text/MesFont.h"

#include <algorithm>

namespace Weltall
{
	namespace
	{
		constexpr FScreenRect Screen{0, 0, 320, 224};
		/**
		 * The mark sits under the title's logo, ending where the logo's last letter ends (its right edge on the screen),
		 * a little below the logo's foot. A line's last letter leaves a few empty columns of its advance.
		 */
		constexpr int32 LogoRight = 310;
		constexpr int32 LastLetterSpace = 4;
		constexpr int32 Top = 134;
	}

	std::string FTitleMarkDrawing::Text(std::string_view Version)
	{
		return "Weltall " + std::string(Version);
	}

	int32 FTitleMarkDrawing::Strength(const FFieldRenderState& Render, bool bUnderMenu)
	{
		if (Render.Panorama.bHidden)
		{
			return 0;
		}
		int32 Dark = !bUnderMenu && Render.MenuShade.bShown ? Render.MenuShade.Level * FFieldMenuShade::GreyPerLevel : 0;
		for (const FFieldFade& Fade : Render.Fades)
		{
			if (Fade.bActive && static_cast<EBlendMode>(Fade.BlendMode & 3) == EBlendMode::Subtract)
			{
				const FMdlColor Color = Fade.DrawnColor();
				Dark = std::max({Dark, int32{Color.R}, int32{Color.G}, int32{Color.B}});
			}
		}
		return 255 - std::min(Dark, 255);
	}

	void FTitleMarkDrawing::Build(std::string_view Version, int32 Level, const FMesFont& Font, FDrawList& Out)
	{
		Out.Primitives.clear();
		if (Level <= 0)
		{
			return;
		}
		const int32 Advance = Words.Prepare(Text(Version), Font) - FMesFont::CellWidth;
		// At half the level a texel is drawn as it is, so the mark's light falls as the picture's white does; the chapter
		// cards draw theirs at the full level, doubled, and stay white until it is half gone.
		Words.Add(LogoRight + LastLetterSpace - Advance, Top, Level / 2, Screen, Out);
	}
}
