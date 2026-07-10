// Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "SMessageTagNodePreview.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/AssetManager.h"
#include "Engine/Blueprint.h"
#include "Engine/StreamableManager.h"
#include "HAL/IConsoleManager.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "GMP/GMPMessageKey.h"
#include "GMP/GMPReflection.h"
#include "GMPNodeTagIndex.h"
#include "MessageTagsManager.h"
#include "MessageTagsModule.h"
#include "GraphEditorSettings.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Layout/WidgetPath.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "Styling/AppStyle.h"
#include "Styling/StyleDefaults.h"
#include "Widgets/Images/SLayeredImage.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MessageTagNodePreview"

namespace MessageTagNodePreview
{
const UEdGraphSchema_K2* GetK2Schema()
{
	return GetDefault<UEdGraphSchema_K2>();
}

FLinearColor GetTypeColor(const FEdGraphPinType& PinType)
{
	return GetK2Schema()->GetPinTypeColor(PinType);
}

// Selects the disconnected pin brush the same way SGraphPin::GetPinIcon does, so the preview matches real node pins.
const FSlateBrush* PickPinBrush(const FEdGraphPinType& PinType)
{
	if (PinType.IsArray())
	{
		return FAppStyle::GetBrush(TEXT("Graph.ArrayPin.Disconnected"));
	}
	if (GetK2Schema()->IsDelegateCategory(PinType.PinCategory))
	{
		return FAppStyle::GetBrush(TEXT("Graph.DelegatePin.Disconnected"));
	}
	if (PinType.bIsReference && !PinType.bIsConst)
	{
		return FAppStyle::GetBrush(TEXT("Graph.RefPin.Disconnected"));
	}
	if (PinType.IsSet())
	{
		return FAppStyle::GetBrush(TEXT("Kismet.VariableList.SetTypeIcon"));
	}
	if (PinType.IsMap())
	{
		return FAppStyle::GetBrush(TEXT("Kismet.VariableList.MapKeyTypeIcon"));
	}

	const bool bVarA = GetDefault<UGraphEditorSettings>()->DataPinStyle == BPST_VariantA;
	return FAppStyle::GetBrush(bVarA ? TEXT("Graph.Pin.Disconnected_VarA") : TEXT("Graph.Pin.Disconnected"));
}
}  // namespace MessageTagNodePreview

TArray<FTagPinRowInfo> SMessageTagNodePreview::ResolveParams(const TArray<FMessageParameter>& Params, EEdGraphPinDirection Direction)
{
	TArray<FTagPinRowInfo> Rows;
	Rows.Reserve(Params.Num());
	for (const FMessageParameter& Param : Params)
	{
		FTagPinRowInfo Row;
		Row.ParamName = Param.Name;
		Row.RawType = Param.Type;
		Row.Direction = Direction;
		Row.bResolved = GMP::Reflection::PinTypeFromString(Param.Type.ToString(), Row.PinType);
		if (!Row.bResolved)
		{
			Row.PinType = FEdGraphPinType();
			Row.PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		}
		Rows.Add(MoveTemp(Row));
	}
	return Rows;
}

TSharedRef<SWidget> SMessageTagNodePreview::MakePinIcon(const FEdGraphPinType& PinType) const
{
	using namespace MessageTagNodePreview;

	const FSlateBrush* PrimaryBrush = PickPinBrush(PinType);
	const FSlateColor PrimaryColor = FSlateColor(GetTypeColor(PinType));

	const FSlateBrush* SecondaryBrush = PinType.IsMap() ? FAppStyle::GetBrush(TEXT("Kismet.VariableList.MapValueTypeIcon")) : nullptr;
	const FSlateColor SecondaryColor = PinType.IsMap() ? FSlateColor(GetK2Schema()->GetSecondaryPinTypeColor(PinType)) : FSlateColor(FLinearColor::White);

	return SNew(SLayeredImage, TAttribute<const FSlateBrush*>(SecondaryBrush), TAttribute<FSlateColor>(SecondaryColor))
		.Image(PrimaryBrush)
		.ColorAndOpacity(PrimaryColor);
}

