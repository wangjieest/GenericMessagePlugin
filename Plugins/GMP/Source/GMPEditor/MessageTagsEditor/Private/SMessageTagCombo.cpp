// Copyright Epic Games, Inc. All Rights Reserved.

#include "SMessageTagCombo.h"
#include "DetailLayoutBuilder.h"
#include "SMessageTagPicker.h"
#include "MessageTagStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "HAL/PlatformApplicationMisc.h"
#include "MessageTagEditorUtilities.h"
#include "Widgets/Input/SComboButton.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"

#define LOCTEXT_NAMESPACE "MessageTagCombo"

//------------------------------------------------------------------------------
// SMessageTagCombo
//------------------------------------------------------------------------------

SLATE_IMPLEMENT_WIDGET(SMessageTagCombo)
void SMessageTagCombo::PrivateRegisterAttributes(FSlateAttributeInitializer& AttributeInitializer)
{
	SLATE_ADD_MEMBER_ATTRIBUTE_DEFINITION_WITH_NAME(AttributeInitializer, "Tag", TagAttribute, EInvalidateWidgetReason::Layout);
}

SMessageTagCombo::SMessageTagCombo()
	: TagAttribute(*this)
{
}

SMessageTagCombo::~SMessageTagCombo()
{
	if (bRegisteredForUndo)
	{
		GEditor->UnregisterForUndo(this);
	}
}

void SMessageTagCombo::Construct(const FArguments& InArgs)
{
	TagAttribute.Assign(*this, InArgs._Tag);
	Filter = InArgs._Filter;
	SettingsName = InArgs._SettingsName;
	bIsReadOnly = InArgs._ReadOnly;
	OnTagChanged = InArgs._OnTagChanged;
	PropertyHandle = InArgs._PropertyHandle;

	if (PropertyHandle.IsValid())
	{
		PropertyHandle->SetOnPropertyValueChanged(FSimpleDelegate::CreateSP(this, &SMessageTagCombo::RefreshTagsFromProperty));
		RefreshTagsFromProperty();
		GEditor->RegisterForUndo(this);
		bRegisteredForUndo = true;

		if (Filter.IsEmpty())
		{
			Filter = UMessageTagsManager::Get().GetCategoriesMetaFromPropertyHandle(PropertyHandle);
		}
		bIsReadOnly = PropertyHandle->IsEditConst();
	}
	
	ChildSlot
	[
		SNew(SHorizontalBox) // Extra box to make the combo hug the chip
						
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SAssignNew(ComboButton, SComboButton)
			.ComboButtonStyle(FMessageTagStyle::Get(), "MessageTags.ComboButton")
			.HasDownArrow(true)
			.ContentPadding(1)
			.IsEnabled(this, &SMessageTagCombo::IsValueEnabled)
			.Clipping(EWidgetClipping::OnDemand)
			.OnMenuOpenChanged(this, &SMessageTagCombo::OnMenuOpenChanged)
			.OnGetMenuContent(this, &SMessageTagCombo::OnGetMenuContent)
			.ButtonContent()
			[
				SNew(SMessageTagChip)
				.OnNavigate(InArgs._OnNavigate)
				.OnMenu(InArgs._OnMenu)
				.ShowClearButton(this, &SMessageTagCombo::ShowClearButton)
				.EnableNavigation(InArgs._EnableNavigation)
				.Text(this, &SMessageTagCombo::GetText)
				.ToolTipText(this, &SMessageTagCombo::GetToolTipText)
				.IsSelected(this, &SMessageTagCombo::IsSelected) 
				.OnClearPressed(this, &SMessageTagCombo::OnClearPressed)
				.OnEditPressed(this, &SMessageTagCombo::OnEditTag)
				.OnMenu(this, &SMessageTagCombo::OnTagMenu)
			]
		]
	];
}

bool SMessageTagCombo::IsValueEnabled() const
{
	if (PropertyHandle.IsValid())
	{
		return !PropertyHandle->IsEditConst();
	}

	return !bIsReadOnly;
}

void SMessageTagCombo::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshTagsFromProperty();
	}
}

void SMessageTagCombo::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		RefreshTagsFromProperty();
	}
}

FReply SMessageTagCombo::OnEditTag() const
{
	FReply Reply = FReply::Handled();
	if (ComboButton->ShouldOpenDueToClick())
	{
		ComboButton->SetIsOpen(true);
		if (TagPicker->GetWidgetToFocusOnOpen())
		{
			Reply.SetUserFocus(TagPicker->GetWidgetToFocusOnOpen().ToSharedRef());
		}
	}
	else
	{
		ComboButton->SetIsOpen(false);
	}
	
	return Reply;
}

