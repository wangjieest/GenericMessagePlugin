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
#include "GraphEditorSettings.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Editor.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Layout/WidgetPath.h"
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

static bool GGMPPreloadTagReferences = true;
static FAutoConsoleVariableRef CVarGMPPreloadTagReferences(
	TEXT("gmp.PreloadTagReferences"),
	GGMPPreloadTagReferences,
	TEXT("When the tag preview panel is shown, async-preload the referencing (unopened) blueprints so their nodes upgrade to jumpable entries automatically."),
	ECVF_Default);

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

	const FString SourceLabel = SourceText.IsEmpty() ? FString(TEXT("Implicit")) : SourceText;
	const FText SourceLine = FText::FromString(FString::Printf(TEXT("Source: %s%s"), *SourceLabel, bIsExplicit ? TEXT("") : TEXT(" (Implicit)")));

	FString SourceIniPath;
	if (PreviewTag.IsValid())
	{
		if (const TSharedPtr<FMessageTagNode> Node = UMessageTagsManager::Get().FindTagNode(PreviewTag))
		{
			for (const FName& SourceName : Node->GetAllSourceNames())
			{
				if (const FMessageTagSource* Src = UMessageTagsManager::Get().FindTagSource(SourceName))
				{
					const FString Cfg = FPaths::ConvertRelativePathToFull(Src->GetConfigFileName());
					if (!Cfg.IsEmpty() && FPaths::FileExists(Cfg))
					{
						SourceIniPath = Cfg;
						break;
					}
				}
			}
		}
	}

	if (bInteractive && !SourceIniPath.IsEmpty())
	{
		Info->AddSlot().AutoHeight()
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
			.ContentPadding(FMargin(0))
			.ToolTipText(FText::FromString(SourceIniPath))
			.OnClicked_Lambda([SourceIniPath]() { FSourceCodeNavigation::OpenSourceFile(SourceIniPath, 0, 0); return FReply::Handled(); })
			[
				SNew(STextBlock)
				.Font(FAppStyle::GetFontStyle(TEXT("SmallFont")))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.4f, 0.65f, 1.0f)))
				.Text(SourceLine)
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
			.Text(SourceLine)
		];
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
void AddLinkRow(TSharedRef<SVerticalBox> List, const FString& Display, const FString& Tooltip, bool bClickable, FSimpleDelegate OnClick)
{
	if (bClickable)
	{
		List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
			.ContentPadding(FMargin(0))
			.ToolTipText(FText::FromString(Tooltip))
			.OnClicked_Lambda([OnClick]() { OnClick.ExecuteIfBound(); return FReply::Handled(); })
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

void AddCppLocRow(TSharedRef<SVerticalBox> List, const FString& Loc, bool bInteractive)
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
	const FString Display = bJumpable ? FString::Printf(TEXT("%s:%d"), *FPaths::GetCleanFilename(FilePath), LineNumber) : Loc;

	AddLinkRow(List, Display, Loc, /*bClickable*/ bInteractive && bJumpable,
		FSimpleDelegate::CreateLambda([FilePath, LineNumber]() { FSourceCodeNavigation::OpenSourceFile(FPaths::ConvertRelativePathToFull(FilePath), LineNumber, 0); }));
}

void AddBpNodeRow(TSharedRef<SVerticalBox> List, UEdGraphNode* Node, bool bInteractive)
{
	UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(Node);
	const FString AssetName = BP ? BP->GetName() : TEXT("Unknown");
	const FString AssetPath = BP ? BP->GetPathName() : FString();
	const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
	const FString Display = FString::Printf(TEXT("%s  -  %s"), *AssetName, *NodeTitle);
	const FString Tooltip = FString::Printf(TEXT("%s\n%s"), *AssetPath, *NodeTitle);
	TWeakObjectPtr<UEdGraphNode> WeakNode(Node);
	AddLinkRow(List, Display, Tooltip, /*bClickable*/ bInteractive,
		FSimpleDelegate::CreateLambda([WeakNode]() { FGMPNodeTagIndex::JumpToNode(WeakNode.Get()); }));
}

// Lazy: async-load the referencing blueprint, then jump to its node matching the tag (panel may already be closed).
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

}  // namespace MessageTagNodePreview