TSharedRef<SWidget> SMessageTagNodePreview::MakePinRow(const FTagPinRowInfo& Pin, bool bLeft) const
{
	using namespace MessageTagNodePreview;

	const FLinearColor TypeColor = GetTypeColor(Pin.PinType).Desaturate(0.15f);
	const FText NameText = FText::FromName(Pin.ParamName);
	const FText TypeText = Pin.bResolved ? UEdGraphSchema_K2::TypeToText(Pin.PinType) : FText::FromName(Pin.RawType);

	TSharedRef<SWidget> IconWidget = MakePinIcon(Pin.PinType);
	TSharedRef<SWidget> NameWidget = SNew(STextBlock)
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ColorAndOpacity(FLinearColor::White)
		.Text(NameText);
	TSharedRef<SWidget> TypeWidget = SNew(STextBlock)
		.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.NormalFont")))
		.ColorAndOpacity(FSlateColor(TypeColor))
		.Text(TypeText);

	TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
	if (bLeft)
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 5, 0))[IconWidget];
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))[NameWidget];
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center)[TypeWidget];
	}
	else
	{
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 8, 0))[TypeWidget];
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 5, 0))[NameWidget];
		Box->AddSlot().AutoWidth().VAlign(VAlign_Center)[IconWidget];
	}
	return Box;
}

TSharedRef<SWidget> SMessageTagNodePreview::MakePinColumn(const TArray<FTagPinRowInfo>& Pins, bool bLeft) const
{
	if (Pins.Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	const FText HeaderText = bLeft ? LOCTEXT("InputsHeader", "INPUTS") : LOCTEXT("OutputsHeader", "OUTPUTS");

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	Column->AddSlot()
		.AutoHeight()
		.HAlign(bLeft ? HAlign_Left : HAlign_Right)
		.Padding(FMargin(0, 0, 0, 3))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f))
			.Text(HeaderText)
		];

	for (const FTagPinRowInfo& Pin : Pins)
	{
		Column->AddSlot()
			.AutoHeight()
			.HAlign(bLeft ? HAlign_Left : HAlign_Right)
			.Padding(FMargin(0, 2))
			[
				MakePinRow(Pin, bLeft)
			];
	}
	return Column;
}

TSharedRef<SWidget> SMessageTagNodePreview::MakeTitleBar(const FMessageTag& InTag, bool bHasResponse) const
{
	const FText TitleText = InTag.IsValid() ? FText::FromString(InTag.ToString()) : LOCTEXT("UnknownTag", "(Unknown Tag)");
	const FName IconName = bHasResponse ? TEXT("Graph.Latent.LatentIcon") : TEXT("GraphEditor.Function_16x");
	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Graph.Node.ColorSpill")))
		.BorderBackgroundColor(FLinearColor(0.12f, 0.32f, 0.55f, 1.0f))
		.Padding(FMargin(8, 5))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0, 0, 6, 0))
			[
				SNew(SImage).Image(FAppStyle::GetBrush(IconName))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("BoldFont")))
				.ColorAndOpacity(FLinearColor::White)
				.Text(TitleText)
			]
		];
}

