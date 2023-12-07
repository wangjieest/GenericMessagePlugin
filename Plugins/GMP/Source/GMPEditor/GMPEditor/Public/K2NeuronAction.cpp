// Copyright K2Neuron, Inc. All Rights Reserved.

#include "K2NeuronAction.h"

#include "Slate.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "BPTerminal.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintCompilationManager.h"
#include "BlueprintEditorSettings.h"
#include "BlueprintFunctionNodeSpawner.h"
#include "BlueprintNodeSpawner.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/MemberReference.h"
#include "Engine/UserDefinedStruct.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GMPCore.h"
#include "GameplayTask.h"
#include "GraphEditorSettings.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeArray.h"
#include "K2Node_PureAssignmentStatement.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "KismetCompiledFunctionContext.h"
#include "KismetCompiler.h"
#include "KismetNodes/KismetNodeInfoContext.h"
#include "KismetNodes/SGraphNodeK2Default.h"
#include "KismetPins/SGraphPinExec.h"
#include "NeuronActionFactory.h"
#include "ObjectEditorUtils.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "SourceCodeNavigation.h"
#include "UObject/UnrealType.h"
#include "UserDefinedStructure/UserDefinedStructEditorData.h"
#include "Widgets/Notifications/SNotificationList.h"

/////////////////////////////////////////////////////
#define LOCTEXT_NAMESPACE "K2NeuronAction"
namespace NeuronAction
{
const FName NeuronMeta = TEXT("NeuronAction");
const FName NeuronMetaFactory = TEXT("NeuronActionFactory");

const FName AsyncActionName = TEXT("AsyncAction");
const FName AsyncActionExposureName = TEXT("AsyncActionExposureAs");
const FName ExposedAsyncProxyName = TEXT("ExposedAsyncProxy");
const FName HideAsyncProxyName = TEXT("HideAsyncProxy");
const FName HideThenName= TEXT("HideThen");

const FName DeterminesDelegateType = TEXT("DeterminesOutputType");
const FName DynamicDelegateParam = TEXT("DynamicOutputParam");

}  // namespace NeuronAction

bool UK2NeuronAction::IsCompatibleWithGraph(UEdGraph const* TargetGraph) const
{
	bool bIsCompatible = Super::IsCompatibleWithGraph(TargetGraph);
	do
	{
		if (!bIsCompatible)
			break;
		EGraphType GraphType = TargetGraph->GetSchema()->GetGraphType(TargetGraph);
		bIsCompatible &= (GraphType == GT_Ubergraph || GraphType == GT_Macro);
		if (!bIsCompatible)
			break;

		UClass* OwnerClass = Cast<UClass>(FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph)->GeneratedClass);
		if (OwnerClass && ProxyFactoryClass && OwnerClass->IsChildOf(ProxyFactoryClass))
			break;

		// filter RestrictedClasses
		UClass const* QueryClass = OwnerClass;
		bool bIsClassListed = RestrictedClasses.Num() == 0;
		while (!bIsClassListed && (QueryClass != nullptr))
		{
			bIsClassListed = RestrictedClasses.Contains(QueryClass->GetName());
			if (bIsClassListed)
				break;

			QueryClass = QueryClass->GetSuperClass();
		}
		bIsCompatible = bIsClassListed;
		if (!bIsCompatible)
			break;

		if (auto FactoryFunc = GetFactoryFunction())
		{
			if (FactoryFunc->HasAllFunctionFlags(FUNC_Static))
				break;

			TSet<FName> PinsToHide;
			FBlueprintEditorUtils::GetHiddenPinsForFunction(TargetGraph, FactoryFunc, PinsToHide);
			bIsCompatible &= !PinsToHide.Contains(UEdGraphSchema_K2::PN_Self);
		}
	} while (0);

	return bIsCompatible;
}

UK2NeuronAction::UK2NeuronAction()
	: UnlinkSelfObjEventName(TEXT("UnlinkObjEvent"))
{
	FillActionDefaultCancelFlags(ProxyClass);
}

void UK2NeuronAction::AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins /*= nullptr*/)
{
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);

	const UEdGraphSchema_K2* K2Schema = GetK2Schema();
	UFunction* FactoryFunc = GetFactoryFunction();

	bool bHideProxy = true;
	bool bHideThen = false;
	FText ExposeProxyDisplayName;
	if (FactoryFunc)
	{
		bHideProxy = FactoryFunc->HasMetaData(NeuronAction::HideAsyncProxyName) || !FactoryFunc->HasMetaData(NeuronAction::ExposedAsyncProxyName);
		ExposeProxyDisplayName = FactoryFunc->GetMetaDataText(NeuronAction::ExposedAsyncProxyName);

		bHideThen = FactoryFunc->HasMetaData(NeuronAction::HideThenName);
		if (!bHideThen)
		{
			for (const UClass* TestClass = FactoryFunc->GetOwnerClass(); TestClass; TestClass = TestClass->GetSuperClass())
			{
				if (TestClass->HasMetaData(NeuronAction::HideThenName))
				{
					bHideThen = true;
					break;
				}
			}
		}
	}

	bool bExposeProxy = !bHideProxy;
	for (const UClass* TestClass = ProxyClass; TestClass; TestClass = TestClass->GetSuperClass())
	{
		bExposeProxy |= !bHideProxy && (TestClass->HasMetaData(NeuronAction::AsyncActionExposureName) || TestClass->HasMetaData(NeuronAction::ExposedAsyncProxyName));
		bHideThen |= TestClass->HasMetaData(NeuronAction::HideThenName);
		if (ExposeProxyDisplayName.IsEmpty())
			ExposeProxyDisplayName = TestClass->GetMetaDataText(NeuronAction::AsyncActionExposureName);
		if (ExposeProxyDisplayName.IsEmpty())
			ExposeProxyDisplayName = TestClass->GetMetaDataText(NeuronAction::ExposedAsyncProxyName);
	}

	const FString ParamPrefix = FactoryFunc ? FString::Printf(TEXT("%c%s%s%s%s"), AffixesSelf.ParamPrefix, *FactoryFunc->GetOwnerClass()->GetName(), *FunctionDelimiter, *FactoryFunc->GetName(), *MemberDelimiter) : TEXT("");

	FProperty* Prop = nullptr;
	UEnum* Enum = nullptr;
	if (IsExpandEnumAsExec(FactoryFunc, &Enum, &Prop))
	{
		int32 NumExecs = (Enum->NumEnums() - 1);
		for (int32 ExecIdx = 0; ExecIdx < NumExecs; ExecIdx++)
		{
			if (!Enum->HasMetaData(TEXT("Hidden"), ExecIdx) || Enum->HasMetaData(TEXT("Spacer"), ExecIdx))
			{
				const FString& ExecName = Enum->GetNameStringByIndex(ExecIdx);
				FName ExecOutName = *(ParamPrefix + ExecName);
				auto ExecPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, ExecOutName);
				ExecPin->PinFriendlyName = FText::FromString(ExecName);
			}
		}
	}
	else
	{
		auto ThenPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
		ThenPin->bHidden = bHideThen;
		ThenPin->bAdvancedView = true;
		bHasAdvancedViewPins = !bHideThen;
	}

#if 0
	{
		auto ProxyClassProp = Cast<FClassProperty>(Function->FindPropertyByName(*Function->GetMetaData(FBlueprintMetadata::MD_DynamicOutputType)));
		auto ProxyParamPropName = Function->GetMetaData(FBlueprintMetadata::MD_DynamicOutputParam);
		auto ProxyParamProp = !ProxyParamPropName.IsEmpty() ? Cast<FObjectProperty>(Function->FindPropertyByName(*ProxyParamPropName)) : Cast<FObjectProperty>(Function->GetReturnProperty());
		if (ProxyClassProp && ProxyParamProp && ProxyParamProp->PropertyClass->IsChildOf(ProxyClassProp->MetaClass) && ensure(ProxyParamProp->PropertyClass->IsChildOf(ProxyClass)))
		{
			DynamicClassPinName = ProxyClassProp->GetFName();
			if (!InOldPins)
			{
				DynamicProxyClass = ProxyParamProp->PropertyClass;
			}
			else if (auto Class = GetSpecialPinClass(*InOldPins, DynamicClassPinName))
			{
				DynamicProxyClass = Class;
			}
			else
			{
				DynamicProxyClass = ProxyClass;
			}
		}
	}
