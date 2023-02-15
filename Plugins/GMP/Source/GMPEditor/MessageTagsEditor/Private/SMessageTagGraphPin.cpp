// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagGraphPin.h"
#include "Widgets/Input/SComboButton.h"
#include "MessageTagsModule.h"
#include "Widgets/Layout/SScaleBox.h"
#include "UnrealCompatibility.h"
#include "MessageTagsManager.h"
#include "SGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableSet.h"
#include "K2Node_FunctionTerminator.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraph.h"

static FName NAME_Categories = FName("Categories");
static FString ExtractTagFilterStringFromGraphPin(UEdGraphPin* InTagPin)
{
	FString FilterString;

	if (ensure(InTagPin))
	{
		const UMessageTagsManager& TagManager = UMessageTagsManager::Get();
		if (UScriptStruct* PinStructType = Cast<UScriptStruct>(InTagPin->PinType.PinSubCategoryObject.Get()))
		{
			FilterString = TagManager.GetCategoriesMetaFromField(PinStructType);
		}

		UEdGraphNode* OwningNode = InTagPin->GetOwningNode();

		if (FilterString.IsEmpty())
		{
			FilterString = OwningNode->GetPinMetaData(InTagPin->PinName, NAME_Categories);
		}

		if (FilterString.IsEmpty())
		{
			if (const UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(OwningNode))
			{
				if (const UFunction* TargetFunction = CallFuncNode->GetTargetFunction())
				{
					FilterString = TagManager.GetCategoriesMetaFromFunction(TargetFunction, ToName(InTagPin->PinName));
				}
			}
			else if (const UK2Node_VariableSet* VariableSetNode = Cast<UK2Node_VariableSet>(OwningNode))
			{
				FilterString = TagManager.GetCategoriesMetaFromField(VariableSetNode->GetPropertyForVariable());
			}
			else if (const UK2Node_FunctionTerminator* FuncTermNode = Cast<UK2Node_FunctionTerminator>(OwningNode))
			{
				const UFunction* SignatureFunction = nullptr;
#if UE_4_23_OR_LATER
				{
					SignatureFunction = FuncTermNode->FindSignatureFunction();
				}
#else
				{
					UClass* FoundClass = FuncTermNode->GetBlueprintClassFromNode();
#if UE_4_20_OR_LATER
					SignatureFunction = FuncTermNode->FunctionReference.ResolveMember<UFunction>(FoundClass);
#else
					SignatureFunction = FindField<UFunction>(FuncTermNode->SignatureClass, FuncTermNode->SignatureName);
#endif

					if (!SignatureFunction && FoundClass && FuncTermNode->GetOuter())
					{
						// The resolve will fail if this is a locally-created function, so search using the event graph name
						SignatureFunction = FindUField<UFunction>(FoundClass, *FuncTermNode->GetOuter()->GetName());
					}
				}
#endif
				if (SignatureFunction)
					FilterString = TagManager.GetCategoriesMetaFromFunction(SignatureFunction, ToName(InTagPin->PinName));
			}
		}
	}

	return FilterString;
}

#define LOCTEXT_NAMESPACE "MessageTagGraphPin"

void SMessageTagGraphPin::Construct( const FArguments& InArgs, UEdGraphPin* InGraphPinObj )
{
	if(InArgs._TagContainer.IsValid())
		TagContainer = InArgs._TagContainer;
	else
		TagContainer = MakeShareable( new FMessageTagContainer() );
	bCloseWidget = InArgs._bRefresh;
	bUseRawName = InArgs._bRawName;
	LastName = *InGraphPinObj->GetDefaultAsString();
	SGraphPin::Construct( SGraphPin::FArguments(), InGraphPinObj );
}

TSharedRef<SWidget> SMessageTagGraphPin::GetLabelWidget(const FName& InPinLabelStyle)
{
	return SNullWidget::NullWidget;
}

TSharedRef<SWidget>	SMessageTagGraphPin::GetDefaultValueWidget()
{
	ParseDefaultValueData();

	//Create widget
	return SNew(SVerticalBox)
		   + SVerticalBox::Slot()
		   .AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.f)
				[
					SAssignNew(ComboButton, SComboButton)
					.OnGetMenuContent(this, &SMessageTagGraphPin::GetListContent)
					.ContentPadding(FMargin(2.0f, 2.0f))
					.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
					.MenuPlacement(MenuPlacement_BelowAnchor)
					.OnMenuOpenChanged(this, &SMessageTagGraphPin::OnMenuOpenChanged)
					.ButtonContent()
					[
						SNew(STextBlock)
						.Text(LOCTEXT("MessageTagWidget_Msg", "Msg:"))
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FGMPStyle::Get(), "Button")
					.OnClicked(this, &SMessageTagGraphPin::FindInAllBlueprints)
					.ContentPadding(0)
					.Visibility_Lambda([this] {
						auto MyTag = TagContainer->First();
						return MyTag.IsValid() ? EVisibility::Visible : EVisibility::Hidden;
					})
					.ToolTipText(LOCTEXT("FindInAllBlueprints", "FindInAllBlueprints"))
					[
						SNew(SImage)
						.Image(FGMPStyle::GetBrush(TEXT("EditorViewport.ScaleGridSnap")))
					]
				]
			]
		   + SVerticalBox::Slot()
		   .AutoHeight()
			[
				SelectedTags()
			];
}

