// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagRuntimeBadge.h"

#include "MessageTagRuntimeInfo.h"
#include "Engine/Blueprint.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Math/TransformCalculus2D.h"
#include "Rendering/DrawElements.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MessageTagRuntimeBadge"

// 0 = coalesced single animation (bursts replay at most one "latest" band). 1 = density mode: every trigger inserts its own sweep band, so concurrent bands visualize trigger density.
static int32 GGMPBadgeAnimDensity = 0;
static FAutoConsoleVariableRef CVarGMPBadgeAnimDensity(
	TEXT("gmp.BadgeAnimDensity"),
	GGMPBadgeAnimDensity,
	TEXT("GMP message node badge animation: 0=coalesced (default), 1=one sweep band per trigger to show density."),
	ECVF_Default);

// One sweep band lasts this long (seconds) in density mode.
static const double GBadgeBandDuration = 0.7;

void SMessageTagRuntimeBadge::Construct(const FArguments& InArgs)
{
	OwnerNode = InArgs._OwnerNode;
	TagAttr = InArgs._Tag;
	bFillRow = InArgs._bFillRow;

#if WITH_EDITOR
	Role = MessageTagRuntimeInfo::GetNodeRole(OwnerNode.Get());
#endif

	PulseCurve = PulseSequence.AddCurve(0.f, 0.9f, ECurveEaseFunction::CubicOut);
	SweepCurve = SweepSequence.AddCurve(0.f, 0.7f, ECurveEaseFunction::Linear);

	// Fills the combo footprint (never larger -> never resizes the node). Background is drawn in OnPaint: collapsed = a small opaque marker at the right edge (combo shows through on the left); hover/pulse = an opaque bar covering the whole width. Content is right-aligned so the marker reads at the right.
	ChildSlot
	.HAlign(HAlign_Fill)
	.VAlign(VAlign_Fill)
	[
		SNew(SHorizontalBox)
		.Visibility(this, &SMessageTagRuntimeBadge::GetBadgeVisibility)
		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SSpacer)
		]
		// Latest time (only when expanded), left of the icon+count cluster.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(0, 0, 6, 0))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(this, &SMessageTagRuntimeBadge::GetBadgeColor)
			.Visibility(this, &SMessageTagRuntimeBadge::GetTimeVisibility)
			.Text_Lambda([this] {
				return CachedLatestTime > 0.0 ? FText::FromString(FString::Printf(TEXT("@%.2fs"), CachedLatestTime)) : FText::GetEmpty();
			})
		]
		// Icon (always shown).
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(this, &SMessageTagRuntimeBadge::GetBadgeColor)
			.Text(this, &SMessageTagRuntimeBadge::GetBadgeIcon)
		]
		// Count (always shown), right-most.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(FMargin(4, 0, 7, 0))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(this, &SMessageTagRuntimeBadge::GetBadgeColor)
			.Text(this, &SMessageTagRuntimeBadge::GetBadgeText)
		]
	];

	// Persistent interactive tooltip whose content list is refilled on data change (a lambda-returned tooltip does not reliably fire inside a graph pin).
	SAssignNew(ToolTipContent, SVerticalBox);
	SetToolTip(
		SNew(SToolTip)
		.IsInteractive(true)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.TextMargin(FMargin(0.0f))
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
			.Padding(FMargin(8, 6))
			[
				SNew(SBox)
				.MaxDesiredWidth(360.0f)
				[
					ToolTipContent.ToSharedRef()
				]
			]
		]);
	RebuildToolTipContent();
}

void SMessageTagRuntimeBadge::RebuildToolTipContent()
{
#if WITH_EDITOR
	if (ToolTipContent.IsValid())
	{
		MessageTagRuntimeInfo::FillRuntimeInfoList(ToolTipContent.ToSharedRef(), Role, TagAttr.Get());
	}
#endif
}