#endif

	if (bExposeProxy && ProxyClass)
	{
		UEdGraphPin* ProxyPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, ProxyClass, NeuronAction::AsyncActionName);
		if (!ExposeProxyDisplayName.IsEmpty())
			ProxyPin->PinFriendlyName = ExposeProxyDisplayName;
	}

	SelfObjClassPinName = NAME_None;
	SelfSpawnedPinNames.Reset();
	FClassProperty* SpawnedClassProp = nullptr;

	bool bHasAdvancedPin = false;

	if (FactoryFunc)
	{
		bool bAllPinsGood = true;

		TSet<FName> PinsToHide;
		{
			FBlueprintEditorUtils::GetHiddenPinsForFunction(GetGraph(), FactoryFunc, PinsToHide);
			const FString& HideMetaStr = FactoryFunc->GetMetaData(MetaHidePins);
			TArray<FString> HideList;
			if (!HideMetaStr.IsEmpty())
				HideMetaStr.ParseIntoArray(HideList, TEXT(","), true);
			for (auto&& PinName : HideList)
				PinsToHide.Add(*PinName.TrimStartAndEnd());
		}

		if (Prop)
			PinsToHide.Add(Prop->GetFName());

		SpawnedClassProp = CastField<FClassProperty>(FactoryFunc->FindPropertyByName(ObjectClassPropName));
		if (!SpawnedClassProp)
			SpawnedClassProp = CastField<FClassProperty>(FactoryFunc->FindPropertyByName(*FactoryFunc->GetMetaData(ObjectClassPropName)));

		FDelegateProperty* SpawnedDelegateProp = CastField<FDelegateProperty>(FactoryFunc->FindPropertyByName(SpawnedDelegatePropName));
		if (!SpawnedDelegateProp)
			SpawnedDelegateProp = CastField<FDelegateProperty>(FactoryFunc->FindPropertyByName(*FactoryFunc->GetMetaData(SpawnedDelegatePropName)));

		// spawned class detect
		if (SpawnedClassProp && IsInputParameter(SpawnedClassProp))
		{
			if (SpawnedDelegateProp)
			{
				for (TFieldIterator<FProperty> PropIt(SpawnedDelegateProp->SignatureFunction); PropIt; ++PropIt)
				{
					auto ParamProp = CastField<FObjectProperty>(*PropIt);
					if (ParamProp && ParamProp->PropertyClass == SpawnedClassProp->MetaClass)
					{
						SelfObjClassPinName = SpawnedClassProp->GetFName();
						break;
					}
				}
			}
			else
			{
			}
		}

		const FString DelegatePinPrefix = FString::Printf(TEXT("%c%s%s%s%s"), AffixesSelf.EventPrefix, *FactoryFunc->GetOwnerClass()->GetName(), *FunctionDelimiter, *FactoryFunc->GetName(), *DelegateDelimiter);
		if (!SelfObjClassPinName.IsNone())
		{
			PinsToHide.Add(SpawnedClassProp->GetFName());
			PinsToHide.Add(SpawnedDelegateProp->GetFName());
			FString DelegateExecName = DelegatePinPrefix + SpawnedDelegateProp->GetName();

			auto EventExecPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *DelegateExecName);
			EventExecPin->PinFriendlyName = FText::FromName(SpawnedDelegateProp->GetFName());
			for (TFieldIterator<FProperty> OutPropIt(SpawnedDelegateProp->SignatureFunction); OutPropIt; ++OutPropIt)
			{
				CreatePinFromInnerProp(SpawnedDelegateProp->SignatureFunction, *OutPropIt, DelegateExecName + MemberDelimiter, TEXT("."), EGPD_Output);
			}
		}
		for (TFieldIterator<FProperty> PropIt(FactoryFunc); PropIt && PropIt->HasAnyPropertyFlags(CPF_Parm) && !PropIt->HasAnyPropertyFlags(CPF_ReturnParm); ++PropIt)
		{
			FProperty* ParamProp = *PropIt;
			if (!IsInputParameter(ParamProp) || PinsToHide.Contains(ParamProp->GetFName()) || ParamProp->IsA<FDelegateProperty>())
				continue;
			CreatePinFromInnerProp(FactoryFunc, ParamProp, ParamPrefix);
		}

		DeterminesDelegateTypes.Reset();
		for (TFieldIterator<FProperty> PropIt(FactoryFunc); PropIt && PropIt->HasAnyPropertyFlags(CPF_Parm) && !PropIt->HasAnyPropertyFlags(CPF_ReturnParm); ++PropIt)
		{
			FProperty* ParamProp = *PropIt;
			if (!IsInputParameter(ParamProp) || PinsToHide.Contains(ParamProp->GetFName()) || !ParamProp->IsA<FDelegateProperty>())
				continue;
			FDelegateProperty* DelegateProp = CastFieldChecked<FDelegateProperty>(ParamProp);

			FString DelegateExecName = DelegatePinPrefix + ParamProp->GetName();
			auto EventExecPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *DelegateExecName);
			EventExecPin->PinFriendlyName = FText::FromName(ParamProp->GetFName());

			for (TFieldIterator<FProperty> OutPropIt(DelegateProp->SignatureFunction); OutPropIt; ++OutPropIt)
			{
				CreatePinFromInnerProp(DelegateProp->SignatureFunction, *OutPropIt, DelegateExecName + MemberDelimiter, TEXT("."), EGPD_Output);
			}

			const FString& DeterminesDelegateType = DelegateProp->GetMetaData(NeuronAction::DeterminesDelegateType);
			if (auto DeterminesTypePin = SearchPin(*(ParamPrefix + DeterminesDelegateType), InOldPins, EGPD_Input))
			{
				const FString& OutputPinNames = DelegateProp->GetMetaData(NeuronAction::DynamicDelegateParam);
				TArray<FString> UserDefinedDynamicProperties;
				// There could be more than one dynamic output, so split by comma
				OutputPinNames.ParseIntoArray(UserDefinedDynamicProperties, TEXT(","), true);
				if (IsTypePickerPin(DeterminesTypePin) && UserDefinedDynamicProperties.Num() > 0)
				{
					for (FString& Name : UserDefinedDynamicProperties)
						Name = Name.TrimStartAndEnd();

					for (TFieldIterator<FProperty> OutPropIt(DelegateProp->SignatureFunction); OutPropIt; ++OutPropIt)
					{
						if (!IsInputParameter(*OutPropIt) || !UserDefinedDynamicProperties.Contains(OutPropIt->GetName()))
							continue;

						UClass* BasePickerClass = CastChecked<UClass>(DeterminesTypePin->PinType.PinSubCategoryObject.Get());
						UEdGraphPin* DynamicOutputPin = FindPin(DelegateExecName + MemberDelimiter + (*OutPropIt)->GetName(), EGPD_Output);
						if (!ensure(DynamicOutputPin))
							continue;

						UClass* OutputParamClass = Cast<UClass>(DynamicOutputPin->PinType.PinSubCategoryObject.Get());
						if (ensure(OutputParamClass && BasePickerClass->IsChildOf(OutputParamClass) || OutputParamClass->IsChildOf(BasePickerClass)))
						{
							DeterminesDelegateTypes.FindOrAdd(DeterminesTypePin->GetFName()).Add(DynamicOutputPin->GetFName());
							ConfirmOutputTypes(DeterminesTypePin, InOldPins);
						}
					}
				}
			}
		}
	}

	if (!SelfObjClassPinName.IsNone())
	{
		CreatePinFromInnerProp(SpawnedClassProp->PropertyClass, SpawnedClassProp, ParamPrefix);
		CreateSelfSpawnActions(SpawnedClassProp->PropertyClass, InOldPins);
		CreateDelegatesForClass(SpawnedClassProp->PropertyClass, AffixesProxy, nullptr, InOldPins);
	}
	else
	{
		SpawnedPinNames = CreateImportPinsForClass(ProxyClass, AffixesProxy, true, InOldPins);
		CreateDelegatesForClass(ProxyClass, AffixesProxy, nullptr, InOldPins);

		CreateObjectSpawnPins(ProxyClass, InOldPins);
	}

	CachedNodeTitle.MarkDirty();
}