TSharedRef<SWidget> SMessageTagNodePreview::MakeInfoFooter(const FString& SourceText, bool bIsExplicit, const FString& Comment) const
{
	TSharedRef<SVerticalBox> Info = SNew(SVerticalBox);

	// One source per line: "Source: A.ini" then each further ini on its own aligned line. Each row jumps to that specific ini when interactive.
	TArray<FString> SourceNames;
	if (SourceText.IsEmpty())
	{
		SourceNames.Add(TEXT("Implicit"));
	}
	else
	{
		SourceText.ParseIntoArray(SourceNames, TEXT(", "), /*CullEmpty*/ true);
	}

	// Resolve each source name to its on-disk config path (for click-to-open); empty if not found.
	auto ResolveIniPath = [this](const FString& SourceName) -> FString
	{
		if (!PreviewTag.IsValid())
		{
			return FString();
		}
		const FMessageTagSource* Src = UMessageTagsManager::Get().FindTagSource(*SourceName);
		if (!Src)
		{
			return FString();
		}
		const FString Cfg = FPaths::ConvertRelativePathToFull(Src->GetConfigFileName());
		return (!Cfg.IsEmpty() && FPaths::FileExists(Cfg)) ? Cfg : FString();
	};

	for (int32 Index = 0; Index < SourceNames.Num(); ++Index)
	{
		const bool bFirst = Index == 0;
		const bool bLast = Index == SourceNames.Num() - 1;
		const FString Suffix = (bLast && !bIsExplicit) ? TEXT(" (Implicit)") : TEXT("");
		const FString RowText = bFirst
			? FString::Printf(TEXT("Source: %s%s"), *SourceNames[Index], *Suffix)
			: FString::Printf(TEXT("            %s%s"), *SourceNames[Index], *Suffix);
		const FString IniPath = ResolveIniPath(SourceNames[Index]);

		if (bInteractive && !IniPath.IsEmpty())
		{
			Info->AddSlot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
				.ContentPadding(FMargin(0))
				.ToolTipText(FText::FromString(IniPath))
				.OnClicked_Lambda([IniPath]() { FSourceCodeNavigation::OpenSourceFile(IniPath, 0, 0); return FReply::Handled(); })
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
					.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.65f, 1.0f)))
					.Text(FText::FromString(RowText))
				]
			];
		}
		else
		{
			Info->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
				.ColorAndOpacity(FLinearColor(1, 1, 1, 0.5f))
				.ToolTipText(IniPath.IsEmpty() ? FText::GetEmpty() : FText::FromString(IniPath))
				.Text(FText::FromString(RowText))
			];
		}
	}

	if (!Comment.IsEmpty())
	{
		Info->AddSlot()
			.AutoHeight()
			.Padding(FMargin(0, 3, 0, 0))
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.ItalicFont")))
				.ColorAndOpacity(FLinearColor(1, 1, 1, 0.7f))
				.AutoWrapText(true)
				.WrapTextAt(MaxWidth - 16.0f)
				.Text(FText::FromString(Comment))
			];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(8, 4))
		[
			Info
		];
}

namespace MessageTagNodePreview
{
// Clickable row without the ugly hyperlink underline: borderless hover button + blue-ish text + tooltip.
// bDismissOnClick: hover tooltips close on click (transient); the right-click panel stays open so multiple links can be followed.
void AddLinkRow(TSharedRef<SVerticalBox> List, const FString& Display, const FString& Tooltip, bool bClickable, FSimpleDelegate OnClick, bool bDismissOnClick)
{
	if (bClickable)
	{
		List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
			.ContentPadding(FMargin(0))
			.ToolTipText(FText::FromString(Tooltip))
			.OnClicked_Lambda([OnClick, bDismissOnClick]() { if (bDismissOnClick) { FSlateApplication::Get().CloseToolTip(); } OnClick.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.65f, 1.0f)))
				.Text(FText::FromString(Display))
			]
		];
	}
	else
	{
		List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
		[
			SNew(STextBlock)
			.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
			.ColorAndOpacity(FLinearColor(1, 1, 1, 0.6f))
			.ToolTipText(FText::FromString(Tooltip))
			.Text(FText::FromString(Display))
		];
	}
}

void AddCppLocRow(TSharedRef<SVerticalBox> List, const FString& Loc, bool bInteractive, bool bDismissOnClick)
{
	FString FilePath = Loc;
	int32 LineNumber = 0;
	bool bJumpable = false;
	int32 ColonIdx = INDEX_NONE;
	if (Loc.FindLastChar(TEXT(':'), ColonIdx) && ColonIdx > 0 && ColonIdx < Loc.Len() - 1 && Loc.Mid(ColonIdx + 1).IsNumeric())
	{
		FilePath = Loc.Left(ColonIdx);
		LineNumber = FCString::Atoi(*Loc.Mid(ColonIdx + 1));
		bJumpable = true;
	}

	// Lua chunks loaded via luaL_loadbuffer report their source as [string "<path>"]; unwrap to the bare path so the display and jump work.
	if (FilePath.StartsWith(TEXT("[string \"")) && FilePath.EndsWith(TEXT("\"]")))
	{
		FilePath = FilePath.Mid(9, FilePath.Len() - 11);
	}
	const FString Display = bJumpable ? FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(FilePath), LineNumber) : Loc;

	AddLinkRow(List, Display, Loc, /*bClickable*/ bInteractive && bJumpable,
		FSimpleDelegate::CreateLambda([FilePath, LineNumber]() { FSourceCodeNavigation::OpenSourceFile(FPaths::ConvertRelativePathToFull(FilePath), LineNumber, 0); }), bDismissOnClick);
}