EVisibility SMessageTagRuntimeBadge::GetBadgeVisibility() const
{
#if WITH_EDITOR
	const bool bInPIE = GEditor && GEditor->PlayWorld != nullptr;
	if (bInPIE && Role != 0 && TagAttr.Get().IsValid())
	{
		return EVisibility::SelfHitTestInvisible;
	}
#endif
	return EVisibility::Collapsed;
}

FText SMessageTagRuntimeBadge::GetBadgeIcon() const
{
	// Notify sends outward, listen receives inward.
	return FText::FromString(Role == +1 ? TEXT(">>") : TEXT("<<"));
}

FText SMessageTagRuntimeBadge::GetBadgeText() const
{
	return FText::AsNumber(CachedCount);
}

bool SMessageTagRuntimeBadge::IsDebugging() const
{
	if (UEdGraphNode* Node = OwnerNode.Get())
	{
		if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node))
		{
			return BP->GetObjectBeingDebugged() != nullptr;
		}
	}
	return false;
}

float SMessageTagRuntimeBadge::GetExpand() const
{
	return IsDebugging() ? 1.0f : 0.0f;
}

EVisibility SMessageTagRuntimeBadge::GetTimeVisibility() const
{
	return IsDebugging() ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
}

FSlateColor SMessageTagRuntimeBadge::GetBadgeColor() const
{
	// Text settles to cyan; flashes brighter only while a pulse is actually playing.
	const FLinearColor Steady(0.55f, 0.82f, 1.0f);
	const FLinearColor Pulse(1.0f, 1.0f, 0.85f);
	const float PulseStrength = PulseSequence.IsPlaying() ? (1.0f - PulseCurve.GetLerp()) : 0.0f;
	return FSlateColor(FMath::Lerp(Steady, Pulse, PulseStrength));
}

FSlateColor SMessageTagRuntimeBadge::GetBadgeBorderColor() const
{
	// Opaque dark background; a direction-tinted flash only while a new-event pulse is actually playing (GetLerp()==0 when never played must read as steady, not peak).
	const FLinearColor Steady(0.05f, 0.07f, 0.11f, 1.0f);
	const float PulseStrength = PulseSequence.IsPlaying() ? (1.0f - PulseCurve.GetLerp()) : 0.0f;
	const FLinearColor Flash = (Role == +1) ? FLinearColor(0.85f, 0.45f, 0.08f, 1.0f) : FLinearColor(0.12f, 0.6f, 0.85f, 1.0f);
	return FSlateColor(FMath::Lerp(Steady, Flash, PulseStrength));
}

void SMessageTagRuntimeBadge::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

#if WITH_EDITOR
	if (Role == 0)
	{
		return;
	}

	const bool bDensity = GGMPBadgeAnimDensity != 0;

	// Expire finished density bands every frame (independent of the poll throttle).
	ActiveBandTimes.RemoveAll([InCurrentTime](double Start) { return InCurrentTime - Start > GBadgeBandDuration; });

	// Density bands are not driven by an FCurveSequence, so request repaint while any are in flight to keep them animating.
	if (ActiveBandTimes.Num() > 0)
	{
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	const bool bAnimBusy = PulseSequence.IsPlaying() || SweepSequence.IsPlaying();

	// Coalesced mode: once the current animation finishes, replay one more if triggers arrived while busy.
	if (!bDensity && !bAnimBusy && bPendingAnim)
	{
		bPendingAnim = false;
		PulseSequence.Play(AsShared());
		SweepSequence.Play(AsShared());
	}

	// Poll the hub at a throttled rate rather than every frame.
	if (InCurrentTime - LastPollTime < 0.3)
	{
		return;
	}
	LastPollTime = InCurrentTime;

	const FMessageTag InTag = TagAttr.Get();
	int32 NewCount = 0;
	double NewLatest = 0.0;
	if (GEditor && GEditor->PlayWorld && InTag.IsValid())
	{
		MessageTagRuntimeInfo::GetLatestActivity(Role, InTag.GetTagName(), NewCount, NewLatest);
	}

	if (NewCount != CachedCount || NewLatest > CachedLatestTime)
	{
		const bool bNewEvent = NewCount > CachedCount || NewLatest > CachedLatestTime;
		CachedCount = NewCount;
		CachedLatestTime = NewLatest;
		RebuildToolTipContent();

		if (bNewEvent)
		{
			PulseSequence.Play(AsShared());  // color flash both modes (restart is harmless)
			if (bDensity)
			{
				// One band per newly arrived trigger so concurrent bands show density; cap to avoid unbounded growth.
				const int32 NewTriggers = FMath::Clamp(NewCount - AnimShownCount, 0, 32);
				for (int32 i = 0; i < NewTriggers; ++i)
				{
					ActiveBandTimes.Add(InCurrentTime);
				}
			}
			else if (bAnimBusy)
			{
				bPendingAnim = true;
			}
			else
			{
				SweepSequence.Play(AsShared());
			}
		}
	}
	AnimShownCount = CachedCount;
#endif
}

