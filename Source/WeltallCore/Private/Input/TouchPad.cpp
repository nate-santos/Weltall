#include "Input/TouchPad.h"

#include <algorithm>
#include <cmath>

namespace Weltall
{
	namespace
	{
		constexpr std::array<EPadButton, TouchControlCount - 1> Buttons{EPadButton::Triangle, EPadButton::Circle, EPadButton::Cross,
			EPadButton::Square, EPadButton::L1, EPadButton::R1, EPadButton::L2, EPadButton::R2, EPadButton::Select, EPadButton::Start,
			EPadButton::L3, EPadButton::R3};

		bool IsRound(ETouchControl Control)
		{
			return Control >= ETouchControl::Triangle && Control <= ETouchControl::Square;
		}

		bool Contains(ETouchControl Control, const FTouchRect& Rect, FTouchPoint Point)
		{
			if (!Rect.Contains(Point) || Rect.Width <= 0.0f || Rect.Height <= 0.0f)
			{
				return false;
			}
			const FTouchPoint Center = Rect.Center();
			const float X = (Point.X - Center.X) / (Rect.Width * 0.5f);
			const float Y = (Point.Y - Center.Y) / (Rect.Height * 0.5f);
			return !IsRound(Control) || X * X + Y * Y <= 1.0f;
		}
	}

	FTouchLayout FTouchLayout::Build(float Width, float Height, const FTouchOptions& Options)
	{
		FTouchLayout Layout;
		if (!std::isfinite(Width) || !std::isfinite(Height) || Width <= 0.0f || Height <= 0.0f)
		{
			return Layout;
		}
		const float Scale = std::isfinite(Options.Scale) ? std::clamp(Options.Scale, 0.7f, 1.4f) : 1.0f;
		const float Unit = std::min(Height * 0.1f, Width * 0.055f) * Scale;
		const float Margin = std::min(Width, Height) * 0.025f;
		const float Left = Margin + Unit * 1.5f;
		const float Right = Width - Left;
		const float Bottom = Height - Margin - Unit * 1.5f;
		const auto Place = [&](ETouchControl Control, float X, float Y, float W, float H)
		{
			const std::size_t Index = static_cast<std::size_t>(Control);
			const FTouchPoint Offset = Options.Offsets[Index];
			const float MoveX = std::isfinite(Offset.X) ? std::clamp(Offset.X, -1.0f, 1.0f) * Width : 0.0f;
			const float MoveY = std::isfinite(Offset.Y) ? std::clamp(Offset.Y, -1.0f, 1.0f) * Height : 0.0f;
			Layout.Controls[Index] = {std::clamp(X - W * 0.5f + MoveX, 0.0f, std::max(0.0f, Width - W)),
				std::clamp(Y - H * 0.5f + MoveY, 0.0f, std::max(0.0f, Height - H)), W, H};
		};
		Place(ETouchControl::DPad, Left, Bottom, Unit * 3.0f, Unit * 3.0f);
		Place(ETouchControl::Triangle, Right, Bottom - Unit, Unit, Unit);
		Place(ETouchControl::Circle, Right + Unit, Bottom, Unit, Unit);
		Place(ETouchControl::Cross, Right, Bottom + Unit, Unit, Unit);
		Place(ETouchControl::Square, Right - Unit, Bottom, Unit, Unit);
		Place(ETouchControl::L1, Margin + Unit * 0.7f, Margin + Unit * 0.45f, Unit * 1.4f, Unit * 0.9f);
		Place(ETouchControl::L2, Margin + Unit * 2.3f, Margin + Unit * 0.45f, Unit * 1.4f, Unit * 0.9f);
		Place(ETouchControl::R1, Width - Margin - Unit * 0.7f, Margin + Unit * 0.45f, Unit * 1.4f, Unit * 0.9f);
		Place(ETouchControl::R2, Width - Margin - Unit * 2.3f, Margin + Unit * 0.45f, Unit * 1.4f, Unit * 0.9f);
		Place(ETouchControl::Select, Options.bReserveSides ? Left : Width * 0.5f - Unit,
			Options.bReserveSides ? Height * 0.32f : Height - Margin - Unit * 0.35f, Unit * 1.6f, Unit * 0.7f);
		Place(ETouchControl::Start, Options.bReserveSides ? Right : Width * 0.5f + Unit,
			Options.bReserveSides ? Height * 0.32f : Height - Margin - Unit * 0.35f, Unit * 1.6f, Unit * 0.7f);
		Place(ETouchControl::L3, Margin + Unit * 0.6f, Margin + Unit * 1.5f, Unit * 1.2f, Unit * 0.7f);
		Place(ETouchControl::R3, Width - Margin - Unit * 0.6f, Margin + Unit * 1.5f, Unit * 1.2f, Unit * 0.7f);
		const float Side = Options.bReserveSides ? std::min(Width * 0.25f, Unit * 3.0f + Margin * 2.0f) : 0.0f;
		Layout.Picture = {Side, 0.0f, Width - Side * 2.0f, Height};
		return Layout;
	}

