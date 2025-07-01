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
#include "GMP/GMPUtils.h"
#include "Editor.h"
#include "UObject/MetaData.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_FunctionTerminator.h"
#include "Engine/Blueprint.h"
#include "K2Node_FunctionEntry.h"
#include "IPropertyUtilities.h"
#include "IDetailGroup.h"
#include "Misc/DelayedAutoRegister.h"
#include "XConsoleManager.h"
#include "K2Node_Variable.h"

#define LOCTEXT_NAMESPACE "GMPBPMetaCustomization"
static FName GetGMPMetaKey(int32 CurIndex = 0)
{
	return FName(TEXT("GMPMeta"), CurIndex);
};

static bool bAllowIninleAdd = true;
FXConsoleCommandLambda XVar_GMPAllowInlineAdd(TEXT("gmp.meta.showInlineAdd"), [](bool bAllowed, UWorld* InWorld) { bAllowIninleAdd = bAllowed; });
static bool bAllowHeaderAdd = false;
FXConsoleCommandLambda XVar_GMPAllowAddMeta(TEXT("gmp.meta.showHeaderAdd"), [](bool bAllowed, UWorld* InWorld) { bAllowHeaderAdd = bAllowed; });
static bool bEnableMetaRename = false;
FXConsoleCommandLambda XVar_GMPAllowRenameMeta(TEXT("gmp.meta.showRename"), [](bool bAllowed, UWorld* InWorld) { bEnableMetaRename = bAllowed; });
static bool bAllowClassMeta = false;
FXConsoleCommandLambda XVar_GMPAllowClsMeta(TEXT("gmp.meta.showClass"), [](bool bAllowed, UWorld* InWorld) { bAllowClassMeta = bAllowed; });
static bool bAllowFunctionMeta = true;
FXConsoleCommandLambda XVar_GMPAllowFuncMeta(TEXT("gmp.meta.showFunction"), [](bool bAllowed, UWorld* InWorld) { bAllowFunctionMeta = bAllowed; });
static bool bAllowParamMeta = true;
FXConsoleCommandLambda XVar_GMPAllowParamMeta(TEXT("gmp.meta.showParam"), [](bool bAllowed, UWorld* InWorld) { bAllowParamMeta = bAllowed; });

FXConsoleCommandLambda XVar_ModifyClassMeta(TEXT("gmp.meta.setClassMeta"), [](const FSoftClassPath& BPClassPath, const FName& MetaKey, const FString& MetaValue, UWorld* InWorld) {
	do
	{
		if (MetaKey.IsNone())
			break;
		if (!GEditor || GEditor->IsPlaySessionInProgress() || (InWorld && InWorld->GetGameInstance()))
			break;
		auto BP = Cast<UBlueprint>(BPClassPath.TryLoad());
		if (!BP || !BP->GeneratedClass)
			break;
		auto Cls = Cast<UClass>(BP->GeneratedClass);
		if (!Cls)
			break;
		UPackage* Package = BP->GetOutermost();
		Package->FullyLoad();
		auto MetaData = Package->GetMetaData();
		auto* MapPtr = MetaData->GetMapForObject(Cls);
		if (!MetaValue.IsEmpty())
		{
			if (!MapPtr || !MapPtr->Contains(MetaKey) || MapPtr->FindChecked(MetaKey) != MetaValue)
			{
				MetaData->SetValue(Cls, MetaKey, *MetaValue);
				MetaData->SetValue(Cls, GetGMPMetaKey(), TEXT("True"));
				UE_LOG(LogTemp, Log, TEXT("GMP SetClassMetaData [%s:%s] for %s"), *MetaKey.ToString(), *MetaValue, *Cls->GetPathName());
				MetaData->Modify();
			}
		}
		else if (MapPtr && MapPtr->Contains(MetaKey))
		{
			MapPtr->Remove(MetaKey);
			UE_LOG(LogTemp, Log, TEXT("GMP SetClassMetaData [%s:%s] for %s"), *MetaKey.ToString(), *MetaValue, *Cls->GetPathName());
			MetaData->Modify();
		}
	} while (false);
});