bool UK2NeuronAction::CreateSelfSpawnActions(UClass* ObjectClass, TArray<UEdGraphPin*>* InOldPins)
{
	if (!BindObjBlueprintCompiledEvent(ObjectClass))
		return false;

	SelfSpawnedPinNames = CreateImportPinsForClass(ObjectClass, AffixesSelf, true, InOldPins);

	if (CreateDelegatesForClass(ObjectClass, AffixesSelf))
	{
		auto UnlinkPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UnlinkSelfObjEventName);
		UnlinkPin->bAdvancedView = true;
		bHasAdvancedViewPins = true;
		
	}
	return true;
}

void UK2NeuronAction::ValidateNodeDuringCompilation(class FCompilerResultsLog& MessageLog) const
{
	Super::ValidateNodeDuringCompilation(MessageLog);
}

void UK2NeuronAction::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);
	CancelPin = nullptr;

	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	check(SourceGraph && K2Schema);

	bool bIsErrorFree = true;
	TSet<FName> SkipPinNames;
	const bool bCompact = IsRunningCommandlet();

	UK2Node_CallFunction* const CallCreateProxyObjectNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallCreateProxyObjectNode->FunctionReference.SetExternalMember(ProxyFactoryFunctionName, ProxyFactoryClass);
	CallCreateProxyObjectNode->AllocateDefaultPins();
	if (CallCreateProxyObjectNode->GetTargetFunction() == nullptr)
	{
		const FText ClassName = ProxyFactoryClass ? FText::FromString(ProxyFactoryClass->GetName()) : LOCTEXT("MissingClassString", "Unknown Class");
		const FString FormattedMessage =
			FText::Format(LOCTEXT("NeuronActionErrorFmt", "NeuronAction: Missing function {0} from class {1} for async task @@"), FText::FromString(ProxyFactoryFunctionName.GetPlainNameString()), ClassName).ToString();

		CompilerContext.MessageLog.Error(*FormattedMessage, this);
		return;
	}

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, GetExecPin(), CallCreateProxyObjectNode->GetExecPin());
	UEdGraphPin* LastThenPin = FindThenPin(CallCreateProxyObjectNode);

	auto TargetFunction = CallCreateProxyObjectNode->GetTargetFunction();
	FString TargetParamPrefix = FString::Printf(TEXT("%c%s%s%s%s"), AffixesSelf.ParamPrefix, *TargetFunction->GetOwnerClass()->GetName(), *FunctionDelimiter, *TargetFunction->GetName(), *MemberDelimiter);
	for (auto ParamPin : CallCreateProxyObjectNode->Pins)
	{
		if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			continue;

		if (ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate)
		{
			UK2Node_CustomEvent* EventNode = MakeEventNode(CompilerContext, SourceGraph, ParamPin->GetName());
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamPin, FindDelegatePin(EventNode));
			EventNode->AutowireNewNode(ParamPin);
			auto EventThenPin = FindThenPin(EventNode);
			FString DelegateExecName = FString::Printf(TEXT("%c%s%s%s%s%s"), AffixesSelf.EventPrefix, *TargetFunction->GetOwnerClass()->GetName(), *FunctionDelimiter, *TargetFunction->GetName(), *DelegateDelimiter, *ParamPin->GetName());
			for (auto EventParamPin : EventNode->Pins)
			{
				if (EventParamPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && EventParamPin->PinName != UK2Node_Event::DelegateOutputName)
				{
					if (UEdGraphPin* EventPin = FindPin(DelegateExecName + MemberDelimiter + EventParamPin->GetName()))
					{
						SkipPinNames.Add(EventPin->GetFName());

						if (!bCompact || HasAnyConnections(EventPin))
							bIsErrorFree &= AssignTempAndGet(CompilerContext, SourceGraph, EventThenPin, EventParamPin, false, EventPin);
						bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*EventPin, *EventParamPin).CanSafeConnect();
					}
				}
			}

			if (auto EventExecPin = FindPin(DelegateExecName))
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventThenPin, EventExecPin);
		}
		else
		{
			if (UEdGraphPin* FuncParamPin = FindPin(TargetParamPrefix + ParamPin->GetName()))
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, FuncParamPin, ParamPin);
		}
	}

	UEdGraphPin* const ProxyObjectPin = CallCreateProxyObjectNode->GetReturnValuePin();
	if (UEdGraphPin* OutputNeuronActionxy = FindPin(NeuronAction::AsyncActionName))
		bIsErrorFree &= !ProxyObjectPin || CompilerContext.MovePinLinksToIntermediate(*OutputNeuronActionxy, *ProxyObjectPin).CanSafeConnect();

	bool bExecutionLinked = false;
	for (auto CurPin : Pins)
	{
		if (CurPin->Direction == EGPD_Output && (HasAnyConnections(CurPin) || NeuronCheckableFlags.Contains(CurPin->PinName))  //
			&& (AffixesSelf.MatchEvent(CurPin->PinName) || AffixesProxy.MatchEvent(CurPin->PinName)))
		{
			bExecutionLinked = true;
			break;
		}
	}

	UClass* SpawnedClass = nullptr;
	UEdGraphPin* SpawnedClassPin = GetSpawnedObjectClassPin(nullptr, &SpawnedClass);
	if (SpawnedClassPin)
	{
		if (!ValidateObjectSpawning(ProxyClass, &CompilerContext, Pins) || !SpawnedClass)
		{
			BreakAllNodeLinks();
			return;
		}
	}
	if (ProxyObjectPin)
	{
		if (ProxyClass)
			bIsErrorFree &= ConnectImportPinsForClass(SpawnedPinNames, ProxyClass, CompilerContext, SourceGraph, LastThenPin, ProxyObjectPin);
		bIsErrorFree &= ConnectLocalFunctions(SkipPinNames, AffixesProxy.ParamPrefix, CompilerContext, SourceGraph, ProxyObjectPin);
	}

	//////////////////////////////////////////////////////////////////////////
	// Connect Delegates
	for (int32 i = 0; bExecutionLinked && ProxyObjectPin && i < Pins.Num(); ++i)
	{
		UEdGraphPin* const CurPin = Pins[i];
		if (SkipPinNames.Contains(CurPin->PinName) || !AffixesProxy.MatchEvent(CurPin->PinName))
			continue;

		FString CurPinName = CurPin->PinName.ToString().Mid(1);
		if (CurPin->Direction == EGPD_Output && CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			bool bShouldCreate = /*!bCompact || */ HasAnyConnections(CurPin) || NeuronCheckableFlags.Contains(CurPin->PinName);
			auto PinMetaInfo = GetPinMetaInfo(CurPinName);
			if (!ensure(PinMetaInfo.OwnerClass))
				continue;

			FMulticastDelegateProperty* MCDProp = PinMetaInfo.SubDelegate;
			if (!ensure(MCDProp))
				continue;

			FString DelegateName = MCDProp->GetName();

			FString EventParamPrefix = FString::Printf(TEXT("%s%s"), *CurPin->PinName.ToString().LeftChop(PinMetaInfo.EnumName.Len()), *MemberDelimiter);
			if (!bShouldCreate)
			{
				for (auto Pin : Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->PinName.ToString().StartsWith(EventParamPrefix) && HasAnyConnections(Pin))
					{
						bShouldCreate = true;
						break;
					}
				}
				if (!bShouldCreate)
					continue;
			}

			SkipPinNames.Add(CurPin->PinName);
			UK2Node_AddDelegate* AddDelegateNode = CompilerContext.SpawnIntermediateNode<UK2Node_AddDelegate>(this, SourceGraph);
			StaticAssignProperty(AddDelegateNode, MCDProp);
			AddDelegateNode->AllocateDefaultPins();
			if (UEdGraphPin* SelfPin = K2Schema->FindSelfPin(*AddDelegateNode, EGPD_Input))
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ProxyObjectPin, SelfPin);
			bIsErrorFree &= ensure(LastThenPin) && TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, AddDelegateNode->GetExecPin());
			LastThenPin = FindThenPin(AddDelegateNode);

			UK2Node_CustomEvent* SpawnedEventNode = MakeEventNode(CompilerContext, SourceGraph, DelegateName);
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, AddDelegateNode->GetDelegatePin(), FindDelegatePin(SpawnedEventNode));
			SpawnedEventNode->AutowireNewNode(AddDelegateNode->GetDelegatePin());
			UEdGraphPin* EventThenPin = FindThenPin(SpawnedEventNode);

			for (int32 j = 0; j < SpawnedEventNode->Pins.Num(); j++)
			{
				auto ParamPin = SpawnedEventNode->Pins[j];
				if (ParamPin->Direction == EGPD_Output && ParamPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && ParamPin->PinName != UK2Node_Event::DelegateOutputName)
				{
					FName FuncParamName = *(EventParamPrefix + ParamPin->PinName.ToString());
					SkipPinNames.Add(FuncParamName);
					UEdGraphPin* FuncParamPin = FindPin(FuncParamName);
					if (ensure(FuncParamPin))
					{
						if (!bCompact || HasAnyConnections(FuncParamPin))
							bIsErrorFree &= AssignTempAndGet(CompilerContext, SourceGraph, EventThenPin, ParamPin, false, FuncParamPin);
						bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FuncParamPin, *ParamPin).CanSafeConnect();
					}
				}
			}

			FProperty* Prop = nullptr;
			UEnum* Enum = nullptr;
			if (!PinMetaInfo.EnumName.IsEmpty() && IsExpandEnumAsExec(MCDProp, &Enum, &Prop) && ensure(Enum->GetIndexByName(*PinMetaInfo.EnumName) != INDEX_NONE) && ensure(SpawnedEventNode->FindPin(Prop->GetFName())))
			{
				UK2Node_SwitchEnum* SwitchEnumNode = CompilerContext.SpawnIntermediateNode<UK2Node_SwitchEnum>(this, SourceGraph);
				SwitchEnumNode->Enum = Enum;
				SwitchEnumNode->AllocateDefaultPins();
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventThenPin, SwitchEnumNode->GetExecPin());
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SpawnedEventNode->FindPin(Prop->GetFName()), SwitchEnumNode->GetSelectionPin());

				int32 NumExecs = (Enum->NumEnums() - 1);
				int32 NumExecsDisplayed = 0;
				for (int32 ExecIdx = 0; ExecIdx < NumExecs; ExecIdx++)
				{
					if (!Enum->HasMetaData(TEXT("Hidden"), ExecIdx) || Enum->HasMetaData(TEXT("Spacer"), ExecIdx))
					{
						const FString& ExecName = Enum->GetNameStringByIndex(ExecIdx);
						FName ExecOutName = *(EventParamPrefix + ExecName);
						SkipPinNames.Add(ExecOutName);

						UEdGraphPin* ExecOutPin = FindPin(ExecOutName);
						auto CasePin = SwitchEnumNode->FindPin(*ExecName);
						if (ensure(ExecOutPin && CasePin))
						{
							bIsErrorFree &= CompilerContext.CopyPinLinksToIntermediate(*ExecOutPin, *CasePin).CanSafeConnect();
						}
					}
				}
			}
			else
			{
				bIsErrorFree &= CompilerContext.CopyPinLinksToIntermediate(*CurPin, *EventThenPin).CanSafeConnect();
			}

			if (CurPin->LinkedTo.Num() == 0 && NeuronCheckableFlags.Contains(CurPin->PinName))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventThenPin, GetCancelPin(CompilerContext, SourceGraph));
			}
		}
	}

	if (UEdGraphPin* SpawnedObjPin = (ProxyObjectPin && SpawnedClassPin) ? ConnectObjectSpawnPins(ProxyClass, CompilerContext, SourceGraph, LastThenPin, ProxyObjectPin) : nullptr)
	{
		bool bEventLinked = !bCompact;
		for (int32 i = 0; i < Pins.Num(); ++i)
		{
			auto CurPin = Pins[i];
			if (CurPin && CurPin->Direction == EGPD_Output && CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && HasAnyConnections(CurPin) && AffixesObject.MatchEvent(CurPin->PinName))
			{
				bEventLinked = true;
				break;
			}
		}

		auto UnlinkObjEventThenPin = FindPin(UnlinkObjEventName);
		const bool bRemoveLinked = UnlinkObjEventThenPin && HasAnyConnections(UnlinkObjEventThenPin);
		UEdGraphPin* FirstRemoveDelegatePin = nullptr;
		for (int32 i = 0; bEventLinked && i < Pins.Num(); ++i)
		{
			UEdGraphPin* const CurPin = Pins[i];
			if (SkipPinNames.Contains(CurPin->PinName) || !AffixesObject.MatchEvent(CurPin->PinName))
				continue;

			FString CurPinName = CurPin->PinName.ToString().Mid(1);
			if (CurPin->Direction == EGPD_Output && CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				bool bShouldCreate = !bCompact || HasAnyConnections(CurPin);
				auto PinMetaInfo = GetPinMetaInfo(CurPinName);
				if (!ensure(PinMetaInfo.OwnerClass))
					continue;

				FMulticastDelegateProperty* MCDProp = PinMetaInfo.SubDelegate;
				if (!ensure(MCDProp))
					continue;

				FString DelegateName = MCDProp->GetName();

				FString EventParamPrefix = FString::Printf(TEXT("%s%s"), *CurPin->PinName.ToString().LeftChop(PinMetaInfo.EnumName.Len()), *MemberDelimiter);
				if (!bShouldCreate)
				{
					for (auto Pin : Pins)
					{
						if (Pin && Pin->Direction == EGPD_Output && Pin->PinName.ToString().StartsWith(EventParamPrefix) && HasAnyConnections(Pin))
						{
							bShouldCreate = true;
							break;
						}
					}
					if (!bShouldCreate)
						continue;
				}

				UK2Node_AddDelegate* AddDelegateNode = CompilerContext.SpawnIntermediateNode<UK2Node_AddDelegate>(this, SourceGraph);
				StaticAssignProperty(AddDelegateNode, MCDProp, false);
				AddDelegateNode->AllocateDefaultPins();

				bIsErrorFree &= ensure(LastThenPin) && TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, AddDelegateNode->GetExecPin());
				LastThenPin = FindThenPin(AddDelegateNode);
				SpawnedObjPin = PureCastTo(CompilerContext, SourceGraph, SpawnedObjPin, AddDelegateNode->FindPin(UEdGraphSchema_K2::PN_Self));

				UK2Node_CustomEvent* SpawnedEventNode = MakeEventNode(CompilerContext, SourceGraph, DelegateName);
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, AddDelegateNode->GetDelegatePin(), FindDelegatePin(SpawnedEventNode));
				SpawnedEventNode->AutowireNewNode(AddDelegateNode->GetDelegatePin());

				if (bRemoveLinked)
				{
					UK2Node_RemoveDelegate* RemoveDelegateNode = CompilerContext.SpawnIntermediateNode<UK2Node_RemoveDelegate>(this, SourceGraph);
					StaticAssignProperty(RemoveDelegateNode, MCDProp, false);
					RemoveDelegateNode->AllocateDefaultPins();

					bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, RemoveDelegateNode->GetDelegatePin(), FindDelegatePin(SpawnedEventNode));
					UEdGraphPin* BoolSuccessPin = SpawnedObjPin;
					SpawnedObjPin = PureCastTo(CompilerContext, SourceGraph, SpawnedObjPin, RemoveDelegateNode->FindPinChecked(UEdGraphSchema_K2::PN_Self), &BoolSuccessPin);
					if (!FirstRemoveDelegatePin)
					{
						FirstRemoveDelegatePin = BranchExec(CompilerContext, SourceGraph, BoolSuccessPin, RemoveDelegateNode->GetExecPin(), nullptr);
					}
					else
					{
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, UnlinkObjEventThenPin, RemoveDelegateNode->GetExecPin());
					}
					UnlinkObjEventThenPin = FindThenPin(RemoveDelegateNode);
				}

				UEdGraphPin* EventThenPin = FindThenPin(SpawnedEventNode, true);

				for (int32 j = 0; j < SpawnedEventNode->Pins.Num(); j++)
				{
					auto ParamPin = SpawnedEventNode->Pins[j];
					if (ParamPin->Direction == EGPD_Output && ParamPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && ParamPin->PinName != UK2Node_Event::DelegateOutputName)
					{
						FName FuncParamName = *(EventParamPrefix + ParamPin->PinName.ToString());
						SkipPinNames.Add(FuncParamName);

						UEdGraphPin* FuncParamPin = FindPin(FuncParamName);
						if (ensure(FuncParamPin))
						{
							if (!bCompact || HasAnyConnections(FuncParamPin))
								bIsErrorFree &= AssignTempAndGet(CompilerContext, SourceGraph, EventThenPin, ParamPin, false, FuncParamPin);
							bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FuncParamPin, *ParamPin).CanSafeConnect();
						}
					}
				}

				FProperty* Prop = nullptr;
				UEnum* Enum = nullptr;
				if (!PinMetaInfo.EnumName.IsEmpty() && IsExpandEnumAsExec(MCDProp, &Enum, &Prop) && ensure(Enum->GetIndexByName(*PinMetaInfo.EnumName) != INDEX_NONE) && ensure(SpawnedEventNode->FindPin(Prop->GetFName())))
				{
					UK2Node_SwitchEnum* SwitchEnumNode = CompilerContext.SpawnIntermediateNode<UK2Node_SwitchEnum>(this, SourceGraph);
					SwitchEnumNode->Enum = Enum;
					SwitchEnumNode->AllocateDefaultPins();
					bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventThenPin, SwitchEnumNode->GetExecPin());
					bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SpawnedEventNode->FindPin(Prop->GetFName()), SwitchEnumNode->GetSelectionPin());

					int32 NumExecs = (Enum->NumEnums() - 1);
					int32 NumExecsDisplayed = 0;
					for (int32 ExecIdx = 0; ExecIdx < NumExecs; ExecIdx++)
					{
						if (!Enum->HasMetaData(TEXT("Hidden"), ExecIdx) || Enum->HasMetaData(TEXT("Spacer"), ExecIdx))
						{
							const FString& ExecName = Enum->GetNameStringByIndex(ExecIdx);
							FName ExecOutName = *(EventParamPrefix + ExecName);
							SkipPinNames.Add(ExecOutName);

							UEdGraphPin* ExecOutPin = FindPin(ExecOutName);
							auto CasePin = SwitchEnumNode->FindPin(*ExecName);
							if (ensure(ExecOutPin && CasePin))
							{
								bIsErrorFree &= CompilerContext.CopyPinLinksToIntermediate(*ExecOutPin, *CasePin).CanSafeConnect();
							}
						}
					}
				}
				else
				{
					bIsErrorFree &= CompilerContext.CopyPinLinksToIntermediate(*(CurPin), *EventThenPin).CanSafeConnect();
				}
			}
		}
		if (FirstRemoveDelegatePin && UnlinkObjEventThenPin)
		{
			bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*(UnlinkObjEventThenPin), *FirstRemoveDelegatePin).CanSafeConnect();
		}
	}

	auto ImportFunc = GetAlternativeAction(ProxyClass);
	const bool bHasImportFunc = !!ImportFunc;
	if (bHasImportFunc)
	{
		TArray<UEdGraphPin*> ParamPins;
		ParamPins.Reserve(ImportedPinNames.Num());
		for (auto i = 0; i < ImportedPinNames.Num(); ++i)
			ParamPins.Add(FindPinChecked(ImportedPinNames[i]));
		bIsErrorFree &= ConnectMessagePins(CompilerContext, SourceGraph, CallCreateProxyObjectNode, ParamPins);

		UK2Node_CallFunction* ImportFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		ImportFuncNode->SetFromFunction(ImportFunc);
		ImportFuncNode->AllocateDefaultPins();

		if (UEdGraphPin* SelfPin = K2Schema->FindSelfPin(*ImportFuncNode, EGPD_Input))
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ProxyObjectPin, SelfPin);
		bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, LastThenPin, ImportFuncNode->GetExecPin());
	}

	FProperty* Prop = nullptr;
	UEnum* Enum = nullptr;
	if (IsExpandEnumAsExec(TargetFunction, &Enum, &Prop))
	{
		UK2Node_SwitchEnum* SwitchEnumNode = CompilerContext.SpawnIntermediateNode<UK2Node_SwitchEnum>(this, SourceGraph);
		SwitchEnumNode->Enum = Enum;
		SwitchEnumNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, SwitchEnumNode->GetExecPin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, CallCreateProxyObjectNode->FindPinChecked(Prop->GetFName()), SwitchEnumNode->GetSelectionPin());

		int32 NumExecs = (Enum->NumEnums() - 1);
		int32 NumExecsDisplayed = 0;
		for (int32 ExecIdx = 0; ExecIdx < NumExecs; ExecIdx++)
		{
			if (!Enum->HasMetaData(TEXT("Hidden"), ExecIdx) || Enum->HasMetaData(TEXT("Spacer"), ExecIdx))
			{
				const FString& ExecName = Enum->GetNameStringByIndex(ExecIdx);
				FName ExecOutName = *(TargetParamPrefix + ExecName);
				SkipPinNames.Add(ExecOutName);

				UEdGraphPin* ExecOutPin = FindPin(ExecOutName);
				auto CasePin = SwitchEnumNode->FindPin(*ExecName);
				if (ensure(ExecOutPin && CasePin))
				{
					bIsErrorFree &= CompilerContext.CopyPinLinksToIntermediate(*ExecOutPin, *CasePin).CanSafeConnect();
				}
			}
		}
	}
	else if (auto ThenPin = FindThenPin(this, false))
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, ThenPin);
	}

	if (!ensure(bIsErrorFree))
	{
		CompilerContext.MessageLog.Error(TEXT("UK2NeuronAction: Internal connection error. @@"), this);
	}
	BreakAllNodeLinks();
}

