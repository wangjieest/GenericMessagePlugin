﻿//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "K2Node_GMPStructUnion.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Selection.h"
#include "Engine/UserDefinedStruct.h"
#include "GMPCore.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_Knot.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "KismetNodes/SGraphNodeK2Default.h"
#include "KismetPins/SGraphPinObject.h"
#include "Modules/ModuleManager.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "StructViewerFilter.h"
#include "StructViewerModule.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "K2Node_DynStruct"

namespace GMP
{
namespace StructUnion
{
	extern GMP_API bool MatchGMPStructUnionCategory(const UScriptStruct* InStruct, FName Category);
	FName StructStorage{TEXT("DynStorage")};
	FName StructType{TEXT("StructType")};
	FName StructData{TEXT("StructData")};
	FName StructResult{TEXT("bResult")};
}  // namespace StructUnion
}  // namespace GMP

struct FGMPStructUtils
{
	static UFunction* DynStructFunc(bool bDataRef, bool bTuple, bool bSetVal)
	{
		if (bDataRef)
		{
			if (bTuple)
				return bSetVal ? GMP_UFUNCTION_CHECKED(UGMPStructLib, SetStructTuple) : GMP_UFUNCTION_CHECKED(UGMPStructLib, GetStructTuple);
			else
				return bSetVal ? GMP_UFUNCTION_CHECKED(UGMPStructLib, SetStructUnion) : GMP_UFUNCTION_CHECKED(UGMPStructLib, GetStructUnion);
		}
		else
		{
			return bSetVal ? GMP_UFUNCTION_CHECKED(UGMPDynStructStorage, SetDynStruct) : GMP_UFUNCTION_CHECKED(UGMPDynStructStorage, GetDynStruct);
		}
	}
	static FName DynStructStorageName(bool bDataRef) { return bDataRef ? TEXT("InStruct") : TEXT("InStorage"); }
};

void UK2Node_GMPStructUnionBase::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	AllocateDefaultPins();

	if (auto OldMemberPin = OldPins.FindByPredicate([](auto Pin) { return Pin && Pin->PinName == GMP::StructUnion::StructData; }))
	{
		auto MemberPin = FindPinChecked(GMP::StructUnion::StructData);
		MemberPin->PinType = (*OldMemberPin)->PinType;
	}

	RestoreSplitPins(OldPins);
}