template<typename T>
TSharedPtr<IDetailCustomization> IGMPDetailCustomization::MakeInstance(TSharedPtr<IBlueprintEditor> InBlueprintEditor)
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
		return MakeShareable(new T(InBlueprintEditor, Blueprint));
	} while (false);
	return nullptr;
}

bool FGMPBPMetaCustomization::MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	auto BP = BlueprintPtr.Get();
	if (!BP)
		return false;
	UClass* Cls = BP->GeneratedClass;
	if (!Cls || !Cls->HasMetaData(GetGMPMetaKey()))
		return false;

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)
		return false;
	auto ObjectBeingCustomized = ObjectsBeingCustomized[0].Get();
	if (!ObjectBeingCustomized)
		return false;

	FProperty* Prop = nullptr;
	if (UK2Node_Variable* VarNode = Cast<UK2Node_Variable>(ObjectBeingCustomized))
	{
		Prop = VarNode->GetPropertyForVariable();
	}
	else if (UPropertyWrapper* PropertyWrapper = Cast<UPropertyWrapper>(ObjectBeingCustomized))
	{
		Prop = PropertyWrapper->GetProperty();
	}

	if (!Prop)
		return false;

	const FName VarName = Prop->GetFName();
	const int32 VarIndex = FBlueprintEditorUtils::FindNewVariableIndex(BP, VarName);
	if (VarIndex == INDEX_NONE)
		return false;

	static const TArray<FName> GMPDefaultMeta = {
		TEXT("AbilityTrigger"),
		TEXT("NeuronAction"),
		TEXT("NeuronNode"),
		TEXT("AsSingleton"),
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

	if (Prop->IsA<FMulticastDelegateProperty>())
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
			.IsChecked(TAttribute<ECheckBoxState>::Create(CreateWeakLambda(BP,
																		   [BP, VarName, MetaKey]() -> ECheckBoxState {
																			   FString VarHolder;
																			   if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(BP, VarName, nullptr, MetaKey, VarHolder) || VarHolder != TEXT("True"))
																			   {
																				   return ECheckBoxState::Unchecked;
																			   }
																			   return ECheckBoxState::Checked;
																		   })))
			.OnCheckStateChanged(CreateWeakLambda(BP,
												  [BP, VarName, MetaKey](ECheckBoxState State) {
													  if (State == ECheckBoxState::Checked)
													  {
														  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, MetaKey, State == ECheckBoxState::Checked ? TEXT("True") : TEXT("False"));
													  }
													  else
													  {
														  FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, VarName, nullptr, MetaKey);
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
			.IsChecked(TAttribute<ECheckBoxState>::Create(CreateWeakLambda(BP,
																		   [BP, VarName, MetaKey]() -> ECheckBoxState {
																			   FString VarHolder;
																			   if (!FBlueprintEditorUtils::GetBlueprintVariableMetaData(BP, VarName, nullptr, MetaKey, VarHolder) || VarHolder != TEXT("True"))
																			   {
																				   return ECheckBoxState::Unchecked;
																			   }
																			   return ECheckBoxState::Checked;
																		   })))
			.OnCheckStateChanged(CreateWeakLambda(BP,
												  [BP, VarName, MetaKey](ECheckBoxState State) {
													  if (State == ECheckBoxState::Checked)
													  {
														  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, MetaKey, State == ECheckBoxState::Checked ? TEXT("True") : TEXT("False"));
													  }
													  else
													  {
														  FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, VarName, nullptr, MetaKey);
													  }
												  }))
		];
	}

	bool bCustomMetaEdit = false;
	if (bCustomMetaEdit)
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
				PropertyCustomizationHelpers::MakeAddButton(CreateWeakLambda(BP,
																			 [BP, VarName] {
																				 int32 CurIndex = 1;
																				 FString VarHolder;
																				 while (FBlueprintEditorUtils::GetBlueprintVariableMetaData(BP, VarName, nullptr, GetGMPMetaKey(CurIndex), VarHolder))
																					 ++CurIndex;
																				 FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, GetGMPMetaKey(CurIndex), TEXT(""));
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
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.SelectAllTextWhenFocused(true)
				.ClearKeyboardFocusOnCommit(false)
				.OnTextCommitted(CreateWeakLambda(BP,
												  [BP, VarName, KeyName{MetaEntry.DataKey}](const FText& NewKeyName, ETextCommit::Type ChangeType) {
													  FString VarHolder;
													  FBlueprintEditorUtils::GetBlueprintVariableMetaData(BP, VarName, nullptr, KeyName, VarHolder);
													  FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, VarName, nullptr, KeyName);
													  FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, *NewKeyName.ToString(), VarHolder);
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
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.SelectAllTextWhenFocused(false)
					.ClearKeyboardFocusOnCommit(false)
					.OnTextCommitted(CreateWeakLambda(
						BP,
						[BP, VarName, KeyName{MetaEntry.DataKey}](const FText& NewName, ETextCommit::Type ChangeType) { FBlueprintEditorUtils::SetBlueprintVariableMetaData(BP, VarName, nullptr, KeyName, NewName.ToString()); }))
					.SelectAllTextOnCommit(false)
					.IsReadOnly(bReadOnly)
					.ModiferKeyForNewLine(EModifierKey::Shift)
					.WrapTextAt(200.0f)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Fill)
				[
					PropertyCustomizationHelpers::MakeRemoveButton(CreateWeakLambda(BP, [BP, VarName, MetadataKey{MetaEntry.DataKey}] { FBlueprintEditorUtils::RemoveBlueprintVariableMetaData(BP, VarName, nullptr, MetadataKey); }),
																   LOCTEXT("VariableMetadataRemove", "Remove"),
																   !bReadOnly)
				]
			];
		}
	}
	return true;
}