bool SMessageTagCombo::ShowClearButton() const
{
	// Show clear button is we have multiple values, or the tag is other than None.
	if (PropertyHandle.IsValid())
	{
		if (bHasMultipleValues)
		{
			return true;
		}
		const FMessageTag MessageTag = TagsFromProperty.IsEmpty() ? FMessageTag() : TagsFromProperty[0]; 
		return MessageTag.IsValid();
	}
	const FMessageTag MessageTag = TagAttribute.Get();
	return MessageTag.IsValid();
}

FText SMessageTagCombo::GetText() const
{
	// Pass tag from the properties
	if (PropertyHandle.IsValid())
	{
		if (bHasMultipleValues)
		{
			return LOCTEXT("MessageTagCombo_MultipleValues", "Multiple Values");
		}
		const FMessageTag MessageTag = TagsFromProperty.IsEmpty() ? FMessageTag() : TagsFromProperty[0]; 
		return FText::FromName(MessageTag.GetTagName());
	}
	return FText::FromName(TagAttribute.Get().GetTagName());
}

FText SMessageTagCombo::GetToolTipText() const
{
	if (PropertyHandle.IsValid())
	{
		return TagsFromProperty.IsEmpty() ? FText::GetEmpty() : FText::FromName(TagsFromProperty[0].GetTagName());
	}
	return FText::FromName(TagAttribute.Get().GetTagName());
}

bool SMessageTagCombo::IsSelected() const
{
	// Show in selected state if we have one value and value is valid.
	if (PropertyHandle.IsValid())
	{
		if (bHasMultipleValues)
		{
			return false;
		}
		const FMessageTag MessageTag = TagsFromProperty.IsEmpty() ? FMessageTag() : TagsFromProperty[0]; 
		return MessageTag.IsValid();
	}
	const FMessageTag MessageTag = TagAttribute.Get();
	return MessageTag.IsValid();
}

FReply SMessageTagCombo::OnClearPressed()
{
	OnClearTag();
	return FReply::Handled();
}

void SMessageTagCombo::OnMenuOpenChanged(const bool bOpen) const
{
	if (bOpen && TagPicker.IsValid())
	{
		const FMessageTag TagToHilight = GetCommonTag();
		TagPicker->RequestScrollToView(TagToHilight);
							
		ComboButton->SetMenuContentWidgetToFocus(TagPicker->GetWidgetToFocusOnOpen());
	}
}

TSharedRef<SWidget> SMessageTagCombo::OnGetMenuContent()
{
	// If property is not set, well put the edited tag into a container and use that for picking.
	TArray<FMessageTagContainer> TagContainers;
	if (!PropertyHandle.IsValid())
	{
		const FMessageTag TagToEdit = TagAttribute.Get();
		TagContainers.Add(FMessageTagContainer(TagToEdit));
	}

	const bool bIsPickerReadOnly = !IsValueEnabled();
	
	TagPicker = SNew(SMessageTagPicker)
		.Filter(Filter)
		.SettingsName(SettingsName)
		.ReadOnly(bIsPickerReadOnly)
		.ShowMenuItems(true)
		.MaxHeight(350.0f)
		.MultiSelect(false)
		.OnTagChanged(this, &SMessageTagCombo::OnTagSelected)
		.Padding(2)
		.PropertyHandle(PropertyHandle)
		.TagContainers(TagContainers);

	if (TagPicker->GetWidgetToFocusOnOpen())
	{
		ComboButton->SetMenuContentWidgetToFocus(TagPicker->GetWidgetToFocusOnOpen());
	}

	return TagPicker.ToSharedRef();
}

void SMessageTagCombo::OnTagSelected(const TArray<FMessageTagContainer>& TagContainers)
{
	if (OnTagChanged.IsBound())
	{
		const FMessageTag NewTag = TagContainers.IsEmpty() ? FMessageTag() : TagContainers[0].First();
		OnTagChanged.Execute(NewTag);
	}
}

FMessageTag SMessageTagCombo::GetCommonTag() const
{
	if (PropertyHandle.IsValid())
	{
		return TagsFromProperty.IsEmpty() ? FMessageTag() : TagsFromProperty[0]; 
	}
	else
	{
		return TagAttribute.Get();
	}
}