TSharedRef<SWidget> SMessageTagNodePreview::MakeReferences(const TArray<FString>& Locations) const
{
	using namespace MessageTagNodePreview;

	TArray<FString> CppListen, CppNotify;
	TArray<UEdGraphNode*> BpListen, BpNotify;
	TSet<FName> IndexedPackages;
	if (PreviewTag.IsValid())
	{
		GMP::GetMessageTagSourceLocationsTyped(PreviewTag.GetTagName(), CppListen, CppNotify);
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
			AddCppLocRow(List, Loc, bInteractive);
		}
		for (UEdGraphNode* Node : Bp)
		{
			AddBpNodeRow(List, Node, bInteractive);
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
			AddCppLocRow(List, Loc, bInteractive);
		}
	}

	// All assets referencing this tag across the project (works without loading them, via AssetRegistry SearchableName).
	if (PreviewTag.IsValid())
	{
		TArray<FAssetIdentifier> Referencers;
		FAssetRegistryModule& ARModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		ARModule.Get().GetReferencers(FAssetIdentifier(FMessageTag::StaticStruct(), PreviewTag.GetTagName()), Referencers, UE::AssetRegistry::EDependencyCategory::SearchableName);
		// Drop assets already shown as jump-to-node entries above (loaded blueprints in the node index).
		Referencers.RemoveAll([&IndexedPackages](const FAssetIdentifier& Ref) { return IndexedPackages.Contains(Ref.PackageName); });

		// Optional preload: when the panel is shown, async-load unopened referencers so their nodes upgrade to jumpable entries (index change triggers a Tick rebuild). RequestAsyncLoad is idempotent, no dedup needed.
		if (GGMPPreloadTagReferences && bInteractive)
		{
			for (const FAssetIdentifier& Ref : Referencers)
			{
				const FString PackageName = Ref.PackageName.ToString();
				if (!PackageName.IsEmpty())
				{
					UAssetManager::GetStreamableManager().RequestAsyncLoad(
						FSoftObjectPath(PackageName + TEXT(".") + FPackageName::GetShortName(PackageName)),
						FStreamableDelegate(), FStreamableManager::DefaultAsyncLoadPriority, /*bManageActiveHandle*/ true);
				}
			}
		}

		if (Referencers.Num() > 0)
		{
			List->AddSlot().AutoHeight().Padding(FMargin(0, 4, 0, 2))
			[
				SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.4f)).Text(LOCTEXT("ReferencingAssetsHeader", "REFERENCING ASSETS (click to load & jump)"))
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
					AddLinkRow(List, AssetLabel, PackageName + TEXT(" (load & jump to node)"), /*bClickable*/ true,
						FSimpleDelegate::CreateLambda([PackageName, TagForJump]() { LazyLoadAndJump(PackageName, TagForJump); }));
				}
				else
				{
					List->AddSlot().AutoHeight().Padding(FMargin(12, 1, 0, 1))
					[
						SNew(STextBlock).Font(FAppStyle::GetFontStyle(TEXT("SmallFont"))).ColorAndOpacity(FLinearColor(1, 1, 1, 0.6f)).Text(FText::FromString(AssetLabel))
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
}

void SMessageTagNodePreview::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);

	// Message tag parameters/response types/source locations are filled in lazily, so watch the counts and rebuild when they change.
	if (PreviewTag.IsValid())
	{
		const TSharedPtr<FMessageTagNode> Node = UMessageTagsManager::Get().FindTagNode(PreviewTag);
		const int32 ParamCount = Node.IsValid() ? Node->Parameters.Num() : 0;
		const int32 ResponseCount = Node.IsValid() ? Node->ResponseTypes.Num() : 0;
		TArray<FString> Locations;
		GMP::GetMessageTagSourceLocations(PreviewTag.GetTagName(), Locations);
		const uint32 IndexChange = FGMPNodeTagIndex::Get().GetChangeCount();
		if (ParamCount != LastParamCount || ResponseCount != LastResponseCount || Locations.Num() != LastLocationCount || IndexChange != LastIndexChangeCount)
		{
			RebuildContent();
		}
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

	LastIndexChangeCount = FGMPNodeTagIndex::Get().GetChangeCount();
	LastParamCount = Inputs.Num();
	LastResponseCount = Outputs.Num();
	LastLocationCount = Locations.Num();

	TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);

	Root->AddSlot().AutoHeight()[MakeTitleBar(PreviewTag, Outputs.Num() > 0)];
	Root->AddSlot().AutoHeight()[SNew(SSeparator).Thickness(1.0f)];

	const bool bHasInputs = Inputs.Num() > 0;
	const bool bHasOutputs = Outputs.Num() > 0;

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

	if (bInteractive && PreviewTag.IsValid())
	{
		Root->AddSlot().AutoHeight().Padding(FMargin(0, 2, 0, 0))[MakeActionBar()];
	}

	ContentBox->SetContent(Root);
}

TSharedRef<SToolTip> MakeMessageTagNodeToolTip(const FMessageTag& Tag)
{
	if (!Tag.IsValid())
	{
		return SNew(SToolTip)
		[
			SNew(STextBlock).Text(NSLOCTEXT("MessageTagNodePreview", "NoTag", "(No Tag)"))
		];
	}

	return SNew(SToolTip)
		.BorderImage(FStyleDefaults::GetNoBrush())
		.TextMargin(FMargin(0.0f))
		[
			SNew(SMessageTagNodePreview)
			.Tag(Tag)
		];
}

void PushMessageTagInteractivePanel(TSharedRef<SWidget> Owner, const FPointerEvent& MouseEvent, const FMessageTag& Tag)
{
	if (!Tag.IsValid())
	{
		return;
	}

	TSharedRef<SWidget> Panel = SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush(TEXT("Menu.Background")))
		.Padding(FMargin(2))
		[
			SNew(SMessageTagNodePreview)
			.Tag(Tag)
			.bInteractive(true)
		];

	const FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(Owner, WidgetPath, Panel, MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));
}

#undef LOCTEXT_NAMESPACE