void UK2NeuronAction::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	Super::PinDefaultValueChanged(Pin);
	if (DeterminesDelegateTypes.Contains(Pin->GetFName()))
	{
		ConfirmOutputTypes(Pin);
	}

	if (IsObjectClassPin(Pin))
	{
		OnSpawnedObjectClassChanged(ProxyClass);
	}
}

void UK2NeuronAction::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (DeterminesDelegateTypes.Contains(Pin->GetFName()))
	{
		ConfirmOutputTypes(Pin);
	}

	if (IsObjectClassPin(Pin))
	{
		OnSpawnedObjectClassChanged(ProxyClass);
	}
	else if (Pin->PinName == SelfObjClassPinName)
	{
		OnSelfSpawnedObjectClassChanged(ClassFromPin(Pin));
	}
}

void UK2NeuronAction::OnSelfSpawnedObjectClassChanged(UClass* ObjectClass, const TArray<UEdGraphPin*>* InPinsToSearch)
{
	auto SpawnedObjClass = GetSpawnedObjectClass(InPinsToSearch);
	CachedNodeTitle.MarkDirty();

	{
		const UEdGraphSchema_K2* K2Schema = GetK2Schema();

		TSet<FName> RemovingPinNames;
		RemovingPinNames.Add(UnlinkSelfObjEventName);
		RemovingPinNames.Append(MoveTemp(SelfSpawnedPinNames));
		RemovingPinNames.Append(MoveTemp(SelfImportPinNames));
		TArray<UEdGraphPin*> ToBeRemoved;

		ToBeRemoved.Append(RemovePinsByName(RemovingPinNames));

		CreateSelfSpawnActions(ObjectClass, &ToBeRemoved);

		RestoreSplitPins(ToBeRemoved);
		RewireOldPinsToNewPins(ToBeRemoved, Pins, nullptr);
		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

void UK2NeuronAction::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
}

UEdGraph* UK2NeuronAction::GetFunctionGraph(const UEdGraphNode*& OutGraphNode) const
{
	OutGraphNode = nullptr;
	UBlueprintGeneratedClass* ParentClass = Cast<UBlueprintGeneratedClass>(ProxyFactoryClass);
	if (ParentClass && ParentClass->ClassGeneratedBy)
	{
		UBlueprint* Blueprint = Cast<UBlueprint>(ParentClass->ClassGeneratedBy);
		while (Blueprint)
		{
			UEdGraph* TargetGraph = nullptr;
			const FName FunctionName = ProxyFactoryFunctionName;
			for (UEdGraph* const Graph : Blueprint->FunctionGraphs)
			{
				if (Graph->GetFName() == FunctionName)
				{
					TargetGraph = Graph;
					break;
				}
			}

			if (!TargetGraph)
			{
				for (const FBPInterfaceDescription& Interface : Blueprint->ImplementedInterfaces)
				{
					for (UEdGraph* const Graph : Interface.Graphs)
					{
						if (Graph->GetFName() == FunctionName)
						{
							TargetGraph = Graph;
							break;
						}
					}

					if (TargetGraph)
					{
						break;
					}
				}
			}

			if ((TargetGraph != nullptr) && !TargetGraph->HasAnyFlags(RF_Transient))
			{
				// Found the function graph in a Blueprint, return that graph
				return TargetGraph;
			}
			else
			{
				// Did not find the function call as a graph, it may be a custom event
				UK2Node_CustomEvent* CustomEventNode = nullptr;

				TArray<UK2Node_CustomEvent*> CustomEventNodes;
				FBlueprintEditorUtils::GetAllNodesOfClass(Blueprint, CustomEventNodes);

				for (UK2Node_CustomEvent* const CustomEvent : CustomEventNodes)
				{
					if (CustomEvent->CustomFunctionName == ProxyFactoryFunctionName)
					{
						OutGraphNode = CustomEvent;
						return CustomEvent->GetGraph();
					}
				}
			}

			ParentClass = Cast<UBlueprintGeneratedClass>(Blueprint->ParentClass);
			Blueprint = ParentClass ? Cast<UBlueprint>(ParentClass->ClassGeneratedBy) : nullptr;
		}
	}
	return nullptr;
}

UObject* UK2NeuronAction::GetJumpTargetForDoubleClick() const
{
	// If there is an event node, jump to it, otherwise jump to the function graph
	const UEdGraphNode* ResultEventNode = nullptr;
	UEdGraph* FunctionGraph = GetFunctionGraph(ResultEventNode);
	if (ResultEventNode != nullptr)
	{
		return const_cast<UEdGraphNode*>(ResultEventNode);
	}
	else
	{
		return FunctionGraph;
	}
}

bool UK2NeuronAction::CanJumpToDefinition() const
{
	bool bRet = false;
	do
	{
		bRet = ProxyClass && !!Cast<UBlueprint>(ProxyClass->ClassGeneratedBy);
		if (bRet)
			break;

		const UFunction* TargetFunction = GetFactoryFunction();
		const bool bNativeFunction = (TargetFunction != nullptr) && (TargetFunction->IsNative());
		bRet = bNativeFunction || (GetJumpTargetForDoubleClick() != nullptr);
		if (bRet)
			break;

		bRet = Super::CanJumpToDefinition();
		if (bRet)
			break;

	} while (0);
	return bRet;
}

void UK2NeuronAction::JumpToDefinition() const
{
	if (UBlueprint* BlueprintToEdit = ProxyClass ? Cast<UBlueprint>(ProxyClass->ClassGeneratedBy) : nullptr)
	{
		GEditor->EditObject(BlueprintToEdit);
		return;
	}

	UFunction* TargetFunction = GetFactoryFunction();
	if (TargetFunction && TargetFunction->IsNative())
	{
		// First try the nice way that will get to the right line number
		bool bSucceeded = false;
		const bool bNavigateToNativeFunctions = GetDefault<UBlueprintEditorSettings>()->bNavigateToNativeFunctionsFromCallNodes;

		if (bNavigateToNativeFunctions)
		{
			if (FSourceCodeNavigation::CanNavigateToFunction(TargetFunction))
			{
				bSucceeded = FSourceCodeNavigation::NavigateToFunction(TargetFunction);
			}

			// Failing that, fall back to the older method which will still get the file open assuming it exists
			if (!bSucceeded)
			{
				FString NativeParentClassHeaderPath;
				const bool bFileFound = FSourceCodeNavigation::FindClassHeaderPath(TargetFunction, NativeParentClassHeaderPath) && (IFileManager::Get().FileSize(*NativeParentClassHeaderPath) != INDEX_NONE);
				if (bFileFound)
				{
					const FString AbsNativeParentClassHeaderPath = FPaths::ConvertRelativePathToFull(NativeParentClassHeaderPath);
					bSucceeded = FSourceCodeNavigation::OpenSourceFile(AbsNativeParentClassHeaderPath);
				}
			}
		}
		else
		{
			// Inform user that the function is native, give them opportunity to enable navigation to native functions:
			FNotificationInfo Info(LOCTEXT("NavigateToNativeDisabled", "Navigation to Native (c++) Functions Disabled"));
			Info.ExpireDuration = 10.0f;
			Info.CheckBoxState = bNavigateToNativeFunctions ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;

			Info.CheckBoxStateChanged = FOnCheckStateChanged::CreateStatic([](ECheckBoxState NewState) {
				const FScopedTransaction Transaction(LOCTEXT("ChangeNavigateToNativeFunctionsFromCallNodes", "Change Navigate to Native Functions from Call Nodes Setting"));

				UBlueprintEditorSettings* MutableEditorSetings = GetMutableDefault<UBlueprintEditorSettings>();
				MutableEditorSetings->Modify();
				MutableEditorSetings->bNavigateToNativeFunctionsFromCallNodes = (NewState == ECheckBoxState::Checked) ? true : false;
				MutableEditorSetings->SaveConfig();
			});
			Info.CheckBoxText = LOCTEXT("EnableNavigationToNative", "Navigate to Native Functions from Blueprint Call Nodes?");

			FSlateNotificationManager::Get().AddNotification(Info);
		}
		return;
	}

	Super::JumpToDefinition();
}

FText UK2NeuronAction::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle)
	{
		return GetNodeTitleImpl();
	}

	if (CachedNodeTitle.IsOutOfDate(this))
	{
		CachedNodeTitle.SetCachedText(GetNodeTitleImpl(), this);
	}
	return CachedNodeTitle;
}

