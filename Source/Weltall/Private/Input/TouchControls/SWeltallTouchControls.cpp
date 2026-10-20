#include "Input/TouchControls/SWeltallTouchControls.h"

#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"

namespace
{
	// Unity builds share this namespace with later engine includes. Import only touch names:
	// Android's std::uint64_t and Unreal's uint64 can be different underlying types.
	using Weltall::ETouchControl;
	using Weltall::FTouchPoint;
	using Weltall::FTouchRect;
	using Weltall::TouchControlNames;

	FTouchPoint LocalPoint(const FGeometry& Geometry, const FPointerEvent& Event)
	{
		const FVector2D Point = Geometry.AbsoluteToLocal(Event.GetScreenSpacePosition());
		return {static_cast<float>(Point.X), static_cast<float>(Point.Y)};
	}

	void Lines(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, TArray<FVector2f> Points, FLinearColor Color, float Width = 2.0f)
	{
		FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), MoveTemp(Points), ESlateDrawEffect::None, Color, true, Width);
	}

	void Outline(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FTouchRect& Rect, FLinearColor Color)
	{
		Lines(Elements, Layer, Geometry, {{Rect.X, Rect.Y}, {Rect.X + Rect.Width, Rect.Y}, {Rect.X + Rect.Width, Rect.Y + Rect.Height},
			{Rect.X, Rect.Y + Rect.Height}, {Rect.X, Rect.Y}}, Color);
	}

	void Text(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FString& Label, FVector2D At,
		FLinearColor Color, int32 FontSize = 16)
	{
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", FontSize);
		const FVector2D TextSize = FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label, Font);
		FSlateDrawElement::MakeText(Elements, Layer, Geometry.ToPaintGeometry(TextSize, FSlateLayoutTransform(At - TextSize * 0.5f)),
			Label, Font, ESlateDrawEffect::None, Color);
	}

	void Fill(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, const FTouchRect& Rect, FLinearColor Color)
	{
		FSlateDrawElement::MakeBox(Elements, Layer, Geometry.ToPaintGeometry(FVector2D(Rect.Width, Rect.Height), FSlateLayoutTransform(FVector2D(Rect.X, Rect.Y))),
			FCoreStyle::Get().GetBrush("WhiteBrush"), ESlateDrawEffect::None, Color);
	}

	void DrawControl(FSlateWindowElementList& Elements, int32 Layer, const FGeometry& Geometry, ETouchControl Control,
		const FTouchRect& Rect, FLinearColor Color)
	{
		const FTouchPoint Center = Rect.Center();
		const float Radius = Rect.Width * 0.45f;
		if (Control == ETouchControl::DPad)
		{
			const float Arm = Rect.Width / 6.0f;
			const float Half = Rect.Width * 0.5f;
			Lines(Elements, Layer, Geometry, {{Center.X - Arm, Center.Y - Half}, {Center.X + Arm, Center.Y - Half},
				{Center.X + Arm, Center.Y - Arm}, {Center.X + Half, Center.Y - Arm}, {Center.X + Half, Center.Y + Arm},
				{Center.X + Arm, Center.Y + Arm}, {Center.X + Arm, Center.Y + Half}, {Center.X - Arm, Center.Y + Half},
				{Center.X - Arm, Center.Y + Arm}, {Center.X - Half, Center.Y + Arm}, {Center.X - Half, Center.Y - Arm},
				{Center.X - Arm, Center.Y - Arm}, {Center.X - Arm, Center.Y - Half}}, Color);
			return;
		}
		if (Control >= ETouchControl::Triangle && Control <= ETouchControl::Square)
		{
			TArray<FVector2f> Circle;
			for (int32 Index = 0; Index <= 32; ++Index)
			{
				const float Angle = static_cast<float>(Index) * 2.0f * PI / 32.0f;
				Circle.Add({Center.X + FMath::Cos(Angle) * Radius, Center.Y + FMath::Sin(Angle) * Radius});
			}
			Lines(Elements, Layer, Geometry, MoveTemp(Circle), Color);
			const float Mark = Radius * 0.4f;
			if (Control == ETouchControl::Triangle)
			{
				Lines(Elements, Layer + 1, Geometry, {{Center.X, Center.Y - Mark}, {Center.X + Mark, Center.Y + Mark},
					{Center.X - Mark, Center.Y + Mark}, {Center.X, Center.Y - Mark}}, Color);
			}
			else if (Control == ETouchControl::Square)
			{
				Outline(Elements, Layer + 1, Geometry, {Center.X - Mark, Center.Y - Mark, Mark * 2, Mark * 2}, Color);
			}
			else if (Control == ETouchControl::Cross)
			{
				Lines(Elements, Layer + 1, Geometry, {{Center.X - Mark, Center.Y - Mark}, {Center.X + Mark, Center.Y + Mark}}, Color);
				Lines(Elements, Layer + 1, Geometry, {{Center.X - Mark, Center.Y + Mark}, {Center.X + Mark, Center.Y - Mark}}, Color);
			}
			else
			{
				TArray<FVector2f> MarkCircle;
				for (int32 Index = 0; Index <= 24; ++Index)
				{
					const float Angle = static_cast<float>(Index) * 2.0f * PI / 24.0f;
					MarkCircle.Add({Center.X + FMath::Cos(Angle) * Mark, Center.Y + FMath::Sin(Angle) * Mark});
				}
				Lines(Elements, Layer + 1, Geometry, MoveTemp(MarkCircle), Color);
			}
			return;
		}
		Outline(Elements, Layer, Geometry, Rect, Color);
		Text(Elements, Layer + 1, Geometry, FString(UTF8_TO_TCHAR(TouchControlNames[static_cast<std::size_t>(Control)])).ToUpper(),
			{Center.X, Center.Y}, Color, FMath::Max(10, FMath::RoundToInt(Rect.Height * 0.34f)));
	}
}

