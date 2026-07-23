// Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraphPin.h"
#include "MessageTagContainer.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/SCompoundWidget.h"

struct FMessageParameter;
struct FMessageTagNode;
struct FPointerEvent;
class SBox;
class SToolTip;
class UEdGraphNode;

/** Resolved single pin row for the node preview. */
struct FTagPinRowInfo
{
	FName ParamName;
	FName RawType;
	FEdGraphPinType PinType;
	bool bResolved = false;
	EEdGraphPinDirection Direction = EGPD_Input;
};

/** Blueprint-node-like preview of a message tag's parameters and response types, used as a rich tooltip. */
class MESSAGETAGSEDITOR_API SMessageTagNodePreview : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMessageTagNodePreview)
		: _MaxWidth(360.0f)
		, _bInteractive(false)
		, _bSuppressReactiveRebuild(false)
		, _bExpandable(false)
	{}
		SLATE_ARGUMENT(FMessageTag, Tag)
		SLATE_ARGUMENT(float, MaxWidth)
		SLATE_ARGUMENT(bool, bInteractive)
		SLATE_ARGUMENT(TWeakObjectPtr<UEdGraphNode>, OwnerNode)
		SLATE_ARGUMENT(bool, bSuppressReactiveRebuild)
		SLATE_ARGUMENT(bool, bExpandable)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SMessageTagNodePreview();

private:
	static TArray<FTagPinRowInfo> ResolveParams(const TArray<FMessageParameter>& Params, EEdGraphPinDirection Direction);

	void RebuildContent();

	TSharedRef<SWidget> MakeTitleBar(const FMessageTag& InTag, bool bHasResponse) const;
	TSharedRef<SWidget> MakePinColumn(const TArray<FTagPinRowInfo>& Pins, bool bLeft) const;
	TSharedRef<SWidget> MakePinRow(const FTagPinRowInfo& Pin, bool bLeft) const;
	TSharedRef<SWidget> MakePinIcon(const FEdGraphPinType& PinType) const;
	TSharedRef<SWidget> MakeInfoFooter(const FString& SourceText, bool bIsExplicit, const FString& Comment) const;
	TSharedRef<SWidget> MakeReferences(const TArray<FString>& Locations) const;
	TSharedRef<SWidget> MakeChildrenSummary() const;
	TSharedRef<SWidget> MakeActionBar() const;

	FMessageTag PreviewTag;
	float MaxWidth = 360.0f;
	bool bInteractive = false;
	bool bSuppressReactiveRebuild = false;
	bool bExpandable = false;
	TWeakObjectPtr<UEdGraphNode> OwnerNode;
	TSharedPtr<SBox> ContentBox;
	FDelegateHandle TagTreeChangedHandle;
};

/** Builds a tooltip wrapping SMessageTagNodePreview; falls back to a plain text tooltip for invalid tags. OwnerNode marks the node currently being viewed so its own reference is shown non-clickable. */
MESSAGETAGSEDITOR_API TSharedRef<SToolTip> MakeMessageTagNodeToolTip(const FMessageTag& Tag, TWeakObjectPtr<UEdGraphNode> OwnerNode = nullptr, bool bExpandable = false);

/** Pops an interactive panel (clickable source links) anchored at the mouse position; closes on click-outside. */
MESSAGETAGSEDITOR_API void PushMessageTagInteractivePanel(TSharedRef<SWidget> Owner, const FPointerEvent& MouseEvent, const FMessageTag& Tag, TWeakObjectPtr<UEdGraphNode> OwnerNode = nullptr);