int32 SMessageTagRuntimeBadge::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	int32 MaxLayer = LayerId;

	if (GetBadgeVisibility().IsVisible())
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const float W = Size.X;
		const float H = Size.Y;
		if (W > 1.f && H > 1.f)
		{
			// Background left edge interpolates: collapsed = a right-side marker (MarkerW), expanded = the full width. Right-anchored, grows leftward.
			const float Expand = GetExpand();
			const float MarkerW = FMath::Min(48.f, W);
			const float BgW = FMath::Lerp(MarkerW, W, Expand);
			const float BgX = W - BgW;

			const FSlateBrush* Box = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
			FSlateDrawElement::MakeBox(
				OutDrawElements,
				MaxLayer,
				AllottedGeometry.ToPaintGeometry(FVector2D(BgW, H), FSlateLayoutTransform(FVector2D(BgX, 0.f))),
				Box,
				ESlateDrawEffect::None,
				GetBadgeBorderColor().GetSpecifiedColor());
			++MaxLayer;
		}
	}

	// Paint the text content on top of the background.
	MaxLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, MaxLayer, InWidgetStyle, bParentEnabled);

	// Sweep bands on top, direction matching the icon (Notify >> left->right, Listen << right->left).
	{
		const FVector2D Size = AllottedGeometry.GetLocalSize();
		const float W = Size.X;
		const float H = Size.Y;
		if (W > 1.f && H > 1.f)
		{
			const FSlateBrush* Box = FCoreStyle::Get().GetBrush(TEXT("GenericWhiteBox"));
			const float BandW = 12.f;
			// Draws one band at progress T in [0,1].
			auto DrawBand = [&](float T) {
				const float CenterX = (Role == +1) ? FMath::Lerp(0.f, W, T) : FMath::Lerp(W, 0.f, T);
				const float X = FMath::Clamp(CenterX - BandW * 0.5f, 0.f, FMath::Max(0.f, W - BandW));
				const float Alpha = FMath::Sin(T * PI) * 0.85f;
				FSlateDrawElement::MakeBox(
					OutDrawElements,
					MaxLayer + 1,
					AllottedGeometry.ToPaintGeometry(FVector2D(BandW, H), FSlateLayoutTransform(FVector2D(X, 0.f))),
					Box,
					ESlateDrawEffect::None,
					FLinearColor(1.0f, 0.95f, 0.55f, Alpha));
			};

			if (GGMPBadgeAnimDensity != 0)
			{
				// Density mode: one band per active trigger, each at its own progress -> concurrent bands show density.
				const double Now = FSlateApplication::Get().GetCurrentTime();
				for (double Start : ActiveBandTimes)
				{
					const float T = (float)FMath::Clamp((Now - Start) / GBadgeBandDuration, 0.0, 1.0);
					DrawBand(T);
				}
				if (ActiveBandTimes.Num() > 0)
				{
					++MaxLayer;
				}
			}
			else if (SweepSequence.IsPlaying())
			{
				DrawBand(SweepCurve.GetLerp());
				++MaxLayer;
			}
		}
	}

	return MaxLayer;
}

#undef LOCTEXT_NAMESPACE