FName UK2NeuronAction::GetCornerIcon() const
{
	return TEXT("Graph.Latent.LatentIcon");
}

FEdGraphNodeDeprecationResponse UK2NeuronAction::GetDeprecationResponse(EEdGraphNodeDeprecationType DeprecationType) const
{
	FEdGraphNodeDeprecationResponse Response = Super::GetDeprecationResponse(DeprecationType);
	if (DeprecationType == EEdGraphNodeDeprecationType::NodeHasDeprecatedReference)
	{
		UFunction* Function = GetFactoryFunction();
		if (ensureMsgf(Function != nullptr, TEXT("This node should not be able to report having a deprecated reference if the target function cannot be resolved.")))
		{
			FString DetailedMessage = Function->GetMetaData(FBlueprintMetadata::MD_DeprecationMessage);
			Response.MessageText = FBlueprintEditorUtils::GetDeprecatedMemberUsageNodeWarning(UK2Node_CallFunction::GetUserFacingFunctionName(GetFactoryFunction()), FText::FromString(DetailedMessage));
		}
	}

	return Response;
}

FText UK2NeuronAction::GetNodeTitleImpl(TArray<UEdGraphPin*>* InPinsToSearch) const
{
	if (GetFactoryFunction() == nullptr)
	{
		return FText(LOCTEXT("UK2NeuronActionGetNodeTitle", "Async Task: Missing Function"));
	}
	const FText FunctionToolTipText = UK2Node_CallFunction::GetUserFacingFunctionName(GetFactoryFunction());
	return FunctionToolTipText;
}