void SWeltallTouchControls::Construct(const FArguments& Arguments)
{
	Options = Arguments._Options;
	OnOptionsChanged = Arguments._OnOptionsChanged;
	OnCancelBinding = Arguments._OnCancelBinding;
	AudioPaused = Arguments._AudioPaused;
	CanResumeAudio = Arguments._CanResumeAudio;
	OnResumeAudio = Arguments._OnResumeAudio;
}

void SWeltallTouchControls::ApplyOptions(const Weltall::FTouchOptions& InOptions)
{
	Options = InOptions;
	RebuildLayout();
}

void SWeltallTouchControls::ResetTouches()
{
	Pad.Reset();
	SettingsPointer.Reset();
	DragPointer.Reset();
	DragControl.Reset();
}

bool SWeltallTouchControls::HandleBack()
{
	if (!BlocksGameInput())
	{
		return false;
	}
	ResetTouches();
	const bool bKeepLayout = bEditing;
	bEditing = false;
	bSettingsOpen = false;
	if (bKeepLayout)
	{
		KeepOptions();
	}
	return true;
}

void SWeltallTouchControls::RebuildLayout()
{
	Layout = Weltall::FTouchLayout::Build(static_cast<float>(Size.X), static_cast<float>(Size.Y), Options);
	Pad.SetLayout(Layout);
}

void SWeltallTouchControls::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
	SLeafWidget::Tick(Geometry, CurrentTime, DeltaTime);
	if (Size != Geometry.GetLocalSize() || Origin != Geometry.GetAbsolutePosition())
	{
		Size = Geometry.GetLocalSize();
		Origin = Geometry.GetAbsolutePosition();
		ResetTouches();
		RebuildLayout();
	}
}