void AddBpNodeRow(TSharedRef<SVerticalBox> List, UEdGraphNode* Node, bool bInteractive, const UEdGraphNode* OwnerNode, bool bDismissOnClick)
{
	UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	const bool bIsThisNode = Node && Node == OwnerNode;
	const FString AssetName = BP ? BP->GetName() : TEXT("Unknown");
	const FString AssetPath = BP ? BP->GetPathName() : FString();
	const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	const FString Display = bIsThisNode
		? FString::Printf(TEXT("%s  (this node)"), *AssetName)
		: AssetName;
	const FString Tooltip = FString::Printf(TEXT("%s\n%s"), *AssetPath, *NodeTitle);
	TWeakObjectPtr<UEdGraphNode> WeakNode(Node);
	AddLinkRow(List, Display, Tooltip, /*bClickable*/ bInteractive && !bIsThisNode,
		FSimpleDelegate::CreateLambda([WeakNode]() { FGMPNodeTagIndex::JumpToNode(WeakNode.Get()); }), bDismissOnClick);
}

// Lazy: async-load the referencing blueprint on click, then jump to its node matching the tag (panel may already be closed).
// Loading triggers each GMP node's PostLoad -> RegisterToTagIndex, so the index is populated by the time we read it here.
void LazyLoadAndJump(const FString& PackageName, FMessageTag Tag)
{
	// Close the popup panel first so it doesn't feel "stuck" while the async load runs.
	FSlateApplication::Get().DismissAllMenus();

	FSoftObjectPath Path(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName));
	UAssetManager::GetStreamableManager().RequestAsyncLoad(
		Path,
		FStreamableDelegate::CreateLambda([Path, Tag]()
		{
			UBlueprint* BP = Cast<UBlueprint>(Path.ResolveObject());
			if (!BP)
			{
				return;
			}
			TArray<UEdGraphNode*> Nodes;
			FGMPNodeTagIndex::Get().GetNodes(Tag.GetTagName(), /*bListen*/ true, Nodes);
			FGMPNodeTagIndex::Get().GetNodes(Tag.GetTagName(), /*bListen*/ false, Nodes);
			for (UEdGraphNode* Node : Nodes)
			{
				if (FBlueprintEditorUtils::FindBlueprintForNode(Node) == BP)
				{
					FGMPNodeTagIndex::JumpToNode(Node);
					return;
				}
			}
			if (GEditor)
			{
				GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(BP);
			}
		}),
		FStreamableManager::DefaultAsyncLoadPriority,
		/*bManageActiveHandle*/ true);
}

// During PIE the live in-memory trace is authoritative; outside PIE there is no running process, so fall back to the last run's ScriptHistory.ini on disk.
bool IsPlayInEditorActive()
{
	return GEditor && GEditor->PlayWorld != nullptr;
}

void ReadScriptHistoryFromDisk(FName MsgKey, TArray<FString>& OutListen, TArray<FString>& OutNotify)
{
	const FString HistoryPath = FPaths::ProjectSavedDir() / TEXT("GMP") / TEXT("ScriptHistory.ini");
	if (!FPaths::FileExists(HistoryPath))
	{
		return;
	}
	FConfigFile History;
	History.Read(HistoryPath);
	const FString Section = MsgKey.ToString();
	History.GetArray(*Section, TEXT("Listen"), OutListen);
	History.GetArray(*Section, TEXT("Notify"), OutNotify);
}

}  // namespace MessageTagNodePreview