bool FGMPBPMetaFunctionCustomization::MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	auto BP = BlueprintPtr.Get();
	if (!BP)
		return false;
	UClass* Cls = BP->GeneratedClass;
	if (!Cls || !Cls->HasMetaData(GetGMPMetaKey()))
		return false;

	TArray<TWeakObjectPtr<UObject>> ObjectsBeingCustomized;
	DetailLayout.GetObjectsBeingCustomized(ObjectsBeingCustomized);
	if (ObjectsBeingCustomized.Num() != 1)
		return false;
	auto ObjectBeingCustomized = ObjectsBeingCustomized[0].Get();
	if (!ObjectBeingCustomized)
		return false;

	UFunction* Func = nullptr;
	FKismetUserDeclaredFunctionMetadata* EventMetaData = nullptr;
	if (auto EventNode = Cast<UK2Node_CustomEvent>(ObjectBeingCustomized))
	{
		EventMetaData = &EventNode->GetUserDefinedMetaData();
		Func = EventNode->FindEventSignatureFunction();
		if (!Func)
			Func = Cls->FindFunctionByName(EventNode->GetFunctionName());
	}
	else if (auto FuncEntryNode = Cast<UK2Node_FunctionEntry>(ObjectBeingCustomized))
	{
		EventMetaData = &FuncEntryNode->MetaData;
		Func = FuncEntryNode->FindSignatureFunction();
	}
	else if (auto FuncTermNode = Cast<UK2Node_FunctionTerminator>(ObjectBeingCustomized))
	{
		Func = FuncTermNode->FindSignatureFunction();
	}

	if (!Func)
		return false;
	static TMap<FName, FString> EmptyMetaMap;

	auto Utilities = DetailLayout.GetPropertyUtilities();
	auto Package = Func->GetOutermost();
	Package->FullyLoad();

	auto& MetaBuilder = DetailLayout.EditCategory(TEXT("GMP Meta"));
	MetaBuilder.InitiallyCollapsed(true);

	auto EditStructMeta = [](UStruct* Obj, IDetailGroup& StructGroup, TSharedRef<IPropertyUtilities> Utilities, FKismetUserDeclaredFunctionMetadata* EventMetaData) {
		do
		{
			auto AddStructMeta = CreateWeakLambda(Obj, [Obj, Utilities] {
				int32 CurIndex = 0;
				while (Obj->HasMetaData(GetGMPMetaKey(CurIndex)))
					++CurIndex;
				Obj->SetMetaData(GetGMPMetaKey(CurIndex), TEXT(""));
				Utilities->RequestForceRefresh();
			});
			if (bAllowHeaderAdd)
			{
				StructGroup.HeaderRow()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Center)
						[
							SNew(SSpacer)
						]
						+ SHorizontalBox::Slot()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						[
							PropertyCustomizationHelpers::MakeAddButton(AddStructMeta, LOCTEXT("MetadataAdd", "AddNewMetadata"))
						]
					];
			}
			auto MetaData = Obj->GetOutermost()->GetMetaData();
			auto* MetaMapPtr = MetaData->GetMapForObject(Obj);
			for (auto& Pair : MetaMapPtr ? *MetaMapPtr : EmptyMetaMap)
			{
				auto MetaKey = Pair.Key;
				auto MetaValue = Pair.Value;

				// for default value?
				if (Obj->FindPropertyByName(MetaKey))
					continue;

				StructGroup
					.AddWidgetRow()
				.NameContent()
				[
					SNew(SEditableTextBox)
					.Text(FText::FromName(MetaKey))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.SelectAllTextWhenFocused(true)
					.ClearKeyboardFocusOnCommit(false)
					.OnTextCommitted(CreateWeakLambda(Obj,
													  [Obj, MetaKey, Utilities](const FText& NewKeyName, ETextCommit::Type ChangeType) {
														  if (ChangeType == ETextCommit::OnCleared || NewKeyName.IsEmpty())
														  {
															  Obj->RemoveMetaData(*MetaKey.ToString());
															  Obj->MarkPackageDirty();
														  }
														  else if (NewKeyName.ToString() != MetaKey.ToString() && !Obj->HasMetaData(*NewKeyName.ToString()))
														  {
															  auto BackupVal = Obj->GetMetaData(MetaKey);
															  Obj->RemoveMetaData(MetaKey);
															  Obj->SetMetaData(*NewKeyName.ToString(), *BackupVal);
															  Obj->MarkPackageDirty();
														  }
														  Utilities->RequestForceRefresh();
													  }))
					.IsReadOnly(!bEnableMetaRename)
					.SelectAllTextOnCommit(true)
					.IsPassword(false)
				]
				.ValueContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					[
						SNew(SEditableTextBox)
						.Text(FText::FromString(MetaValue))
						.Font(IDetailLayoutBuilder::GetDetailFont())
						.SelectAllTextWhenFocused(true)
						.ClearKeyboardFocusOnCommit(false)
						.OnTextCommitted(CreateWeakLambda(Obj,
														  [Obj, EventMetaData, MetaKey, Utilities](const FText& NewMetaValue, ETextCommit::Type ChangeType) {
															  if (EventMetaData)
															  {
																  EventMetaData->SetMetaData(*MetaKey.ToString(), NewMetaValue.ToString());
															  }
															  Obj->SetMetaData(*MetaKey.ToString(), *NewMetaValue.ToString());
															  Obj->MarkPackageDirty();
															  Utilities->RequestForceRefresh();
														  }))
						.SelectAllTextOnCommit(true)
						.IsPassword(false)
						.IsReadOnly(false)
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					.Padding(10, 0, 0, 0)
					.AutoWidth()
					[
						PropertyCustomizationHelpers::MakeClearButton(CreateWeakLambda(Obj,
																					   [Obj, EventMetaData, MetaKey, Utilities] {
																						   if (EventMetaData)
																						   {
																							   EventMetaData->RemoveMetaData(*MetaKey.ToString());
																						   }
																						   Obj->RemoveMetaData(*MetaKey.ToString());
																						   Obj->MarkPackageDirty();
																						   Utilities->RequestForceRefresh();
																					   }),
																	  LOCTEXT("MetaClearTooltip", "Remove this meta"))
					]
				];
			}
			if (!bAllowIninleAdd)
				break;
			TSharedRef<SEditableTextBox> TextBox = SNew(SEditableTextBox)
													.HintText(LOCTEXT("EnterNewMetaKey", "EnterNewMetaKey"))
													.Font(IDetailLayoutBuilder::GetDetailFont())
													.SelectAllTextWhenFocused(true)
													.ClearKeyboardFocusOnCommit(false)
													.IsReadOnly(false)
													.SelectAllTextOnCommit(true)
													.IsPassword(false);
			StructGroup
				.AddWidgetRow()
			.NameContent()
			[
				TextBox
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("AddMeta", "AddMeta"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				[
					PropertyCustomizationHelpers::MakeAddButton(CreateWeakLambda(Obj,
																				 [Obj, TextBox, Utilities] {
																					 auto Txt = TextBox->GetText().ToString();
																					 if (Txt.IsEmpty())
																					 {
																						 int32 CurIndex = 0;
																						 while (Obj->HasMetaData(GetGMPMetaKey(CurIndex)))
																							 ++CurIndex;
																						 Obj->SetMetaData(GetGMPMetaKey(CurIndex), TEXT(""));
																					 }
																					 else if (!Obj->HasMetaData(*Txt))
																					 {
																						 Obj->SetMetaData(*Txt, TEXT(""));
																					 }
																					 else
																					 {
																						 int32 CurIndex = 0;
																						 while (Obj->HasMetaData(FName(*Txt, CurIndex)))
																							 ++CurIndex;
																						 Obj->SetMetaData(FName(*Txt, CurIndex), TEXT(""));
																					 }
																					 Obj->MarkPackageDirty();
																					 Utilities->RequestForceRefresh();
																				 }),
																LOCTEXT("MetadataAdd", "AddNewMetadata"))
				]
			];
		} while (false);
	};

	if (bAllowClassMeta)
	{
		auto& ClsGroup = MetaBuilder.AddGroup(TEXT("Class Meta"), LOCTEXT("ClassMeta", "ClassMeta"));
		ClsGroup.ToggleExpansion(false);
		EditStructMeta(Cls, ClsGroup, Utilities, EventMetaData);
	}
	if (bAllowFunctionMeta)
	{  // TODO:if function name changed won't auto refresh
		auto& FuncGroup = MetaBuilder.AddGroup(TEXT("Function Meta"), LOCTEXT("FunctionMeta", "FunctionMeta"));
		FuncGroup.ToggleExpansion(true);
		EditStructMeta(Func, FuncGroup, Utilities, EventMetaData);
	}

	if (!bAllowParamMeta)
		return false;
	// TODO:if parameter name changed won't auto refresh
	IDetailGroup* InputsGroup = nullptr;
	IDetailGroup* OutputsGroup = nullptr;
	auto GetGroup = [&](bool bOut) -> IDetailGroup& {
		if (bOut)
		{
			if (!OutputsGroup)
				OutputsGroup = &MetaBuilder.AddGroup(TEXT("Outputs"), LOCTEXT("Outputs", "Outputs"));
			return *OutputsGroup;
		}
		if (!InputsGroup)
			InputsGroup = &MetaBuilder.AddGroup(TEXT("Inputs"), LOCTEXT("Inputs", "Inputs"));
		return *InputsGroup;
	};

	for (TFieldIterator<FProperty> It(Func); It; ++It)
	{
		FProperty* Property = *It;
		if (!Property || !Property->HasAnyPropertyFlags(CPF_Parm))
			continue;
		const FName VarName = Property->GetFName();
		auto AddPropMeta = CreateWeakLambda(Func, [Func, Utilities, VarName] {
			if (auto Prop = Func->FindPropertyByName(VarName))
			{
				int32 CurIndex = 0;
				while (Prop->HasMetaData(GetGMPMetaKey(CurIndex)))
					++CurIndex;
				Prop->SetMetaData(GetGMPMetaKey(CurIndex), TEXT(""));
				Utilities->RequestForceRefresh();
			}
		});
		bool bIsOut = Property->HasAnyPropertyFlags(CPF_OutParm) && !Property->HasAnyPropertyFlags(CPF_ReferenceParm);
		IDetailGroup& ParamGroup = GetGroup(bIsOut).AddGroup(VarName, FText::FromName(VarName));
		if (bAllowHeaderAdd)
		{
			ParamGroup.HeaderRow()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromName(VarName))
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
					]
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Center)
					[
						PropertyCustomizationHelpers::MakeAddButton(AddPropMeta, LOCTEXT("ParameterMetadataAdd", "AddNewMetadata"))
					]
				];
		}
		auto MapPtr = Property->GetMetaDataMap();
		for (auto& Pair : MapPtr ? *MapPtr : EmptyMetaMap)
		{
			auto MetaKey = Pair.Key;
			auto MetaValue = Pair.Value;

			ParamGroup
				.AddWidgetRow()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromName(MetaKey))
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Center)
				[
					SNew(SEditableTextBox)
					.Text(FText::FromString(MetaValue))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.SelectAllTextWhenFocused(true)
					.ClearKeyboardFocusOnCommit(false)
					.OnTextCommitted(CreateWeakLambda(Func,
													  [Func, MetaKey, VarName](const FText& NewMetaValue, ETextCommit::Type ChangeType) {
														  auto Prop = Func->FindPropertyByName(VarName);
														  if (!Prop)
															  return;
														  Prop->SetMetaData(*MetaKey.ToString(), *NewMetaValue.ToString());
													  }))
					.SelectAllTextOnCommit(true)
					.IsPassword(false)
					.IsReadOnly(!bEnableMetaRename)
				]
				+ SHorizontalBox::Slot()
				.HAlign(HAlign_Right)
				.VAlign(VAlign_Center)
				.Padding(10, 0, 0, 0)
				.AutoWidth()
				[
					PropertyCustomizationHelpers::MakeClearButton(CreateWeakLambda(Func,
																				   [Func, VarName, MetaKey, Utilities] {
																					   auto Prop = Func->FindPropertyByName(VarName);
																					   if (!Prop)
																						   return;
																					   Prop->RemoveMetaData(*MetaKey.ToString());
																					   Utilities->RequestForceRefresh();
																				   }),
																  LOCTEXT("MetaClearTooltip", "Remove this meta"))
				]
			];
		}

		if (!bAllowIninleAdd)
			continue;
		TSharedRef<SEditableTextBox> TextBox = SNew(SEditableTextBox)
												.HintText(LOCTEXT("EnterNewMetaKey", "EnterNewMetaKey"))
												.Font(IDetailLayoutBuilder::GetDetailFont())
												.SelectAllTextWhenFocused(true)
												.ClearKeyboardFocusOnCommit(false)
												.IsReadOnly(false)
												.SelectAllTextOnCommit(true)
												.IsPassword(false);

		ParamGroup
			.AddWidgetRow()
		.NameContent()
		[
			TextBox
		]
		.ValueContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Center)
			[
				SNew(SSpacer)
			]
			+ SHorizontalBox::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			[
				PropertyCustomizationHelpers::MakeAddButton(CreateWeakLambda(Func,
																			 [Func, VarName, TextBox, Utilities] {
																				 auto Prop = Func->FindPropertyByName(VarName);
																				 if (!Prop)
																					 return;

																				 auto Txt = TextBox->GetText().ToString();
																				 if (Txt.IsEmpty())
																				 {
																					 int32 CurIndex = 0;
																					 while (Prop->HasMetaData(GetGMPMetaKey(CurIndex)))
																						 ++CurIndex;
																					 Prop->SetMetaData(GetGMPMetaKey(CurIndex), TEXT(""));
																				 }
																				 else if (!Prop->HasMetaData(*Txt))
																				 {
																					 Prop->SetMetaData(*Txt, TEXT(""));
																				 }
																				 else
																				 {
																					 int32 CurIndex = 0;
																					 while (Prop->HasMetaData(FName(*Txt, CurIndex)))
																						 ++CurIndex;
																					 Prop->SetMetaData(FName(*Txt, CurIndex), TEXT(""));
																				 }
																				 Utilities->RequestForceRefresh();
																			 }),
															LOCTEXT("ParamMetadataAdd", "AddNewMetadata"))
			]
		];
	}

	return true;
}