Weltall::FTouchRect SWeltallTouchControls::SettingsButton() const
{
	const float Width = FMath::Min(static_cast<float>(Size.X) * 0.17f, static_cast<float>(Size.Y) * 0.24f);
	const float Height = static_cast<float>(Size.Y) * 0.08f;
	// Keep the recovery shortcut in the edge area: the top-center battle HUD contains the party's health.
	// Use the default D-pad anchor so moving a control cannot hide the shortcut behind the settings panel.
	const float CenterX = Weltall::FTouchLayout::Build(static_cast<float>(Size.X), static_cast<float>(Size.Y), {}).Controls[0].Center().X;
	return {CenterX - Width * 0.5f, static_cast<float>(Size.Y) * 0.41f, Width, Height};
}

Weltall::FTouchRect SWeltallTouchControls::SettingsPanel() const
{
	const float Width = FMath::Min(static_cast<float>(Size.X) * 0.6f, static_cast<float>(Size.Y) * 0.9f);
	const float Height = static_cast<float>(Size.Y) * 0.78f;
	return {(static_cast<float>(Size.X) - Width) * 0.5f, (static_cast<float>(Size.Y) - Height) * 0.5f, Width, Height};
}

Weltall::FTouchRect SWeltallTouchControls::AudioPausePanel() const
{
	const float Width = FMath::Min(static_cast<float>(Size.X) * 0.55f, static_cast<float>(Size.Y) * 0.8f);
	const float Height = static_cast<float>(Size.Y) * 0.16f;
	return {(static_cast<float>(Size.X) - Width) * 0.5f, static_cast<float>(Size.Y) * 0.22f, Width, Height};
}

int32 SWeltallTouchControls::OnPaint(const FPaintArgs&, const FGeometry& Geometry, const FSlateRect&,
	FSlateWindowElementList& Elements, int32 Layer, const FWidgetStyle& Style, bool) const
{
	using namespace Weltall;
	for (std::size_t Index = 0; Index < TouchControlCount; ++Index)
	{
		const auto Control = static_cast<ETouchControl>(Index);
		const float Opacity = bEditing ? 0.8f : Pad.IsTouched(Control) ? Options.PressedOpacity : Options.IdleOpacity;
		const FLinearColor Color = FLinearColor(1.0f, 1.0f, 1.0f, FMath::Clamp(Opacity, 0.0f, 1.0f)) * Style.GetColorAndOpacityTint();
		DrawControl(Elements, Layer, Geometry, Control, Layout.Controls[Index], Color);
	}
	const FTouchRect Button = SettingsButton();
	const float ButtonOpacity = bEditing ? 0.8f : SettingsPointer.IsSet() ? Options.PressedOpacity : Options.IdleOpacity;
	const FLinearColor ButtonColor = FLinearColor(1, 1, 1, FMath::Clamp(ButtonOpacity, 0.0f, 1.0f)) * Style.GetColorAndOpacityTint();
	Outline(Elements, Layer + 2, Geometry, Button, ButtonColor);
	Text(Elements, Layer + 3, Geometry, bEditing ? TEXT("Done") : bSettingsOpen ? TEXT("Close") : TEXT("Controls"),
		{Button.Center().X, Button.Center().Y}, ButtonColor, FMath::Max(12, FMath::RoundToInt(Button.Height * 0.32f)));
	if (bEditing)
	{
		Text(Elements, Layer + 3, Geometry, TEXT("Drag a control. Tap Done to save."), {Size.X * 0.5, Size.Y * 0.12}, FLinearColor::White,
			FMath::Max(12, FMath::RoundToInt(static_cast<float>(Size.Y) * 0.025f)));
	}
	if (bSettingsOpen)
	{
		const FTouchRect Panel = SettingsPanel();
		Fill(Elements, Layer + 4, Geometry, Panel, FLinearColor(0.015f, 0.02f, 0.03f, 0.96f));
		const float RowHeight = Panel.Height / 7.0f;
		const TArray<FString> Rows = {TEXT("Touch controls"),
			FString::Printf(TEXT("Idle: %d%% visible"), FMath::RoundToInt(Options.IdleOpacity * 100)),
			FString::Printf(TEXT("Pressed: %d%% visible"), FMath::RoundToInt(Options.PressedOpacity * 100)),
			FString::Printf(TEXT("Size: %d%%"), FMath::RoundToInt(Options.Scale * 100)),
			Options.bReserveSides ? TEXT("Side areas: on") : TEXT("Side areas: off"), TEXT("Move controls"), TEXT("Reset layout and appearance")};
		for (int32 Row = 0; Row < Rows.Num(); ++Row)
		{
			const float Y = Panel.Y + RowHeight * (static_cast<float>(Row) + 0.5f);
			Text(Elements, Layer + 5, Geometry, Rows[Row], {Panel.X + Panel.Width * 0.5f, Y}, FLinearColor::White,
				FMath::Max(12, FMath::RoundToInt(RowHeight * (Row == 0 ? 0.34f : 0.25f))));
			if (Row >= 1 && Row <= 3)
			{
				Text(Elements, Layer + 5, Geometry, TEXT("-"), {Panel.X + RowHeight * 0.5f, Y}, FLinearColor::White, FMath::RoundToInt(RowHeight * 0.45f));
				Text(Elements, Layer + 5, Geometry, TEXT("+"), {Panel.X + Panel.Width - RowHeight * 0.5f, Y}, FLinearColor::White, FMath::RoundToInt(RowHeight * 0.45f));
			}
		}
	}
	if (AudioPaused.Get(false))
	{
		const FTouchRect Panel = AudioPausePanel();
		Fill(Elements, Layer + 6, Geometry, Panel, FLinearColor(0.015f, 0.02f, 0.03f, 0.94f));
		Text(Elements, Layer + 7, Geometry, TEXT("Paused for audio"),
			{Panel.Center().X, Panel.Y + Panel.Height * 0.34f}, FLinearColor::White, FMath::Max(12, FMath::RoundToInt(Panel.Height * 0.22f)));
		Text(Elements, Layer + 7, Geometry, CanResumeAudio.Get(false) ? TEXT("Tap to resume") : TEXT("Waiting for the interruption to finish"),
			{Panel.Center().X, Panel.Y + Panel.Height * 0.7f}, FLinearColor::White, FMath::Max(10, FMath::RoundToInt(Panel.Height * 0.15f)));
	}
	return Layer + 7;
}