TSharedRef<SWidget> SMessageTagNodePreview::MakeReferences(const TArray<FString>& Locations) const
{
	using namespace MessageTagNodePreview;

	TArray<FString> CppListen, CppNotify;
	TArray<UEdGraphNode*> BpListen, BpNotify;
	TSet<FName> IndexedPackages;
	if (PreviewTag.IsValid())
	{
		if (IsPlayInEditorActive())
		{
			GMP::GetMessageTagSourceLocationsTyped(PreviewTag.GetTagName(), CppListen, CppNotify);
		}
		else
		{
			ReadScriptHistoryFromDisk(PreviewTag.GetTagName(), CppListen, CppNotify);
		}
		FGMPNodeTagIndex::Get().GetNodes(PreviewTag.GetTagName(), /*bListen*/ true, BpListen);
		FGMPNodeTagIndex::Get().GetNodes(PreviewTag.GetTagName(), /*bListen*/ false, BpNotify);
		for (UEdGraphNode* Node : BpListen)
		{
			if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node))
			{
				IndexedPackages.Add(BP->GetOutermost()->GetFName());
			}
		}
		for (UEdGraphNode* Node : BpNotify)
		{
			if (UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node))
			{
				IndexedPackages.Add(BP->GetOutermost()->GetFName());
			}
		}
	}

	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);

	auto AddSection = [&](const FText& Header, const TArray<FString>& Cpp, const TArray<UEdGraphNode*>& Bp)
	{
		if (Cpp.Num() == 0 && Bp.Num() == 0)
		{
			return;
		}
		List->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 2))
		[
			SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f)).Text(Header)
		];
		for (const FString& Loc : Cpp)
		{
			AddCppLocRow(List, Loc, bInteractive, bSuppressReactiveRebuild);
		}
		for (UEdGraphNode* Node : Bp)
		{
			AddBpNodeRow(List, Node, bInteractive, OwnerNode.Get(), bSuppressReactiveRebuild);
		}
	};

	AddSection(LOCTEXT("ListenersHeader", "LISTENERS"), CppListen, BpListen);
	AddSection(LOCTEXT("NotifiersHeader", "NOTIFIERS"), CppNotify, BpNotify);

	// Fallback: untyped C++ locations (collected before direction was known).
	if (CppListen.Num() == 0 && CppNotify.Num() == 0 && BpListen.Num() == 0 && BpNotify.Num() == 0 && Locations.Num() > 0)
	{
		List->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 2))
		[
			SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f)).Text(LOCTEXT("ReferencesHeader", "REFERENCED IN"))
		];
		for (const FString& Loc : Locations)
		{
			AddCppLocRow(List, Loc, bInteractive, bSuppressReactiveRebuild);
		}
	}

	// All assets referencing this tag across the project (via AssetRegistry SearchableName, no loading). No auto-preload here — clicking a row lazily loads that single asset and jumps, avoiding the hover-time async churn that caused flicker.
	if (PreviewTag.IsValid())
	{
		TArray<FAssetIdentifier> Referencers;
		FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		ARModule.Get().GetReferencers(FAssetIdentifier(FMessageTag::StaticStruct(), PreviewTag.GetTagName()), Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);
		// Drop assets already shown as jump-to-node entries above (loaded blueprints in the node index).
		Referencers.RemoveAll([&IndexedPackages](const FAssetIdentifier& Ref) { return IndexedPackages.Contains(Ref.PackageName); });

		if (Referencers.Num() > 0)
		{
			List->AddSlot().AutoHeight().Padding(FMargin(0, 4, 0, 2))
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f)).Text(LOCTEXT("ReferencingAssetsHeader", "REFERENCING ASSETS"))
			];
			const FMessageTag TagForJump = PreviewTag;
			for (const FAssetIdentifier& Ref : Referencers)
			{
				const FString PackageName = Ref.PackageName.ToString();
				if (PackageName.IsEmpty())
				{
					continue;
				}
				const FString AssetLabel = FPackageName::GetShortName(PackageName);
				if (bInteractive)
				{
					AddLinkRow(List, AssetLabel, PackageName, /*bClickable*/ true,
						FSimpleDelegate::CreateLambda([PackageName, TagForJump]() { LazyLoadAndJump(PackageName, TagForJump); }), bSuppressReactiveRebuild);
				}
				else
				{
					List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
					[
						SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.6f)).ToolTipText(FText::FromString(PackageName)).Text(FText::FromString(AssetLabel))
					];
				}
			}
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(8, 4))
		[
			List
		];
}