bool UK2Node_GMPStructUnionBase::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin->PinName == GMP::StructUnion::StructData)
	{
		bool bAllowed = MyPin->LinkedTo.Num() == 0 && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard;
		if (bAllowed)
		{
			bAllowed = false;
			auto TestPin = OtherPin;
			while (auto Knot = Cast<UK2Node_Knot>(TestPin->GetOwningNode()))
			{
				auto ConnectPin = Knot->GetInputPin();
				if (ConnectPin == TestPin)
					ConnectPin = Knot->GetOutputPin();

				if (ConnectPin->LinkedTo.Num() == 1)
					TestPin = ConnectPin->LinkedTo[0];
				else
					break;
			}
			if (TestPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
			{
				auto ScriptStruct = Cast<UScriptStruct>(TestPin->PinType.PinSubCategoryObject.Get());
				bAllowed = GMP::StructUnion::MatchGMPStructUnionCategory(ScriptStruct, Category);
			}
		}
		if (!bAllowed)
		{
			OutReason = TEXT("Not Allowed");
			return false;
		}
	}

	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

TSharedPtr<SGraphNode> UK2Node_GMPStructUnionBase::CreateVisualWidget()
{
	class FGraphPinStructFilter : public IStructViewerFilter
	{
	public:
		FName Category;
		// TODO: Have a flag controlling whether we allow UserDefinedStructs, even when a MetaClass is set (as they cannot support inheritance, but may still be allowed (eg, data tables))?
		virtual bool IsStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const UScriptStruct* InStruct, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			if (InStruct->IsA<UUserDefinedStruct>())
			{
				return false;
			}
			return GMP::StructUnion::MatchGMPStructUnionCategory(InStruct, Category);
		}

		virtual bool IsUnloadedStructAllowed(const FStructViewerInitializationOptions& InInitOptions, const FName InStructPath, TSharedRef<FStructViewerFilterFuncs> InFilterFuncs) override
		{
			// User Defined Structs don't support inheritance, so only include them if we have don't a MetaStruct set
			return false;
		}
	};

	class SGraphPinStruct : public SGraphPinObject
	{
	public:
		SLATE_BEGIN_ARGS(SGraphPinStruct) {}
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs, UEdGraphPin* InGraphPinObj, FName InCategory = NAME_None)
		{
			SGraphPin::Construct(SGraphPin::FArguments(), InGraphPinObj);
			Category = InCategory;
		}

		FName Category;
		FReply OnClickUse()
		{
			FEditorDelegates::LoadSelectedAssetsIfNeeded.Broadcast();

			UObject* SelectedObject = GEditor->GetSelectedObjects()->GetTop(UScriptStruct::StaticClass());
			if (SelectedObject)
			{
				const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeStructPinValue", "Change Struct Pin Value"));
				GraphPinObj->Modify();

				GraphPinObj->GetSchema()->TrySetDefaultObject(*GraphPinObj, SelectedObject);
			}

			return FReply::Handled();
		}

		TSharedRef<SWidget> GenerateAssetPicker()
		{
			FStructViewerModule& StructViewerModule = FModuleManager::LoadModuleChecked<FStructViewerModule>("StructViewer");

			// Fill in options
			FStructViewerInitializationOptions Options;
			Options.Mode = EStructViewerMode::StructPicker;
			Options.bShowNoneOption = true;

			// TODO: We would need our own PC_ type to be able to get the meta-struct here
			const UScriptStruct* MetaStruct = nullptr;

			TSharedRef<FGraphPinStructFilter> StructFilter = MakeShared<FGraphPinStructFilter>();
			Options.StructFilter = StructFilter;
			StructFilter->Category = Category;

			return SNew(SBox)
			.WidthOverride(280)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				.MaxHeight(500)
				[
					SNew(SBorder)
					.Padding(4)
					.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
					[
						StructViewerModule.CreateStructViewer(Options, FOnStructPicked::CreateSP(this, &SGraphPinStruct::OnPickedNewStruct))
					]
				]
			];
		}

		FOnClicked GetOnUseButtonDelegate() { return FOnClicked::CreateSP(this, &SGraphPinStruct::OnClickUse); }

		void OnPickedNewStruct(const UScriptStruct* ChosenStruct)
		{
			if (GraphPinObj->IsPendingKill())
			{
				return;
			}

			FString NewPath;
			if (ChosenStruct)
			{
				NewPath = ChosenStruct->GetPathName();
			}

			if (GraphPinObj->GetDefaultAsString() != NewPath)
			{
				const FScopedTransaction Transaction(NSLOCTEXT("GraphEditor", "ChangeStructPinValue", "Change Struct Pin Value"));
				GraphPinObj->Modify();

				AssetPickerAnchor->SetIsOpen(false);
				GraphPinObj->GetSchema()->TrySetDefaultObject(*GraphPinObj, const_cast<UScriptStruct*>(ChosenStruct));
			}
		}

		FText GetDefaultComboText() const { return LOCTEXT("DefaultComboText", "Select Struct"); }
	};

	class SGraphNodeGetDynStruct : public SGraphNodeK2Default
	{
	public:
		SLATE_BEGIN_ARGS(SGraphNodeGetDynStruct) {}
		SLATE_END_ARGS()
		void Construct(const FArguments& InArgs, UK2Node_GMPStructUnionBase* InNode)
		{
			SGraphNodeK2Default::Construct({}, InNode);
			Category = InNode->Category;
		}

	protected:
		FName Category;
		virtual void CreateStandardPinWidget(UEdGraphPin* Pin) override
		{
			if (Pin->GetFName() == GMP::StructUnion::StructType)
			{
				const bool bShowPin = ShouldPinBeHidden(Pin);
				if (bShowPin)
				{
					TSharedPtr<SGraphPin> NewPin = SNew(SGraphPinStruct, Pin, Category);
					check(NewPin.IsValid());
					this->AddPin(NewPin.ToSharedRef());
				}
			}
			else
			{
				SGraphNodeK2Default::CreateStandardPinWidget(Pin);
			}
		}
	};

	return SNew(SGraphNodeGetDynStruct, this);
}

void UK2Node_GMPStructUnionBase::AllocateDefaultPins()
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);

	if (bStructRef)
	{
		if (bTuple)
			CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FGMPStructTuple::StaticStruct(), GMP::StructUnion::StructStorage)->PinType.bIsReference = true;
		else
			CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Struct, FGMPStructUnion::StaticStruct(), GMP::StructUnion::StructStorage)->PinType.bIsReference = true;
	}
	else
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UGMPDynStructStorage::StaticClass(), GMP::StructUnion::StructStorage)->PinType.bIsReference = true;

	if (bSetVal)
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, GMP::StructUnion::StructData)->PinType.bIsReference = true;
	}
	else
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UScriptStruct::StaticClass(), GMP::StructUnion::StructType)->bNotConnectable = true;
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, GMP::StructUnion::StructData);
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Boolean, GMP::StructUnion::StructResult);
	}
}