	std::optional<ETouchControl> FTouchLayout::Hit(FTouchPoint Point) const
	{
		// The last-drawn control wins if a player deliberately overlaps controls while editing.
		for (std::size_t Index = Controls.size(); Index-- > 0;)
		{
			const auto Control = static_cast<ETouchControl>(Index);
			if (Contains(Control, Controls[Index], Point))
			{
				return Control;
			}
		}
		return std::nullopt;
	}

	void FTouchPad::SetLayout(const FTouchLayout& InLayout)
	{
		Reset();
		Layout = InLayout;
	}

	bool FTouchPad::Begin(int32 Pointer, FTouchPoint Point)
	{
		End(Pointer);
		if (const std::optional<ETouchControl> Control = Layout.Hit(Point))
		{
			Fingers.push_back({Pointer, *Control, Point});
			return true;
		}
		return false;
	}

	bool FTouchPad::Move(int32 Pointer, FTouchPoint Point)
	{
		const auto Finger = std::find_if(Fingers.begin(), Fingers.end(), [Pointer](const FFinger& Held) { return Held.Pointer == Pointer; });
		if (Finger == Fingers.end())
		{
			return false;
		}
		Finger->Point = Point;
		return true;
	}

	bool FTouchPad::End(int32 Pointer)
	{
		return std::erase_if(Fingers, [Pointer](const FFinger& Held) { return Held.Pointer == Pointer; }) != 0;
	}

	FPadState FTouchPad::Held() const
	{
		FPadState Result;
		for (const FFinger& Finger : Fingers)
		{
			const FTouchRect& Rect = Layout.Controls[static_cast<std::size_t>(Finger.Control)];
			if (Finger.Control == ETouchControl::DPad)
			{
				// A thumb owns the D-pad until lifted, including dragging beyond its visible outline.
				// A square dead zone and angular threshold allow clean diagonals without opposite directions.
				const FTouchPoint Center = Rect.Center();
				const float X = Finger.Point.X - Center.X;
				const float Y = Finger.Point.Y - Center.Y;
				const float Dead = Rect.Width * 0.08f;
				if (std::max(std::abs(X), std::abs(Y)) >= Dead)
				{
					if (std::abs(X) >= std::max(Dead, std::abs(Y) * 0.45f))
					{
						Result.Buttons |= FPadState::Bit(X < 0 ? EPadButton::Left : EPadButton::Right);
					}
					if (std::abs(Y) >= std::max(Dead, std::abs(X) * 0.45f))
					{
						Result.Buttons |= FPadState::Bit(Y < 0 ? EPadButton::Up : EPadButton::Down);
					}
				}
			}
			else if (Contains(Finger.Control, Rect, Finger.Point))
			{
				Result.Buttons |= FPadState::Bit(Buttons[static_cast<std::size_t>(Finger.Control) - 1]);
			}
		}
		return Result;
	}

	bool FTouchPad::IsTouched(ETouchControl Control) const
	{
		return std::any_of(Fingers.begin(), Fingers.end(), [Control](const FFinger& Finger) { return Finger.Control == Control; });
	}
}
