#pragma once

#include "CoreMinimal.h"
#include "Input/TouchPad.h"
#include "Widgets/SLeafWidget.h"

/** Original, image-free phone controls. Geometry is inside an SSafeZone supplied by the controller. */
class SWeltallTouchControls : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SWeltallTouchControls) {}
		SLATE_ARGUMENT(Weltall::FTouchOptions, Options)
		SLATE_EVENT(TDelegate<void(const Weltall::FTouchOptions&)>, OnOptionsChanged)
		SLATE_EVENT(TDelegate<bool()>, OnCancelBinding)
	SLATE_END_ARGS()

	void Construct(const FArguments& Arguments);
	void ApplyOptions(const Weltall::FTouchOptions& InOptions);
	void ResetTouches();
	Weltall::FPadState HeldPad() { return Pad.TakeForTick(); }
	bool BlocksGameInput() const { return bSettingsOpen || bEditing; }
	bool HandleBack();

	virtual void Tick(const FGeometry& AllottedGeometry, double CurrentTime, float DeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect,
		FSlateWindowElementList& DrawElements, int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override;
	virtual FReply OnTouchStarted(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnTouchMoved(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual FReply OnTouchEnded(const FGeometry& Geometry, const FPointerEvent& Event) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& Event) override;
	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D::ZeroVector; }
	/** Pressed feedback follows input every frame, including under Slate global invalidation. */
	virtual bool ComputeVolatility() const override { return true; }

private:
	void RebuildLayout();
	void KeepOptions();
	Weltall::FTouchRect SettingsButton() const;
	Weltall::FTouchRect SettingsPanel() const;
	bool ChangeSetting(Weltall::FTouchPoint Point);
	Weltall::FTouchOptions Options;
	Weltall::FTouchLayout Layout;
	Weltall::FTouchPad Pad;
	TDelegate<void(const Weltall::FTouchOptions&)> OnOptionsChanged;
	TDelegate<bool()> OnCancelBinding;
	FVector2D Size = FVector2D::ZeroVector;
	FVector2D Origin = FVector2D::ZeroVector;
	bool bSettingsOpen = false;
	bool bEditing = false;
	TOptional<uint32> SettingsPointer;
	TOptional<uint32> DragPointer;
	TOptional<Weltall::ETouchControl> DragControl;
	Weltall::FTouchPoint DragStart;
	Weltall::FTouchPoint OriginalOffset;
};