UScriptStruct* UK2Node_GMPStructUnionBase::GetStructType() const
{
	if (auto DataPin = FindPin(GMP::StructUnion::StructData))
	{
		if (UScriptStruct* Ret = !DataPin->LinkedTo.Num() ? Cast<UScriptStruct>(DataPin->PinType.PinSubCategoryObject.Get()) : nullptr)
		{
			return Ret;
		}
		if (DataPin->LinkedTo.Num() == 1)
		{
			auto TestPin = DataPin->LinkedTo[0];
			while (auto Knot = Cast<UK2Node_Knot>(TestPin->GetOwningNode()))
			{
				auto ConnectPin = Knot->GetInputPin();
				if (ConnectPin == TestPin)
					ConnectPin = Knot->GetOutputPin();

				if (ConnectPin->LinkedTo.Num() == 1)
					TestPin = ConnectPin->LinkedTo[0];
				else
					break;
			}
			return Cast<UScriptStruct>(DataPin->PinType.PinSubCategoryObject.Get());
		}
	}

	if (auto TypePin = FindPin(GMP::StructUnion::StructType))
	{
		return Cast<UScriptStruct>(TypePin->DefaultObject);
	}
	return nullptr;
}

void UK2Node_GMPStructUnionBase::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (!Pin || Pin->PinName != GMP::StructUnion::StructData)
		return;

	if (bSetVal)
	{
		if (Pin->LinkedTo.Num() == 1)
		{
			auto TestPin = Pin->LinkedTo[0];
			while (auto Knot = Cast<UK2Node_Knot>(TestPin->GetOwningNode()))
			{
				if (Knot->GetInputPin()->LinkedTo.Num() == 1)
					TestPin = Knot->GetInputPin()->LinkedTo[0];
				else
					break;
			}
			Pin->PinType = TestPin->PinType;
			Pin->PinType.bIsReference = false;
		}
		else
		{
			FEdGraphPinType WildPinType;
			WildPinType.bIsReference = true;
			WildPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;

			Pin->BreakAllPinLinks();
			Pin->PinType = WildPinType;
		}

		GetGraph()->NotifyGraphChanged();
		CachedNodeTitle.MarkDirty();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
	else  // GetVal
	{
		auto TypePin = FindPinChecked(GMP::StructUnion::StructType);
		TypePin->bHidden = Pin->LinkedTo.Num() > 0;
		if (TypePin->bHidden)
		{
			UScriptStruct* ValStruct = Cast<UScriptStruct>(Pin->PinType.PinSubCategoryObject.Get());
			Pin->PinType = Pin->LinkedTo[0]->PinType;
			if (!TypePin->DefaultObject || TypePin->PinType.PinSubCategoryObject.Get() != ValStruct)
			{
				GetSchema()->TrySetDefaultObject(*TypePin, Pin->PinType.PinSubCategoryObject.Get());
				CachedNodeTitle.MarkDirty();
			}
		}
		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_GMPStructUnionBase::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (!bSetVal)
	{
		if (Pin && (Pin->PinName == GMP::StructUnion::StructType))
		{
			auto ValuePin = FindPinChecked(GMP::StructUnion::StructData);
			UScriptStruct* TypeStruct = Cast<UScriptStruct>(Pin->DefaultObject);
			UScriptStruct* ValStruct = Cast<UScriptStruct>(ValuePin->PinType.PinSubCategoryObject.Get());
			if (!TypeStruct || TypeStruct->GetFName() == TEXT("ScriptStruct"))
			{
				ValuePin->BreakAllPinLinks(true);
				FEdGraphPinType WildPinType;
				WildPinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
				ValuePin->PinType = WildPinType;
			}
			else if (ValuePin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct || !ValStruct || !ValStruct->IsChildOf(TypeStruct))
			{
				ValuePin->PinType.ResetToDefaults();
				ValuePin->PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				ValuePin->PinType.PinSubCategoryObject = TypeStruct;
			}

			GetGraph()->NotifyGraphChanged();
			CachedNodeTitle.MarkDirty();
			FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
		}
	}
}

void UK2Node_GMPStructUnionBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that
	// actions might have to be updated (or deleted) if their object-key is
	// mutated (or removed)... here we use the node's class (so if the node
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();
	if (ActionKey->HasAnyClassFlags(CLASS_Abstract))
		return;

	// to keep from needlessly instantiating a UBlueprintNodeSpawner, first
	// check to make sure that the registrar is looking for actions of this type
	// (could be regenerating actions for a specific asset, and therefore the
	// registrar would only accept actions corresponding to that asset)
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner != nullptr);

		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_GMPStructUnionBase::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	auto DataValPin = FindPinChecked(GMP::StructUnion::StructData);
	if (DataValPin->LinkedTo.Num() == 0)
	{
		MessageLog.Error(TEXT("Data Error @@"), DataValPin);
		return;
	}

	auto TestPin = DataValPin->LinkedTo[0];
	while (auto Knot = Cast<UK2Node_Knot>(TestPin->GetOwningNode()))
	{
		if (Knot->GetInputPin()->LinkedTo.Num() == 1)
			TestPin = Knot->GetInputPin()->LinkedTo[0];
		else
			break;
	}

	if (!FGMPStructUtils::DynStructFunc(bStructRef, bTuple, bSetVal))
	{
		MessageLog.Error(TEXT("Missing DynStruct Function  @@"), DataValPin);
		return;
	}
}