void SWeltallTouchControls::KeepOptions()
{
	RebuildLayout();
	OnOptionsChanged.ExecuteIfBound(Options);
}

bool SWeltallTouchControls::ChangeSetting(Weltall::FTouchPoint Point)
{
	const Weltall::FTouchRect Panel = SettingsPanel();
	if (!Panel.Contains(Point))
	{
		return false;
	}
	const float RowHeight = Panel.Height / 7.0f;
	const int32 Row = FMath::FloorToInt((Point.Y - Panel.Y) / RowHeight);
	const float Step = Point.X < Panel.X + RowHeight ? -0.05f : Point.X >= Panel.X + Panel.Width - RowHeight ? 0.05f : 0.0f;
	switch (Row)
	{
	case 1: Options.IdleOpacity = FMath::Clamp(Options.IdleOpacity + Step, 0.0f, 1.0f); break;
	case 2: Options.PressedOpacity = FMath::Clamp(Options.PressedOpacity + Step, 0.0f, 1.0f); break;
	case 3: Options.Scale = FMath::Clamp(Options.Scale + Step, 0.7f, 1.4f); break;
	case 4: Options.bReserveSides = !Options.bReserveSides; break;
	case 5: bSettingsOpen = false; bEditing = true; break;
	case 6: Options = Weltall::FTouchOptions{}; break;
	default: return true;
	}
	KeepOptions();
	return true;
}