FReply SMessageTagGraphPin::FindInAllBlueprints()
{
	auto MyTag = TagContainer->First();
	if (MyTag.IsValid())
	{
		extern void MesageTagsEditor_FindMessageInBlueprints(const FString& InStr, class UBlueprint* Blueprint = nullptr);
		MesageTagsEditor_FindMessageInBlueprints(FString::Printf(TEXT("('%s')"),*MyTag.GetTagName().ToString()));
	}
	return FReply::Handled();
}

void SMessageTagGraphPin::ParseDefaultValueData()
{
	FString TagString = GraphPinObj->GetDefaultAsString();

	FilterString = ExtractTagFilterStringFromGraphPin(GraphPinObj);

	if (TagString.StartsWith(TEXT("("), ESearchCase::CaseSensitive) && TagString.EndsWith(TEXT(")"), ESearchCase::CaseSensitive))
	{
		TagString = TagString.LeftChop(1);
		TagString = TagString.RightChop(1);
		TagString.Split(TEXT("="), nullptr, &TagString, ESearchCase::CaseSensitive);
		if (TagString.StartsWith(TEXT("\""), ESearchCase::CaseSensitive) && TagString.EndsWith(TEXT("\""), ESearchCase::CaseSensitive))
		{
			TagString = TagString.LeftChop(1);
			TagString = TagString.RightChop(1);
		}
	}

	if (!TagString.IsEmpty())
	{
		FMessageTag MessageTag = FMessageTag::RequestMessageTag(FName(*TagString), true);
		TagContainer->AddTag(MessageTag);
	}
}

TSharedRef<SWidget> SMessageTagGraphPin::GetListContent()
{
	EditableContainers.Empty();
	EditableContainers.Add( SMessageTagWidget::FEditableMessageTagContainerDatum( GraphPinObj->GetOwningNode(), TagContainer.Get() ) );

	TSharedRef<SWidget> Widget =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.MaxHeight(400)
		[
			SAssignNew(TagWidget, SMessageTagWidget, EditableContainers)
			.OnTagChanged(this, &SMessageTagGraphPin::RefreshTagList, true)
			.TagContainerName(TEXT("SMessageTagGraphPin"))
			.Visibility(this, &SGraphPin::GetDefaultValueVisibility)
			.MessageTagUIMode(EMessageTagUIMode::SelectionMode | EMessageTagUIMode::ExplicitSelMode)
			.MultiSelect(false)
			.Filter(FilterString)
			.bShowClearAll(false)
			.ScrollTo(LastName)
		];
	// ComboButton->SetMenuContentWidgetToFocus(TagWidget);
	// TagWidget->DeferredSetFcous();
	return Widget;
}

void SMessageTagGraphPin::OnMenuOpenChanged(bool bOpened)
{
	if(!bOpened)
		TagWidget.Reset();
}

TSharedRef<SWidget> SMessageTagGraphPin::SelectedTags()
{
	RefreshTagList();

	SAssignNew( TagListView, SListView<TSharedPtr<FString>> )
		.ListItemsSource(&TagNames)
		.SelectionMode(ESelectionMode::None)
		.OnGenerateRow(this, &SMessageTagGraphPin::OnGenerateRow);

	return TagListView->AsShared();
}

TSharedRef<ITableRow> SMessageTagGraphPin::OnGenerateRow(TSharedPtr<FString> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew( STableRow< TSharedPtr<FString> >, OwnerTable )
		[
			SNew(STextBlock) .Text( FText::FromString(*Item.Get()) )
		];
}

void SMessageTagGraphPin::RefreshTagList(bool bRefresh)
{	
	// Clear the list
	TagNames.Empty();

	// Add tags to list
	FString TagName;
	if (TagContainer.IsValid())
	{
		for (auto It = TagContainer->CreateConstIterator(); It; ++It)
		{
			TagName = It->ToString();
			TagNames.Add( MakeShareable( new FString( TagName ) ) );
		}
	}

	// Refresh the slate list
	if( TagListView.IsValid() )
	{
		TagListView->RequestListRefresh();
	}

	// Set Pin Data
	FString TagString;
	if (!TagName.IsEmpty())
	{
		if (bUseRawName)
		{
			TagString = TagName;
		}
		else
		{
			TagString = TEXT("(");
			TagString += TEXT("TagName=\"");
			TagString += TagName;
			TagString += TEXT("\"");
			TagString += TEXT(")");
		}
	}
	FString CurrentDefaultValue = GraphPinObj->GetDefaultAsString();
	if (CurrentDefaultValue.IsEmpty() || CurrentDefaultValue == TEXT("(TagName=\"\")"))
	{
		CurrentDefaultValue = FString(TEXT(""));
	}
	if (!CurrentDefaultValue.Equals(TagString))
	{
		GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, TagString);
		if (bCloseWidget && bRefresh)
			GraphPinObj->GetOwningNode()->GetGraph()->NotifyGraphChanged();
	}
}

#undef LOCTEXT_NAMESPACE