TSharedRef<SWidget> SMessageTagNodePreview::MakeChildrenSummary() const
{
	const TSharedPtr<FMessageTagNode> Node = PreviewTag.IsValid() ? UMessageTagsManager::Get().FindTagNode(PreviewTag) : nullptr;
	if (!Node.IsValid() || Node->GetChildTagNodes().Num() == 0)
	{
		return SNullWidget::NullWidget;
	}

	const TArray<TSharedPtr<FMessageTagNode>>& Children = Node->GetChildTagNodes();
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	List->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 2))
	[
		SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f))
		.Text(FText::Format(LOCTEXT("SubTagsHeader", "SUB-TAGS ({0})"), FText::AsNumber(Children.Num())))
	];

	const int32 MaxRows = 12;
	const int32 ShownRows = FMath::Min(Children.Num(), MaxRows);
	for (int32 Index = 0; Index < ShownRows; ++Index)
	{
		const TSharedPtr<FMessageTagNode>& Child = Children[Index];
		if (!Child.IsValid())
		{
			continue;
		}
		const int32 GrandChildren = Child->GetChildTagNodes().Num();
		const int32 ParamNum = Child->Parameters.Num();
		const int32 RespNum = Child->ResponseTypes.Num();
		FString Suffix;
		if (GrandChildren > 0)
		{
			Suffix += FString::Printf(TEXT("  ▸ %d"), GrandChildren);
		}
		if (ParamNum > 0 && RespNum > 0)
		{
			Suffix += FString::Printf(TEXT("  (%d in / %d out)"), ParamNum, RespNum);
		}
		else if (ParamNum > 0)
		{
			Suffix += FString::Printf(TEXT("  (%d in)"), ParamNum);
		}
		else if (RespNum > 0)
		{
			Suffix += FString::Printf(TEXT("  (%d out)"), RespNum);
		}
		List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.75f))
				.Text(FText::FromName(Child->GetSimpleTagName()))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.35f))
				.Text(FText::FromString(Suffix))
			]
		];
	}
	if (Children.Num() > MaxRows)
	{
		List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
		[
			SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.ItalicFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.35f))
			.Text(FText::Format(LOCTEXT("MoreSubTags", "… (+{0} more)"), FText::AsNumber(Children.Num() - MaxRows)))
		];
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(8, 4))
		[
			List
		];
}

TSharedRef<SWidget> SMessageTagNodePreview::MakeActionBar() const
{
	const FMessageTag ActionTag = PreviewTag;

	auto MakeIconButton = [](const FName IconName, const FText& Tip, FSimpleDelegate OnClick) -> TSharedRef<SWidget>
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
			.ContentPadding(FMargin(4, 2))
			.ToolTipText(Tip)
			.OnClicked_Lambda([OnClick]() { OnClick.ExecuteIfBound(); return FReply::Handled(); })
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(16.0f)
				[
					SNew(SImage)
					.Image(FAppStyle::GetBrush(IconName))
					.ColorAndOpacity(FSlateColor::UseForeground())
				]
			];
	};

	TSharedRef<SHorizontalBox> Bar = SNew(SHorizontalBox);

	if (FEditorDelegates::OnOpenReferenceViewer.IsBound())
	{
		Bar->AddSlot().AutoWidth().Padding(FMargin(0, 0, 4, 0))
		[
			MakeIconButton(TEXT("Icons.Search"), LOCTEXT("ActionFindRefs", "Search For References"), FSimpleDelegate::CreateLambda([ActionTag]()
			{
				TArray<FAssetIdentifier> AssetIdentifiers;
				AssetIdentifiers.Add(FAssetIdentifier(FMessageTag::StaticStruct(), ActionTag.GetTagName()));
				FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
			}))
		];
	}

	Bar->AddSlot().AutoWidth().Padding(FMargin(0, 0, 4, 0))
	[
		MakeIconButton(TEXT("BlueprintEditor.FindInBlueprint"), LOCTEXT("ActionFindInBP", "Find In Blueprints"), FSimpleDelegate::CreateLambda([ActionTag]()
		{
			extern void MesageTagsEditor_FindMessageInBlueprints(const FString& MessageKey, class UBlueprint* Blueprint);
			MesageTagsEditor_FindMessageInBlueprints(ActionTag.ToString(), nullptr);
		}))
	];

	Bar->AddSlot().AutoWidth()
	[
		MakeIconButton(TEXT("GenericCommands.Copy"), LOCTEXT("ActionCopyName", "Copy Name to Clipboard"), FSimpleDelegate::CreateLambda([ActionTag]()
		{
			FPlatformApplicationMisc::ClipboardCopy(*ActionTag.ToString());
		}))
	];

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("ToolPanel.GroupBorder")))
		.Padding(FMargin(8, 4))
		[
			Bar
		];
}