FReply SWeltallTouchControls::OnTouchStarted(const FGeometry& Geometry, const FPointerEvent& Event)
{
	const Weltall::FTouchPoint Point = LocalPoint(Geometry, Event);
	if (AudioPaused.Get(false))
	{
		const bool bPanel = AudioPausePanel().Contains(Point);
		if (CanResumeAudio.Get(false) && (bPanel || SettingsButton().Contains(Point) || Layout.Hit(Point).has_value())
			&& OnResumeAudio.IsBound() && OnResumeAudio.Execute())
		{
			ResetTouches();
			return FReply::Handled();
		}
		if (bPanel) return FReply::Handled();
	}
	if (SettingsButton().Contains(Point))
	{
		ResetTouches();
		if (bEditing)
		{
			bEditing = false;
			KeepOptions();
		}
		else
		{
			bSettingsOpen = !bSettingsOpen;
		}
		SettingsPointer = Event.GetPointerIndex();
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	if (bSettingsOpen)
	{
		ChangeSetting(Point);
		return FReply::Handled();
	}
	if (bEditing)
	{
		if (!DragPointer.IsSet())
		{
			if (const auto Control = Layout.Hit(Point))
			{
				DragPointer = Event.GetPointerIndex();
				DragControl = *Control;
				DragStart = Point;
				OriginalOffset = Options.Offsets[static_cast<std::size_t>(*Control)];
				return FReply::Handled().CaptureMouse(SharedThis(this));
			}
		}
		return FReply::Handled();
	}
	// Unrelated touches bubble to the viewport. No invisible full-screen gameplay button is used.
	if (Layout.Hit(Point) == Weltall::ETouchControl::Circle && OnCancelBinding.IsBound() && OnCancelBinding.Execute())
	{
		ResetTouches();
		return FReply::Handled();
	}
	return Pad.Begin(Event.GetPointerIndex(), Point) ? FReply::Handled().CaptureMouse(SharedThis(this)) : FReply::Unhandled();
}

FReply SWeltallTouchControls::OnTouchMoved(const FGeometry& Geometry, const FPointerEvent& Event)
{
	if (SettingsPointer.IsSet() && *SettingsPointer == Event.GetPointerIndex())
	{
		return FReply::Handled();
	}
	const Weltall::FTouchPoint Point = LocalPoint(Geometry, Event);
	if (DragPointer.IsSet() && DragControl.IsSet() && *DragPointer == Event.GetPointerIndex())
	{
		Options.Offsets[static_cast<std::size_t>(*DragControl)] = {
			FMath::Clamp(OriginalOffset.X + (Point.X - DragStart.X) / static_cast<float>(Size.X), -1.0f, 1.0f),
			FMath::Clamp(OriginalOffset.Y + (Point.Y - DragStart.Y) / static_cast<float>(Size.Y), -1.0f, 1.0f)};
		RebuildLayout();
		return FReply::Handled();
	}
	return Pad.Move(Event.GetPointerIndex(), Point) ? FReply::Handled() : FReply::Unhandled();
}

FReply SWeltallTouchControls::OnTouchEnded(const FGeometry&, const FPointerEvent& Event)
{
	if (SettingsPointer.IsSet() && *SettingsPointer == Event.GetPointerIndex())
	{
		SettingsPointer.Reset();
		return FReply::Handled().ReleaseMouseCapture();
	}
	if (DragPointer.IsSet() && *DragPointer == Event.GetPointerIndex())
	{
		DragPointer.Reset();
		DragControl.Reset();
		KeepOptions();
		return FReply::Handled().ReleaseMouseCapture();
	}
	return Pad.End(Event.GetPointerIndex()) ? FReply::Handled().ReleaseMouseCapture() : FReply::Unhandled();
}

void SWeltallTouchControls::OnMouseCaptureLost(const FCaptureLostEvent& Event)
{
	// Slate releases capture per pointer. Clearing every finger here would break multi-touch
	// whenever the player lifted one action finger while still holding the D-pad.
	Pad.Cancel(Event.PointerIndex);
	if (SettingsPointer.IsSet() && Event.PointerIndex >= 0 && *SettingsPointer == static_cast<uint32>(Event.PointerIndex))
	{
		SettingsPointer.Reset();
	}
	if (DragPointer.IsSet() && Event.PointerIndex >= 0 && *DragPointer == static_cast<uint32>(Event.PointerIndex))
	{
		DragPointer.Reset();
		DragControl.Reset();
	}
}