bool FGMPBPMetaParametersCustomization::MyCustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	if (!FGMPBPMetaFunctionCustomization::MyCustomizeDetails(DetailLayout))
		return false;

	return true;
}

#undef LOCTEXT_NAMESPACE

struct FBlueprintRegisterStruct : public TSharedFromThis<FBlueprintRegisterStruct>
{
	FDelegateHandle MetaVarHandle;
	FDelegateHandle MetaCustomEventHandle;
	FDelegateHandle MetaFuncHandle;

	void Register(FBlueprintEditorModule* Inc)
	{
		MetaVarHandle = Inc->RegisterVariableCustomization(FProperty::StaticClass(), FOnGetVariableCustomizationInstance::CreateStatic(&FGMPBPMetaCustomization::MakeInstance));
		MetaCustomEventHandle = Inc->RegisterFunctionCustomization(UK2Node_CustomEvent::StaticClass(), FOnGetFunctionCustomizationInstance::CreateStatic(&FGMPBPMetaParametersCustomization::MakeInstance));
		MetaFuncHandle = Inc->RegisterFunctionCustomization(UK2Node_FunctionTerminator::StaticClass(), FOnGetFunctionCustomizationInstance::CreateStatic(&FGMPBPMetaParametersCustomization::MakeInstance));
	}