void SMessageTagNodePreview::Construct(const FArguments& InArgs)
{
	MaxWidth = InArgs._MaxWidth;
	PreviewTag = InArgs._Tag;
	bInteractive = InArgs._bInteractive;
	OwnerNode = InArgs._OwnerNode;
	bSuppressReactiveRebuild = InArgs._bSuppressReactiveRebuild;

	ChildSlot
	[
		SNew(SBox)
		.MaxDesiredWidth(MaxWidth)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Graph.Node.Body")))
			.Padding(FMargin(1))
			[
				SAssignNew(ContentBox, SBox)
			]
		]
	];

	RebuildContent();

	// Event-driven rebuild: refresh when the tag tree changes (params/response/source/children fill in on tree rebuild/load). Hover cards opt out (bSuppressReactiveRebuild) to stay flicker-free — the initial build is enough for a transient tooltip.
	if (!bSuppressReactiveRebuild)
	{
		TagTreeChangedHandle = IMessageTagsModule::OnMessageTagTreeChanged.AddSP(this, &SMessageTagNodePreview::RebuildContent);
	}
}

SMessageTagNodePreview::~SMessageTagNodePreview()
{
	if (TagTreeChangedHandle.IsValid())
	{
		IMessageTagsModule::OnMessageTagTreeChanged.Remove(TagTreeChangedHandle);
	}
}

