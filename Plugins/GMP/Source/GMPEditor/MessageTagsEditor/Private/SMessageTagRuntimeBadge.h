// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/CurveSequence.h"
#include "MessageTagContainer.h"
#include "UObject/WeakObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UEdGraphNode;
class SBorder;

// PIE-only count badge shown in a GMP message node's Msg row: displays the live listener/call count for the tag, pulses when the count changes, and expands an interactive listeners/callinfos panel on hover. Hidden outside PIE and on non-GMP nodes.
class SMessageTagRuntimeBadge : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMessageTagRuntimeBadge)
		: _bFillRow(false)
	{}
		SLATE_ARGUMENT(TWeakObjectPtr<UEdGraphNode>, OwnerNode)
		SLATE_ATTRIBUTE(FMessageTag, Tag)
		SLATE_ARGUMENT(bool, bFillRow)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

private:
	EVisibility GetBadgeVisibility() const;
	FText GetBadgeText() const;
	FText GetBadgeIcon() const;
	FSlateColor GetBadgeColor() const;
	FSlateColor GetBadgeBorderColor() const;
	void RebuildToolTipContent();

	// True when the owning blueprint has a debug object selected. Debugging = pill covers the whole combo (full width); otherwise the pill stays a fixed marker and only animates in place.
	bool IsDebugging() const;
	// Expand fraction: 1 when debugging (full-width cover), 0 otherwise (marker). No hover width animation.
	float GetExpand() const;
	EVisibility GetTimeVisibility() const;     // time text only when full (debugging)

	TWeakObjectPtr<UEdGraphNode> OwnerNode;
	TAttribute<FMessageTag> TagAttr;
	bool bFillRow = false;
	int32 Role = 0;
	int32 CachedCount = 0;
	double CachedLatestTime = 0.0;
	double LastPollTime = 0.0;
	bool bPendingAnim = false;   // a trigger arrived while the animation was busy; play one more once it finishes (coalesced, not queued per-event)
	int32 AnimShownCount = 0;    // how many triggers have already been turned into a band (density mode), to detect newly arrived ones
	TArray<double> ActiveBandTimes;  // density mode: Slate-time start of each in-flight sweep band; multiple bands coexist to show trigger density

	FCurveSequence PulseSequence;
	FCurveHandle PulseCurve;
	FCurveSequence SweepSequence;
	FCurveHandle SweepCurve;
	TSharedPtr<class SVerticalBox> ToolTipContent;
};