FReply SMessageTagCombo::OnTagMenu(const FPointerEvent& MouseEvent)
{
	FMenuBuilder MenuBuilder(/*bShouldCloseWindowAfterMenuSelection=*/ true, /*CommandList=*/ nullptr);

	const FMessageTag MessageTag = GetCommonTag();
	
	auto IsValidTag = [MessageTag]()
	{
		return MessageTag.IsValid();		
	};

	MenuBuilder.AddMenuEntry(
		LOCTEXT("MessageTagCombo_SearchForReferences", "Search For References"),
		FText::Format(LOCTEXT("MessageTagCombo_SearchForReferencesTooltip", "Find references to the tag {0}"), FText::AsCultureInvariant(MessageTag.ToString())),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Search"),
		FUIAction(FExecuteAction::CreateLambda([MessageTag]()
		{
			// Single tag search
			const FName TagFName = MessageTag.GetTagName();
			if (FEditorDelegates::OnOpenReferenceViewer.IsBound() && !TagFName.IsNone())
			{
				TArray<FAssetIdentifier> AssetIdentifiers;
				AssetIdentifiers.Emplace(FMessageTag::StaticStruct(), TagFName);
				FEditorDelegates::OnOpenReferenceViewer.Broadcast(AssetIdentifiers, FReferenceViewerParams());
			}
		}))
		);

	MenuBuilder.AddSeparator();

	MenuBuilder.AddMenuEntry(
	NSLOCTEXT("PropertyView", "CopyProperty", "Copy"),
	FText::Format(LOCTEXT("MessageTagCombo_CopyTagTooltip", "Copy tag {0} to clipboard"), FText::AsCultureInvariant(MessageTag.ToString())),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Copy"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagCombo::OnCopyTag, MessageTag), FCanExecuteAction::CreateLambda(IsValidTag)));

	MenuBuilder.AddMenuEntry(
	NSLOCTEXT("PropertyView", "PasteProperty", "Paste"),
	LOCTEXT("MessageTagCombo_PasteTagTooltip", "Paste tags from clipboard."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "GenericCommands.Paste"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagCombo::OnPasteTag),FCanExecuteAction::CreateSP(this, &SMessageTagCombo::CanPaste)));

	MenuBuilder.AddMenuEntry(
	LOCTEXT("MessageTagCombo_ClearTag", "Clear Message Tag"),
		FText::GetEmpty(),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.X"),
		FUIAction(FExecuteAction::CreateSP(this, &SMessageTagCombo::OnClearTag), FCanExecuteAction::CreateLambda(IsValidTag)));

	// Spawn context menu
	FWidgetPath WidgetPath = MouseEvent.GetEventPath() != nullptr ? *MouseEvent.GetEventPath() : FWidgetPath();
	FSlateApplication::Get().PushMenu(AsShared(), WidgetPath, MenuBuilder.MakeWidget(), MouseEvent.GetScreenSpacePosition(), FPopupTransitionEffect(FPopupTransitionEffect::ContextMenu));

	return FReply::Handled();
}

void SMessageTagCombo::OnClearTag()
{
	if (PropertyHandle.IsValid())
	{
		FScopedTransaction Transaction(LOCTEXT("MessageTagCombo_ClearTag", "Clear Message Tag"));
		PropertyHandle->SetValueFromFormattedString(UE::MessageTags::EditorUtilities::MessageTagExportText(FMessageTag()));
	}
				
	OnTagChanged.ExecuteIfBound(FMessageTag());
}

void SMessageTagCombo::OnCopyTag(const FMessageTag TagToCopy) const
{
	// Copy tag as a plain string, MessageTag's import text can handle that.
	FPlatformApplicationMisc::ClipboardCopy(*TagToCopy.ToString());
}

void SMessageTagCombo::OnPasteTag()
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	const FMessageTag PastedTag = UE::MessageTags::EditorUtilities::MessageTagTryImportText(PastedText);
	
	if (PastedTag.IsValid())
	{
		if (PropertyHandle.IsValid())
		{
			FScopedTransaction Transaction(LOCTEXT("MessageTagCombo_PasteTag", "Paste Message Tag"));
			PropertyHandle->SetValueFromFormattedString(PastedText);
			RefreshTagsFromProperty();
		}
		
		OnTagChanged.ExecuteIfBound(PastedTag);
	}
}

bool SMessageTagCombo::CanPaste() const
{
	FString PastedText;
	FPlatformApplicationMisc::ClipboardPaste(PastedText);
	FMessageTag PastedTag = UE::MessageTags::EditorUtilities::MessageTagTryImportText(PastedText);

	return PastedTag.IsValid();
}

void SMessageTagCombo::RefreshTagsFromProperty()
{
	check(PropertyHandle.IsValid());

	bHasMultipleValues = false;
	TagsFromProperty.Reset();
	
	SMessageTagPicker::EnumerateEditableTagContainersFromPropertyHandle(PropertyHandle.ToSharedRef(), [this](const FMessageTagContainer& TagContainer)
	{
		const FMessageTag TagFromProperty = TagContainer.IsEmpty() ? FMessageTag() : TagContainer.First(); 
		if (TagsFromProperty.Num() > 0 && TagsFromProperty[0] != TagFromProperty)
		{
			bHasMultipleValues = true;
		}
		TagsFromProperty.Add(TagFromProperty);

		return true;
	});
}

#undef LOCTEXT_NAMESPACE