void UK2NeuronAction::FillActionDefaultCancelFlags(UClass* InClass)
{
	if (!InClass || !InClass->IsChildOf<UGameplayTask>())
		return;

	for (TFieldIterator<FMulticastDelegateProperty> It(InClass); It; ++It)
	{
		if (HasSpecialExportTag(*It) && It->GetBoolMetaData("CancelFlag") == true)
		{
			NeuronCheckableFlags.Add(*FString::Printf(TEXT("%c%s%s%s"), AffixesProxy.EventPrefix, *InClass->GetName(), *DelegateDelimiter, *It->GetName()));
		}
	}
}

UEdGraphPin* UK2NeuronAction::GetCancelPin(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	if (!CancelPin)
	{
		UK2Node_CallFunction* CancelExecution = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		CancelExecution->SetFromFunction(GMP_UFUNCTION_CHECKED(UGameplayTask, EndTask));
		CancelExecution->AllocateDefaultPins();
		CancelPin = CancelExecution->GetExecPin();
	}
	return CancelPin;
}

void UK2NeuronAction::ConfirmOutputTypes(UEdGraphPin* InTypePin, TArray<UEdGraphPin*>* InOldPins)
{
	bool bModified = false;

	auto FixPinType = [&](UEdGraphPin* TypePin, const TArray<FName>& Params, const TArray<UEdGraphPin*>* InOldPins) {
		UClass* PickedClass = ClassFromPin(TypePin);
		if (!ensure(PickedClass))
			return;
		for (auto Param : Params)
		{
			auto ParamPin = SearchPin(Param, InOldPins);
			UClass* OutputParamClass = ParamPin ? Cast<UClass>(ParamPin->PinType.PinSubCategoryObject.Get()) : nullptr;
			if (ensure(OutputParamClass) /*&& !PickedClass->IsChildOf(OutputParamClass) */ && PickedClass != OutputParamClass)
			{
				ParamPin->PinType.PinSubCategoryObject = PickedClass;
				auto Info = GetPinMetaInfo(ParamPin->PinName.ToString().Mid(1));
				if (ensure(Info.FuncDelegate))
				{
					FString PinDescName = DelimiterStr;
					PinDescName.Append(GetDisplayString(Info.FuncDelegate));
					PinDescName.Append(FString::Printf(TEXT(" (parameter of %s)"), *Info.DelegateName));
					ParamPin->PinToolTip = FString::Printf(TEXT("%s\n%s\n%s"), *PinDescName, *GetK2Schema()->TypeToText(ParamPin->PinType).ToString(), *Info.FuncDelegate->GetToolTipText().ToString());
				}
				if (!InOldPins)
				{
					bModified = true;
					Modify();
				}
			}
		}
	};

	if (!InTypePin)
	{
		for (auto It = DeterminesDelegateTypes.CreateIterator(); It; ++It)
		{
			if (auto TypePin = SearchPin(It->Key, InOldPins))
			{
				FixPinType(TypePin, It->Value, InOldPins);
			}
			else
			{
				It.RemoveCurrent();
				bModified = true;
				Modify();
			}
		}
	}
	else if (auto ParamName = DeterminesDelegateTypes.Find(InTypePin->PinName))
	{
		FixPinType(InTypePin, *ParamName, InOldPins);
	}

	if (InTypePin && bModified)
	{
		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

bool UK2NeuronAction::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const
{
	const UBlueprint* SourceBlueprint = GetBlueprint();

	bool bSelfResult = false;
	if (auto SelfObjClassPin = FindPin(SelfObjClassPinName))
	{
		if (auto SelfObjClass = ClassFromPin(SelfObjClassPin))
		{
			bSelfResult = !OptionalOutput->Contains(SelfObjClass);
			OptionalOutput->AddUnique(SelfObjClass);
		}
	}

	const bool bProxyFactoryResult = (ProxyFactoryClass != NULL) && (ProxyFactoryClass->ClassGeneratedBy != SourceBlueprint);
	if (bProxyFactoryResult && OptionalOutput)
	{
		OptionalOutput->AddUnique(ProxyFactoryClass);
	}

	const bool bProxyResult = ProxyClass && (ProxyClass->ClassGeneratedBy != SourceBlueprint);
	if (bProxyResult && OptionalOutput)
	{
		OptionalOutput->AddUnique(ProxyClass);
	}

	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	return bSelfResult || bProxyFactoryResult || bProxyResult || bSuperResult;
}

UFunction* UK2NeuronAction::GetFactoryFunction() const
{
	if (ProxyFactoryClass == nullptr)
	{
		if (!HasAnyFlags(RF_ClassDefaultObject))
			UE_LOG(LogBlueprint, Error, TEXT("ProxyFactoryClass null in %s. Was a class deleted or saved on a non promoted build?"), *GetFullName());
		return nullptr;
	}

	FMemberReference FunctionReference;
	FunctionReference.SetExternalMember(ProxyFactoryFunctionName, ProxyFactoryClass);

	UFunction* FactoryFunction = FunctionReference.ResolveMember<UFunction>(GetBlueprint());

	if (FactoryFunction == nullptr)
	{
		FactoryFunction = ProxyFactoryClass->FindFunctionByName(ProxyFactoryFunctionName);
		UE_CLOG(FactoryFunction == nullptr, LogBlueprint, Error, TEXT("FactoryFunction %s null in %s. Was a class deleted or saved on a non promoted build?"), *ProxyFactoryFunctionName.ToString(), *GetFullName());
	}

	return FactoryFunction;
}

void UK2NeuronAction::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	if (!ActionRegistrar.IsOpenForRegistration(GetClass()))
		return;

	struct GetMenuActions_Utils
	{
		static void SetNodeFunc(UEdGraphNode* NewNode, bool /*bIsTemplateNode*/, TWeakObjectPtr<UFunction> FunctionPtr)
		{
			if (UFunction* FactoryFunc = FunctionPtr.Get())
			{
				UK2NeuronAction* NeuronActionNode = CastChecked<UK2NeuronAction>(NewNode);

				NeuronActionNode->ProxyFactoryFunctionName = FactoryFunc->GetFName();
				NeuronActionNode->ProxyFactoryClass = FactoryFunc->GetOuterUClass();

				NeuronActionNode->RestrictedClasses.Reset();
				if (FactoryFunc->HasMetaData(FBlueprintMetadata::MD_RestrictedToClasses))
				{
					FString const& ClassRestrictions = FactoryFunc->GetMetaData(FBlueprintMetadata::MD_RestrictedToClasses);

					// Parse the the metadata into an array that is delimited by ',' and trim whitespace
					TArray<FString> RestrictedClasses;
					ClassRestrictions.ParseIntoArray(RestrictedClasses, TEXT(","));
					for (FString& ValidClassName : NeuronActionNode->RestrictedClasses)
						NeuronActionNode->RestrictedClasses.Emplace(ValidClassName.TrimStartAndEnd());
				}

				FObjectProperty* ReturnProp = CastField<FObjectProperty>(FactoryFunc->GetReturnProperty());
				if (ReturnProp && !ReturnProp->IsA<FClassProperty>())
				{
					NeuronActionNode->ProxyClass = ReturnProp->PropertyClass;

					if (ReturnProp->PropertyClass->HasMetaData(FBlueprintMetadata::MD_RestrictedToClasses))
					{
						FString ClassRestrictions;
						auto TestClass = ReturnProp->PropertyClass;
						while (ClassRestrictions.IsEmpty() && TestClass)
						{
							ClassRestrictions = TestClass->GetMetaData(FBlueprintMetadata::MD_RestrictedToClasses);
							TestClass = TestClass->GetSuperClass();
						}

						TArray<FString> SubRestrictedClasses;
						ClassRestrictions.ParseIntoArray(SubRestrictedClasses, TEXT(","));

						if (NeuronActionNode->RestrictedClasses.Num() > 0 && SubRestrictedClasses.Num() > 0)
						{
							for (auto i = SubRestrictedClasses.Num() - 1; i >= 0; --i)
							{
								FString& ValidClassName = SubRestrictedClasses[i];
								ValidClassName = ValidClassName.TrimStartAndEnd();
								if (!NeuronActionNode->RestrictedClasses.Contains(ValidClassName))
								{
									SubRestrictedClasses.RemoveAtSwap(i);
								}

								NeuronActionNode->RestrictedClasses.Reset();
								if (SubRestrictedClasses.Num() > 0)
								{
									NeuronActionNode->RestrictedClasses.Append(SubRestrictedClasses);
								}
								else
								{
									// ensureMsgf(false, TEXT("conflict of RestrictedToClasses"));
									NeuronActionNode->RestrictedClasses.Add(TEXT("None"));
								}
							}
						}
						else
						{
							for (FString& ValidClassName : NeuronActionNode->RestrictedClasses)
								NeuronActionNode->RestrictedClasses.Emplace(ValidClassName.TrimStartAndEnd());
						}
					}

					auto NeuronActionName = NeuronActionNode->ProxyClass->GetMetaData(NeuronAction::NeuronMeta);
					if (!NeuronActionName.IsEmpty() && ensure(NeuronActionNode->ProxyClass->FindFunctionByName(*NeuronActionName)))
					{
						NeuronActionNode->ProxyActivateFunctionName = *NeuronActionName;
					}
					else if (NeuronActionNode->ProxyClass->IsChildOf<UGameplayTask>())
					{
						NeuronActionNode->ProxyActivateFunctionName = GET_FUNCTION_NAME_CHECKED(UGameplayTask, ReadyForActivation);
					}
					else if (NeuronActionNode->ProxyClass->IsChildOf<UBlueprintAsyncActionBase>())
					{
						NeuronActionNode->ProxyActivateFunctionName = GET_FUNCTION_NAME_CHECKED(UBlueprintAsyncActionBase, Activate);
					}
				}
				else
				{
					NeuronActionNode->ProxyClass = nullptr;
					NeuronActionNode->ProxyActivateFunctionName = NAME_None;
				}
			}
		}
	};

	UClass* NodeClass = GetClass();
	auto FuncSpawner = [NodeClass](const UFunction* FactoryFunc) -> UBlueprintNodeSpawner* {
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintFunctionNodeSpawner::Create(FactoryFunc);
		check(NodeSpawner != nullptr);
		NodeSpawner->NodeClass = NodeClass;
		TWeakObjectPtr<UFunction> FunctionPtr = MakeWeakObjectPtr(const_cast<UFunction*>(FactoryFunc));
		NodeSpawner->CustomizeNodeDelegate = UBlueprintNodeSpawner::FCustomizeNodeDelegate::CreateStatic(GetMenuActions_Utils::SetNodeFunc, FunctionPtr);

		return NodeSpawner;
	};

	static auto IsFactoryMethod = [](UFunction* Function, const UClass* InProxyType = nullptr) {
		if (!Function->HasAllFunctionFlags(FUNC_Static | FUNC_BlueprintCallable))
			return false;
		if (!Function->HasMetaData(NeuronAction::NeuronMeta) && !Function->GetOwnerClass()->HasMetaData(NeuronAction::NeuronMetaFactory))
			return false;

		if (InProxyType && !Function->HasMetaData(NeuronAction::NeuronMeta))
			return false;

		if (!Function->HasMetaData(FBlueprintMetadata::MD_DeprecatedFunction) || GetDefault<UBlueprintEditorSettings>()->bExposeDeprecatedFunctions)
		{
			FObjectProperty* ReturnProperty = CastField<FObjectProperty>(Function->GetReturnProperty());
			bool bIsFactoryMethod = !!ReturnProperty && !ReturnProperty->IsA<FClassProperty>() && (!InProxyType || ReturnProperty->PropertyClass->IsChildOf(InProxyType));
			if (!bIsFactoryMethod)
			{
				for (TFieldIterator<FProperty> FuncPropIt(Function); FuncPropIt; ++FuncPropIt)
				{
					FProperty* FuncProp = *FuncPropIt;
					if (FuncProp->IsA<FDelegateProperty>() && UK2Neuron::IsInputParameter(FuncProp))
					{
						return true;
					}
				}
			}
			return bIsFactoryMethod;
		}
		else
		{
			return false;
		}
	};

	static auto RegisterClassFactoryActions = [](FBlueprintActionDatabaseRegistrar& ActionRegistrar, const auto& InFuncSpawner, UClass* FactoryClass = nullptr, UClass* ProxyClassType = nullptr) {
		int32 RegisteredCount = 0;
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			UClass* TestClass = *ClassIt;
			if (TestClass->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated))
				continue;

			if (ProxyClassType || (FactoryClass && TestClass->IsChildOf(FactoryClass)) || TestClass->HasMetaData(NeuronAction::NeuronMeta) || TestClass->HasMetaData(NeuronAction::NeuronMetaFactory))
			{
				// UE_LOG(LogTemp, Log, TEXT("NeuronAction:%s"), *TestClass->GetName());
				for (TFieldIterator<UFunction> FuncIt(TestClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
				{
					UFunction* Function = *FuncIt;
					if (!IsFactoryMethod(Function, ProxyClassType))
					{
						continue;
					}
					else if (UBlueprintNodeSpawner* NodeSpawner = InFuncSpawner(Function))
					{
						if (!Function->GetBoolMetaData(FBlueprintMetadata::MD_BlueprintInternalUseOnly))
						{
							auto& MetaValCategory = Function->GetOwnerClass()->GetMetaData(NeuronAction::NeuronMeta);
							NodeSpawner->DefaultMenuSignature.Category = FText::FromString(MetaValCategory.IsEmpty() ? FString::Printf(TEXT("NeuronAction|%s"), *NodeSpawner->DefaultMenuSignature.Category.ToString()) : MetaValCategory);

							auto& MetaValMenuName = Function->GetMetaData(NeuronAction::NeuronMeta);
							if (!MetaValMenuName.IsEmpty())
								NodeSpawner->DefaultMenuSignature.MenuName = FText::FromString(MetaValMenuName);
						}

						RegisteredCount += ActionRegistrar.AddBlueprintAction(Function, NodeSpawner) ? 1 : 0;
					}
				}
			}
		}
		return RegisteredCount;
	};

	auto OnAssetsLoaded = [this, &ActionRegistrar, FuncSpawner] { RegisterClassFactoryActions(ActionRegistrar, FuncSpawner, UNeuronActionFactory::StaticClass()); };

	OnAssetsLoaded();
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	if (AssetRegistry.IsLoadingAssets())
	{
		AssetRegistry.OnFilesLoaded().AddLambda(OnAssetsLoaded);
	}
	else
	{
		//OnAssetsLoaded();
	}
}
UFunction* UK2NeuronAction::GetAlternativeAction(UClass* InClass) const
{
	return InClass ? InClass->FindFunctionByName(ProxyActivateFunctionName) : nullptr;
}

#undef LOCTEXT_NAMESPACE