void SMessageTagNodePreview::RebuildContent()
{
	if (!ContentBox.IsValid())
	{
		return;
	}

	TSharedPtr<FMessageTagNode> Node = PreviewTag.IsValid() ? UMessageTagsManager::Get().FindTagNode(PreviewTag) : nullptr;

	TArray<FTagPinRowInfo> Inputs;
	TArray<FTagPinRowInfo> Outputs;
	FString DevComment;
	FString SourceText;
	bool bIsExplicit = true;
	if (Node.IsValid())
	{
		Inputs = ResolveParams(Node->Parameters, EGPD_Input);
		Outputs = ResolveParams(Node->ResponseTypes, EGPD_Output);
		DevComment = Node->GetDevComment();
		bIsExplicit = Node->IsExplicitTag();
#if WITH_EDITORONLY_DATA
		TArray<FString> SourceStrings;
		for (const FName& Source : Node->GetAllSourceNames())
		{
			SourceStrings.Add(Source.ToString());
		}
		SourceText = FString::Join(SourceStrings, TEXT(", "));
#endif
	}
	TArray<FString> Locations;
	if (PreviewTag.IsValid())
	{
		GMP::GetMessageTagSourceLocations(PreviewTag.GetTagName(), Locations);
	}

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	const bool bHasInputs = Inputs.Num() > 0;
	const bool bHasOutputs = Outputs.Num() > 0;
	const bool bHasChildren = Node.IsValid() && Node->GetChildTagNodes().Num() > 0;
	// An implicit tag only groups sub-tags (its own params, if any, belong conceptually to children); show sub-tags alone, no header/footer.
	const bool bShowHeader = bIsExplicit || !bHasChildren;

	if (bShowHeader)
	{
		Root->AddSlot().AutoHeight()[MakeTitleBar(PreviewTag, Outputs.Num() > 0)];
		Root->AddSlot().AutoHeight()[SNew(SSeparator).Thickness(1.0f)];

		if (!bHasInputs && !bHasOutputs)
		{
			Root->AddSlot()
				.AutoHeight()
				.Padding(FMargin(8, 6))
				[
					SNew(STextBlock)
					.Font(FAppStyle::GetFontStyle(TEXT("PropertyWindow.ItalicFont")))
					.ColorAndOpacity(FLinearColor(1, 1, 1, 0.35f))
					.Text(LOCTEXT("NoParams", "(no parameters)"))
				];
		}
		else
		{
			Root->AddSlot()
				.AutoHeight()
				.Padding(FMargin(8, 6))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Left).VAlign(VAlign_Top)
					[
						MakePinColumn(Inputs, /*bLeft*/ true)
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f)
					[
						SNew(SBox).MinDesiredWidth((bHasInputs && bHasOutputs) ? 24.0f : 0.0f)
					]
					+ SHorizontalBox::Slot().AutoWidth().HAlign(HAlign_Right).VAlign(VAlign_Top)
					[
						MakePinColumn(Outputs, /*bLeft*/ false)
					]
				];
		}

		Root->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 0))[MakeInfoFooter(SourceText, bIsExplicit, DevComment)];
	}

	if (Node.IsValid() && Node->GetChildTagNodes().Num() > 0)
	{
		Root->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 0))[MakeChildrenSummary()];
	}

	bool bHasReferences = Locations.Num() > 0;
	if (!bHasReferences && PreviewTag.IsValid())
	{
		TArray<UEdGraphNode*> Tmp;
		FGMPNodeTagIndex::Get().GetNodes(PreviewTag.GetTagName(), true, Tmp);
		FGMPNodeTagIndex::Get().GetNodes(PreviewTag.GetTagName(), false, Tmp);
		bHasReferences = Tmp.Num() > 0;
		if (!bHasReferences)
		{
			TArray<FAssetIdentifier> Referencers;
			FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			ARModule.Get().GetReferencers(FAssetIdentifier(FMessageTag::StaticStruct(), PreviewTag.GetTagName()), Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);
			bHasReferences = Referencers.Num() > 0;
		}
	}
	if (bHasReferences)
	{
		Root->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 0))[MakeReferences(Locations)];
	}

	// Skip the per-message action bar on implicit namespace parents (same predicate as the header): those actions target a concrete message.
	if (bInteractive && bShowHeader && PreviewTag.IsValid())
	{
		Root->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 0))[MakeActionBar()];
	}

	ContentBox->SetContent(Root);
}

TSharedRef<SToolTip> MakeMessageTagNodeToolTip(const FMessageTag& Tag, TWeakObjectPtr<UEdGraphNode> OwnerNode)
{
	if (!Tag.IsValid())
	{
		return SNew(SToolTip)
		[
			SNew(STextBlock).Text(NSLOCTEXT("MessageTagNodePreview", "NoTag", "(No Tag)"))
		];
	}

	// Interactive tooltip: the hover card stays put and the mouse can move into it to click the reference links, dismissing when the mouse leaves. Avoids needing the right-click pinned panel.
	return SNew(SToolTip)
		.IsInteractive(true)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.TextMargin(FMargin(0.0f))
		[
			SNew(SMessageTagNodePreview)
			.Tag(Tag)
			.bInteractive(true)
			.bSuppressReactiveRebuild(true)
			.OwnerNode(OwnerNode)
		];
}

void PushMessageTagInteractivePanel(TSharedRef<SWidget> Owner, const FPointerEvent& MouseEvent, const FMessageTag& Tag, TWeakObjectPtr<UEdGraphNode> OwnerNode)
{
	if (!Tag.IsValid())
	{
		return;
	}

	// Implicit namespace parents (no message of their own, only sub-tags) have nothing actionable in the panel; skip it.
	if (const TSharedPtr<FMessageTagNode> Node = UMessageTagsManager::Get().FindTagNode(Tag))
	{
		if (!Node->IsExplicitTag() && Node->GetChildTagNodes().Num() > 0)
		{
			return;
		}
	}

	// Right-click reuses the same interactive hover tooltip instead of a separate menu, so both paths behave identically (one card, mouse-in stays open, move-out closes).
	FSlateApplication::Get().SpawnToolTip(MakeMessageTagNodeToolTip(Tag, OwnerNode), MouseEvent.GetScreenSpacePosition());
}

#undef LOCTEXT_NAMESPACE