void UK2Node_GMPStructUnionBase::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	auto DataValPin = FindPinChecked(GMP::StructUnion::StructData);
	if (DataValPin->LinkedTo.Num() == 0)
	{
		CompilerContext.MessageLog.Error(TEXT("Data Error @@"), DataValPin);
		BreakAllNodeLinks();
		return;
	}

	auto TestPin = DataValPin->LinkedTo[0];
	while (auto Knot = Cast<UK2Node_Knot>(TestPin->GetOwningNode()))
	{
		if (Knot->GetInputPin()->LinkedTo.Num() == 1)
			TestPin = Knot->GetInputPin()->LinkedTo[0];
		else
			break;
	}

	auto Func = FGMPStructUtils::DynStructFunc(bStructRef, bTuple, bSetVal);
	if (!Func)
	{
		CompilerContext.MessageLog.Error(TEXT("Data Error @@"), DataValPin);
		BreakAllNodeLinks();
		return;
	}

	UK2Node_CallFunction* FuncVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	FuncVarNode->SetFromFunction(Func);
	FuncVarNode->AllocateDefaultPins();

	CompilerContext.MovePinLinksToIntermediate(*GetExecPin(), *FuncVarNode->GetExecPin());
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then), *FuncVarNode->GetThenPin());

	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(GMP::StructUnion::StructStorage), *FuncVarNode->FindPinChecked(FGMPStructUtils::DynStructStorageName(bStructRef)));
	CompilerContext.GetSchema()->TrySetDefaultObject(*FuncVarNode->FindPinChecked(TEXT("InType")), DataValPin->PinType.PinSubCategoryObject.Get());
	if (bSetVal)
	{
		FuncVarNode->FindPinChecked(TEXT("InVal"))->PinType = DataValPin->PinType;
		CompilerContext.MovePinLinksToIntermediate(*DataValPin, *FuncVarNode->FindPinChecked(TEXT("InVal")));
	}
	else
	{
		FuncVarNode->FindPinChecked(TEXT("OutVal"))->PinType = DataValPin->PinType;
		CompilerContext.MovePinLinksToIntermediate(*DataValPin, *FuncVarNode->FindPinChecked(TEXT("OutVal")));
	}

	auto ResultPin = FindPin(GMP::StructUnion::StructResult);
	auto ReturnPin = FuncVarNode->GetReturnValuePin();
	if (ReturnPin && ResultPin)
	{
		CompilerContext.MovePinLinksToIntermediate(*ReturnPin, *ResultPin);
	}

	BreakAllNodeLinks();
}

FText UK2Node_GMPStructUnionBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedNodeTitle.IsOutOfDate(this))
	{
		FText Title;
		if (bStructRef)
		{
			if (bTuple)
				Title = bSetVal ? LOCTEXT("DynSetStructTupleNode", "SetStructTuple") : LOCTEXT("DynGetStructTupleNode", "GetStructTuple");
			else
				Title = bSetVal ? LOCTEXT("DynSetStructUnionNode", "SetStructUnion") : LOCTEXT("DynGetStructUnionNode", "GetStructUnion");
		}
		else
		{
			Title = bSetVal ? LOCTEXT("DynSetStructStorageNode", "SetDynStructStorage") : LOCTEXT("DynGetStructStorageNode", "GetDynStructStorage");
		}

		CachedNodeTitle.SetCachedText(FText::Format(LOCTEXT("DynStructNodeTitle{0}{1}", "{0}('{1}')"), Title, FText::FromString(GetNameSafe(GetStructType()))), this);
	}
	return CachedNodeTitle.GetCachedText();
}

#undef LOCTEXT_NAMESPACE

UK2Node_SetStructUnion::UK2Node_SetStructUnion()
{
	bStructRef = false;
	bSetVal = true;
}

UK2Node_GetStructUnion::UK2Node_GetStructUnion()
{
	bStructRef = false;
	bSetVal = false;
}

UK2Node_GetStructTuple::UK2Node_GetStructTuple()
{
	bTuple = true;
}

UK2Node_SetStructTuple::UK2Node_SetStructTuple()
{
	bTuple = true;
}

UK2Node_SetDynStructOnScope::UK2Node_SetDynStructOnScope()
{
	bStructRef = true;
	bSetVal = true;
}

UK2Node_GetDynStructOnScope::UK2Node_GetDynStructOnScope()
{
	bStructRef = true;
	bSetVal = false;
}