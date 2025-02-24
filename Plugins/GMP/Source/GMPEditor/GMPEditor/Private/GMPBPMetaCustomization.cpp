//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPBPMetaCustomization.h"
#include "BlueprintEditorModule.h"
#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SMultiLineEditableTextBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Widgets/Input/SCheckBox.h"
#include "Framework/Application/SlateApplication.h"

#define LOCTEXT_NAMESPACE "GMPBPMetaCustomization"

TSharedPtr<IDetailCustomization> FGMPBPMetaCustomization::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
{
	do
	{
		if (!InBlueprintEditor.IsValid())
			break;

		const TArray<UObject*>* Objects = InBlueprintEditor->GetObjectsCurrentlyBeingEdited();
		if (!Objects || Objects->Num() != 1)
			break;

		UBlueprint* Blueprint = Cast<UBlueprint>((*Objects)[0]);
		if (!Blueprint)
			break;
		return MakeShareable(new FGMPBPMetaCustomization(InBlueprintEditor, Blueprint));
	} while (false);
	return nullptr;
}

void FGMPBPMetaCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	static const TArray<FName> GMPDefaultMeta = {
		TEXT("AbilityTrigger"),
		TEXT("NeuronAction"),
		TEXT("NeuronNode"),
	};

	static const TSet<FName> ReadOnlyKeys = []() {
		TSet<FName> Ret{
			TEXT("Tooltip"),
			TEXT("MakeEditWidget"),
			TEXT("BlueprintPrivate"),
			TEXT("Bitmask"),
			TEXT("BitmaskEnum"),
			TEXT("MultiLine"),
			TEXT("ExposeOnSpawn"),
			TEXT("DeprecationMessage"),
		};
		Ret.Append(GMPDefaultMeta);
		return Ret;
	}();

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() == 0)
		return;

	UPropertyWrapper* PropertyWrapper = Cast<UPropertyWrapper>(ObjectsBeingCustomized[0].Get());
	const TWeakFieldPtr<FProperty> PropertyBeingCustomized = PropertyWrapper ? PropertyWrapper->GetProperty() : nullptr;
	if (!PropertyBeingCustomized.IsValid())
		return;

	const FName VarName = PropertyBeingCustomized->GetFName();
	const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(BlueprintPtr.Get(), VarName);
	if (VarIndex == INDEX_NONE)
		return;

	if (PropertyBeingCustomized->IsA<FMulticastDelegateProperty>())
	{
		FName MetaKey = TEXT("ExposeOnSpawn");
		DetailLayout
		.EditCategory(TEXT("Variable"))
		.AddCustomRow(LOCTEXT("GMPMetadataControl", "GMPMetadata"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(NSLOCTEXT("BlueprintDetailsCustomization", "VariableExposeToSpawnLabel", "Expose on Spawn"))
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(TAttribute<ECheckBoxState>::Create(CreateWeakLambda(this,
																		   [this, VarName, MetaKey]() -> ECheckBoxState {
																			   FString VarHolder;
																			   if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetaKey, VarHolder) || VarHolder != TEXT("True"))
																			   {
																				   return ECheckBoxState::Unchecked;
																			   }
																			   return ECheckBoxState::Checked;
																		   })))
			.OnCheckStateChanged(CreateWeakLambda(this,
												  [this, VarName, MetaKey](ECheckBoxState State) {
													  if (State == ECheckBoxState::Checked)
													  {
														  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetaKey, State == ECheckBoxState::Checked ? TEXT("True") : TEXT("False"));
													  }
													  else
													  {
														  FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetaKey);
													  }
												  }))
		];
	}

	for (const auto& MetaKey : GMPDefaultMeta)
	{
		DetailLayout
		.EditCategory(TEXT("Variable"))
		.AddCustomRow(LOCTEXT("GMPMetadataControl", "GMPMetadata"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(FText::FromName(MetaKey))
			.Font(IDetailLayoutBuilder::GetDetailFontBold())
		]
		.ValueContent()
		[
			SNew(SCheckBox)
			.IsChecked(TAttribute<ECheckBoxState>::Create(CreateWeakLambda(this,
																		   [this, VarName, MetaKey]() -> ECheckBoxState {
																			   FString VarHolder;
																			   if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetaKey, VarHolder) || VarHolder != TEXT("True"))
																			   {
																				   return ECheckBoxState::Unchecked;
																			   }
																			   return ECheckBoxState::Checked;
																		   })))
			.OnCheckStateChanged(CreateWeakLambda(this,
												  [this, VarName, MetaKey](ECheckBoxState State) {
													  if (State == ECheckBoxState::Checked)
													  {
														  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetaKey, State == ECheckBoxState::Checked ? TEXT("True") : TEXT("False"));
													  }
													  else
													  {
														  FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetaKey);
													  }
												  }))
		];
	}

	bool bCustomMetaEdit = false;
	if (!bCustomMetaEdit)
	{
		return;
	}

	{
		DetailLayout
			.EditCategory("Variable")
		.AddCustomRow(LOCTEXT("VariableMetadataControl", "Metadata Control"), true)
		.NameContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			[
				SNew(STextBlock)
				.ToolTip(FSlateApplicationBase::Get().MakeToolTip(LOCTEXT("VariableMetadataInfo", "Describes the variables internal metadata")))
				.Text(LOCTEXT("VariableMetadataHeader", "Metadata"))
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Fill)
			[
				PropertyCustomizationHelpers::MakeAddButton(CreateWeakLambda(this,
																			 [this, VarName] {
																				 int32 CurIndex = 1;
																				 FString VarHolder;
																				 while (FBlueprintEditorUtils::GetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, *FString::Printf(TEXT("GMPMeta_%d"), CurIndex), VarHolder))
																					 ++CurIndex;
																				 FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, *FString::Printf(TEXT("GMPMeta_%d"), CurIndex), TEXT(""));
																			 }),
															LOCTEXT("VariableMetadataAdd", "Add"))
			]
		];

		for (const FBPVariableMetaDataEntry& MetaEntry : BlueprintPtr->NewVariables[VarIndex].MetaDataArray)
		{
			bool bReadOnly = ReadOnlyKeys.Contains(MetaEntry.DataKey);

			DetailLayout
				.EditCategory("Variable")
			.AddCustomRow(LOCTEXT("VariableMetadataEntry", "Metadata Entry"), true)
			.NameContent()
			[
				SNew(SEditableTextBox)
				.Text(FText::FromName(MetaEntry.DataKey))
				.Font(DetailLayout.GetDetailFont())
				.SelectAllTextWhenFocused(true)
				.ClearKeyboardFocusOnCommit(false)
				.OnTextCommitted(CreateWeakLambda(this,
												  [this, VarName, KeyName{MetaEntry.DataKey}](const FText& NewKeyName, ETextCommit::Type ChangeType) {
													  FString VarHolder;
													  FBlueprintEditorUtils::GetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, KeyName, VarHolder);
													  FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, KeyName);
													  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, *NewKeyName.ToString(), VarHolder);
												  }))
				.IsReadOnly(bReadOnly)
				.SelectAllTextOnCommit(true)
				.IsPassword(false)
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SMultiLineEditableTextBox)
					.Text(FText::FromString(MetaEntry.DataValue))
					.Font(DetailLayout.GetDetailFont())
					.SelectAllTextWhenFocused(false)
					.ClearKeyboardFocusOnCommit(false)
					.OnTextCommitted(CreateWeakLambda(this,
													  [this, VarName, KeyName{MetaEntry.DataKey}](const FText& NewName, ETextCommit::Type ChangeType) {
														  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, KeyName, NewName.ToString());
													  }))
					.SelectAllTextOnCommit(false)
					.IsReadOnly(bReadOnly)
					.ModiferKeyForNewLine(EModifierKey::Shift)
					.WrapTextAt(200.0f)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Fill)
				[
					PropertyCustomizationHelpers::MakeRemoveButton(
						CreateWeakLambda(this, [this, VarName, MetadataKey{MetaEntry.DataKey}] { FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BlueprintPtr.Get(), VarName, nullptr, MetadataKey); }),
						LOCTEXT("VariableMetadataRemove", "Remove"),
						!bReadOnly)
				]
			];
		}
	}
}
#undef LOCTEXT_NAMESPACE

#include "Misc/DelayedAutoRegister.h"
static FDelayedAutoRegisterHelper AutoRegister_GMPBPMetaCustomization(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
	TSharedRef<FDelegateHandle> MetaCustomizationHandle = MakeShared<FDelegateHandle>();
	GMP::FGMPModuleUtils::OnModuleLifetime<FBlueprintEditorModule>(
		"Kismet",
		TDelegate<void(FBlueprintEditorModule*)>::CreateLambda([MetaCustomizationHandle](FBlueprintEditorModule* Inc) {
			MetaCustomizationHandle.Get() = Inc->RegisterVariableCustomization(FProperty::StaticClass(), FOnGetVariableCustomizationInstance::CreateStatic(&FGMPBPMetaCustomization::MakeInstance));
		}),
		TDelegate<void(FBlueprintEditorModule*)>::CreateLambda([MetaCustomizationHandle](FBlueprintEditorModule* Inc) { Inc->UnregisterVariableCustomization(FProperty::StaticClass(), MetaCustomizationHandle.Get()); }));
});