	void Unregister(FBlueprintEditorModule* Inc)
	{
		Inc->UnregisterVariableCustomization(FProperty::StaticClass(), MetaVarHandle);
		Inc->UnregisterFunctionCustomization(UK2Node_CustomEvent::StaticClass(), MetaCustomEventHandle);
		Inc->UnregisterFunctionCustomization(UK2Node_FunctionTerminator::StaticClass(), MetaFuncHandle);
		MetaVarHandle.Reset();
		MetaCustomEventHandle.Reset();
		MetaFuncHandle.Reset();
	}

	void Bind()
	{
		GMP::FGMPModuleUtils::template OnModuleLifetime<FBlueprintEditorModule>(  //
			"Kismet",
			TDelegate<void(FBlueprintEditorModule*)>::CreateSP(SharedThis(this), &FBlueprintRegisterStruct::Register),
			TDelegate<void(FBlueprintEditorModule*)>::CreateSP(SharedThis(this), &FBlueprintRegisterStruct::Unregister));
	}
};

TSharedPtr<FBlueprintRegisterStruct> BlueprintRegisterStruct;

static FDelayedAutoRegisterHelper AutoRegister_GMPBPMetaCustomization(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
	if (!BlueprintRegisterStruct)
	{
		BlueprintRegisterStruct = MakeShared<FBlueprintRegisterStruct>();
		BlueprintRegisterStruct->Bind();
	}
});
