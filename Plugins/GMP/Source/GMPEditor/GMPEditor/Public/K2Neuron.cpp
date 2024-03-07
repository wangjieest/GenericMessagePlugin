// Copyright K2Neuron, Inc. All Rights Reserved.

#include "K2Neuron.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Blueprint/BlueprintSupport.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintCompilationManager.h"
#include "BlueprintEditorSettings.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphUtilities.h"
#include "Editor.h"
#include "Engine/CollisionProfile.h"
#include "Engine/MemberReference.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GMPCore.h"
#include "GraphEditorSettings.h"
#include "HAL/FileManager.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallArrayFunction.h"
#include "K2Node_CallFunction.h"
#include "K2Node_ClassDynamicCast.h"
#include "K2Node_CreateDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_DynamicCast.h"
#include "K2Node_EnumLiteral.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MacroInstance.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeStruct.h"
#include "K2Node_PureAssignmentStatement.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_Self.h"
#include "K2Node_SwitchEnum.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_VariableGet.h"
#include "Kismet/KismetArrayLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/DelayedAutoRegister.h"
#include "Misc/ScopeExit.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "SourceCodeNavigation.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/CoreRedirects.h"
#include "UObject/DevObjectVersion.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"
#include "UnrealCompatibility.h"
#include "Widgets/Notifications/SNotificationList.h"

#if defined(GENERICSTORAGES_API)
#include "Editor/UnrealEditorUtils.h"
#include "GenericStorages/Private/Editor/SClassPickerGraphPin.h"
#else

struct SClassPickerGraphPin
{
	static bool IsCustomClassPinPicker(UEdGraphPin* InGraphPinObj)
	{
		bool bRet = false;
		do
		{
			if (!InGraphPinObj)
				break;

			UK2Node_CallFunction* CallFuncNode = Cast<UK2Node_CallFunction>(InGraphPinObj->GetOwningNode());
			if (!CallFuncNode)
				break;

			const UFunction* ThisFunction = CallFuncNode->GetTargetFunction();
			if (!ThisFunction || !ThisFunction->HasMetaData(TEXT("CustomClassPinPicker")))
				break;

			TArray<FString> ParameterNames;
			ThisFunction->GetMetaData(TEXT("CustomClassPinPicker")).ParseIntoArray(ParameterNames, TEXT(","), true);
			bRet = (ParameterNames.Contains(::ToString(InGraphPinObj->PinName)));
		} while (0);
		return bRet;
	}

	static bool IsMatchedToCreate(UEdGraphPin* InGraphPinObj)
	{
		if (IsMatchedPinType(InGraphPinObj))
		{
			return IsCustomClassPinPicker(InGraphPinObj);
		}
		return false;
	}

	static UClass* GetChoosenClass(UEdGraphPin* InGraphPinObj)
	{
		if (InGraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass || InGraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
			return Cast<UClass>(InGraphPinObj->DefaultObject);
		else
			return TSoftClassPtr<UObject>(InGraphPinObj->DefaultValue).LoadSynchronous();
	}

	static bool IsMatchedPinType(UEdGraphPin* InGraphPinObj)
	{
		return !InGraphPinObj->PinType.IsContainer()
			   && ((InGraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && InGraphPinObj->PinType.PinSubCategoryObject == TBaseStructure<FSoftClassPath>::Get())
				   || InGraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass || InGraphPinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Class);
	}
};

namespace UnrealEditorUtils
{
namespace PrivateAccessStatic
{
	using ZZ_VisualPinFactoriesPtrFEdGraphUtilitiesVisualPinFactoriesType = TArray<TSharedPtr<FGraphPanelPinFactory>>;
	using ZZ_VisualPinFactoriesPtrFEdGraphUtilitiesVisualPinFactories = ZZ_VisualPinFactoriesPtrFEdGraphUtilitiesVisualPinFactoriesType*;
	template<ZZ_VisualPinFactoriesPtrFEdGraphUtilitiesVisualPinFactories MemPtr>
	struct ZZ_GetFEdGraphUtilitiesVisualPinFactories
	{
		friend ZZ_VisualPinFactoriesPtrFEdGraphUtilitiesVisualPinFactories AccessFEdGraphUtilitiesVisualPinFactories() { return MemPtr; }
	};
	ZZ_VisualPinFactoriesPtrFEdGraphUtilitiesVisualPinFactories AccessFEdGraphUtilitiesVisualPinFactories();
	template struct ZZ_GetFEdGraphUtilitiesVisualPinFactories<&FEdGraphUtilities::VisualPinFactories>;
	auto& VisualPinFactories()
	{
		return *AccessFEdGraphUtilitiesVisualPinFactories();
	}
}  // namespace PrivateAccessStatic

static bool ShouldUseStructReference(UEdGraphPin* TestPin)
{
	check(TestPin);

	if (TestPin->PinType.bIsReference)
		return true;

	if (TestPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Struct)
		return false;

	static TSet<TWeakObjectPtr<UScriptStruct>> SupportedTypes = [] {
		TSet<TWeakObjectPtr<UScriptStruct>> Ret;
		Ret.Add(TBaseStructure<FLinearColor>::Get());
		Ret.Add(TBaseStructure<FVector>::Get());
		Ret.Add(TBaseStructure<FVector2D>::Get());
		Ret.Add(TBaseStructure<FRotator>::Get());
		Ret.Add(FKey::StaticStruct());
		Ret.Add(FCollisionProfileName::StaticStruct());
		return Ret;
	}();

	auto ScriptStruct = Cast<UScriptStruct>(TestPin->PinType.PinSubCategoryObject.Get());
	if (SupportedTypes.Contains(ScriptStruct))
	{
		return false;
	}

	if (!ensure(ScriptStruct->GetBoolMetaData(FBlueprintTags::BlueprintType)))
	{
		return true;
	}

	if (GIsEditor)
	{
		for (auto FactoryIt = PrivateAccessStatic::VisualPinFactories().CreateIterator(); FactoryIt; ++FactoryIt)
		{
			TSharedPtr<FGraphPanelPinFactory> FactoryPtr = *FactoryIt;
			if (FactoryPtr.IsValid())
			{
				TSharedPtr<SGraphPin> ResultVisualPin = FactoryPtr->CreatePin(TestPin);
				if (ResultVisualPin.IsValid())
				{
					SupportedTypes.Add(ScriptStruct);
					return false;
				}
			}
		}
	}

	return true;
}
}  // namespace UnrealEditorUtils
#endif

#define LOCTEXT_NAMESPACE "K2Node_NeuronBase"

void UK2Neuron::FindInBlueprint(const FString& InStr, UBlueprint* Blueprint)
{
	extern void EditorSearchNodeTitleInBlueprints(const FString& InStr, UBlueprint* Blueprint = nullptr);
	EditorSearchNodeTitleInBlueprints(InStr, Blueprint);
}

namespace K2Neuron
{
DECLARE_MULTICAST_DELEGATE_OneParam(FGlobalBlueprintPreCompiled, UBlueprint*);
TMap<TWeakObjectPtr<UObject>, FGlobalBlueprintPreCompiled> BlueprintPreCompilingEvents;

DECLARE_MULTICAST_DELEGATE_TwoParams(FGlobalBlueprintCompiled, UBlueprint*, UObject*);
TMap<TWeakObjectPtr<UObject>, FGlobalBlueprintCompiled> BlueprintCompiledEvents;

TArray<TPair<TWeakObjectPtr<UBlueprint>, TWeakObjectPtr<UObject>>> PrecompiledBPs;
TMap<TWeakObjectPtr<UBlueprint>, TPair<TMap<TWeakObjectPtr<const UK2Node>, uint32>, uint32>> Orders;
TMap<TWeakObjectPtr<UBlueprint>, uint32> Indexes;
const FName DefaultToCtx = TEXT("DefaultToCtx");
void OnEngineInitComplete()
{
	if (!GIsEditor || IsRunningCommandlet())
		return;

	if (GEditor)
	{
		GEditor->OnBlueprintPreCompile().AddLambda([](UBlueprint* BP) {
			PrecompiledBPs.Emplace(BP, BP->GeneratedClass ? BP->GeneratedClass->ClassDefaultObject : nullptr);
			if (auto Find = Orders.Find(BP))
			{
				Find->Value = 0;
				Find->Key.Reset();
			}
			Indexes.Remove(BP);

			if (auto Find = BlueprintPreCompilingEvents.Find(BP))
			{
				Find->Broadcast(BP);
			}
			for (auto It = BlueprintPreCompilingEvents.CreateIterator(); It; ++It)
			{
				if (It->Key.IsStale())
					It.RemoveCurrent();
			}
		});
		GEditor->OnBlueprintCompiled().AddLambda([] {
			auto Tmp = MoveTemp(PrecompiledBPs);
			for (auto& Pair : Tmp)
			{
				if (auto CurBP = Pair.Key.Get())
				{
					if (auto Find = BlueprintCompiledEvents.Find(CurBP))
						Find->Broadcast(CurBP, Pair.Value.Get());

					for (auto It = BlueprintCompiledEvents.CreateIterator(); It; ++It)
					{
						if (It->Key.IsStale())
							It.RemoveCurrent();
					}

					BlueprintCompiledEvents.FindOrAdd(nullptr).Broadcast(CurBP, Pair.Value.Get());
				}
			}
		});
	}
}

void BindBlueprintEditorEvents()
{
	static FDelayedAutoRegisterHelper DelayOnEngineInitCompleted(EDelayedRegisterRunPhase::EndOfEngineInit, [] { OnEngineInitComplete(); });
}

FGlobalBlueprintPreCompiled& AddGlobalOnBlueprintPreCompiling(UObject* Obj)
{
	return BlueprintPreCompilingEvents.FindOrAdd(Obj);
}

void RemoveGlobalOnBlueprintPreCompiling(UObject* Obj, FDelegateHandle Handle)
{
	if (auto Find = BlueprintPreCompilingEvents.Find(Obj))
	{
		Find->Remove(Handle);
		if (!Find->IsBound() && Obj)
			BlueprintPreCompilingEvents.Remove(Obj);
	}
}

FGlobalBlueprintCompiled& AddGlobalOnBlueprintCompiled(UObject* Obj)
{
	return BlueprintCompiledEvents.FindOrAdd(Obj);
}

void RemoveGlobalOnBlueprintCompiled(UObject* Obj, FDelegateHandle Handle)
{
	if (auto Find = BlueprintCompiledEvents.Find(Obj))
	{
		Find->Remove(Handle);
		if (!Find->IsBound() && Obj)
			BlueprintCompiledEvents.Remove(Obj);
	}
}

uint32 GetBlueprintOrder(const UK2Node* Node)
{
	uint32 Order = 0;
	auto Blueprint = Node->GetBlueprint();
	if (auto Find = Orders.Find(Blueprint))
	{
		auto Value = Find->Key.Find(Node);
		if (!Value)
		{
			Value = &Find->Key.FindOrAdd(Node);
			*Value = ++(Find->Value);
		}
		Order = *Value;
	}
	else
	{
		auto& Ref = Orders.FindOrAdd(Blueprint);
		Ref.Value = Order;
		Ref.Key.FindOrAdd(Node) = Order;
	}
	return Order;
}

uint32 GetUniqueIDForBlueprint(UBlueprint* Blueprint, uint32 MaxBpDerivedLevel = 8)
{
	const auto LevelBits = FMath::CeilLogTwo(MaxBpDerivedLevel);

	static auto GetBpDerivedLevel = [](UBlueprint* Blueprint) {
		uint32 LevelToNative = 0;
		UClass* Class = Cast<UClass>(Blueprint->GeneratedClass);
		for (; Class; Class = Class->GetSuperClass())
		{
			if ((Class->HasAnyClassFlags(CLASS_Native)))
			{
				--LevelToNative;
				break;
			}
			++LevelToNative;
		}
		return LevelToNative;
	};

	auto BpDerivedLevel = GetBpDerivedLevel(Blueprint);
	checkSlow(BpDerivedLevel < (1u << LevelBits));

	uint32 IncreasedIndex = 0;
	if (auto Find = Indexes.Find(Blueprint))
	{
		IncreasedIndex = ++(*Find);
	}
	else
	{
		Indexes.FindOrAdd(Blueprint) = IncreasedIndex;
	}
	return BpDerivedLevel + (IncreasedIndex << LevelBits);
}

static bool bMarkBlueprintDirtyBeforeOpen = true;
FAutoConsoleVariableRef CVar_MarkBlueprintDirtyBeforeOpen(TEXT("z.MarkBlueprintDirtyBeforeOpen"), bMarkBlueprintDirtyBeforeOpen, TEXT(""));

}  // namespace K2Neuron
bool UK2Neuron::ShouldMarkBlueprintDirtyBeforeOpen()
{
	return K2Neuron::bMarkBlueprintDirtyBeforeOpen;
}

const bool UK2Neuron::HasVariadicSupport = (GMP_WITH_VARIADIC_SUPPORT);

class FKCHandler_K2Neuron : public FNodeHandlingFunctor
{
public:
	FKCHandler_K2Neuron(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
	}

	virtual void RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		UK2Neuron* Neuron = CastChecked<UK2Neuron>(Node);

		for (auto VarPin : Neuron->Pins)
		{
			if (!(VarPin->Direction == EGPD_Input && VarPin->LinkedTo.Num() == 0 && VarPin->DefaultObject != nullptr  //
				  && VarPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && VarPin->bDefaultValueIsIgnored))
				continue;

			auto PinMetaInfo = Neuron->GetPinMetaInfo(VarPin, true);
			if (!PinMetaInfo.GetObjOrFunc() || !PinMetaInfo.Prop)
				continue;

			if (PinMetaInfo.Prop->HasMetaData(K2Neuron::DefaultToCtx) || PinMetaInfo.Prop->HasMetaData(FBlueprintMetadata::MD_WorldContext) || PinMetaInfo.SubFuncion  //
				|| PinMetaInfo.SubFuncion->GetMetaData(K2Neuron::DefaultToCtx) == PinMetaInfo.MemberName || PinMetaInfo.SubFuncion->GetMetaData(FBlueprintMetadata::MD_WorldContext) == PinMetaInfo.MemberName)
			{
				// Defualt to Self
				FBPTerminal* Term = new FBPTerminal();
				Context.Literals.Add(Term);
				Term->CopyFromPin(VarPin, VarPin->PinName);
				Term->bIsLiteral = true;
				Context.NetMap.Add(VarPin, Term);
			}
		}
	}
};

UEdGraphPin* UK2Neuron::GetCtxPin() const
{
	check(CtxSelfNode.IsValid());
	return CtxSelfNode->Pins[0];
}

bool UK2Neuron::ShouldDefaultToCtx(FProperty* InProp) const
{
	auto ObjProp = CastField<FObjectProperty>(InProp);
	if (ObjProp && ObjProp->PropertyClass && GetBlueprintClass()->IsChildOf(ObjProp->PropertyClass) && ObjProp->HasMetaData(K2Neuron::DefaultToCtx))
	{
		return true;
		// 		if (auto Func = GetPropOwnerUObject<UFunction>(ObjProp))
		// 		{
		// 			if (Func->GetMetaData(K2Neuron::DefaultToCtx) == ObjProp->GetName())
		// 				return true;
		// 		}
	}
	return false;
}

bool UK2Neuron::ShouldDefaultToCtx(UEdGraphPin* InPin) const
{
	if (InPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object || InPin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self || InPin->Direction != EGPD_Input || !InPin->bDefaultValueIsIgnored)
		return false;
	auto Info = GetPinMetaInfo(InPin);
	return ShouldDefaultToCtx(Info.Prop);
}

FNodeHandlingFunctor* UK2Neuron::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_K2Neuron(CompilerContext);
}

FName FEdPinExtraMetaBase::IsObjClsPin = TEXT("PinObjCls");
FName FEdPinExtraMetaBase::IsImportedPin = TEXT("PinImported");
FName FEdPinExtraMetaBase::IsSpawnedPin = TEXT("PinSpawned");
FName FEdPinExtraMetaBase::IsSelfSpawnedPin = TEXT("PinSelfSpawned");
FName FEdPinExtraMetaBase::IsBindedPin = TEXT("PinBinded");
FName FEdPinExtraMetaBase::IsCustomStructurePin = TEXT("PinCustomStructure");
FName FEdPinExtraMetaBase::IsAutoCreateRefTermPin = TEXT("PinAutoCreateRefTerm");
FName FEdPinExtraMetaBase::IsNeuronCheckablePin = TEXT("PinNeuronCheckable");

UK2Neuron::UK2Neuron(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, MetaHidePins(TEXT("HidePins"))
	, MetaTagFlag(TEXT("NeuronNode"))
	, MetaTagPostfix(TEXT("_NN"))  // NeuronNode
	, NameHideProperties(TEXT("NameHideProperties"))
	, NameShowProperties(TEXT("NameShowProperties"))
	, DelimiterChar(TEXT('.'))
	, DelimiterStr(FString::Printf(TEXT("%c"), DelimiterChar))
	, DelegateDelimiter(TEXT("∷"))
	, FunctionDelimiter(TEXT("∵"))
	, MemberDelimiter(TEXT("∴"))
	, AdditionalDelimiter(TEXT("⊙"))
	, RequiresConnection(TEXT("RequiresConnection"))
	, DisableConnection(TEXT("DisableConnection"))
	, RequiresReference(TEXT("RequiresReference"))
	, ExecEnumPrefix(TEXT("☇"))
	, MetaEventPrefix(TEXT('‡'))
	, AffixesSelf(UK2Neuron::FPinNameAffixes{TEXT('●'), TEXT('◆'), TEXT('■')})
	, AffixesProxy(UK2Neuron::FPinNameAffixes{TEXT('○'), TEXT('◇'), TEXT('□')})
	, AffixesObject(UK2Neuron::FPinNameAffixes{TEXT('♩'), TEXT('♪'), TEXT('♫')})
	, BeginSpawningFuncName(TEXT("BeginSpawningObject"))
	, PostSpawningFuncName(TEXT("PostSpawningObject"))
	, SpawnedSucceedName(TEXT("SpawnedSucceed"))
	, SpawnedObjectName(TEXT("SpawnedObj"))
	, SpawnedFailedName(TEXT("SpawnedFailed"))
	, SpawnedObjectPropName(TEXT("SpawnedObject"))
	, ObjectClassPropName(TEXT("SpawnedClass"))
	, SpawnedDelegatePropName(TEXT("SpawnedDelegate"))
	, UnlinkObjEventName(TEXT("UnlinkObjectEvent"))
	, MetaSplitStructPin(TEXT("ShowStructSplitPin"))
{
	OrphanedPinSaveMode = ESaveOrphanPinMode::SaveAll;

	if (TrueOnFirstCall([] {}))
		K2Neuron::BindBlueprintEditorEvents();
}

void UK2Neuron::StaticAssignProperty(UK2Node_BaseMCDelegate* InNode, const FProperty* Property, bool bSelfContext, UClass* OwnerClass)
{
#if UE_4_24_OR_LATER
	InNode->SetFromProperty(Property, bSelfContext, OwnerClass);
#else
	InNode->SetFromProperty(Property, bSelfContext);
#endif
}

const UEdGraphSchema_K2* UK2Neuron::GetK2Schema(const FKismetCompilerContext& CompilerContext)
{
	return CompilerContext.GetSchema();
}

const UEdGraphSchema_K2* UK2Neuron::GetK2Schema(const UK2Node* InNode)
{
	return (InNode && InNode->GetGraph()) ? Cast<const UEdGraphSchema_K2>(InNode->GetSchema()) : GetDefault<UEdGraphSchema_K2>();
}

UClass* UK2Neuron::GetBlueprintClass(const UK2Node* InNode)
{
	UBlueprint* BP = FBlueprintEditorUtils::FindBlueprintForNode(InNode);
	UClass* BPClass = BP ? (BP->GeneratedClass ? BP->GeneratedClass : BP->SkeletonGeneratedClass) : nullptr;
	return BPClass;
}

bool UK2Neuron::IsInputParameter(const FProperty* FuncParam, bool bEnsure)
{
	check(FuncParam);
	if (!Cast<UFunction>(GetPropertyOwnerUObject(FuncParam)))
		return true;

	bool bIsInput = !(FuncParam->HasAnyPropertyFlags(CPF_ReturnParm) || (FuncParam->HasAnyPropertyFlags(CPF_OutParm) && !FuncParam->HasAnyPropertyFlags(CPF_ReferenceParm) && !FuncParam->HasAnyPropertyFlags(CPF_ConstParm)));
	ensureAlways(!bEnsure || bIsInput);
	return bIsInput;
}

bool UK2Neuron::IsExecPin(UEdGraphPin* Pin, EEdGraphPinDirection Direction)
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && (Direction == EGPD_MAX || ((Direction == Pin->Direction) != Pins.Contains(Pin)));
}

bool UK2Neuron::IsExpandEnumAsExec(UFunction* Function, UEnum** OutEnum, FProperty** OutProp, FName ExecParamName) const
{
	FProperty* Prop = nullptr;
	UEnum* Enum = nullptr;
	if (Function)
	{
		if (ExecParamName.IsNone())
			ExecParamName = *Function->GetMetaData(FBlueprintMetadata::MD_ExpandEnumAsExecs);

		if (auto* ByteProp = FindFProperty<FByteProperty>(Function, ExecParamName))
		{
			Prop = ByteProp;
			Enum = ByteProp->Enum;
		}
		else if (auto* EnumProp = FindFProperty<FEnumProperty>(Function, ExecParamName))
		{
			Prop = EnumProp;
			Enum = EnumProp->GetEnum();
		}
	}
	if (OutEnum)
		*OutEnum = Enum;
	if (OutProp)
		*OutProp = Prop;
	if (Enum && Prop)
	{
		return IsInputParameter(Prop);
	}
	return false;
}

bool UK2Neuron::IsExpandEnumAsExec(FMulticastDelegateProperty* MCDProp, UEnum** OutEnum, FProperty** OutProp) const
{
	FProperty* Prop = nullptr;
	UEnum* Enum = nullptr;
	if (MCDProp)
	{
		auto Function = MCDProp->SignatureFunction;
		const FString& EnumParamName = MCDProp->GetMetaData(FBlueprintMetadata::MD_ExpandEnumAsExecs);
		return IsExpandEnumAsExec(Function, OutEnum, OutProp, *EnumParamName);
	}
	return false;
}

bool UK2Neuron::IsExpandBoolAsExec(UFunction* Function, FBoolProperty** OutProp, FName ExecParamName) const
{
	FBoolProperty* Prop = nullptr;
	if (Function)
	{
		if (ExecParamName.IsNone())
			ExecParamName = *Function->GetMetaData(FBlueprintMetadata::MD_ExpandBoolAsExecs);

		if (auto* BoolProp = FindFProperty<FBoolProperty>(Function, ExecParamName))
		{
			Prop = BoolProp;
		}
	}
	if (OutProp)
		*OutProp = Prop;
	if (Prop)
	{
		return IsInputParameter(Prop);
	}
	return false;
}

bool UK2Neuron::IsExpandBoolAsExec(FMulticastDelegateProperty* MCDProp, FBoolProperty** OutProp) const
{
	FBoolProperty* Prop = nullptr;
	UEnum* Enum = nullptr;
	if (MCDProp)
	{
		auto Function = MCDProp->SignatureFunction;
		const FString& BoolParamName = MCDProp->GetMetaData(FBlueprintMetadata::MD_ExpandBoolAsExecs);
		return IsExpandBoolAsExec(Function, OutProp, *BoolParamName);
	}
	return false;
}

UEdGraphPin* UK2Neuron::FindThenPin(const UK2Node* InNode, bool bChecked)
{
	UEdGraphPin* Pin = InNode ? InNode->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
	check(!bChecked || Pin);
	return Pin;
}

UEdGraphPin* UK2Neuron::FindExecPin(const UK2Node* InNode, EEdGraphPinDirection Dir, bool bChecked)
{
	for (UEdGraphPin* Pin : InNode->Pins)
	{
		if (Pin && !Pin->IsPendingKill() && !Pin->bHidden && (Dir == EGPD_MAX || Dir == Pin->Direction) && (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec))
			return Pin;
	}
	return nullptr;
}

UEdGraphPin* UK2Neuron::FindValuePin(const UK2Node* InNode, EEdGraphPinDirection Dir, FName PinCategory)
{
	for (UEdGraphPin* Pin : InNode->Pins)
	{
		if (Pin && !Pin->IsPendingKill() && !Pin->bHidden && (Dir == EGPD_MAX || Dir == Pin->Direction) &&                                                                      //
			Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && Pin->PinName != UEdGraphSchema_K2::PN_Self && Pin->PinType.PinCategory != UEdGraphSchema_K2::PSC_Self &&  //
			(PinCategory.IsNone() || PinCategory == Pin->PinType.PinCategory))
		{
			return Pin;
		}
	}
	return nullptr;
}

UEdGraphPin* UK2Neuron::FindObjectPin(const UK2Node* InNode, UClass* ObjClass, EEdGraphPinDirection Dir)
{
	for (UEdGraphPin* Pin : InNode->Pins)
	{
		if (Pin && !Pin->IsPendingKill() && !Pin->bHidden && (Dir == EGPD_MAX || Dir == Pin->Direction) &&  //
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && Pin->PinName != UEdGraphSchema_K2::PN_Self)
		{
			auto PinClass = Cast<UClass>(Pin->PinType.PinSubCategoryObject.Get());
			if (PinClass && (!ObjClass || PinClass->IsChildOf(ObjClass)))
			{
				return Pin;
			}
		}
	}
	return nullptr;
}

UEdGraphPin* UK2Neuron::SearchPin(const FName PinName, const TArray<UEdGraphPin*>* InPinsToSearch, const EEdGraphPinDirection Direction) const
{
	static auto InnerSeach = [](const FName PinName, const TArray<UEdGraphPin*>& PinsToSearch, const EEdGraphPinDirection Direction) {
		UEdGraphPin* RetPin = nullptr;
		for (UEdGraphPin* Pin : PinsToSearch)
		{
			if ((Direction == EGPD_MAX || Direction == Pin->Direction) && Pin->PinName == PinName)
			{
				RetPin = Pin;
				break;
			}
		}
		return RetPin;
	};

	UEdGraphPin* ResultPin = nullptr;
	if (InPinsToSearch)
		ResultPin = InnerSeach(PinName, *InPinsToSearch, Direction);
	if (!ResultPin)
		ResultPin = InnerSeach(PinName, Pins, Direction);
	return ResultPin;
}

UEdGraphPin* UK2Neuron::SearchPin(const FGuid PinGuid, const TArray<UEdGraphPin*>* InPinsToSearch, const EEdGraphPinDirection Direction) const
{
	static auto InnerSeach = [](const FGuid PinGuid, const TArray<UEdGraphPin*>& PinsToSearch, const EEdGraphPinDirection Direction) {
		UEdGraphPin* RetPin = nullptr;
		for (UEdGraphPin* Pin : PinsToSearch)
		{
			if ((Direction == EGPD_MAX || Direction == Pin->Direction) && GetPinGuid(Pin) == PinGuid)
			{
				RetPin = Pin;
				break;
			}
		}
		return RetPin;
	};

	UEdGraphPin* ResultPin = nullptr;
	if (InPinsToSearch)
		ResultPin = InnerSeach(PinGuid, *InPinsToSearch, Direction);
	if (!ResultPin)
		ResultPin = InnerSeach(PinGuid, Pins, Direction);
	return ResultPin;
}

UEdGraphPin* UK2Neuron::FindDelegatePin(const UK2Node_Event* InEventNode, bool bChecked)
{
	UEdGraphPin* Pin = InEventNode ? InEventNode->FindPin(UK2Node_Event::DelegateOutputName) : nullptr;
	check(!bChecked || Pin);
	return Pin;
}

UClass* UK2Neuron::ClassFromPin(UEdGraphPin* ClassPin, bool bFallback)
{
	if (!ClassPin)
		return nullptr;
	if (ClassPin->LinkedTo.Num() == 0 && SClassPickerGraphPin::IsMatchedToCreate(ClassPin))
	{
		UClass* OutClass = SClassPickerGraphPin::GetChoosenClass(ClassPin);
		return OutClass;
	}
	else
	{
		UClass* OutClass = Cast<UClass>(ClassPin->DefaultObject);
		UClass* FallbackClass = Cast<UClass>(ClassPin->PinType.PinSubCategoryObject.Get());
		if (bFallback && (!OutClass || (FallbackClass && FallbackClass->IsChildOf(OutClass))))
			OutClass = FallbackClass;

		if (ClassPin->LinkedTo.Num() > 0)
		{
			UClass* CommonInputClass = nullptr;
			FallbackClass = ClassPin->LinkedTo[0] ? Cast<UClass>(ClassPin->LinkedTo[0]->PinType.PinSubCategoryObject.Get()) : FallbackClass;
			for (UEdGraphPin* LinkedPin : ClassPin->LinkedTo)
			{
				while (auto Knot = Cast<UK2Node_Knot>(LinkedPin->GetOwningNode()))
				{
					if (ensure(Knot->GetInputPin()->LinkedTo.Num() == 1))
						LinkedPin = Knot->GetInputPin()->LinkedTo[0];
					else
						break;
				}

				const FEdGraphPinType& LinkedPinType = LinkedPin->PinType;
				UClass* LinkClass = Cast<UClass>(LinkedPinType.PinSubCategoryObject.Get());
				if (LinkClass == nullptr && LinkedPinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
				{
					if (UK2Node* K2Node = Cast<UK2Node>(LinkedPin->GetOwningNode()))
					{
						LinkClass = K2Node->GetBlueprint()->GeneratedClass;
					}
				}

				if (LinkClass != nullptr)
				{
					if (CommonInputClass != nullptr)
					{
						while (!LinkClass->IsChildOf(CommonInputClass))
						{
							CommonInputClass = CommonInputClass->GetSuperClass();
						}
					}
					else
					{
						CommonInputClass = LinkClass;
					}
				}
			}
			OutClass = CommonInputClass;
			if (bFallback && (!OutClass || (FallbackClass && FallbackClass->IsChildOf(OutClass))))
				OutClass = FallbackClass;
		}
		return OutClass;
	}
}

UEdGraphPin* UK2Neuron::GetSpecialClassPin(const TArray<UEdGraphPin*>& InPinsToSearch, FName PinName, UClass** OutClass) const
{
	UEdGraphPin* ClassPin = nullptr;
	for (auto TestPin : InPinsToSearch)
	{
		if (TestPin && TestPin->PinName == PinName)
		{
			ClassPin = TestPin;
			break;
		}
	}

	// 	if (!ClassPin)
	// 	{
	// 		FString PinNamePart;
	// 		for (auto TestPin : InPinsToSearch)
	// 		{
	// 			if (!TestPin)
	// 				continue;
	// 			PinNamePart = TestPin->GetName();
	// 			TestPin->GetName().Split(MemberDelimiter, nullptr, &PinNamePart);
	// 			if (PinNamePart == PinName.ToString())
	// 			{
	// 				ClassPin = TestPin;
	// 				break;
	// 			}
	// 		}
	// 	}

	check(ClassPin == nullptr || ClassPin->Direction == EGPD_Input);
	if (OutClass)
		*OutClass = ClassFromPin(ClassPin);
	return ClassPin;
}

UEdGraphPin* UK2Neuron::GetSpecialClassPin(const TArray<UEdGraphPin*>& InPinsToSearch, FGuid PinGuid, UClass** OutClass) const
{
	UEdGraphPin* ClassPin = nullptr;
	for (auto TestPin : InPinsToSearch)
	{
		if (TestPin && GetPinGuid(TestPin) == PinGuid)
		{
			ClassPin = TestPin;
			break;
		}
	}

	check(ClassPin == nullptr || ClassPin->Direction == EGPD_Input);
	if (OutClass)
		*OutClass = ClassFromPin(ClassPin);
	return ClassPin;
}

UClass* UK2Neuron::GetSpecialPinClass(const TArray<UEdGraphPin*>& InPinsToSearch, FGuid PinGuid, UEdGraphPin** OutPin) const
{
	UClass* RetClass = nullptr;
	UEdGraphPin* ClassPin = GetSpecialClassPin(InPinsToSearch, PinGuid, &RetClass);
	if (OutPin)
		*OutPin = ClassPin;

	return RetClass;
}

UClass* UK2Neuron::GetSpecialPinClass(const TArray<UEdGraphPin*>& InPinsToSearch, FName PinName, UEdGraphPin** OutPin) const
{
	UClass* RetClass = nullptr;
	UEdGraphPin* ClassPin = GetSpecialClassPin(InPinsToSearch, PinName, &RetClass);
	if (OutPin)
		*OutPin = ClassPin;

	return RetClass;
}

bool UK2Neuron::IsTypePickerPin(UEdGraphPin* Pin)
{
	return Pin && (Pin->Direction == EGPD_Input)
		   && (((Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class) || (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Interface) || (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object))
			   || SClassPickerGraphPin::IsMatchedToCreate(Pin));
}

bool UK2Neuron::HasAnyConnections(const UEdGraphPin* InPin)
{
	return InPin && (InPin->LinkedTo.Num() > 0 || InPin->SubPins.Num() > 0);
}

void UK2Neuron::FillCustomStructureParameterNames(const UFunction* Function, TArray<FString>& OutNames)
{
	OutNames.Reset();
	if (Function)
	{
		const FString& MetaDataValue = Function->GetMetaData(FBlueprintMetadata::MD_CustomStructureParam);
		if (!MetaDataValue.IsEmpty())
		{
			MetaDataValue.ParseIntoArray(OutNames, TEXT(","), true);
		}
	}
}

void UK2Neuron::HandleSinglePinWildStatus(UEdGraphPin* Pin)
{
	if (Pin)
	{
		if (Pin->LinkedTo.Num() > 0)
		{
			UEdGraphPin* LinkedTo = Pin->LinkedTo[0];
			check(LinkedTo);
			const bool bWasRef = Pin->PinType.bIsReference;
			const bool bWasConst = Pin->PinType.bIsConst;
			Pin->PinType = LinkedTo->PinType;
			Pin->PinType.bIsReference = bWasRef;
			Pin->PinType.bIsConst = bWasConst;

			if (UK2Node* Node = Cast<UK2Node>(Pin->GetOwningNode()))
			{
				ensure(!LinkedTo->PinType.IsContainer() || Node->DoesWildcardPinAcceptContainer(Pin));
			}
			else
			{
				ensure(!LinkedTo->PinType.IsContainer());
			}
#if 0
			// fix Reference status
			do
			{
				auto FuncNode = Cast<UK2Node_CallFunction>(Pin->GetOwningNode());
				if (!FuncNode)
					break;

				auto Func = FuncNode->GetTargetFunction();
				if (!ensure(Func))
					break;

				auto ParamProp = Func->FindPropertyByName(Pin->PinName);
				if (!ensure(ParamProp))
					break;
				Pin->PinType.bIsReference = ParamProp->HasAllPropertyFlags(CPF_ReferenceParm | CPF_Parm);
				Pin->PinType.bIsConst = ParamProp->HasAllPropertyFlags(CPF_ConstParm | CPF_Parm);

			} while (false);
#endif
		}
		else
		{
			// constness and refness are controlled by our declaration
			// but everything else needs to be reset to default wildcard:
			const bool bWasRef = Pin->PinType.bIsReference;
			const bool bWasConst = Pin->PinType.bIsConst;

			Pin->PinType = FEdGraphPinType();
			Pin->PinType.bIsReference = bWasRef;
			Pin->PinType.bIsConst = bWasConst;
			Pin->PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
			Pin->PinType.PinSubCategory = NAME_None;
			Pin->PinType.PinSubCategoryObject = nullptr;
		}
	}
}

void UK2Neuron::UpdateCustomStructurePins(const UFunction* Function, UK2Node* Node, UEdGraphPin* SinglePin /*= nullptr*/)
{
	if (Function && Node)
	{
		TArray<FString> Names;
		FillCustomStructureParameterNames(Function, Names);
		if (SinglePin)
		{
			if (Names.Contains(SinglePin->PinName.ToString()))
			{
				HandleSinglePinWildStatus(SinglePin);
			}
		}
		else
		{
			for (const FString& Name : Names)
			{
				if (UEdGraphPin* Pin = Node->FindPin(Name))
				{
					HandleSinglePinWildStatus(Pin);
				}
			}
		}
	}
}

void UK2Neuron::UpdateCustomStructurePin(UEdGraphPin* SinglePin)
{
	if (SinglePin)
	{
		if (CustomStructurePinGuids.Contains(GetPinGuid(SinglePin)))
		{
			HandleSinglePinWildStatus(SinglePin);
		}
	}
	else
	{
		for (const FGuid& Guid : CustomStructurePinGuids)
		{
			if (UEdGraphPin* Pin = FindPin(Guid))
			{
				HandleSinglePinWildStatus(Pin);
			}
		}
	}
}

void UK2Neuron::UpdateCustomStructurePin(TArray<UEdGraphPin*>* InOldPins)
{
	auto& PinArr = InOldPins ? *InOldPins : Pins;
	for (const FGuid& Guid : CustomStructurePinGuids)
	{
		if (UEdGraphPin* Pin = FindPin(Guid, PinArr))
		{
			HandleSinglePinWildStatus(Pin);
		}
	}
}

UEdGraphPin* UK2Neuron::FindPin(FGuid InGuid, const TArray<UEdGraphPin*>& InPins)
{
	auto PinPtr = InPins.FindByPredicate([&](auto TestPin) { return GetPinGuid(TestPin) == InGuid; });
	return (PinPtr && InGuid.IsValid()) ? const_cast<UEdGraphPin*>(*PinPtr) : nullptr;
}

UEdGraphPin* UK2Neuron::FindPin(FGuid InGuid, TArray<UEdGraphPin*>* InOldPins /*= nullptr*/) const
{
	auto& PinArr = InOldPins ? *InOldPins : Pins;
	return FindPin(InGuid, PinArr);
}

bool UK2Neuron::HasSpecialTag(const FFieldVariant& Field) const
{
	return Field ? (Field.HasMetaData(MetaTagFlag) || Field.GetName().EndsWith(MetaTagPostfix)) : false;
}

bool UK2Neuron::HasSpecialImportTag(const FFieldVariant& Field) const
{
	return Field && !Field.IsA<FMulticastDelegateProperty>() && (HasSpecialTag(Field) || (CastField<FProperty>(Field) && UEdGraphSchema_K2::IsPropertyExposedOnSpawn(CastField<FProperty>(Field))));
}

bool UK2Neuron::HasSpecialExportTag(const FFieldVariant& Field) const
{
	return Field && Field.IsA<FMulticastDelegateProperty>() && static_cast<FMulticastDelegateProperty*>(Field.ToField())->HasAnyPropertyFlags(CPF_BlueprintAssignable) && HasSpecialTag(Field);
}

FString UK2Neuron::GetPinFriendlyName(FString Name)
{
	Name.RemoveFromEnd(MetaTagPostfix);
	return Name;
}

FString UK2Neuron::GetDisplayString(const FFieldVariant& InFieldVariant, const TCHAR* Prefix)
{
	FString DisplayName = GetPinFriendlyName(InFieldVariant.GetName());
	if (UStruct* Struct = Cast<UStruct>(InFieldVariant.ToUObject()))
	{
		const auto& MetaDisplayName = Struct->GetMetaData(FBlueprintMetadata::MD_DisplayName);
		if (!MetaDisplayName.IsEmpty())
			DisplayName = MetaDisplayName;
	}
	return Prefix ? FString::Printf(TEXT("%s%s"), Prefix, *DisplayName) : DisplayName;
}

FText UK2Neuron::GetDisplayText(const FFieldVariant& InFieldVariant, const TCHAR* Prefix)
{
	return FText::FromString(GetDisplayString(InFieldVariant, Prefix));
}

FText UK2Neuron::GetCustomDisplayName(const FString& ClassName, const FFieldVariant& InFieldVariant, const FString& Postfix)
{
	return FText::FromString(ClassName + DelimiterStr + GetDisplayString(InFieldVariant) + Postfix);
}

FText UK2Neuron::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	FText DisplayName = FText::GetEmpty();

	if (Pin)
	{
		UEdGraphNode* Node = Pin->GetOwningNode();
		if (Node->ShouldOverridePinNames())
		{
			DisplayName = Node->GetPinNameOverride(*Pin);
		}
		else
		{
			DisplayName = GetK2Schema()->UEdGraphSchema::GetPinDisplayName(Pin);

			// bit of a hack to hide 'execute' and 'then' pin names
			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				FName DisplayFName(*DisplayName.ToString(), FNAME_Find);
				if ((DisplayFName == UEdGraphSchema_K2::PN_Execute) || (DisplayFName == UEdGraphSchema_K2::PN_Then))
				{
					DisplayName = FText::GetEmpty();
				}
			}
		}
	}
	return DisplayName;
}

void UK2Neuron::PostPasteNode()
{
	Super::PostPasteNode();
	NodeUniqueID = 0;
}

bool UK2Neuron::HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const
{
	const bool bSuperResult = Super::HasExternalDependencies(OptionalOutput);
	bool bMore = false;
	const UBlueprint* SourceBlueprint = GetBlueprint();
	if (auto ObjClassPin = FindPin(ObjClassPinGuid))
	{
		auto ObjClass = ClassFromPin(ObjClassPin);
		if (ObjClass && !ObjClass->IsNative() && ObjClass->ClassGeneratedBy != SourceBlueprint)
		{
			bMore = bMore || !OptionalOutput->Contains(ObjClass);
			OptionalOutput->AddUnique(ObjClass);
		}
	}

	for (UEdGraphPin* Pin : Pins)
	{
		UStruct* DepStruct = Pin ? Cast<UStruct>(Pin->PinType.PinSubCategoryObject.Get()) : nullptr;

		UClass* DepClass = Cast<UClass>(DepStruct);
		if (DepClass)
		{
			auto PinMetaInfo = GetPinMetaInfo(Pin, false, false);
			if (PinMetaInfo.OwnerClass && PinMetaInfo.OwnerClass->ClassGeneratedBy != SourceBlueprint)
			{
				bMore = bMore || !OptionalOutput->Contains(PinMetaInfo.OwnerClass);
				OptionalOutput->AddUnique(PinMetaInfo.OwnerClass);
			}

			//Don't include self
			if (DepClass->ClassGeneratedBy == SourceBlueprint)
				continue;
		}

		if (DepStruct && !DepStruct->IsNative())
		{
			if (OptionalOutput)
			{
				OptionalOutput->AddUnique(DepStruct);
			}
			bMore = true;
		}
	}

	return bSuperResult || bMore;
}

bool UK2Neuron::CanSplitPin(const UEdGraphPin* Pin) const
{
	// TODO Disable
	return Super::CanSplitPin(Pin);
}

FText UK2Neuron::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle)
	{
		return LOCTEXT("K2Neuron", "K2Neuron");
	}

	return Super::GetNodeTitle(TitleType);
}

void UK2Neuron::AllocateDefaultPins()
{
	CallAllocateDefaultPinsImpl();
}

void UK2Neuron::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	CallAllocateDefaultPinsImpl(&OldPins);
	RestoreSplitPins(OldPins);
}

bool UK2Neuron::CanJumpToDefinition() const
{
	auto SpawnedClass = GetSpawnedObjectClass();
	return SpawnedClass && !!Cast<UBlueprint>(SpawnedClass->ClassGeneratedBy);
}

void UK2Neuron::JumpToDefinition() const
{
	JumpToDefinitionClass(GetSpawnedObjectClass());
}

void UK2Neuron::JumpToDefinitionClass(UClass* TargetClass) const
{
	if (!TargetClass)
		return;

	if (TargetClass->IsNative())
	{
		bool bSucceeded = false;
		const bool bNavigateToNativeFunctions = GetDefault<UBlueprintEditorSettings>()->bNavigateToNativeFunctionsFromCallNodes;
		if (bNavigateToNativeFunctions)
		{
			if (FSourceCodeNavigation::CanNavigateToClass(TargetClass))
			{
				bSucceeded = FSourceCodeNavigation::NavigateToClass(TargetClass);
			}

			if (!bSucceeded)
			{
				FString NativeParentClassHeaderPath;
				const bool bFileFound = FSourceCodeNavigation::FindClassHeaderPath(TargetClass, NativeParentClassHeaderPath) && (IFileManager::Get().FileSize(*NativeParentClassHeaderPath) != INDEX_NONE);
				if (bFileFound)
				{
					const FString AbsNativeParentClassHeaderPath = FPaths::ConvertRelativePathToFull(NativeParentClassHeaderPath);
					bSucceeded = FSourceCodeNavigation::OpenSourceFile(AbsNativeParentClassHeaderPath);
				}
			}
		}
		else
		{
			// Inform user that the function is native, give them opportunity to enable navigation to native
			// functions:
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
	}
	else if (auto* BlueprintToEdit = Cast<UBlueprint>(TargetClass->ClassGeneratedBy))
	{
		GEditor->EditObject(BlueprintToEdit);
	}
}

void UK2Neuron::CallAllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins)
{
	ensure(&Pins != InOldPins);

	bHasAdvancedViewPins = false;
	{
		TGuardValue<bool> GuardVal(bAllocWithOldPins, !!InOldPins);
		AllocateDefaultPinsImpl(InOldPins);
	}
	if (InOldPins)
	{
		RestoreSplitPins(*InOldPins);
	}

	CachedNodeTitle.MarkDirty();
	if (bHasAdvancedViewPins && (ENodeAdvancedPins::NoPins == AdvancedPinDisplay))
		AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
}

void UK2Neuron::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	UpdateCustomStructurePin(Pin);
}

void UK2Neuron::ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	TemporaryVariables.Empty();
	LiteralVariables.Empty();
	Super::ExpandNode(CompilerContext, SourceGraph);
	auto SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(this, SourceGraph);
	SelfNode->AllocateDefaultPins();
	CtxSelfNode = SelfNode;
}

void UK2Neuron::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	for (auto Pin : Pins)
	{
		if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			continue;
		bool bAnyConnection = HasAnyConnections(Pin);

		auto PinName = Pin->PinName.ToString();
		if (MatchAffixes(Pin, true, true, true))
		{
			auto Info = GetPinMetaInfo(Pin);
			if (Info.Prop && Info.Prop->HasMetaData(RequiresConnection))
			{
				MessageLog.Error(TEXT("RequiresConnection For Pin @@"), Pin);
			}
		}
	}
}

void UK2Neuron::ClearCachedBlueprintData(UBlueprint* Blueprint)
{
	Super::ClearCachedBlueprintData(Blueprint);
	if (Blueprint->GeneratedClass)
		DataCDO = Blueprint->GeneratedClass->ClassDefaultObject;

	RemoteEventNodes.Empty();
}

void UK2Neuron::PostReconstructNode()
{
	Super::PostReconstructNode();
	UpdateCustomStructurePin();
}

bool UK2Neuron::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
#if UE_5_00_OR_LATER && GMP_FORCE_DOUBLE_PROPERTY
	if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real && MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
	{
		if (MyPin->PinType.PinSubCategory != OtherPin->PinType.PinSubCategory)
		{
			OutReason = TEXT("Precision Mismatch!");
			return true;
		}
	}
#endif
	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

UK2Node* UK2Neuron::GetConnectedNode(UEdGraphPin* Pin, TSubclassOf<UK2Node> NodeClass) const
{
	for (auto Linked : Pin->LinkedTo)
	{
		auto TestPin = Linked;
		if (TestPin->GetOwningNode()->IsA(NodeClass))
		{
			return Cast<UK2Node>(TestPin->GetOwningNode());
		}
		else if (auto Knot = Cast<UK2Node_Knot>(TestPin->GetOwningNode()))
		{
			if (auto Node = GetConnectedNode(Knot->GetOutputPin(), NodeClass))
				return Node;
		}
	}
	return nullptr;
}

UEdGraphPin* UK2Neuron::CastIfFloatType(UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin /*= nullptr*/)
{
	do
	{
#if UE_5_00_OR_LATER && !UE_5_02_OR_LATER
		if (!TestSelfPin || TestSelfPin->Direction != EGPD_Input)
			break;

		if (TestSelfPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Real)
			break;
		if (!LinkPin)
			LinkPin = TestSelfPin;

		if (TestSelfPin == LinkPin && LinkPin->LinkedTo.Num() > 0)
			LinkPin = LinkPin->LinkedTo[0];

		if (!LinkPin || TestSelfPin == LinkPin)
			break;

		if (LinkPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Real)
			break;
		if (LinkPin->PinType.PinSubCategory == TestSelfPin->PinType.PinSubCategory)
			break;

		auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		bool bIsSelfFloat = TestSelfPin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float;
		NodeMakeLiteral->SetFromFunction(bIsSelfFloat ? GMP_UFUNCTION_CHECKED(UKismetMathLibrary, Conv_DoubleToFloat) : GMP_UFUNCTION_CHECKED(UKismetMathLibrary, Conv_FloatToDouble));
		NodeMakeLiteral->AllocateDefaultPins();

		auto InputRealPin = NodeMakeLiteral->FindPinChecked(bIsSelfFloat ? TEXT("InDouble") : TEXT("InFloat"));
		ensure(TryCreateConnection(CompilerContext, SourceGraph, TestSelfPin, InputRealPin));
		NodeMakeLiteral->NotifyPinConnectionListChanged(InputRealPin);
		UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
		return VariablePin;
#endif
	} while (false);
	return TestSelfPin;
}

struct FNeuronVersion
{
	enum Type
	{
		InitialVersion = 0,

		// Add new version here
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};
	static const FGuid GUID;
};
const FGuid FNeuronVersion::GUID(0x4718D635, 0x775D4A15, 0x9115D850, 0x082575C1);
FDevVersionRegistration GRegisterFrameworkObjectVersion(FNeuronVersion::GUID, FNeuronVersion::LatestVersion, TEXT("Dev-NeuronVersion"));

void UK2Neuron::Serialize(FArchive& Ar)
{
	Ar.UsingCustomVersion(FNeuronVersion::GUID);
	if (Ar.IsSaving())
	{
		// When this is a reference collector/modifier
		if (Ar.IsObjectReferenceCollector() || Ar.Tell() < 0)
		{
		}

		if (((Ar.GetPortFlags() & PPF_Duplicate) == 0))
		{
			for (UEdGraphPin* Pin : Pins)
			{
				auto PinName = Pin->PinName.ToString();
				if (MatchAffixes(Pin, true, true, true))
				{
					// update PinExtraMetas before serialize
					auto MetaInfo = GetPinMetaInfo(Pin, false, false);
				}
			}
		}
	}

	Super::Serialize(Ar);

	if (Ar.IsLoading() && ((Ar.GetPortFlags() & PPF_Duplicate) == 0))
	{
		if (GIsEditor)
		{
			// We need to serialize string data references on load in editor builds so the cooker knows about them
			for (UEdGraphPin* Pin : Pins)
			{
			}
		}

		if (Ar.CustomVer(FNeuronVersion::GUID) < FNeuronVersion::InitialVersion)
		{
			for (UEdGraphPin* Pin : Pins)
			{
				auto PinName = Pin->PinName.ToString();
				if (MatchAffixes(Pin, true, true, true))
				{
					SetPinMetaDataStr(Pin, PinName);
				}
			}
		}
	}
}

TArray<UK2Neuron*> UK2Neuron::GetOtherNodesOfClass(TSubclassOf<UK2Neuron> NeuronClass) const
{
	if (!NeuronClass.Get())
		NeuronClass = GetClass();
	TArray<UK2Neuron*> OutNodes;
	for (auto Node : GetGraph()->Nodes)
	{
		if (Node && Node != this && (Node->IsA(NeuronClass.Get())))
		{
			OutNodes.Add(CastChecked<UK2Neuron>(Node));
		}
	}
	return OutNodes;
}

bool UK2Neuron::VerifyNodeID(FCompilerResultsLog* MessageLog) const
{
	for (auto& Node : GetOtherNodesOfClass())
	{
		if (GetNodeUniqueID() == Node->GetNodeUniqueID())
		{
			if (MessageLog)
				MessageLog->Error(TEXT("duplicated node id : @@"), this);
			return false;
		}
	}
	return true;
}

bool UK2Neuron::IsBeingCompiled() const
{
	return GetBlueprint()->bBeingCompiled;
}

uint8 UK2Neuron::GenerateUniqueNodeID(TArray<UK2Neuron*>& Neurons, TSubclassOf<UK2Neuron> NeuronClass, bool bCompact)
{
	check(NeuronClass.Get() && NeuronClass->IsChildOf<UK2Neuron>());

	for (auto Node : GetGraph()->Nodes)
	{
		if (Node->IsA(NeuronClass))
		{
			Neurons.Add(static_cast<UK2Neuron*>(Node));
		}
	}

	uint8 NewIndex = 0;
	if (!bCompact && !Neurons.FindByPredicate([this, SelfIndex{NodeUniqueID}](auto Node) { return Node != this && Node->NodeUniqueID == SelfIndex; }))
	{
		NewIndex = NodeUniqueID;
	}
	else
	{
		Neurons.Sort([](auto& lhs, auto& rhs) { return lhs.NodeUniqueID < rhs.NodeUniqueID; });
		while (true)
		{
			if (!Neurons.FindByPredicate([&](auto Node) { return Node != this && Node->NodeUniqueID == NewIndex; }))
			{
				break;
			}
			check(NewIndex < TNumericLimits<uint8>::Max());
			++NewIndex;
		}
	}
	NodeUniqueID = NewIndex;
	Neurons.Sort([](auto& lhs, auto& rhs) { return lhs.NodeUniqueID < rhs.NodeUniqueID; });
	return NodeUniqueID;
}

TSet<FString> UK2Neuron::GetHideProperties(const FFieldVariant& InFieldVariant, FField* InFieldOptional)
{
	TSet<FString> IgnorePropertyList;
	TArray<FString> TempArr;
	if (InFieldVariant.IsValid())
	{
		if (FField* InField = InFieldVariant.ToField())
		{
			const FString& HidePropertyListStr = InField->GetMetaData(NameHideProperties);
			if (!HidePropertyListStr.IsEmpty())
			{
				HidePropertyListStr.ParseIntoArray(TempArr, TEXT(","), true);
				IgnorePropertyList.Append(MoveTemp(TempArr));
			}
			const FString& ShowPropertyListStr = InField->GetMetaData(NameShowProperties);
			if (!ShowPropertyListStr.IsEmpty())
			{
				ShowPropertyListStr.ParseIntoArray(TempArr, TEXT(","), true);
				for (auto& Cell : TempArr)
					IgnorePropertyList.Remove(Cell);
			}
		}
		else if (UStruct* InStruct = Cast<UStruct>(InFieldVariant.ToUObject()))
		{
			const FString& HidePropertyListStr = InStruct->GetMetaData(NameHideProperties);
			if (!HidePropertyListStr.IsEmpty())
			{
				HidePropertyListStr.ParseIntoArray(TempArr, TEXT(","), true);
				IgnorePropertyList.Append(MoveTemp(TempArr));
			}
			const FString& ShowPropertyListStr = InStruct->GetMetaData(NameShowProperties);
			if (!ShowPropertyListStr.IsEmpty())
			{
				ShowPropertyListStr.ParseIntoArray(TempArr, TEXT(","), true);
				for (auto& Cell : TempArr)
					IgnorePropertyList.Remove(Cell);
			}
		}
	}

	if (InFieldOptional)
	{
		const FString& HidePropertyListStr = InFieldOptional->GetMetaData(NameHideProperties);
		if (!HidePropertyListStr.IsEmpty())
		{
			HidePropertyListStr.ParseIntoArray(TempArr, TEXT(","), true);
			IgnorePropertyList.Append(MoveTemp(TempArr));
		}
		const FString& ShowPropertyListStr = InFieldOptional->GetMetaData(NameShowProperties);
		if (!ShowPropertyListStr.IsEmpty())
		{
			ShowPropertyListStr.ParseIntoArray(TempArr, TEXT(","), true);
			for (auto& Cell : TempArr)
				IgnorePropertyList.Remove(Cell);
		}
	}
	return IgnorePropertyList;
}

bool UK2Neuron::DoOnce(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin* OnceExecPin)
{
	bool bIsErrorFree = true;
	if (!OnceExecPin)
		return bIsErrorFree;

	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	auto OnceVarNode = CompilerContext.SpawnInternalVariable(this, UEdGraphSchema_K2::PC_Boolean);
	OnceVarNode->AllocateDefaultPins();
	auto VariablePin = OnceVarNode->GetVariablePin();
	VariablePin->DefaultValue = LexToString(true);

	UK2Node_AssignmentStatement* AssignTempVar = CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
	AssignTempVar->AllocateDefaultPins();
	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, VariablePin, AssignTempVar->GetVariablePin());
	AssignTempVar->PinConnectionListChanged(AssignTempVar->GetVariablePin());
	AssignTempVar->GetValuePin()->DefaultValue = LexToString(false);

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, AssignTempVar->GetThenPin(), OnceExecPin);
	bIsErrorFree &= ConditionDo(CompilerContext, SourceGraph, VariablePin, InOutThenPin, AssignTempVar->GetExecPin());

	return bIsErrorFree;
}

bool UK2Neuron::LaterDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins)
{
	check(IsExecPin(InOutThenPin, EGPD_Output));

	bool bIsErrorFree = true;
	auto IdxFirst = ExecPins.IndexOfByPredicate([](auto Pin) { return Pin && !Pin->IsPendingKill(); });
	if (IdxFirst == INDEX_NONE)
		return bIsErrorFree;

	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	auto FirstPin = ExecPins[IdxFirst];
	check(IsExecPin(FirstPin, EGPD_Input));
	UK2Node_ExecutionSequence* SeqNode = CompilerContext.SpawnIntermediateNode<UK2Node_ExecutionSequence>(this, SourceGraph);
	SeqNode->AllocateDefaultPins();

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, SeqNode->GetExecPin());
	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SeqNode->GetThenPinGivenIndex(1), FirstPin);
	InOutThenPin = SeqNode->GetThenPinGivenIndex(0);

	int32 i = IdxFirst + 1;
	while (i < ExecPins.Num())
	{
		if (!ExecPins[i])
			continue;
		check(IsExecPin(ExecPins[i], EGPD_Input));
		SeqNode->AddInputPin();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SeqNode->GetThenPinGivenIndex(i + 1), ExecPins[i]);
		++i;
	}
	return bIsErrorFree;
}

bool UK2Neuron::SequenceDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins)
{
	check(IsExecPin(InOutThenPin, EGPD_Output));
	bool bIsErrorFree = true;
	auto IdxFirst = ExecPins.IndexOfByPredicate([](auto Pin) { return Pin && !Pin->IsPendingKill(); });
	if (IdxFirst == INDEX_NONE)
		return bIsErrorFree;

	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	auto FirstPin = ExecPins[IdxFirst];
	check(IsExecPin(FirstPin, EGPD_Input));

	// opt: reduce inner sequence nodes
	if (ExecPins.Num() == 1 && !Pins.Contains(FirstPin) && FirstPin->GetOwningNode()->FindPin(UEdGraphSchema_K2::PN_Then) && !FirstPin->GetOwningNode()->FindPin(UEdGraphSchema_K2::PN_Then)->LinkedTo.Num())
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, FirstPin);
		InOutThenPin = FirstPin->GetOwningNode()->FindPin(UEdGraphSchema_K2::PN_Then);
	}
	else
	{
		UK2Node_ExecutionSequence* SeqNode = CompilerContext.SpawnIntermediateNode<UK2Node_ExecutionSequence>(this, SourceGraph);
		SeqNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, SeqNode->GetExecPin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SeqNode->GetThenPinGivenIndex(0), FirstPin);

		int32 i = IdxFirst + 1;
		while (i < ExecPins.Num())
		{
			if (!ExecPins[i])
				continue;
			check(IsExecPin(ExecPins[i], EGPD_Input));
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SeqNode->GetThenPinGivenIndex(i), ExecPins[i]);
			SeqNode->AddInputPin();
			++i;
		}
		InOutThenPin = SeqNode->GetThenPinGivenIndex(i);
	}
	return bIsErrorFree;
}

bool UK2Neuron::ConditionDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* ConditionPin, UEdGraphPin*& InOutThenPin, UEdGraphPin* ExecPin, UEdGraphPin* ElsePin /*= nullptr*/)
{
	check(ConditionPin && InOutThenPin && !ConditionPin->PinType.IsContainer());
	if (!ExecPin && !ElsePin)
		return true;

	bool bIsErrorFree = true;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	UK2Node_IfThenElse* IfThenElseNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
	IfThenElseNode->AllocateDefaultPins();
	if (ConditionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class || ConditionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
	{
		ConditionPin = PureValidateObjectOrClass(CompilerContext, SourceGraph, ConditionPin);
	}

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ConditionPin, IfThenElseNode->GetConditionPin());
	if (ExecPin)
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ExecPin, IfThenElseNode->GetThenPin());
	}
	if (ElsePin)
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ElsePin, IfThenElseNode->GetElsePin());
	}

	if (Pins.Contains(InOutThenPin) && FindPin(UEdGraphSchema_K2::PN_Then))
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, IfThenElseNode->GetExecPin());
		InOutThenPin = FindPin(UEdGraphSchema_K2::PN_Then);
	}
	else if (!ExecPin)
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, IfThenElseNode->GetExecPin());
		InOutThenPin = IfThenElseNode->GetThenPin();
	}
	else
	{
		bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, InOutThenPin, IfThenElseNode->GetExecPin());
	}
	return bIsErrorFree;
}

UEdGraphPin* UK2Neuron::BranchExec(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* ConditionPin, UEdGraphPin* ExecPin, UEdGraphPin* ElsePin)
{
	bool bIsErrorFree = true;
	if (ConditionPin)
	{
		if (ConditionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class || ConditionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			ConditionPin = PureValidateObjectOrClass(CompilerContext, SourceGraph, ConditionPin);

		UK2Node_IfThenElse* IfThenElseNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
		IfThenElseNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ConditionPin, IfThenElseNode->GetConditionPin());
		bIsErrorFree &= !ExecPin || TryCreateConnection(CompilerContext, SourceGraph, ExecPin, IfThenElseNode->GetThenPin());
		bIsErrorFree &= !ElsePin || TryCreateConnection(CompilerContext, SourceGraph, ElsePin, IfThenElseNode->GetElsePin());
		ensure(bIsErrorFree);
		return IfThenElseNode->GetExecPin();
	}
	ensure(!ElsePin && ExecPin);
	return ExecPin;
}

bool UK2Neuron::BranchThen(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin* ConditionPin, UEdGraphPin*& ElsePin)
{
	bool bIsErrorFree = true;
	if (ConditionPin)
	{
		if (ConditionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class || ConditionPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			ConditionPin = PureValidateObjectOrClass(CompilerContext, SourceGraph, ConditionPin);

		UK2Node_IfThenElse* IfThenElseNode = CompilerContext.SpawnIntermediateNode<UK2Node_IfThenElse>(this, SourceGraph);
		IfThenElseNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ConditionPin, IfThenElseNode->GetConditionPin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, IfThenElseNode->GetExecPin());
		InOutThenPin = IfThenElseNode->GetThenPin();
		if (ElsePin)
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ElsePin, IfThenElseNode->GetElsePin());
		else
			ElsePin = IfThenElseNode->GetElsePin();
	}
	return ensure(bIsErrorFree);
}

bool UK2Neuron::AssignTempAndGet(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin*& InOutVariablePin, bool bPure, UEdGraphPin* ConnectingPin)
{
	check(InOutThenPin && InOutVariablePin);

	UEdGraphPin* VariablePin = nullptr;
	if (ConnectingPin)
		VariablePin = TemporaryVariables.FindOrAdd(ConnectingPin);

	if (!VariablePin)
	{
		if (InOutVariablePin->LinkedTo.Num() == 0 && InOutVariablePin->Direction == EGPD_Input && InOutVariablePin->PinType.PinCategory != UEdGraphSchema_K2::PC_Object)
			VariablePin = MakeLiteralVariable(CompilerContext, SourceGraph, InOutVariablePin, InOutVariablePin->GetDefaultAsString());
		if (VariablePin)
		{
			InOutVariablePin = VariablePin;
			TemporaryVariables.FindOrAdd(ConnectingPin) = VariablePin;
			return true;
		}

		UK2Node_TemporaryVariable* TempVarOutput = CompilerContext.SpawnInternalVariable(this,
																						 InOutVariablePin->PinType.PinCategory,
																						 InOutVariablePin->PinType.PinSubCategory,
																						 InOutVariablePin->PinType.PinSubCategoryObject.Get(),
																						 InOutVariablePin->PinType.ContainerType,
																						 InOutVariablePin->PinType.PinValueType);

		VariablePin = TempVarOutput->GetVariablePin();

		if (ConnectingPin)
			TemporaryVariables.FindOrAdd(ConnectingPin) = VariablePin;
	}

	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	bool bIsErrorFree = VariablePin != nullptr;
	if (bPure)
	{
		UK2Node_PureAssignmentStatement* PureAssignNode = CompilerContext.SpawnIntermediateNode<UK2Node_PureAssignmentStatement>(this, SourceGraph);
		PureAssignNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, PureAssignNode->GetVariablePin(), VariablePin);
		PureAssignNode->PinConnectionListChanged(PureAssignNode->GetVariablePin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutVariablePin, PureAssignNode->GetValuePin(), false);
		InOutVariablePin = PureAssignNode->GetOutputPin();
	}
	else
	{
		UK2Node_AssignmentStatement* AssignTempVar = CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
		AssignTempVar->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, VariablePin, AssignTempVar->GetVariablePin());
		AssignTempVar->PinConnectionListChanged(VariablePin);
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutVariablePin, AssignTempVar->GetValuePin(), false);
		InOutVariablePin = VariablePin;

		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, AssignTempVar->GetExecPin());
		InOutThenPin = AssignTempVar->GetThenPin();
	}

	return bIsErrorFree;
}

UEdGraphPin* UK2Neuron::AssignValueAndGet(UK2Node_TemporaryVariable* InVarNode, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& LastThenPin, const FString& InValue)
{
	bool bIsErrorFree = true;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	UK2Node_AssignmentStatement* AssignTempVar = CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
	AssignTempVar->AllocateDefaultPins();
	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InVarNode->GetVariablePin(), AssignTempVar->GetVariablePin());
	AssignTempVar->PinConnectionListChanged(AssignTempVar->GetVariablePin());

	K2Schema->TrySetDefaultValue(*AssignTempVar->GetValuePin(), InValue);

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, AssignTempVar->GetExecPin());
	if (!ensure(bIsErrorFree))
		return nullptr;
	LastThenPin = AssignTempVar->GetThenPin();
	return AssignTempVar->GetValuePin();
}

bool UK2Neuron::CastAssignAndGet(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin*& InOutVarPin, UClass* InClass, bool bPure /*= false*/)
{
	bool bIsErrorFree = !!InClass;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	UK2Node_DynamicCast* CastNode = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(this, SourceGraph);
	bool bPureCast = bPure || Pins.Contains(InOutThenPin);
	CastNode->SetPurity(bPureCast);
	CastNode->TargetType = InClass;
	CastNode->AllocateDefaultPins();
	UEdGraphPin* CastInput = CastNode->GetCastSourcePin();
	bIsErrorFree &= ensure(CastInput) && TryCreateConnection(CompilerContext, SourceGraph, InOutVarPin, CastInput);
	InOutVarPin = CastNode->GetCastResultPin();
	InOutVarPin->PinType.PinSubCategoryObject = InClass;
	if (!bPureCast)
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InOutThenPin, CastNode->GetExecPin());
		InOutThenPin = FindThenPin(CastNode);
	}
	bIsErrorFree &= AssignTempAndGet(CompilerContext, SourceGraph, InOutThenPin, InOutVarPin, bPure);
	return ensure(bIsErrorFree);
}

UEdGraphPin* UK2Neuron::PureValidateObjectOrClass(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InObjectPin)
{
	if (!ensure(InObjectPin && !InObjectPin->PinType.IsContainer()))
		return nullptr;

	bool bIsObject = InObjectPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object;
	bool bIsClass = InObjectPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class;
	if (!ensure(bIsClass || bIsObject))
		return nullptr;

	const FName IsValidObjectName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValid);
	const FName IsValidClassName = GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, IsValidClass);
	UK2Node_CallFunction* IsValidFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	IsValidFuncNode->FunctionReference.SetExternalMember(bIsObject ? IsValidObjectName : IsValidClassName, UKismetSystemLibrary::StaticClass());
	IsValidFuncNode->AllocateDefaultPins();
	UEdGraphPin* IsValidInputPin = bIsObject ? IsValidFuncNode->FindPinChecked(TEXT("Object")) : IsValidFuncNode->FindPinChecked(TEXT("Class"));
	bool bIsErrorFree = CompilerContext.GetSchema()->TryCreateConnection(InObjectPin, IsValidInputPin);
	return IsValidFuncNode->GetReturnValuePin();
}

UEdGraphPin* UK2Neuron::SpawnPureVariableTo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* SourceTypePin, const FString& Value)
{
	if (!SourceTypePin)
		return nullptr;

	bool bIsErrorFree = true;
	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();

	UK2Node_TemporaryVariable* LocalVariable = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	LocalVariable->VariableType = SourceTypePin->PinType;
	LocalVariable->VariableType.bIsReference = false;
	LocalVariable->VariableType.bIsConst = true;
	LocalVariable->AllocateDefaultPins();
	auto VariablePin = LocalVariable->GetVariablePin();

	UK2Node_PureAssignmentStatement* AssignDefaultValue = CompilerContext.SpawnIntermediateNode<UK2Node_PureAssignmentStatement>(this, SourceGraph);
	AssignDefaultValue->AllocateDefaultPins();
	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, VariablePin, AssignDefaultValue->GetVariablePin());
	AssignDefaultValue->NotifyPinConnectionListChanged(AssignDefaultValue->GetVariablePin());

	if (SourceTypePin->Direction == EGPD_Input)
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, AssignDefaultValue->GetOutputPin(), SourceTypePin, false);
	}
	else
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SourceTypePin, AssignDefaultValue->GetValuePin(), false);
	}

	if (!Value.IsEmpty())
		K2Schema->TrySetDefaultValue(*AssignDefaultValue->GetValuePin(), Value);

	return bIsErrorFree ? AssignDefaultValue->GetOutputPin() : nullptr;
}

UEdGraphPin* UK2Neuron::SpawnPureVariable(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InVariablePin, const FString& DefaultValue, bool bConst)
{
	if (!SourceGraph)
		return nullptr;
	auto VarType = InVariablePin->PinType;
	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	if (!VarType.IsContainer() && bConst)
	{
		if (VarType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Float)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralFloat));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Boolean)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralBool));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Name)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralName));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Byte)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralByte));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			auto ReturnValuePin = NodeMakeLiteral->GetReturnValuePin();
			ReturnValuePin->PinType = VarType;
			return ReturnValuePin;
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_String)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralString));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Text)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralText));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
	}

	UK2Node_TemporaryVariable* TempVarOutput = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	TempVarOutput->VariableType = VarType;
	TempVarOutput->VariableType.bIsReference = false;
	TempVarOutput->VariableType.bIsConst = bConst;
	TempVarOutput->AllocateDefaultPins();

	UEdGraphPin* VariablePin = TempVarOutput->GetVariablePin();
	check(VariablePin);

	UK2Node_PureAssignmentStatement* PureAssignNode = CompilerContext.SpawnIntermediateNode<UK2Node_PureAssignmentStatement>(this, SourceGraph);
	PureAssignNode->AllocateDefaultPins();
	bool bIsErrorFree = K2Schema->TryCreateConnection(PureAssignNode->GetVariablePin(), VariablePin);
	PureAssignNode->PinConnectionListChanged(PureAssignNode->GetVariablePin());

	K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(PureAssignNode->GetValuePin());
	K2Schema->TrySetDefaultValue(*PureAssignNode->GetValuePin(), DefaultValue);

	if (Pins.Contains(InVariablePin))
	{
		bIsErrorFree &= ensure(CompilerContext.MovePinLinksToIntermediate(*InVariablePin, *PureAssignNode->GetValuePin()).CanSafeConnect());
	}
	else
	{
		bIsErrorFree &= ensure(K2Schema->TryCreateConnection(InVariablePin, PureAssignNode->GetValuePin()));
	}

	if (!bIsErrorFree)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("AutoCreateRefTermPin_AssignmentError", "SpawnPureVariable Expansion: Assignment Error @@").ToString(), PureAssignNode);
		BreakAllNodeLinks();
		return nullptr;
	}
	return PureAssignNode->GetOutputPin();
}

UEdGraphPin* UK2Neuron::MakeTemporaryVariable(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const FEdGraphPinType& PinType)
{
	UK2Node_TemporaryVariable* LocalVariable = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
	LocalVariable->VariableType = PinType;
	LocalVariable->VariableType.bIsReference = false;
	LocalVariable->VariableType.bIsConst = false;
	LocalVariable->AllocateDefaultPins();
	return LocalVariable->GetVariablePin();
}

UEdGraphPin* UK2Neuron::MakeLiteralVariable(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* SrcPin, const FString& Value)
{
	check(SrcPin);

	if (LiteralVariables.Contains(SrcPin))
		return LiteralVariables[SrcPin];

	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	if (!SrcPin->PinType.IsContainer())
	{
		static auto LiteralTypes = TMap<const FName, const FName>{
			{UEdGraphSchema_K2::PC_Boolean, FName{TEXT("MakeLiteralBool")}},
			{UEdGraphSchema_K2::PC_Byte, FName{TEXT("MakeLiteralByte")}},
			{UEdGraphSchema_K2::PC_Float, FName{TEXT("MakeLiteralFloat")}},
			{UEdGraphSchema_K2::PC_Int, FName{TEXT("MakeLiteralInt")}},
			{UEdGraphSchema_K2::PC_String, FName{TEXT("MakeLiteralString")}},
			{UEdGraphSchema_K2::PC_Name, FName{TEXT("MakeLiteralName")}},
			{UEdGraphSchema_K2::PC_Text, FName{TEXT("MakeLiteralText")}},
			{UEdGraphSchema_K2::PC_SoftClass, FName{TEXT("MakeSoftClassPath")}},
			{UEdGraphSchema_K2::PC_SoftObject, FName{TEXT("MakeSoftObjectPath")}},
		};
		UFunction* Func = nullptr;
		if (auto Find = LiteralTypes.Find(SrcPin->PinType.PinCategory))
		{
			Func = UKismetSystemLibrary::StaticClass()->FindFunctionByName(*Find);
		}
		else if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class)
		{
			Func = GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralClass);
		}
		else if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && !SrcPin->PinType.bIsWeakPointer)
		{
			Func = GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralObject);
		}
		else if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && ensure(SrcPin->PinType.PinSubCategoryObject.IsValid()))
		{
			if (SrcPin->PinType.PinSubCategoryObject == TBaseStructure<FSoftClassPath>::Get())
			{
				Func = GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeSoftClassPath);
			}
			else if (SrcPin->PinType.PinSubCategoryObject == TBaseStructure<FSoftObjectPath>::Get())
			{
				Func = GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeSoftObjectPath);
			}

#if 0
			else if (SrcPin->PinType.PinSubCategoryObject == TBaseStructure<FKey>::Get())
			{
				UK2Node_MakeStruct* MakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_MakeStruct>(this, SourceGraph);
				MakeLiteral->StructType = Cast<UScriptStruct>(SrcPin->PinType.PinSubCategoryObject.Get());
				MakeLiteral->AllocateDefaultPins();
				K2Schema->SetPinDefaultValueAtConstruction(FindValuePin(MakeLiteral, EGPD_Input), Value);
				MakeLiteral->bMadeAfterOverridePinRemoval = true;
				CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeLiteral, this);
			}

			UK2Node_MakeStruct* MakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_MakeStruct>(this, SourceGraph);
			MakeLiteral->StructType = Cast<UScriptStruct>(SrcPin->PinType.PinSubCategoryObject.Get());
			MakeLiteral->AllocateDefaultPins();
			MakeLiteral->bMadeAfterOverridePinRemoval = true;
			CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeLiteral, this);
#endif
		}

		if (Func)
		{
			auto MakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			MakeLiteral->SetFromFunction(Func);
			MakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*FindValuePin(MakeLiteral, EGPD_Input), Value);
			K2Schema->SetPinDefaultValueAtConstruction(FindValuePin(MakeLiteral, EGPD_Input), Value);
			auto ValuePin = MakeLiteral->GetReturnValuePin();
			if (SrcPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object)
			{
				ValuePin->PinType = SrcPin->PinType;
				ValuePin->PinType.bIsReference = false;
			}
			LiteralVariables.Add(SrcPin, ValuePin);
			return ValuePin;
		}
	}
	return nullptr;
}

bool UK2Neuron::TryCreateConnection(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InPinA, UEdGraphPin* InPinB, bool bMove /*= true*/)
{
	static auto ConvertIfFloatType = [](UK2Neuron* This, UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin = nullptr) {
		do
		{
#if UE_5_00_OR_LATER && !UE_5_02_OR_LATER
			if (!TestSelfPin || TestSelfPin->Direction != EGPD_Input)
				break;

			if (TestSelfPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Real)
				break;
			if (!LinkPin)
				LinkPin = TestSelfPin;

			if (TestSelfPin == LinkPin && LinkPin->LinkedTo.Num() > 0)
				LinkPin = LinkPin->LinkedTo[0];

			if (!LinkPin || TestSelfPin == LinkPin)
				break;

			if (LinkPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Real)
				break;
			if (LinkPin->PinType.PinSubCategory == TestSelfPin->PinType.PinSubCategory)
				break;

			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(This, SourceGraph);
			bool bIsSelfFloat = TestSelfPin->PinType.PinSubCategory == UEdGraphSchema_K2::PC_Float;
			NodeMakeLiteral->SetFromFunction(bIsSelfFloat ? GMP_UFUNCTION_CHECKED(UKismetMathLibrary, Conv_DoubleToFloat) : GMP_UFUNCTION_CHECKED(UKismetMathLibrary, Conv_FloatToDouble));
			NodeMakeLiteral->AllocateDefaultPins();

			auto InputRealPin = NodeMakeLiteral->FindPinChecked(bIsSelfFloat ? TEXT("InDouble") : TEXT("InFloat"));
			UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
			ensure(This->TryCreateConnection(CompilerContext, SourceGraph, TestSelfPin, VariablePin));
			NodeMakeLiteral->NotifyPinConnectionListChanged(VariablePin);
			return InputRealPin;
#endif
		} while (false);
		return TestSelfPin;
	};

	bool bIsErrorFree = ensureAlways(InPinA && InPinB && !(Pins.Contains(InPinA) && Pins.Contains(InPinB)));
	if (bIsErrorFree)
	{
		if (Pins.Contains(InPinA))
		{
			ensureAlways(!Pins.Contains(InPinB));
			ensureAlways(InPinB->Direction == InPinA->Direction);
			if (ShouldDefaultToCtx(InPinA))
			{
				bIsErrorFree &= CompilerContext.GetSchema()->TryCreateConnection(GetCtxPin(), InPinB);
			}
			else
			{
				bIsErrorFree &= bMove ? CompilerContext.MovePinLinksToIntermediate(*InPinA, *ConvertIfFloatType(this, InPinB, CompilerContext, SourceGraph, InPinA)).CanSafeConnect()
									  : CompilerContext.CopyPinLinksToIntermediate(*InPinA, *ConvertIfFloatType(this, InPinB, CompilerContext, SourceGraph, InPinA)).CanSafeConnect();

				if (CustomStructurePinGuids.Contains(GetPinGuid(InPinA)))
				{
					if (UK2Node* Node = Cast<UK2Node>(InPinB->GetOwningNode()))
					{
						ensure(!InPinA->PinType.IsContainer() || Node->DoesWildcardPinAcceptContainer(InPinB));
					}
					else
					{
						ensure(!InPinA->PinType.IsContainer());
					}

					InPinB->PinType = InPinA->PinType;
				}
			}
		}
		else if (Pins.Contains(InPinB))
		{
			ensureAlways(!Pins.Contains(InPinA));
			ensureAlways(InPinB->Direction == InPinA->Direction);
			if (ShouldDefaultToCtx(InPinB))
			{
				bIsErrorFree &= CompilerContext.GetSchema()->TryCreateConnection(GetCtxPin(), InPinA);
			}
			else
			{
				bIsErrorFree &= bMove ? CompilerContext.MovePinLinksToIntermediate(*InPinB, *ConvertIfFloatType(this, InPinA, CompilerContext, SourceGraph, InPinB)).CanSafeConnect()
									  : CompilerContext.CopyPinLinksToIntermediate(*InPinB, *ConvertIfFloatType(this, InPinA, CompilerContext, SourceGraph, InPinB)).CanSafeConnect();

				if (CustomStructurePinGuids.Contains(GetPinGuid(InPinB)))
				{
					if (UK2Node* Node = Cast<UK2Node>(InPinA->GetOwningNode()))
					{
						ensure(!InPinB->PinType.IsContainer() || Node->DoesWildcardPinAcceptContainer(InPinA));
					}
					else
					{
						ensure(!InPinB->PinType.IsContainer());
					}

					InPinA->PinType = InPinB->PinType;
				}
			}
		}
		else
		{
			ensureAlways(!(InPinB->Direction == InPinA->Direction));
			bIsErrorFree &= CompilerContext.GetSchema()->TryCreateConnection(InPinA, InPinB);
			if (!ensure(bIsErrorFree))
			{
				const auto Response = CompilerContext.GetSchema()->CanCreateConnection(InPinA, InPinB);
				CompilerContext.MessageLog.Error(*FText::Format(LOCTEXT("FailedBuildingConnection_ErrorFmt", "COMPILER ERROR: failed building connection with '{0}' at @@"), Response.Message).ToString(), this);
			}
		}
	}
	return bIsErrorFree;
}

UEdGraphPin* UK2Neuron::ParamsToArrayPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const TArray<UEdGraphPin*>& InPins)
{
	if (InPins.Num() == 0)
		return nullptr;

	bool bIsErrorFree = true;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	if (HasVariadicSupport)
	{
		// MessageFromVariadic
		auto MessageNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		MessageNode->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, MessageFromVariadic));
		MessageNode->AllocateDefaultPins();

		// MessageFromVariadic(TArray<FGMPTypedAddr>& MsgArr, ...)
		bIsErrorFree &= ConnectAdditionalPins(CompilerContext, SourceGraph, MessageNode, InPins);
		return bIsErrorFree ? MessageNode->FindPinChecked(TEXT("MsgArr")) : nullptr;
	}

	int32 ArrInputIndex = 0;
	UK2Node_MakeArray* MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
	MakeArrayNode->NumInputs = InPins.Num();
	MakeArrayNode->AllocateDefaultPins();

	for (auto ParamPin : InPins)
	{
		UK2Node_CallFunction* ConvertFunc = nullptr;
		UEdGraphPin* ValuePin = nullptr;
		if (ParamPin->PinType.IsArray())
		{
			// AddrFromArray
			ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			static UFunction* SetParamsFunc = GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromArray);
			ConvertFunc->SetFromFunction(SetParamsFunc);
			ConvertFunc->AllocateDefaultPins();
			ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
			ValuePin->PinType.PinCategory = ParamPin->PinType.PinCategory;
		}
		else if (ParamPin->PinType.IsSet())
		{
			// AddrFromSet
			ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			static UFunction* SetParamsFunc = GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromSet);
			ConvertFunc->SetFromFunction(SetParamsFunc);
			ConvertFunc->AllocateDefaultPins();
			ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
		}
		else if (ParamPin->PinType.IsMap())
		{
			// AddrFromMap
			ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			static UFunction* SetParamsFunc = GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromMap);
			ConvertFunc->SetFromFunction(SetParamsFunc);
			ConvertFunc->AllocateDefaultPins();
			ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
		}
		else /*(!NodeParamPin->PinType.IsContainer())*/
		{
			// AddrFromWild
			ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			static UFunction* SetParamsFunc = GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromWild);
			ConvertFunc->SetFromFunction(SetParamsFunc);
			ConvertFunc->AllocateDefaultPins();
			ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
			ValuePin->PinType = ParamPin->PinType;
		}

		if (Pins.Contains(ParamPin))
		{
			if (ParamPin->LinkedTo.Num() == 0)
			{
				auto VariablePin = MakeLiteralVariable(CompilerContext, SourceGraph, ParamPin, ParamPin->GetDefaultAsString());
				if (!VariablePin)
					VariablePin = SpawnPureVariableTo(CompilerContext, SourceGraph, ParamPin, ParamPin->GetDefaultAsString());
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamPin, VariablePin);
			}
			else
			{
				bIsErrorFree &= ParamPin && CompilerContext.CopyPinLinksToIntermediate(*ParamPin, *ValuePin).CanSafeConnect();
			}
		}
		else
		{
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamPin, ValuePin);
		}

		ConvertFunc->NotifyPinConnectionListChanged(ValuePin);
		auto ArrInpuPin = MakeArrayNode->FindPinChecked(MakeArrayNode->GetPinName(ArrInputIndex++));
		bIsErrorFree = TryCreateConnection(CompilerContext, SourceGraph, ConvertFunc->GetReturnValuePin(), ArrInpuPin);
		MakeArrayNode->NotifyPinConnectionListChanged(ArrInpuPin);
	}
	ensure(bIsErrorFree);
	return MakeArrayNode->GetOutputPin();
}

bool UK2Neuron::ConnectAdditionalPins(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* FuncNode, const TArray<UEdGraphPin*>& InPins, bool bWithCnt)
{
	bool bIsErrorFree = !!FuncNode;
	if (ensure(bIsErrorFree))
	{
		const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
		if (bWithCnt)
		{
			FEdGraphPinType CntPinType;
			CntPinType.PinCategory = UEdGraphSchema_K2::PC_Int;
			auto ValuePin = FuncNode->CreatePin(EGPD_Input, CntPinType, TEXT("VaridicArgmentCount"));
			auto VariablePin = MakeLiteralVariable(CompilerContext, SourceGraph, ValuePin, LexToString(InPins.Num()));
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, VariablePin, ValuePin);
		}

		for (int32 Idx = 0; Idx < InPins.Num(); ++Idx)
		{
			const auto& ParamPin = InPins[Idx];
			auto ValuePin = FuncNode->CreatePin(EGPD_Input, ParamPin->PinType, IsRunningCommandlet() ? FName(*LexToString(Idx)) : ParamPin->GetFName());
			if (Pins.Contains(ParamPin))
			{
				if (ParamPin->LinkedTo.Num() == 0)
				{
					auto VariablePin = MakeLiteralVariable(CompilerContext, SourceGraph, ParamPin, ParamPin->GetDefaultAsString());
					if (!VariablePin)
						VariablePin = SpawnPureVariableTo(CompilerContext, SourceGraph, ParamPin, ParamPin->GetDefaultAsString());
					bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, VariablePin, ValuePin);
				}
				else
				{
					bIsErrorFree &= ParamPin && CompilerContext.CopyPinLinksToIntermediate(*ParamPin, *ValuePin).CanSafeConnect();
				}
			}
			else
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamPin, ValuePin);
			}
		}
	}
	return bIsErrorFree;
}

bool UK2Neuron::ConnectMessagePins(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* FuncNode, const TArray<UEdGraphPin*>& InPins)
{
	bool bIsErrorFree = !!FuncNode;
	if (HasVariadicSupport && FuncNode->GetTargetFunction()->HasMetaData(FBlueprintMetadata::MD_Variadic))
	{
		bIsErrorFree &= ConnectAdditionalPins(CompilerContext, SourceGraph, FuncNode, InPins);
	}
	else
	{
		auto Find = FuncNode->Pins.FindByPredicate([](auto Pin) { return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && Pin->PinType.IsArray() && Pin->PinType.PinSubCategoryObject == FGMPTypedAddr::StaticStruct(); });
		auto ParamArrayPin = ParamsToArrayPin(CompilerContext, SourceGraph, InPins);
		bIsErrorFree &= !ParamArrayPin || (Find && TryCreateConnection(CompilerContext, SourceGraph, ParamArrayPin, *Find));
	}
	return bIsErrorFree;
}

UEdGraphPin* UK2Neuron::CreatePinFromInnerProp(const UObject* ClsOrFunc, FProperty* Property, const FString& InPrefix, const FString& InDisplayPrefix, EEdGraphPinDirection Direction)
{
	if (!Property)
		return nullptr;
	auto PropDirection = IsInputParameter(Property) ? EGPD_Input : EGPD_Output;
	if (Direction == EGPD_MAX)
		Direction = PropDirection;
	// 	if (Direction != PropDirection)
	// 		return nullptr;

	FString PinNameStr = FString::Printf(TEXT("%s%s"), *InPrefix, *Property->GetName());
	if (!ensure(!Property->HasAnyPropertyFlags(CPF_ReturnParm)))
		return nullptr;

	auto K2Schema = GetK2Schema();
	FString PinDescName = InDisplayPrefix.IsEmpty() ? DelimiterStr : InDisplayPrefix;
	auto PinFriendlyName = GetDisplayText(Property, *PinDescName);
	UEdGraphPin* Pin = CreatePin(Direction, NAME_None, *PinFriendlyName.ToString());
	SetPinMetaDataStr(Pin, *PinNameStr);

	ensure(K2Schema->ConvertPropertyToPinType(Property, Pin->PinType));
	Pin->bAdvancedView = EGPD_Output == Direction;
	PinDescName.Append(GetDisplayString(Property));

	FString DefaultValueAsString;
	auto Class = Cast<UClass>(ClsOrFunc);
	auto Func = Cast<UFunction>(ClsOrFunc);
	ensure(Class || Func);
	auto OuterClass = GetPropertyOwnerUObject<UClass>(Property);
	auto OuterFunc = GetPropertyOwnerUObject<UFunction>(Property);
	Class = Class ? Class : OuterClass;
	Func = Func ? Func : OuterFunc;
	if (Class)
	{
		const bool bUseReference = Pin->PinType.IsContainer() || Property->HasMetaData(RequiresConnection) || Property->HasMetaData(RequiresReference) || UnrealEditorUtils::ShouldUseStructReference(Pin);
		Pin->PinType.bIsReference = bUseReference;
		Pin->bDefaultValueIsIgnored = bUseReference || Property->HasAnyPropertyFlags(CPF_Transient);
		PinDescName.Append(FString::Printf(TEXT(" (member of %s"), *Class->GetName()));
		if (!bUseReference)
		{
			if (ShouldDefaultToCtx(Property))
			{
				Pin->bDefaultValueIsIgnored = true;
				PinDescName.Append(TEXT(", default to self"));
			}
			else if (Class == OuterClass && IsValid(ClsOrFunc) && ClsOrFunc->GetClass()->IsChildOf(Class))
			{
				FBlueprintEditorUtils::PropertyValueToString(Property, (const uint8*)ClsOrFunc, DefaultValueAsString);
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, DefaultValueAsString);
			}
			else if (FBlueprintCompilationManager::GetDefaultValue(Class, Property, DefaultValueAsString))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, DefaultValueAsString);
			}
			else
			{
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}
		}
		else
		{
			Pin->PinToolTip += TEXT("\nconnect it to use override value, disconnect it to use the default value");
		}
		if (Property->HasMetaData(DisableConnection))
			Pin->bNotConnectable = true;
		else if (Property->HasMetaData(RequiresConnection))
			Pin->bAdvancedView = false;
		else if (Property->HasMetaData(RequiresReference) || Property->HasAnyPropertyFlags(CPF_AdvancedDisplay) || Property->HasMetaData(TEXT("AdvancedDisplay")))
			Pin->bAdvancedView = true;
	}
	else if (Func)
	{
		bool bRequireConnection = Property->HasAnyPropertyFlags(CPF_ReferenceParm);
		Pin->PinType.bIsReference = bRequireConnection;
		Pin->bDefaultValueIsIgnored = bRequireConnection;

		auto FuncName = Func->GetName();
		FuncName.RemoveFromEnd(HEADER_GENERATED_DELEGATE_SIGNATURE_SUFFIX);
		PinDescName.Append(FString::Printf(TEXT(" (parameter of %s)"), *FuncName));

		if (!Pin->PinType.IsContainer() && !bRequireConnection)
		{
			if (ShouldDefaultToCtx(Property))
			{
				Pin->bDefaultValueIsIgnored = true;
				PinDescName.Append(TEXT(", default to self"));
			}
			else if (K2Schema->FindFunctionParameterDefaultValue(Func, Property, DefaultValueAsString))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, DefaultValueAsString);
			}
			else if (auto PinClass = ClassFromPin(Pin))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, FSoftClassPath(PinClass).ToString());
			}
			else
			{
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}
		}
	}
	else
	{
		ensureAlways(false);
	}

	bHasAdvancedViewPins |= Pin->bAdvancedView;
	Pin->PinToolTip = FString::Printf(TEXT("%s\n%s\n%s"), *PinDescName, *K2Schema->TypeToText(Pin->PinType).ToString(), *Property->GetToolTipText().ToString());

	if (!IsAllocWithOldPins() && Property->HasMetaData(MetaSplitStructPin))
		GetK2Schema()->SplitPin(Pin, false);

	return Pin;
}

UEdGraphPin* UK2Neuron::CreatePinFromInnerClsProp(const UClass* InDerivedCls, FProperty* Property, const FString& InPrefix, const FString& InDisplayPrefix /*= TEXT(".")*/, EEdGraphPinDirection Direction /*= EGPD_MAX*/)
{
	if (!ensure(Property && InDerivedCls && InDerivedCls->IsChildOf(GetPropertyOwnerUObject<UClass>(Property))))
		return nullptr;

	auto PropDirection = IsInputParameter(Property) ? EGPD_Input : EGPD_Output;
	if (Direction == EGPD_MAX)
		Direction = PropDirection;
	// 	if (Direction != PropDirection)
	// 		return nullptr;

	FString PinNameStr = FString::Printf(TEXT("%s%s"), *InPrefix, *Property->GetName());
	if (!ensure(!Property->HasAnyPropertyFlags(CPF_ReturnParm) /* || FindPin(PinName)*/))
		return nullptr;

	auto K2Schema = GetK2Schema();
	FString PinDescName = InDisplayPrefix.IsEmpty() ? DelimiterStr : InDisplayPrefix;
	auto PinFriendlyName = GetDisplayText(Property, *PinDescName);
	UEdGraphPin* Pin = CreatePin(Direction, NAME_None, *PinFriendlyName.ToString());
	SetPinMetaDataStr(Pin, PinNameStr);
	if (ShouldInputPropCheckable(Property))
	{
		NeuronCheckableGuids.Add(GetPinGuid(Pin));
	}

	ensure(K2Schema->ConvertPropertyToPinType(Property, Pin->PinType));
	Pin->bAdvancedView = EGPD_Output == Direction;
	PinDescName.Append(GetDisplayString(Property));

	FString DefaultValueAsString;
	auto Class = Cast<UClass>(InDerivedCls);
	auto OuterClass = GetPropertyOwnerUObject<UClass>(Property);
	Class = Class ? Class : OuterClass;
	if (Class)
	{
		const bool bUseReference = Pin->PinType.IsContainer() || Property->HasMetaData(RequiresConnection) || Property->HasMetaData(RequiresReference) || UnrealEditorUtils::ShouldUseStructReference(Pin);
		Pin->PinType.bIsReference = bUseReference;
		Pin->bDefaultValueIsIgnored = bUseReference || Property->HasAnyPropertyFlags(CPF_Transient);
		PinDescName.Append(FString::Printf(TEXT(" (member of %s"), *Class->GetName()));
		if (!bUseReference)
		{
			if (ShouldDefaultToCtx(Property))
			{
				Pin->bDefaultValueIsIgnored = true;
				PinDescName.Append(TEXT(", default to self"));
			}
			else if (FBlueprintCompilationManager::GetDefaultValue(Class, Property, DefaultValueAsString))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, DefaultValueAsString);
			}
			else
			{
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}
		}
		else
		{
			Pin->PinToolTip += TEXT("\nconnect it to use override value, disconnect it to use the default value");
		}
		if (Property->HasMetaData(DisableConnection))
			Pin->bNotConnectable = true;
		else if (Property->HasMetaData(RequiresConnection))
			Pin->bAdvancedView = false;
		else if (Property->HasMetaData(RequiresReference))
			Pin->bAdvancedView = true;
	}
	else
	{
		ensureAlways(false);
	}

	bHasAdvancedViewPins |= Pin->bAdvancedView;
	Pin->PinToolTip = FString::Printf(TEXT("%s\n%s\n%s"), *PinDescName, *K2Schema->TypeToText(Pin->PinType).ToString(), *Property->GetToolTipText().ToString());

	if (!IsAllocWithOldPins() && Property->HasMetaData(MetaSplitStructPin))
		GetK2Schema()->SplitPin(Pin, false);

	return Pin;
}

UEdGraphPin* UK2Neuron::CreatePinFromInnerFuncProp(FFieldVariant InFuncOrDelegate, FProperty* Property, const FString& InPrefix, const FString& InDisplayPrefix /*= TEXT(".")*/, EEdGraphPinDirection Direction /*= EGPD_MAX*/)
{
	FDelegateProperty* InDelegateProp = InFuncOrDelegate.Get<FDelegateProperty>();
	UFunction* InFunc = InDelegateProp ? InDelegateProp->SignatureFunction : InFuncOrDelegate.Get<UFunction>();

	static auto IsStructureWildcardProperty = [](const UFunction* Function, const FName PropertyName) {
		if (Function && !PropertyName.IsNone())
		{
			TArray<FString> Names;
			FillCustomStructureParameterNames(Function, Names);
			if (Names.Contains(PropertyName.ToString()))
			{
				return true;
			}
		}
		return false;
	};

	if (!ensure(Property && GetPropertyOwnerUObject<UFunction>(Property) == InFunc))
		return nullptr;

	auto PropDirection = IsInputParameter(Property) ? EGPD_Input : EGPD_Output;
	if (Direction == EGPD_MAX)
		Direction = PropDirection;
	// 	if (Direction != PropDirection)
	// 		return nullptr;

	FString PinNameStr = FString::Printf(TEXT("%s%s"), *InPrefix, *Property->GetName());
	if (!ensure(!Property->HasAnyPropertyFlags(CPF_ReturnParm) /* || FindPin(PinName)*/))
		return nullptr;

	auto K2Schema = GetK2Schema();
	FString PinDescName = InDisplayPrefix.IsEmpty() ? DelimiterStr : InDisplayPrefix;
	auto PinFriendlyName = GetDisplayText(Property, *PinDescName);
	UEdGraphPin* Pin = CreatePin(Direction, NAME_None, *PinFriendlyName.ToString());
	SetPinMetaDataStr(Pin, PinNameStr);

	if (InDelegateProp && ShouldEventParamCheckable(Property))
	{
		NeuronCheckableGuids.Add(GetPinGuid(Pin));
	}

	if (IsStructureWildcardProperty(InFunc, Property->GetFName()))
	{
		CustomStructurePinGuids.Add(GetPinGuid(Pin));
		SetPinMeta(Pin, FEdPinExtraMetaBase::IsCustomStructurePin);
	}

	const bool bHasAutoCreateRefTerms = InFunc->HasMetaData(FBlueprintMetadata::MD_AutoCreateRefTerm);
	if (bHasAutoCreateRefTerms)
	{
		TArray<FString> AutoCreateRefTermNames;
		K2Schema->GetAutoEmitTermParameters(InFunc, AutoCreateRefTermNames);
		if (AutoCreateRefTermNames.Contains(Property->GetName()))
		{
			AutoCreateRefTermPinGuids.Add(GetPinGuid(Pin));
			SetPinMeta(Pin, FEdPinExtraMetaBase::IsAutoCreateRefTermPin);
		}
	}

	ensure(K2Schema->ConvertPropertyToPinType(Property, Pin->PinType));
	Pin->bAdvancedView = EGPD_Output == Direction || Property->HasAnyPropertyFlags(CPF_AdvancedDisplay);

	PinDescName.Append(GetDisplayString(Property));

	FString DefaultValueAsString;
	auto Func = InFunc;
	auto OuterFunc = GetPropertyOwnerUObject<UFunction>(Property);
	Func = Func ? Func : OuterFunc;
	if (Func)
	{
		bool bRequireConnection = Property->HasAnyPropertyFlags(CPF_ReferenceParm);
		Pin->PinType.bIsReference = bRequireConnection;
		Pin->bDefaultValueIsIgnored = bRequireConnection;

		auto FuncName = Func->GetName();
		FuncName.RemoveFromEnd(HEADER_GENERATED_DELEGATE_SIGNATURE_SUFFIX);
		PinDescName.Append(FString::Printf(TEXT(" (parameter of %s)"), *FuncName));

		if (!Pin->PinType.IsContainer() && !bRequireConnection)
		{
			if (ShouldDefaultToCtx(Property))
			{
				Pin->bDefaultValueIsIgnored = true;
				PinDescName.Append(TEXT(", default to self"));
			}
			else if (K2Schema->FindFunctionParameterDefaultValue(Func, Property, DefaultValueAsString))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, DefaultValueAsString);
			}
			else if (auto PinClass = ClassFromPin(Pin))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(Pin, FSoftClassPath(PinClass).ToString());
			}
			else
			{
				K2Schema->SetPinAutogeneratedDefaultValueBasedOnType(Pin);
			}
		}
	}
	else
	{
		ensureAlways(false);
	}

	bHasAdvancedViewPins |= Pin->bAdvancedView;
	Pin->PinToolTip = FString::Printf(TEXT("%s\n%s\n%s"), *PinDescName, *K2Schema->TypeToText(Pin->PinType).ToString(), *Property->GetToolTipText().ToString());

	if (!IsAllocWithOldPins() && Property->HasMetaData(MetaSplitStructPin))
		GetK2Schema()->SplitPin(Pin, false);

	return Pin;
}

bool UK2Neuron::ConnectObjectPreparedPins(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* FuncNode, bool bUseKey)
{
	bool bIsErrorFree = !!FuncNode;
	TArray<UEdGraphPin*> InPins;
	for (const auto& Pair : ObjectPreparedPins)
		InPins.Add(bUseKey ? Pair.Key : Pair.Value);

	if (HasVariadicSupport && FuncNode->GetTargetFunction()->HasMetaData(FBlueprintMetadata::MD_Variadic))
	{
		bIsErrorFree &= ConnectAdditionalPins(CompilerContext, SourceGraph, FuncNode, InPins);
	}
	else
	{
		auto Find = FuncNode->Pins.FindByPredicate([](auto Pin) { return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && Pin->PinType.IsArray() && Pin->PinType.PinSubCategoryObject == FGMPTypedAddr::StaticStruct(); });
		auto ParamArrayPin = ParamsToArrayPin(CompilerContext, SourceGraph, InPins);
		bIsErrorFree &= !ParamArrayPin || (Find && TryCreateConnection(CompilerContext, SourceGraph, ParamArrayPin, *Find));
	}
	return bIsErrorFree;
}

bool UK2Neuron::IsObjectClassPin(const UEdGraphPin* Pin, bool bExact) const
{
	bool bExactMatch = ((ObjClassPinGuid == GetPinGuid(Pin)) && ensure(Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Class && !Pin->PinType.IsContainer()));
	return bExact || bExactMatch ? bExactMatch : [&] {
		auto MemberPart = Pin->GetName();
		Pin->GetName().Split(MemberDelimiter, nullptr, &MemberPart);
		return MemberPart == ObjectClassPropName.ToString();
	}();
}

UEdGraphPin* UK2Neuron::GetObjectFactoryClassPin(const TArray<UEdGraphPin*>* InPinsToSearch) const
{
	UEdGraphPin* RetClassPin = nullptr;
	FString MemberPart;
	for (auto Pin : InPinsToSearch ? *InPinsToSearch : Pins)
	{
		if (ObjClassPinGuid == GetPinGuid(Pin))
		{
			RetClassPin = Pin;
			break;
		}
		MemberPart = Pin->GetName();
		if (!MemberPart.IsEmpty() && MemberPart[0] == AffixesObject.PropPrefix)
		{
			Pin->GetName().Split(MemberDelimiter, nullptr, &MemberPart);
			if (MemberPart == ObjectClassPropName.ToString())
			{
				RetClassPin = Pin;
				break;
			}
		}
	}

	return RetClassPin;
}

UEdGraphPin* UK2Neuron::GetSpawnedObjectClassPin(const TArray<UEdGraphPin*>* InPinsToSearch, UClass** OutClass) const
{
	const TArray<UEdGraphPin*>* PinsToSearch = InPinsToSearch ? InPinsToSearch : &Pins;
	auto RetPin = GetSpecialClassPin(*PinsToSearch, ObjClassPinGuid, OutClass);
	return RetPin;
}

UClass* UK2Neuron::GetSpawnedObjectClass(const TArray<UEdGraphPin*>* InPinsToSearch, UEdGraphPin** OutPin) const
{
	UClass* Class = nullptr;
	auto Pin = GetSpawnedObjectClassPin(InPinsToSearch, &Class);
	if (OutPin)
		*OutPin = Pin;
	return Class;
}

bool UK2Neuron::CreateObjectSpawnPins(UClass* OwnerClass, TArray<UEdGraphPin*>* InPinsToSearch, UClass* InOverrideClass)
{
	UClass* ObjectClass = nullptr;
	do
	{
		if (!OwnerClass)
			break;

		auto ObjectClassPin = GetSpecialClassPin(InPinsToSearch ? *InPinsToSearch : Pins, ObjClassPinGuid, &ObjectClass);

		if (InOverrideClass)
			ObjectClass = InOverrideClass;

		if (!ObjectClass || !ensureAlways(ValidateObjectSpawning(OwnerClass, nullptr, InPinsToSearch ? *InPinsToSearch : Pins)))
			break;
	} while (0);

	if (!BindObjBlueprintCompiledEvent(ObjectClass))
		return false;

	auto IgnorePropertyList = GetHideProperties(ObjectClass);
	FString ObjClassName = ObjectClass->GetName();
	ObjClassName.RemoveFromEnd(MetaTagPostfix);

	const UEdGraphSchema_K2* K2Schema = GetK2Schema();
	auto SpawnedSucceedPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, SpawnedSucceedName);
	SpawnedSucceedPin->bAdvancedView = true;
	SpawnedSucceedPin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("%s(%s)"), *SpawnedSucceedName.ToString(), *ObjClassName));

	// default show
	const auto HideInstMeta = TEXT("HideInstanceExposure");
	if (!(ObjectClass->GetBoolMetaData(HideInstMeta) || OwnerClass->GetBoolMetaData(HideInstMeta)))
	{
		auto SpawnedObjectPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Object, SpawnedObjectName);
		SpawnedObjectPin->PinType.PinSubCategoryObject = ObjectClass;
		SpawnedObjectPin->PinFriendlyName = FText::FromString(TEXT(".SpawnedInstance"));
		SpawnedObjectPin->bAdvancedView = true;
	}

	auto SpawnedFailedPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, SpawnedFailedName);
	SpawnedFailedPin->bAdvancedView = true;
	SpawnedFailedPin->PinFriendlyName = FText::FromString(FString::Printf(TEXT("%s(%s)"), *SpawnedFailedName.ToString(), *ObjClassName));

	if (UFunction* PreSpawnFunction = OwnerClass->FindFunctionByName(BeginSpawningFuncName))
	{
		FString FuncPrefix = FString::Printf(TEXT("%c%s%s%s%s"), AffixesObject.ParamPrefix, *OwnerClass->GetName(), *FunctionDelimiter, *BeginSpawningFuncName.ToString(), *MemberDelimiter);
		const auto& WorldCtxName = PreSpawnFunction->GetMetaData(FBlueprintMetadata::MD_WorldContext);
		for (TFieldIterator<FProperty> PropertyIt(PreSpawnFunction); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
		{
			if (!IsInputParameter(*PropertyIt) || PropertyIt->GetFName() == ObjectClassPropName || PropertyIt->GetName() == WorldCtxName)
				continue;
			CreatePinFromInnerFuncProp(PreSpawnFunction, *PropertyIt, FuncPrefix, TEXT("`"));
		}
	}

	if (UFunction* PostSpawnFunction = OwnerClass->FindFunctionByName(PostSpawningFuncName))
	{
		FString FuncPrefix = FString::Printf(TEXT("%c%s%s%s%s"), AffixesObject.ParamPrefix, *OwnerClass->GetName(), *FunctionDelimiter, *PostSpawningFuncName.ToString(), *MemberDelimiter);
		const auto& WorldCtxName = PostSpawnFunction->GetMetaData(FBlueprintMetadata::MD_WorldContext);
		for (TFieldIterator<FProperty> PropertyIt(PostSpawnFunction); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
		{
			if (!IsInputParameter(*PropertyIt) || PropertyIt->GetFName() == SpawnedObjectPropName || PropertyIt->GetName() == WorldCtxName)
				continue;
			CreatePinFromInnerFuncProp(PostSpawnFunction, *PropertyIt, FuncPrefix, TEXT("`"));
		}
	}

	for (UClass* CurrentClass = ObjectClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		FString MemberPrefix = FString::Printf(TEXT("%c%s%s"), AffixesObject.PropPrefix, *CurrentClass->GetName(), *MemberDelimiter);
		for (TFieldIterator<FProperty> PropertyIt(CurrentClass, EFieldIteratorFlags::ExcludeSuper); PropertyIt; ++PropertyIt)
		{
			FProperty* Property = *PropertyIt;
			if (IgnorePropertyList.Contains(Property->GetName()) || Property->IsA(FMulticastDelegateProperty::StaticClass()))
				continue;

			const bool bIsExposedToSpawn = UEdGraphSchema_K2::IsPropertyExposedOnSpawn(Property);
			const bool bIsSettableExternally = !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
			if (bIsExposedToSpawn && bIsSettableExternally && !Property->HasAnyPropertyFlags(CPF_Parm) /*&& Property->HasAnyPropertyFlags(CPF_BlueprintVisible)*/)
			{
				CreatePinFromInnerClsProp(ObjectClass, Property, MemberPrefix, DelimiterStr, EGPD_Input);
			}
		}
	}

	if (IsInUbergraph() && CreateDelegatesForClass(ObjectClass, AffixesObject))
	{
		auto UnlinkPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UnlinkObjEventName);
		UnlinkPin->bAdvancedView = true;
	}
	return true;
}

namespace K2Neuron
{
template<typename... TArgs>
static void ErrorLog(FKismetCompilerContext* InCompilerContext, const TCHAR* Fmt, TArgs... Args)
{
	UE_LOG(LogTemp, Error, TEXT("%s"), Fmt);
	if (InCompilerContext)
		InCompilerContext->MessageLog.Error(Fmt, Args...);
}
}  // namespace K2Neuron

UClass* UK2Neuron::ValidateObjectSpawning(UClass* OwnerClass, FKismetCompilerContext* CompilerContext, const TArray<UEdGraphPin*>& InPinsToSearch, UClass** OutClass)
{
	if (!OwnerClass)
		return nullptr;

	FName ProxyPrespawnFunctionName = BeginSpawningFuncName;
	UFunction* PreSpawnFunction = OwnerClass->FindFunctionByName(ProxyPrespawnFunctionName);

	FName ProxyPostpawnFunctionName = PostSpawningFuncName;
	UFunction* PostSpawnFunction = OwnerClass->FindFunctionByName(ProxyPostpawnFunctionName);

	UClass* SpawnedClass = nullptr;
	auto SpawnedClassPin = GetSpecialClassPin(InPinsToSearch, ObjClassPinGuid, &SpawnedClass);

	if (OutClass)
		*OutClass = SpawnedClass;

	do
	{
		if (SpawnedClassPin || PreSpawnFunction || PostSpawnFunction)
		{
			if (!SpawnedClassPin || !SpawnedClass)
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Attempting to use Spawning Object but Proxy Factory Function missing a SpawnedClass parameter. @@"), this);
				break;
			}
			else if (SpawnedClass->IsChildOf(GetBlueprintClass(this)))
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Attempting to use Spawning Object that is a sub class of this Blueprint. @@"), SpawnedClassPin);
				break;
			}

			if (!PreSpawnFunction)
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Missing function BeginSpawningObejct. @@"), SpawnedClassPin);
				break;
			}

			auto ClassProp = CastField<FClassProperty>(PreSpawnFunction->FindPropertyByName(ObjectClassPropName));
			if (!ClassProp || !IsInputParameter(ClassProp))
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: BeginSpawningObejct Missing InputParameter {SpawnedClass}. @@"), SpawnedClassPin);
				break;
			}

			auto ObjectProp = CastField<FObjectProperty>(PreSpawnFunction->FindPropertyByName(UEdGraphSchema_K2::PN_ReturnValue));
			if (!ObjectProp)
				ObjectProp = CastField<FObjectProperty>(PreSpawnFunction->FindPropertyByName(SpawnedObjectPropName));
			if (!ObjectProp || !ObjectProp->PropertyClass || !SpawnedClass->IsChildOf(ObjectProp->PropertyClass) || IsInputParameter(ObjectProp))
			{
				K2Neuron::ErrorLog(CompilerContext,
								   *FString::Printf(TEXT("ValidateObjectSpawning: BeginSpawningObejct {SpawnedObject} Type Error [%s]-[%s]. @@"), *GetNameSafe(SpawnedClass), *GetNameSafe(ObjectProp ? ObjectProp->PropertyClass : nullptr)),
								   SpawnedClassPin);

				break;
			}

			if (!PostSpawnFunction && SpawnedClass->IsChildOf<AActor>())
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Missing function PostSpawningObejct For Actor Type. @@"), SpawnedClassPin);
				break;
			}

			if (PostSpawnFunction)
			{
				auto SpawnedObjectProp = CastField<FObjectProperty>(PostSpawnFunction->FindPropertyByName(SpawnedObjectPropName));
				if (!SpawnedObjectProp)
				{
					K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: PostSpawningObejct Missing InputParameter {SpawnedObject}. @@"), SpawnedClassPin);
					break;
				}
				if (!SpawnedObjectProp->PropertyClass || !SpawnedClass->IsChildOf(SpawnedObjectProp->PropertyClass) || !IsInputParameter(SpawnedObjectProp))
				{
					K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: PostSpawningObejct {SpawnedObject} Type Error. @@"), SpawnedClassPin);
					break;
				}
			}

			return SpawnedClass;
		}
	} while (false);
	return nullptr;
}

FProperty* UK2Neuron::DetectObjectSpawning(UClass* OwnerClass, FKismetCompilerContext* CompilerContext, FName DetectPinName)
{
	do
	{
		if (!OwnerClass)
			break;

		UFunction* PreSpawnFunction = OwnerClass->FindFunctionByName(BeginSpawningFuncName);
		UFunction* PostSpawnFunction = OwnerClass->FindFunctionByName(PostSpawningFuncName);

		auto SpawnedClassProp = PreSpawnFunction ? CastField<FClassProperty>(PreSpawnFunction->FindPropertyByName(ObjectClassPropName)) : nullptr;
		const UClass* SpawnedClass = SpawnedClassProp ? SpawnedClassProp->MetaClass : nullptr;

		if (SpawnedClassProp || PreSpawnFunction || PostSpawnFunction)
		{
			if (!SpawnedClassProp || !IsInputParameter(SpawnedClassProp) || !SpawnedClass)
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: BeginSpawningObejct Missing InputParameter {SpawnedClass}. @@"), this);
				break;
			}
			else if (SpawnedClass->IsChildOf(GetBlueprintClass(this)))
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Attempting to use Spawning Object that is a sub class of this Blueprint. @@"), this);
				break;
			}

			if (!PreSpawnFunction)
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Missing function BeginSpawningObejct. @@"), this);
				break;
			}

			auto ObjectProp = CastField<FObjectProperty>(PreSpawnFunction->FindPropertyByName(UEdGraphSchema_K2::PN_ReturnValue));
			if (!ObjectProp)
				ObjectProp = CastField<FObjectProperty>(PreSpawnFunction->FindPropertyByName(SpawnedObjectPropName));
			if (!ObjectProp || !ObjectProp->PropertyClass || !SpawnedClass->IsChildOf(ObjectProp->PropertyClass) || IsInputParameter(ObjectProp))
			{
				K2Neuron::ErrorLog(CompilerContext,
								   *FString::Printf(TEXT("ValidateObjectSpawning: BeginSpawningObejct {SpawnedObject} Type Error [%s]-[%s]. @@"), *GetNameSafe(SpawnedClass), *GetNameSafe(ObjectProp ? ObjectProp->PropertyClass : nullptr)),
								   this);

				break;
			}

			if (!PostSpawnFunction && SpawnedClass->IsChildOf<AActor>())
			{
				K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: Missing function PostSpawningObejct For Actor Type. @@"), this);
				break;
			}

			if (PostSpawnFunction)
			{
				auto SpawnedObjectProp = CastField<FObjectProperty>(PostSpawnFunction->FindPropertyByName(SpawnedObjectPropName));
				if (!SpawnedObjectProp)
				{
					K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: PostSpawningObejct Missing InputParameter {SpawnedObject}. @@"), this);
					break;
				}
				if (!SpawnedObjectProp->PropertyClass || !SpawnedClass->IsChildOf(SpawnedObjectProp->PropertyClass) || !IsInputParameter(SpawnedObjectProp))
				{
					K2Neuron::ErrorLog(CompilerContext, TEXT("ValidateObjectSpawning: PostSpawningObejct {SpawnedObject} Type Error. @@"), this);
					break;
				}
			}

			return SpawnedClassProp;
		}
	} while (false);
	return nullptr;
}

UEdGraphPin* UK2Neuron::ConnectObjectSpawnPins(UClass* OwnerClass, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& LastThenPin, UEdGraphPin* InstancePin, UFunction* ProxyFunc)
{
	ObjectPreparedPins.Empty();
	ProxySpawnFuncNode = nullptr;
	UEdGraphPin* SpawnedObjectReturnPin = nullptr;
	if (!OwnerClass || !ValidateObjectSpawning(OwnerClass, &CompilerContext, Pins))
		return SpawnedObjectReturnPin;

	bool bIsErrorFree = true;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	auto SpawnObjNode = MakeEventNode(CompilerContext, SourceGraph, "Spawn");
	auto EventLastThenPin = FindThenPin(SpawnObjNode);

	auto CreateEventPin = [&](UEdGraphPin* InputPin) -> UEdGraphPin*& {
		auto InputParamPin = SpawnObjNode->CreateUserDefinedPin(*LexToString(ObjectPreparedPins.Num()), InputPin->PinType, EGPD_Output);
		ObjectPreparedPins.Emplace(InputPin, InputParamPin);
		return ObjectPreparedPins.Last().Value;
	};

	UK2Node_CallFunction* const CallPrespawnProxyObjectNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	CallPrespawnProxyObjectNode->FunctionReference.SetExternalMember(BeginSpawningFuncName, OwnerClass);
	CallPrespawnProxyObjectNode->AllocateDefaultPins();

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventLastThenPin, CallPrespawnProxyObjectNode->GetExecPin());
	EventLastThenPin = FindThenPin(CallPrespawnProxyObjectNode, true);
	UEdGraphPin* const ParamInstPin = InstancePin;  // CreateEventPin(InstancePin);

	UEdGraphPin* PrespawnCallSelfPin = K2Schema->FindSelfPin(*CallPrespawnProxyObjectNode, EGPD_Input);
	check(PrespawnCallSelfPin);
	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamInstPin, PrespawnCallSelfPin);

	SpawnedObjectReturnPin = CallPrespawnProxyObjectNode->FindPin(UEdGraphSchema_K2::PN_ReturnValue);
	if (!SpawnedObjectReturnPin)
		SpawnedObjectReturnPin = CallPrespawnProxyObjectNode->FindPinChecked(SpawnedObjectPropName);
	check(SpawnedObjectReturnPin);

	UClass* ObjectClass = nullptr;
	auto InputClassPin = GetSpawnedObjectClassPin(&Pins, &ObjectClass);
	check(InputClassPin);
	UEdGraphPin* const ParamObjectClassPin = InputClassPin->LinkedTo.Num() == 0 ? InputClassPin : CreateEventPin(InputClassPin);

	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamObjectClassPin, CallPrespawnProxyObjectNode->FindPinChecked(ObjectClassPropName));
	auto AddFunctionParameters = [&](UK2Node_CallFunction* FuncNode, const FString& PinPrefix) {
		for (TFieldIterator<FProperty> PropIt(FuncNode->GetTargetFunction()); PropIt; ++PropIt)
		{
			const FProperty* Param = *PropIt;
			if (IsInputParameter(Param))
			{
				auto InputValue = [&]() -> UEdGraphPin* {
					if (Param->GetFName() == ObjectClassPropName)
					{
						return ParamObjectClassPin;
					}
					else if (Param->GetFName() == SpawnedObjectPropName)
					{
						return SpawnedObjectReturnPin;
					}
					else if (UEdGraphPin* InputParamPin = FindPin(PinPrefix + Param->GetName()))
					{
						return InputParamPin->LinkedTo.Num() == 0 ? InputParamPin : CreateEventPin(InputParamPin);
					}
					return nullptr;
				}();

				bIsErrorFree &= ensure(InputValue) && TryCreateConnection(CompilerContext, SourceGraph, InputValue, FuncNode->FindPinChecked(Param->GetFName()), false);
			}
		}
	};

	bIsErrorFree &= ConditionDo(CompilerContext, SourceGraph, SpawnedObjectReturnPin, EventLastThenPin, nullptr, FindPin(SpawnedFailedName));

	const FString PrePrefix = FString::Printf(TEXT("%c%s%s%s%s"), AffixesObject.ParamPrefix, *OwnerClass->GetName(), *FunctionDelimiter, *BeginSpawningFuncName.ToString(), *MemberDelimiter);
	const FString PostPrefix = FString::Printf(TEXT("%c%s%s%s%s"), AffixesObject.ParamPrefix, *OwnerClass->GetName(), *FunctionDelimiter, *PostSpawningFuncName.ToString(), *MemberDelimiter);

	AddFunctionParameters(CallPrespawnProxyObjectNode, PrePrefix);

	UK2Node_CallFunction* PostExecutionNode = nullptr;
	if (UFunction* PostSpawnFunction = OwnerClass->FindFunctionByName(PostSpawningFuncName))
	{
		UK2Node_CallFunction* const CallPostSpawnnProxyObjectNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		PostExecutionNode = CallPostSpawnnProxyObjectNode;
		CallPostSpawnnProxyObjectNode->SetFromFunction(PostSpawnFunction);
		CallPostSpawnnProxyObjectNode->AllocateDefaultPins();
		UEdGraphPin* PostspawnCallSelfPin = K2Schema->FindSelfPin(*CallPostSpawnnProxyObjectNode, EGPD_Input);
		check(PostspawnCallSelfPin);
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamInstPin, PostspawnCallSelfPin);
		if (auto SpawnedClassPin = CallPostSpawnnProxyObjectNode->FindPin(ObjectClassPropName))
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InputClassPin, SpawnedClassPin, false);
	}

	for (UEdGraphPin* CurPin : Pins)
	{
		if (!CurPin || CurPin->bOrphanedPin)
			continue;

		if (!MatchAffixesMember(CurPin, true, false, false))
			continue;
		auto PinMetaInfo = GetPinMetaInfo(CurPin);
		if (!PinMetaInfo.OwnerClass || !PinMetaInfo.Prop)
			continue;

		FString PropertyName = PinMetaInfo.Prop->GetName();
		const bool bPinLinked = HasAnyConnections(CurPin);
		const bool bHasDefaultValue = !CurPin->DefaultValue.IsEmpty() || !CurPin->DefaultTextValue.IsEmpty() || CurPin->DefaultObject;
		if (bPinLinked || bHasDefaultValue)
		{
			if (!bPinLinked)
			{
				FProperty* Property = FindFProperty<FProperty>(ObjectClass, *PropertyName);
				if (!Property)
				{
					continue;
				}

				FString DefaultValueAsString;
				FBlueprintCompilationManager::GetDefaultValue(ObjectClass, Property, DefaultValueAsString);
				if (DefaultValueAsString == CurPin->DefaultValue)
				{
					continue;
				}
			}

			UFunction* SetByNameFunction = K2Schema->FindSetVariableByNameFunction(CurPin->PinType);
			if (ensure(SetByNameFunction))
			{
				UK2Node_CallFunction* SetVarNode = nullptr;
				if (CurPin->PinType.IsArray())
				{
					SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallArrayFunction>(this, SourceGraph);
				}
				else
				{
					SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				}
				SetVarNode->SetFromFunction(SetByNameFunction);
				SetVarNode->AllocateDefaultPins();

				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventLastThenPin, SetVarNode->GetExecPin());
				EventLastThenPin = SetVarNode->GetThenPin();

				static const FName ObjectParamName(TEXT("Object"));
				static const FName ValueParamName(TEXT("Value"));
				static const FName PropertyNameParamName(TEXT("PropertyName"));

				UEdGraphPin* ObjectPin = SetVarNode->FindPinChecked(ObjectParamName);
				SpawnedObjectReturnPin->MakeLinkTo(ObjectPin);

				UEdGraphPin* PropertyNamePin = SetVarNode->FindPinChecked(PropertyNameParamName);
				PropertyNamePin->DefaultValue = PropertyName;

				UEdGraphPin* ValuePin = SetVarNode->FindPinChecked(ValueParamName);

				if (bPinLinked)
				{
					bool bIsDefaultMember = false;
					if (UEdGraphPin* LinkPin = CurPin->SubPins.Num() == 0 ? CurPin->LinkedTo[0] : nullptr)
					{
						while (auto Knot = Cast<UK2Node_Knot>(LinkPin->GetOwningNode()))
						{
							if (ensure(Knot->GetInputPin()->LinkedTo.Num() == 1))
								LinkPin = Knot->GetInputPin()->LinkedTo[0];
							else
								break;
						}
						if (auto VarGetNode = Cast<UK2Node_VariableGet>(LinkPin->GetOwningNode()))
						{
							if (VarGetNode->GetGraph() == GetGraph() && VarGetNode->IsNodePure() && VarGetNode->VariableReference.IsSelfContext() && !VarGetNode->VariableReference.IsLocalScope()
								&& !VarGetNode->VariableReference.GetMemberParentClass())
							{
								if (auto Prop = FindFProperty<FProperty>(GetBlueprintClass(this), VarGetNode->VariableReference.GetMemberName()))
								{
									bIsDefaultMember = !UEdGraphSchema_K2::IsPropertyExposedOnSpawn(Prop)                               //
													   && Prop->HasAnyPropertyFlags(CPF_DisableEditOnInstance | CPF_BlueprintReadOnly)  //
										/*&& !Prop->HasAnyPropertyFlags(CPF_Net)*/;
								}
							}
						}
						else if (auto CallFuncNode = Cast<UK2Node_CallFunction>(LinkPin->GetOwningNode()))
						{
							static auto LiteralFuncs = TSet<UFunction*>{
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralBool),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralByte),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralFloat),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralString),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralName),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralText),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeSoftClassPath),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeSoftObjectPath),
								GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralBool),
								GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralInt),
								GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralByte),
								GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralClass),
								GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralObject),
							};
							bIsDefaultMember = LiteralFuncs.Contains(CallFuncNode->GetTargetFunction()) && CallFuncNode->FindPinChecked(TEXT("Value"))->LinkedTo.Num() == 0;
						}
					}
					bIsDefaultMember = false;
					if (!bIsDefaultMember)
					{
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, CreateEventPin(CurPin), ValuePin, false);
					}
					else
					{
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, CurPin, ValuePin, false);
						SetVarNode->PinConnectionListChanged(ValuePin);
					}
				}
				else if (!CurPin->DefaultValue.IsEmpty() && CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && CurPin->PinType.PinSubCategoryObject.IsValid() && CurPin->PinType.PinSubCategoryObject->IsA<UEnum>())
				{
					UK2Node_EnumLiteral* EnumLiteralNode = CompilerContext.SpawnIntermediateNode<UK2Node_EnumLiteral>(this, SourceGraph);
					EnumLiteralNode->Enum = CastChecked<UEnum>(CurPin->PinType.PinSubCategoryObject.Get());
					EnumLiteralNode->AllocateDefaultPins();
					EnumLiteralNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->MakeLinkTo(ValuePin);

					UEdGraphPin* InPin = EnumLiteralNode->FindPinChecked(UK2Node_EnumLiteral::GetEnumInputPinName());
					InPin->DefaultValue = CurPin->DefaultValue;
				}
				else if (!CurPin->PinType.IsContainer() && CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
				{
					CompilerContext.CopyPinLinksToIntermediate(*CurPin, *ValuePin);
					if (CurPin->PinType.PinSubCategoryObject == TBaseStructure<FSoftClassPath>::Get())
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, MakeLiteralVariable(CompilerContext, SourceGraph, CurPin, CurPin->GetDefaultAsString()), ValuePin);
					if (CurPin->PinType.PinSubCategoryObject.Get() && CurPin->PinType.PinSubCategoryObject->GetFName() == TEXT("Key"))
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SpawnPureVariable(CompilerContext, SourceGraph, CurPin, CurPin->GetDefaultAsString()), ValuePin);
				}
			}
		}
	}
	// 	else if (auto MultiProp = CastField<FMulticastDelegateProperty>(OwnerClass->FindPropertyByName(PostSpawnFuncName)))
	// 	{
	// 		UK2Node_CallDelegate* const CallPostSpawnnProxyObjectNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallDelegate>(this, SourceGraph);
	// 		PostExecutionNode = CallPostSpawnnProxyObjectNode;
	// 		StaticAssignProperty(CallPostSpawnnProxyObjectNode, MultiProp);
	// 		CallPostSpawnnProxyObjectNode->AllocateDefaultPins();
	// 		UEdGraphPin* PostspawnCallSelfPin = K2Schema->FindSelfPin(*CallPostSpawnnProxyObjectNode, EGPD_Input);
	// 		check(PostspawnCallSelfPin);
	// 		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamInstPin, PostspawnCallSelfPin);
	// 	}

	if (PostExecutionNode)
		AddFunctionParameters(PostExecutionNode, PostPrefix);

	if (auto SpawnedObjPin = FindPin(SpawnedObjectName, EGPD_Output))
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, SpawnedObjectReturnPin, SpawnedObjPin);

	TArray<UEdGraphPin*> Execs;
	if (PostExecutionNode)
		Execs.Add(PostExecutionNode->GetExecPin());
	if (auto SpawnedSucceedPin = FindPin(SpawnedSucceedName))
	{
		if (HasAnyConnections(SpawnedSucceedPin))
			Execs.Add(SpawnedSucceedPin);
	}

	bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, EventLastThenPin, Execs);

	ProxySpawnFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	ProxySpawnFuncNode->SetFromFunction(ProxyFunc ? ProxyFunc : (HasVariadicSupport ? GMP_UFUNCTION_CHECKED(UGMPBPLib, CallFunctionVariadic) : GMP_UFUNCTION_CHECKED(UGMPBPLib, CallFunctionPacked)));
	ProxySpawnFuncNode->AllocateDefaultPins();
	ProxySpawnFuncNode->FindPinChecked(TEXT("FuncName"))->DefaultValue = SpawnObjNode->CustomFunctionName.ToString();

	bIsErrorFree &= ConnectObjectPreparedPins(CompilerContext, SourceGraph, ProxySpawnFuncNode.Get());
	bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, ProxySpawnFuncNode->GetExecPin());
	LastThenPin = EventLastThenPin;

	return ensure(bIsErrorFree) ? SpawnedObjectReturnPin : nullptr;
}

void UK2Neuron::OnSpawnedObjectClassChanged(UClass* OwnerClass, const TArray<UEdGraphPin*>* InPinsToSearch)
{
	UEdGraphPin* SpawnedClassPin = nullptr;
	auto SpawnedObjClass = GetSpawnedObjectClass(InPinsToSearch, &SpawnedClassPin);
	CachedNodeTitle.MarkDirty();

	{
		TArray<UEdGraphPin*> ToBeRemoved;
		const UEdGraphSchema_K2* K2Schema = GetK2Schema();
		TSet<FGuid> RemovingPinGuids = [&] {
			TSet<FGuid> RetGuid;
#if 0
			if (auto Pin = FindPin(ObjClassPinGuid))
				RetGuid.Add(GetPinGuid(Pin));
#endif
			if (auto Pin = FindPin(UnlinkObjEventName))
				RetGuid.Add(GetPinGuid(Pin));
			if (auto Pin = FindPin(SpawnedFailedName))
				RetGuid.Add(GetPinGuid(Pin));
			if (auto Pin = FindPin(SpawnedSucceedName))
				RetGuid.Add(GetPinGuid(Pin));
			if (auto Pin = FindPin(SpawnedObjectName))
				RetGuid.Add(GetPinGuid(Pin));
			return RetGuid;
		}();

		RemovingPinGuids.Append(MoveTemp(SpawnedPinGuids));
		RemovingPinGuids.Append(MoveTemp(ImportedPinGuids));
		RemovingPinGuids.Append(MoveTemp(BindedPinGuids));

		ToBeRemoved.Add(SpawnedClassPin);
		ToBeRemoved.Append(RemovePinsByGuid(RemovingPinGuids));
		ToBeRemoved.RemoveAll([](auto* TestPin) { return !TestPin; });

		CreateObjectSpawnPins(OwnerClass, &ToBeRemoved);
		ToBeRemoved.Remove(SpawnedClassPin);

		RestoreSplitPins(ToBeRemoved);
		RewireOldPinsToNewPins(ToBeRemoved, Pins, nullptr);
		RemoveUselessPinMetas();
		GetGraph()->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
	}
}

TArray<UEdGraphPin*> UK2Neuron::RemovePinsByName(const TSet<FName>& Names, bool bAffixes)
{
	TArray<UEdGraphPin*> ToBeRemoved;
	for (auto idx = Pins.Num() - 1; idx >= 0; --idx)
	{
		if (Names.Contains(Pins[idx]->PinName))
		{
			ToBeRemoved.Add(Pins[idx]);
			Pins.RemoveAtSwap(idx);
			continue;
		}

		auto PinName = Pins[idx]->PinName.ToString();
		if (MatchAffixes(Pins[idx], true, false, true))
		{
			ToBeRemoved.Add(Pins[idx]);
			Pins.RemoveAtSwap(idx);
		}
	}
	return ToBeRemoved;
}

TArray<UEdGraphPin*> UK2Neuron::RemovePinsByGuid(const TSet<FGuid>& Guids, bool bAffixes)
{
	TArray<UEdGraphPin*> ToBeRemoved;
	for (auto idx = Pins.Num() - 1; idx >= 0; --idx)
	{
		if (Guids.Contains(GetPinGuid(Pins[idx])))
		{
			ToBeRemoved.Add(Pins[idx]);
			Pins.RemoveAtSwap(idx);
			continue;
		}

		if (MatchAffixes(Pins[idx], true, false, true))
		{
			ToBeRemoved.Add(Pins[idx]);
			Pins.RemoveAtSwap(idx);
		}
	}
	return ToBeRemoved;
}

bool UK2Neuron::IsInUbergraph() const
{
	return GetK2Schema()->GetGraphType(GetGraph()) == GT_Ubergraph;
}

bool UK2Neuron::CreateEventsForClass(UClass* InClass, const UK2Neuron::FPinNameAffixes& Affixes, UClass* StopClass, TArray<UEdGraphPin*>* OldPins)
{
	TArray<UEdGraphPin*> ToBeRemoved;
	if (OldPins)
	{
		TArray<UEdGraphPin*>& StorePins = *OldPins;
		for (auto idx = StorePins.Num() - 1; idx >= 0; --idx)
		{
			if (StorePins[idx] && StorePins[idx]->Direction == EGPD_Output && MatchAffixes(StorePins[idx], true, true, true))
			{
				ToBeRemoved.Add(StorePins[idx]);
				StorePins.RemoveAtSwap(idx);
			}
		}
	}
	ON_SCOPE_EXIT
	{
		if (ToBeRemoved.Num() > 0)
		{
			RewireOldPinsToNewPins(ToBeRemoved, Pins, nullptr);
			GetGraph()->NotifyGraphChanged();
			FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
		}
	};

	if (!InClass)
		return false;
	check(!StopClass || InClass->IsChildOf(StopClass));

	auto HideProperties = GetHideProperties(InClass);

	bool bDelegateCreated = false;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema();
	for (UClass* CurrentClass = InClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		if (!StopClass && (CurrentClass == AActor::StaticClass() || CurrentClass == UObject::StaticClass()))
			break;

		UBlueprint* InBlueprint = UBlueprint::GetBlueprintFromClass(CurrentClass);
		FString CurrentClassName = CurrentClass->GetName();
		FString DisplayClassName = InBlueprint ? InBlueprint->GetName() : CurrentClassName;
		DisplayClassName.RemoveFromEnd(TEXT("_C"));

		for (TFieldIterator<UFunction> It(CurrentClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			UFunction* EventFunc = *It;
			if (!HasSpecialExportTag(*It) || HideProperties.Contains(It->GetName()))
				continue;

			auto Function = EventFunc;
			const FString& FriendlyEventName = GetDisplayString(EventFunc);
			const FString& EventName = EventFunc->GetName();

			bool bAdvanceView = !CurrentClass->IsNative();
			FProperty* Prop = nullptr;
			UEnum* Enum = nullptr;

			FString FuncPinName = FString::Printf(TEXT("%c%s%s%s"), Affixes.EventPrefix, *CurrentClassName, *DelegateDelimiter, *EventName);
			bool bCreateThenPin = true;
			const FString& EnumParamName = EventFunc->GetMetaData(FBlueprintMetadata::MD_ExpandEnumAsExecs);
			if (IsExpandEnumAsExec(EventFunc, &Enum, &Prop))
			{
				int32 NumExecs = (Enum->NumEnums() - 1);
				int32 NumExecsDisplayed = 0;
				for (int32 EnumIndex = 0; EnumIndex < NumExecs; EnumIndex++)
				{
					bool const bShouldBeHidden = Enum->HasMetaData(TEXT("Hidden"), EnumIndex) || Enum->HasMetaData(TEXT("Spacer"), EnumIndex);
					if (!bShouldBeHidden)
					{
						bAdvanceView = true;
						// Can't use Enum->GetNameByIndex here because it doesn't do namespace mangling
						const FString& ExecName = Enum->GetNameStringByIndex(EnumIndex);
						FString PinNameStr = FuncPinName + MemberDelimiter + ExecName;
						auto PinFriendlyName = CurrentClass->IsNative() ? FText::FromString(FriendlyEventName + DelimiterStr + Enum->GetDisplayNameTextByIndex(EnumIndex).ToString())
																		: GetCustomDisplayName(DisplayClassName, EventFunc, DelimiterStr + Enum->GetDisplayNameTextByIndex(EnumIndex).ToString());

						UEdGraphPin* Pin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *PinFriendlyName.ToString());
						SetPinMetaDataStr(Pin, PinNameStr);
						BindedPinGuids.Add(GetPinGuid(Pin));
						SetPinMeta(Pin, FEdPinExtraMetaBase::IsBindedPin);

						Pin->bAdvancedView = bAdvanceView;
						K2Schema->ConstructBasicPinTooltip(*Pin, EventFunc->GetToolTipText(), Pin->PinToolTip);
						Pin->PinToolTip = FString::Printf(TEXT("%s\ncase %s:"), *Pin->PinToolTip, *Enum->GetDisplayNameTextByIndex(EnumIndex).ToString());

						++NumExecsDisplayed;
					}
				}

				if (NumExecsDisplayed > 0)
					bCreateThenPin = false;
			}

			if (bCreateThenPin)
			{
				auto PinFriendlyName = CurrentClass->IsNative() ? FText::FromString(GetPinFriendlyName(FriendlyEventName)) : GetCustomDisplayName(DisplayClassName, EventFunc);
				UEdGraphPin* Pin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *PinFriendlyName.ToString());
				SetPinMetaDataStr(Pin, FuncPinName);
				BindedPinGuids.Add(GetPinGuid(Pin));
				SetPinMeta(Pin, FEdPinExtraMetaBase::IsBindedPin);

				Pin->bAdvancedView = bAdvanceView;
				K2Schema->ConstructBasicPinTooltip(*Pin, EventFunc->GetToolTipText(), Pin->PinToolTip);
			}
			bDelegateCreated = true;
			CreateOutPinsForDelegate(FuncPinName, EventFunc, bAdvanceView);
		}
		if (CurrentClass == StopClass)
			break;
	}

	return bDelegateCreated;
}

bool UK2Neuron::CreateDelegatesForClass(UClass* InClass, const UK2Neuron::FPinNameAffixes& Affixes, UClass* StopClass, TArray<UEdGraphPin*>* OldPins)
{
	TArray<UEdGraphPin*> ToBeRemoved;
	if (OldPins)
	{
		TArray<UEdGraphPin*>& StorePins = *OldPins;
		for (auto idx = StorePins.Num() - 1; idx >= 0; --idx)
		{
			if (StorePins[idx] && StorePins[idx]->Direction == EGPD_Output && MatchAffixes(StorePins[idx], true, true, true))
			{
				ToBeRemoved.Add(StorePins[idx]);
				StorePins.RemoveAtSwap(idx);
			}
		}
	}
	ON_SCOPE_EXIT
	{
		if (ToBeRemoved.Num() > 0)
		{
			RewireOldPinsToNewPins(ToBeRemoved, Pins, nullptr);
			RemoveUselessPinMetas();
			GetGraph()->NotifyGraphChanged();
			FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
		}
	};

	if (!InClass)
		return false;
	check(!StopClass || InClass->IsChildOf(StopClass));

	auto HideProperties = GetHideProperties(InClass);

	bool bDelegateCreated = false;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema();
	for (UClass* CurrentClass = InClass; CurrentClass; CurrentClass = CurrentClass->GetSuperClass())
	{
		if (!StopClass && (CurrentClass == AActor::StaticClass() || CurrentClass == UObject::StaticClass()))
			break;

		UBlueprint* InBlueprint = UBlueprint::GetBlueprintFromClass(CurrentClass);
		FString CurrentClassName = CurrentClass->GetName();
		FString DisplayClassName = InBlueprint ? InBlueprint->GetName() : CurrentClassName;
		DisplayClassName.RemoveFromEnd(TEXT("_C"));

		for (TFieldIterator<FMulticastDelegateProperty> It(CurrentClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			FMulticastDelegateProperty* MCDProp = *It;
			if (!HasSpecialExportTag(*It) || HideProperties.Contains(It->GetName()))
				continue;

			auto Function = MCDProp->SignatureFunction;
			const FString& FriendlyDelegateName = GetDisplayString(MCDProp);
			const FString& DelegateName = MCDProp->GetName();

			bool bCreateThenPin = true;
			const FString& EnumParamName = MCDProp->GetMetaData(FBlueprintMetadata::MD_ExpandEnumAsExecs);

			bool bAdvanceView = !CurrentClass->IsNative();
			FProperty* Prop = nullptr;
			UEnum* Enum = nullptr;

			FString FuncPinName = FString::Printf(TEXT("%c%s%s%s"), Affixes.EventPrefix, *CurrentClassName, *DelegateDelimiter, *DelegateName);
			if (IsExpandEnumAsExec(MCDProp, &Enum, &Prop))
			{
				int32 NumExecs = (Enum->NumEnums() - 1);
				int32 NumExecsDisplayed = 0;
				for (int32 EnumIndex = 0; EnumIndex < NumExecs; EnumIndex++)
				{
					bool const bShouldBeHidden = Enum->HasMetaData(TEXT("Hidden"), EnumIndex) || Enum->HasMetaData(TEXT("Spacer"), EnumIndex);
					if (!bShouldBeHidden)
					{
						bAdvanceView = true;
						// Can't use Enum->GetNameByIndex here because it doesn't do namespace mangling
						const FString& ExecName = Enum->GetNameStringByIndex(EnumIndex);
						auto PinFriendlyName = CurrentClass->IsNative() ? FText::FromString(FriendlyDelegateName + DelimiterStr + Enum->GetDisplayNameTextByIndex(EnumIndex).ToString())
																		: GetCustomDisplayName(DisplayClassName, MCDProp, DelimiterStr + Enum->GetDisplayNameTextByIndex(EnumIndex).ToString());
						UEdGraphPin* Pin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *PinFriendlyName.ToString());
						SetPinMetaDataStr(Pin, FuncPinName + MemberDelimiter + ExecName);
						BindedPinGuids.Add(GetPinGuid(Pin));
						SetPinMeta(Pin, FEdPinExtraMetaBase::IsBindedPin);

						Pin->bAdvancedView = bAdvanceView;
						K2Schema->ConstructBasicPinTooltip(*Pin, MCDProp->GetToolTipText(), Pin->PinToolTip);
						Pin->PinToolTip = FString::Printf(TEXT("%s\ncase %s:"), *Pin->PinToolTip, *Enum->GetDisplayNameTextByIndex(EnumIndex).ToString());

						++NumExecsDisplayed;
					}
				}

				if (NumExecsDisplayed > 0)
					bCreateThenPin = false;
			}

			if (bCreateThenPin)
			{
				auto PinFriendlyName = CurrentClass->IsNative() ? FText::FromString(GetPinFriendlyName(FriendlyDelegateName)) : GetCustomDisplayName(DisplayClassName, MCDProp);
				UEdGraphPin* Pin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, *PinFriendlyName.ToString());
				SetPinMetaDataStr(Pin, FuncPinName);
				BindedPinGuids.Add(GetPinGuid(Pin));
				SetPinMeta(Pin, FEdPinExtraMetaBase::IsBindedPin);

				Pin->bAdvancedView = bAdvanceView;
				K2Schema->ConstructBasicPinTooltip(*Pin, MCDProp->GetToolTipText(), Pin->PinToolTip);
			}
			bDelegateCreated = true;
			CreateOutPinsForDelegate(FuncPinName, MCDProp->SignatureFunction, bAdvanceView);
		}
		if (CurrentClass == StopClass)
			break;
	}

	return bDelegateCreated;
}

bool UK2Neuron::ValidDataPin(const UEdGraphPin* Pin, EEdGraphPinDirection Direction)
{
	const bool bValidDataPin = Pin && !Pin->bOrphanedPin && (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec);

	const bool bProperDirection = Pin && (Pin->Direction == Direction);

	return bValidDataPin && bProperDirection;
}

bool UK2Neuron::CopyEventSignature(UK2Node_CustomEvent* CENode, UFunction* Function, const UEdGraphSchema_K2* Schema)
{
	check(CENode && Function && Schema);

	bool bResult = true;
	for (TFieldIterator<FProperty> PropIt(Function); PropIt && (PropIt->PropertyFlags & CPF_Parm); ++PropIt)
	{
		const FProperty* Param = *PropIt;
		if (!Param->HasAnyPropertyFlags(CPF_OutParm) || Param->HasAnyPropertyFlags(CPF_ReferenceParm))
		{
			FEdGraphPinType PinType;
			bResult &= Schema->ConvertPropertyToPinType(Param, PinType);
			bResult &= (nullptr != CENode->CreateUserDefinedPin(Param->GetFName(), PinType, EGPD_Output));
		}
	}
	return bResult;
}

bool UK2Neuron::CreateDelegateForNewFunction(UEdGraphPin* DelegateInputPin, FName FunctionName, UK2Node* CurrentNode, UEdGraph* SourceGraph, FKismetCompilerContext& CompilerContext)
{
	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();
	check(DelegateInputPin && Schema && CurrentNode && SourceGraph && (FunctionName != NAME_None));
	bool bResult = true;

	// WORKAROUND, so we can create delegate from nonexistent function by avoiding check at expanding step
	// instead simply: Schema->TryCreateConnection(AddDelegateNode->GetDelegatePin(), CurrentCENode->FindPinChecked(UK2Node_CustomEvent::DelegateOutputName));
	UEdGraphPin* SelfPin = [&] {
		if (!IsRunningCommandlet() || !CurrentNode->IsA<UK2Neuron>())
		{
			auto SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(CurrentNode, SourceGraph);
			SelfNode->AllocateDefaultPins();
			return SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self);
		}
		return Cast<UK2Neuron>(CurrentNode)->GetCtxPin();
	}();

	UK2Node_CreateDelegate* CreateDelegateNode = CompilerContext.SpawnIntermediateNode<UK2Node_CreateDelegate>(CurrentNode, SourceGraph);
	CreateDelegateNode->AllocateDefaultPins();
	bResult &= Schema->TryCreateConnection(DelegateInputPin, CreateDelegateNode->GetDelegateOutPin());
	bResult &= Schema->TryCreateConnection(SelfPin, CreateDelegateNode->GetObjectInPin());
	CreateDelegateNode->SetFunction(FunctionName);

	return bResult;
}

bool UK2Neuron::CreateOutPinsForDelegate(const FString& InPrefix, UFunction* Function, bool bAdvanceView, UEdGraphPin** OutParamPin)
{
	const UEdGraphSchema_K2* K2Schema = GetK2Schema();
	bool bAllPinsGood = true;
	for (TFieldIterator<FProperty> PropIt(Function); PropIt; ++PropIt)
	{
		if (!(PropIt->PropertyFlags & CPF_Parm))
			continue;

		FProperty* Param = *PropIt;
		FEdGraphPinType TestType;
		if (ensure(IsInputParameter(Param, true)) && K2Schema->ConvertPropertyToPinType(Param, TestType))
		{
			auto PinFriendlyName = GetDisplayText(Param, *DelimiterStr);
			UEdGraphPin* Pin = CreatePin(EGPD_Output, TestType, *PinFriendlyName.ToString());
			SetPinMetaDataStr(Pin, FString::Printf(TEXT("%s%s%s"), *InPrefix, *MemberDelimiter, *Param->GetName()));
			BindedPinGuids.Add(GetPinGuid(Pin));
			SetPinMeta(Pin, FEdPinExtraMetaBase::IsBindedPin);

			Pin->bAdvancedView = bAdvanceView;
			if (OutParamPin && !(*OutParamPin))
				*OutParamPin = Pin;
		}
		else
		{
			bAllPinsGood = false;
		}
	}
	return ensure(bAllPinsGood);
}

TArray<FGuid> UK2Neuron::CreateImportPinsForClass(UClass* InClass, const UK2Neuron::FPinNameAffixes& Affixes, bool bImportFlag, TArray<UEdGraphPin*>* OldPins)
{
	ObjClassPinGuid = FGuid();
	ImportedPinGuids.Reset();
	BindedPinGuids.Reset();

	TArray<FGuid> SpawnParamPins;
	if (!InClass)
		return SpawnParamPins;

	const UEdGraphSchema_K2* K2Schema = GetK2Schema();
	auto HideProperties = GetHideProperties(InClass);

	static const FGuid PlaceHolderGuid = FGuid();

	FProperty* FuncSpawnedClassProp = nullptr;
	auto ImportFunc = GetAlternativeAction(InClass);
	const FString FuncPrefix = ImportFunc ? FString::Printf(TEXT("%c%s%s%s%s"), Affixes.PropPrefix, *GetPropertyOwnerClass(ImportFunc)->GetName(), *FunctionDelimiter, *ImportFunc->GetName(), *MemberDelimiter) : TEXT("");
	if (bImportFlag && ImportFunc)
	{
		HideProperties.Add(ImportFunc->GetName());
		const auto& WorldCtxName = ImportFunc->GetMetaData(FBlueprintMetadata::MD_WorldContext);
		for (TFieldIterator<FProperty> PropertyIt(ImportFunc); PropertyIt && (PropertyIt->PropertyFlags & CPF_Parm); ++PropertyIt)
		{
			if (WorldCtxName == PropertyIt->GetName())
				continue;
			if (!FuncSpawnedClassProp && PropertyIt->GetFName() == ObjectClassPropName)
			{
				FuncSpawnedClassProp = *PropertyIt;
				ImportedPinGuids.Add(PlaceHolderGuid);
				continue;
			}

			if (UEdGraphPin* Pin = CreatePinFromInnerFuncProp(ImportFunc, *PropertyIt, FuncPrefix, TEXT("'")))
			{
				ImportedPinGuids.Add(GetPinGuid(Pin));
				SetPinMeta(Pin, FEdPinExtraMetaBase::IsImportedPin);

				Pin->PinToolTip.Append(FString::Printf(TEXT("\nParameter of %s"), *ImportFunc->GetName()));
			}
		}
	}

	FProperty* MemberSpawnedClassProp = nullptr;
	const bool bInstancedFlag = GetInstancedFlag(InClass);
	const FString MemberPrefix = FString::Printf(TEXT("%c%s%s"), Affixes.PropPrefix, *InClass->GetName(), *MemberDelimiter);
	for (TFieldIterator<FProperty> PropertyIt(InClass, EFieldIteratorFlags::IncludeSuper); PropertyIt; ++PropertyIt)
	{
		if (!HasSpecialImportTag(*PropertyIt) || PropertyIt->HasAnyPropertyFlags(CPF_DisableEditOnInstance) || (!bInstancedFlag && !PropertyIt->HasAnyFlags(RF_Transient) && !PropertyIt->HasAnyPropertyFlags(CPF_Transient)))
			continue;

		const bool bIsDelegate = PropertyIt->IsA<FMulticastDelegateProperty>();
		if (!bIsDelegate && !HideProperties.Contains(PropertyIt->GetName()))
		{
			if (!MemberSpawnedClassProp && PropertyIt->GetFName() == ObjectClassPropName)
			{
				MemberSpawnedClassProp = *PropertyIt;
				SpawnParamPins.Add(PlaceHolderGuid);
				continue;
			}

			if (UEdGraphPin* Pin = CreatePinFromInnerClsProp(InClass, *PropertyIt, MemberPrefix))
			{
				SpawnParamPins.Add(GetPinGuid(Pin));
				SetPinMeta(Pin, FEdPinExtraMetaBase::IsSpawnedPin);
				Pin->PinToolTip.Append(FString::Printf(TEXT("\nMember of %s"), *GetNameSafe(InClass)));
			}
		}
	}

	// as the last pin
	if (bImportFlag)
	{
		UEdGraphPin* ObjectClassPin = nullptr;
		if (auto ClassProp = DetectObjectSpawning(InClass, nullptr))
		{
			auto ClassPropFunc = GetPropertyOwnerUObject<UFunction>(ClassProp);
			const FString ClassPinPrefix = FString::Printf(TEXT("%c%s%s%s%s"), Affixes.ParamPrefix, *GetPropertyOwnerClass(ClassPropFunc)->GetName(), *FunctionDelimiter, *ClassPropFunc->GetName(), *MemberDelimiter);
			ObjectClassPin = CreatePinFromInnerFuncProp(ClassPropFunc, ClassProp, ClassPinPrefix, TEXT("'"));
			if (ensureAlways(ObjectClassPin))
			{
				ObjClassPinGuid == GetPinGuid(ObjectClassPin);
				ObjectClassPin->bAdvancedView = false;
				ObjectClassPin->PinFriendlyName = GetDisplayText(ClassProp, TEXT("'"));
				ObjectClassPin->PinToolTip.Append(FString::Printf(TEXT("\nIn : %s"), *GetPropertyOwnerName(ClassProp)));
			}
		}
		else
		{
			ensure(!(FuncSpawnedClassProp && MemberSpawnedClassProp));
			ClassProp = FuncSpawnedClassProp ? FuncSpawnedClassProp : MemberSpawnedClassProp;
			if (ClassProp)
			{
				bool bIsFuncProp = !!FuncSpawnedClassProp;
				ObjectClassPin = bIsFuncProp ? CreatePinFromInnerFuncProp(ImportFunc, ClassProp, FuncPrefix, TEXT("'")) : CreatePinFromInnerClsProp(InClass, ClassProp, MemberPrefix, TEXT("."));
				if (ensureAlways(ObjectClassPin))
				{
					ObjClassPinGuid == GetPinGuid(ObjectClassPin);
					ObjectClassPin->bAdvancedView = false;
					ObjectClassPin->PinFriendlyName = GetDisplayText(ClassProp, bIsFuncProp ? TEXT("'") : TEXT("."));
					ObjectClassPin->PinToolTip.Append(FString::Printf(TEXT("\nIn : %s"), bIsFuncProp ? *GetPropertyOwnerName(ClassProp) : *GetNameSafe(InClass)));
				}
			}
		}

		if (ObjectClassPin)
		{
			if (auto PlaceName = ImportedPinGuids.FindByKey(PlaceHolderGuid))
			{
				*PlaceName = GetPinGuid(ObjectClassPin);
				SetPinMeta(ObjectClassPin, FEdPinExtraMetaBase::IsImportedPin);
			}

			if (auto PlaceGuid = SpawnParamPins.FindByKey(PlaceHolderGuid))
			{
				*PlaceGuid = GetPinGuid(ObjectClassPin);
				SetPinMeta(ObjectClassPin, &Affixes == &AffixesSelf ? FEdPinExtraMetaBase::IsSelfSpawnedPin : FEdPinExtraMetaBase::IsSpawnedPin);
			}
		}
	}

	for (TFieldIterator<UFunction> FuncIt(InClass, EFieldIteratorFlags::ExcludeSuper); FuncIt; ++FuncIt)
	{
		if (HideProperties.Contains(FuncIt->GetName()) || FuncIt->HasAnyFunctionFlags(FUNC_Const | FUNC_BlueprintPure) || !HasSpecialImportTag(*FuncIt))
			continue;

		bool bValidate = true;
		for (TFieldIterator<FProperty> PropIt(*FuncIt); PropIt; ++PropIt)
		{
			if (!(PropIt->PropertyFlags & CPF_Parm) || !IsInputParameter(*PropIt, true))
			{
				bValidate = false;
				break;
			}
		}
		if (!bValidate)
			continue;

		FString FuncPinName = FString::Printf(TEXT("%c%s%s%s"), Affixes.ParamPrefix, *GetPropertyOwnerClass(*FuncIt)->GetName(), *FunctionDelimiter, *FuncIt->GetName());
		auto PinFriendlyName = FText::FromString(GetPinFriendlyName(FString::Printf(TEXT("->%s"), *FuncIt->GetName())));
		UEdGraphPin* FuncPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, *PinFriendlyName.ToString());
		SetPinMetaDataStr(FuncPin, FuncPinName);

		FuncPin->bAdvancedView = true;

		for (TFieldIterator<FProperty> PropIt(*FuncIt); PropIt; ++PropIt)
		{
			FProperty* Param = *PropIt;
			FEdGraphPinType TestType;
			if (ensure(K2Schema->ConvertPropertyToPinType(Param, TestType)))
			{
				auto ParamPinFriendlyName = GetDisplayText(Param, *DelimiterStr);
				UEdGraphPin* Pin = CreatePin(EGPD_Input, TestType, *ParamPinFriendlyName.ToString());
				SetPinMetaDataStr(Pin, FString::Printf(TEXT("%s%s%s"), *FuncPinName, *MemberDelimiter, *PropIt->GetName()));

				Pin->bAdvancedView = true;

				if (!IsAllocWithOldPins() && Param->HasMetaData(MetaSplitStructPin))
					GetK2Schema()->SplitPin(Pin, false);
			}
		}
	}
	return SpawnParamPins;
}

bool UK2Neuron::ConnectImportPinsForClass(UClass* InClass, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& LastThenPin, UEdGraphPin* InstancePin, bool bOverrideDefault, bool bOverrideRemote)
{
	bool bIsErrorFree = true;
	if (!ensure(InClass))
		return bIsErrorFree;

	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	UEdGraphPin* EventLastThenPin = nullptr;
	UK2Node_CustomEvent* RemoteEventNode = nullptr;
	TArray<UEdGraphPin*> InputPins;

	static const FName ObjectParamName(TEXT("Object"));
	static const FName ValueParamName(TEXT("Value"));
	static const FName PropertyNameParamName(TEXT("PropertyName"));
	for (const FGuid& PinGuid : SpawnedPinGuids)
	{
		UEdGraphPin* SpawnVarPin = FindPin(PinGuid);
		if (!SpawnVarPin || SpawnVarPin->bOrphanedPin)
		{
			continue;
		}
		auto PinMeta = GetPinMetaInfo(SpawnVarPin);

		FString PropertyName = PinMeta.MemberName;
		const bool bHasDefaultValue = !SpawnVarPin->DefaultValue.IsEmpty() || !SpawnVarPin->DefaultTextValue.IsEmpty() || SpawnVarPin->DefaultObject;
		if (HasAnyConnections(SpawnVarPin) || bHasDefaultValue)
		{
			FProperty* Property = nullptr;
			if (!HasAnyConnections(SpawnVarPin))
			{
				if (SpawnVarPin->PinType.bIsReference /*|| SpawnVarPin->bDefaultValueIsIgnored*/)
				{
					continue;
				}

				Property = FindFProperty<FProperty>(InClass, *PropertyName);
				if (!ensure(Property))
				{
					continue;
				}
				bool bSkipIfSame = !bOverrideDefault && !NeuronCheckableGuids.Contains(PinGuid);
				if (bSkipIfSame)
				{
					FString DefaultValueAsString;
					FBlueprintCompilationManager::GetDefaultValue(InClass, Property, DefaultValueAsString);
					if (DefaultValueAsString == SpawnVarPin->DefaultValue)
					{
						continue;
					}
				}
			}

			auto CreateSetVariableNode = [&](UEdGraphPin* InputVarPin) mutable -> UK2Node_CallFunction* {
				// 				if (!Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly))
				// 				{
				// 					UK2Node_VariableSet* SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_VariableSet>(this, SourceGraph);
				// 					SetVarNode->VariableReference.SetExternalMember(Property->GetFName(), TSubclassOf<UObject>(InClass));
				// 					SetVarNode->AllocateDefaultPins();
				// 					TryCreateConnection(CompilerContext, SourceGraph, InputVarPin, SetVarNode->FindPinChecked(PropertyName), false);
				// 					TryCreateConnection(CompilerContext, SourceGraph, InstancePin, SetVarNode->FindPinChecked(UEdGraphSchema_K2::PN_Self), false);
				// 					return SetVarNode;
				// 				}

				UFunction* SetByNameFunction = K2Schema->FindSetVariableByNameFunction(InputVarPin->PinType);
				if (!ensure(SetByNameFunction))
					return nullptr;

				UK2Node_CallFunction* SetVarNode = nullptr;
				if (InputVarPin->PinType.IsArray())
				{
					SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallArrayFunction>(this, SourceGraph);
				}
				else
				{
					SetVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				}
				SetVarNode->SetFromFunction(SetByNameFunction);
				SetVarNode->AllocateDefaultPins();

				// Connect the new actor to the 'object' pin
				UEdGraphPin* ObjectPin = SetVarNode->FindPinChecked(ObjectParamName);
				InstancePin->MakeLinkTo(ObjectPin);

				// Fill in literal for 'property name' pin - name of pin is property name
				UEdGraphPin* PropertyNamePin = SetVarNode->FindPinChecked(PropertyNameParamName);
				PropertyNamePin->DefaultValue = PropertyName;

				UEdGraphPin* ValuePin = SetVarNode->FindPinChecked(ValueParamName);
				auto NodeVarType = ValuePin->PinType;
				const bool bDiffClassType = (NodeVarType.PinCategory == UEdGraphSchema_K2::PC_Class && NodeVarType.PinSubCategoryObject != InputVarPin->PinType.PinSubCategoryObject);
				const bool bDiffObjectType = (NodeVarType.PinCategory == UEdGraphSchema_K2::PC_Object && NodeVarType.PinSubCategoryObject != InputVarPin->PinType.PinSubCategoryObject);
#if UE_5_00_OR_LATER
				const bool bDiffRealType = (NodeVarType.PinCategory == UEdGraphSchema_K2::PC_Real && NodeVarType.PinSubCategory != InputVarPin->PinType.PinSubCategory);
#else
				const bool bDiffRealType = false;
#endif
				if (!bDiffClassType && !bDiffObjectType && !bDiffRealType)
					ValuePin->PinType = InputVarPin->PinType;
				if (InputVarPin->LinkedTo.Num() == 0 && InputVarPin->DefaultValue != FString() && InputVarPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte && InputVarPin->PinType.PinSubCategoryObject.IsValid()
					&& InputVarPin->PinType.PinSubCategoryObject->IsA<UEnum>())
				{
					// Pin is an enum, we need to alias the enum value to an int:
					UK2Node_EnumLiteral* EnumLiteralNode = CompilerContext.SpawnIntermediateNode<UK2Node_EnumLiteral>(this, SourceGraph);
					EnumLiteralNode->Enum = CastChecked<UEnum>(InputVarPin->PinType.PinSubCategoryObject.Get());
					EnumLiteralNode->AllocateDefaultPins();
					EnumLiteralNode->FindPinChecked(UEdGraphSchema_K2::PN_ReturnValue)->MakeLinkTo(ValuePin);

					UEdGraphPin* InPin = EnumLiteralNode->FindPinChecked(UK2Node_EnumLiteral::GetEnumInputPinName());
					InPin->DefaultValue = InputVarPin->DefaultValue;
				}
				else
				{
					// For non-array struct pins that are not linked, transfer the pin type so that the node will expand an auto-ref that will assign the value by-ref.
					if (!InputVarPin->PinType.IsContainer() && InputVarPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Struct && InputVarPin->LinkedTo.Num() == 0)
					{
						if (InputVarPin->PinType.PinSubCategoryObject == TBaseStructure<FSoftClassPath>::Get())
							TryCreateConnection(CompilerContext, SourceGraph, MakeLiteralVariable(CompilerContext, SourceGraph, InputVarPin, InputVarPin->GetDefaultAsString()), ValuePin);
						else if (InputVarPin->PinType.PinSubCategoryObject.Get() && InputVarPin->PinType.PinSubCategoryObject->GetFName() == TEXT("Key"))
							TryCreateConnection(CompilerContext, SourceGraph, SpawnPureVariable(CompilerContext, SourceGraph, InputVarPin, InputVarPin->GetDefaultAsString()), ValuePin);
						else if (InputVarPin->LinkedTo.Num() == 0)
							TryCreateConnection(CompilerContext, SourceGraph, SpawnPureVariable(CompilerContext, SourceGraph, InputVarPin, InputVarPin->GetDefaultAsString()), ValuePin);
						else
							TryCreateConnection(CompilerContext, SourceGraph, InputVarPin, ValuePin);
					}
					else
					{
						if (bDiffObjectType)
						{
							TryCreateConnection(CompilerContext, SourceGraph, PureCastTo(CompilerContext, SourceGraph, InputVarPin, ValuePin), ValuePin, false);
						}
						else if (bDiffClassType)
						{
							TryCreateConnection(CompilerContext, SourceGraph, PureCastClassTo(CompilerContext, SourceGraph, InputVarPin, ValuePin), ValuePin, false);
						}
						else
						{
							TryCreateConnection(CompilerContext, SourceGraph, InputVarPin, ValuePin, false);
						}
					}
					SetVarNode->PinConnectionListChanged(ValuePin);
				}
				return SetVarNode;
			};

			UK2Node_CallFunction* SetVariableNode = CreateSetVariableNode(SpawnVarPin);
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastThenPin, SetVariableNode->GetExecPin());
			LastThenPin = SetVariableNode->GetThenPin();
			InputPins.Add(SpawnVarPin);

			if (bOverrideRemote)
			{
				if (!RemoteEventNode)
					RemoteEventNode = GetMetaEventForClass(InClass, CompilerContext, SourceGraph, true);
				if (!EventLastThenPin)
					EventLastThenPin = FindThenPin(RemoteEventNode);

				auto Pin = RemoteEventNode->CreateUserDefinedPin(*PropertyName, SpawnVarPin->PinType, EGPD_Output, false);
				UK2Node_CallFunction* SetRemoteVariableNode = CreateSetVariableNode(Pin);
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventLastThenPin, SetRemoteVariableNode->GetExecPin());
				EventLastThenPin = SetRemoteVariableNode->GetThenPin();
			}
		}
	}

	auto ImportFunc = GetAlternativeAction(InClass);
	if (ImportFunc)
	{
		UK2Node_CallFunction* ImportFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		ImportFuncNode->SetFromFunction(ImportFunc);
		ImportFuncNode->AllocateDefaultPins();

		for (auto& PinGuid : ImportedPinGuids)
		{
			auto ImportPin = FindPin(PinGuid);
			if (!ensure(ImportPin))
			{
				FString ParamStr;
				bIsErrorFree &= PinGuid.ToString().Split(FunctionDelimiter, nullptr, &ParamStr);

				if (auto ParamPin = ImportFuncNode->FindPin(*ParamStr))
				{
				}
				continue;
			}
		}
	}
	return ensure(bIsErrorFree);
}

void UK2Neuron::RemoveUselessPinMetas()
{
	TSet<FGuid> PinGuids;
	for (const auto Pin : Pins)
	{
		if (Pin)
		{
			if (Pin->IsPendingKill())
			{
				PinExtraMetas.Remove(GetPinGuid(Pin));
			}
			else
			{
				PinGuids.Add(GetPinGuid(Pin));
			}
		}
	}

	for (auto It = PinExtraMetas.CreateIterator(); It; ++It)
	{
		if (!PinGuids.Contains(It.Key()))
		{
			It.RemoveCurrent();
		}
	}
}

UK2Neuron::FPinMetaInfo UK2Neuron::GetPinMetaInfo(FString PinNameString, bool bRedirect, bool bEnsure) const
{
	FPinMetaInfo PinNameMeta;

	// ClassName MemberDelimiter + MemberName
	// ClassName + DelegateDelimiter + DelegateName [+ MemberDelimiter + MemberName]
	// ClassName + FunctionDelimiter + FunctionName [+ MemberDelimiter + MemberName]
	// ClassName + FunctionDelimiter + FunctionName + DelegateDelimiter + DelegateName [+ MemberDelimiter + MemberName]

	FString& ClassName = PinNameMeta.ClassName;
	FString& MemberName = PinNameMeta.MemberName;
	FString& DelegateName = PinNameMeta.DelegateName;
	FString& FunctionName = PinNameMeta.FunctionName;
	FString& EnumAsExec = PinNameMeta.EnumExecName;

	PinNameString.Split(MemberDelimiter, &ClassName, &MemberName);
	if (!ClassName.IsEmpty())
		PinNameString = ClassName;

	PinNameString.Split(DelegateDelimiter, &ClassName, &DelegateName);
	if (!ClassName.IsEmpty())
		PinNameString = ClassName;
	PinNameString.Split(FunctionDelimiter, &ClassName, &FunctionName);

	PinNameMeta.OwnerClass = FindObject<UClass>(ANY_PACKAGE_COMPATIABLE, *ClassName, false);
	ensure(!bEnsure || PinNameMeta.OwnerClass);
	if (PinNameMeta.OwnerClass)
	{
		UStruct* PropOwnerClassOrFunc = PinNameMeta.OwnerClass;
		PinNameMeta.SubFuncion = FindUField<UFunction>(PinNameMeta.OwnerClass, *FunctionName);
		if (!PinNameMeta.SubFuncion && bRedirect && FunctionName.EndsWith(MetaTagPostfix))
			PinNameMeta.SubFuncion = FindUField<UFunction>(PinNameMeta.OwnerClass, *FunctionName.Left(MetaTagPostfix.Len()));

		if (PinNameMeta.SubFuncion)
		{
			PropOwnerClassOrFunc = PinNameMeta.SubFuncion;
			PinNameMeta.FuncDelegate = FindFProperty<FDelegateProperty>(PinNameMeta.SubFuncion, *DelegateName);
			if (!PinNameMeta.FuncDelegate && bRedirect && DelegateName.EndsWith(MetaTagPostfix))
				PinNameMeta.FuncDelegate = FindFProperty<FDelegateProperty>(PinNameMeta.SubFuncion, *DelegateName.Left(MetaTagPostfix.Len()));
		}
		else
		{
			PinNameMeta.SubDelegate = FindFProperty<FMulticastDelegateProperty>(PinNameMeta.OwnerClass, *DelegateName);
			if (!PinNameMeta.SubDelegate && bRedirect && DelegateName.EndsWith(MetaTagPostfix))
				PinNameMeta.SubDelegate = FindFProperty<FMulticastDelegateProperty>(PinNameMeta.OwnerClass, *DelegateName.Left(MetaTagPostfix.Len()));
		}

		do
		{
			if (MemberName.IsEmpty())
				break;

			UEnum* Enum = nullptr;
			FProperty* ParamProp = nullptr;
			if (PinNameMeta.SubFuncion)
			{
				PinNameMeta.Prop = PinNameMeta.FuncDelegate;
				if (PinNameMeta.FuncDelegate && IsExpandEnumAsExec(PinNameMeta.FuncDelegate->SignatureFunction, &Enum, &ParamProp))
				{
					ensure(Enum->GetIndexByNameString(MemberName) != INDEX_NONE);
					EnumAsExec = MemberName;
					break;
				}

				if (IsExpandEnumAsExec(PinNameMeta.SubFuncion, &Enum, &ParamProp))
				{
					ensure(Enum->GetIndexByNameString(MemberName) != INDEX_NONE);
					EnumAsExec = MemberName;
					break;
				}
			}

			if (PinNameMeta.SubDelegate && IsExpandEnumAsExec(PinNameMeta.SubDelegate, &Enum, &ParamProp))
			{
				ensure(Enum->GetIndexByNameString(MemberName) != INDEX_NONE);
				EnumAsExec = MemberName;
				break;
			}

			PinNameMeta.Prop = FindFProperty<FProperty>(PropOwnerClassOrFunc, *MemberName);
			if (!PinNameMeta.Prop && bRedirect && MemberName.EndsWith(MetaTagPostfix))
				PinNameMeta.Prop = FindFProperty<FProperty>(PinNameMeta.SubFuncion, *MemberName.Left(MetaTagPostfix.Len()));

		} while (0);
	}
	return PinNameMeta;
}

UK2Neuron::FPinMetaInfo UK2Neuron::GetPinMetaInfo(const UEdGraphPin* InPin, bool bRedirect /*= false*/, bool bEnsure /*= true*/) const
{
	auto Find = PinExtraMetas.Find(GetPinGuid(InPin));
	if (Find)
	{
		return GetPinMetaInfo(Find->PinMetaStringOld.Mid(1), bRedirect, bEnsure);
	}
	else if (bEnsure)
	{
		UE_LOG(LogTemp, Warning, TEXT("PinExtraMetas not found for %s"), *InPin->GetName());
	}
	FPinMetaInfo Ret;
	return Ret;
}

bool UK2Neuron::MatchAffixes(const UEdGraphPin* InPin, bool bSelf, bool bProxy, bool bObject) const
{
	if (auto Info = FindPinExtraMeta(InPin))
	{
		return (Info->PinNeuronType != EPinNeuronType::None) && (bSelf && (Info->PinNeuronScope == EPinNeuronScope::Self)) || (bProxy && (Info->PinNeuronScope == EPinNeuronScope::Proxy))
			   || (bObject && (Info->PinNeuronScope == EPinNeuronScope::Object));
	}

	auto PinName = InPin->GetName();
	return (bSelf && AffixesSelf.MatchAll(PinName)) || (bProxy && AffixesProxy.MatchAll(PinName)) || (bObject && AffixesObject.MatchAll(PinName));
}

bool UK2Neuron::MatchAffixesEvent(const UEdGraphPin* InPin, bool bSelf, bool bProxy, bool bObject) const
{
	if (auto Info = FindPinExtraMeta(InPin))
	{
		return (Info->PinNeuronType == EPinNeuronType::Event) && MatchAffixes(InPin, bSelf, bProxy, bObject);
	}
	auto PinName = InPin->GetName();
	return (bSelf && AffixesSelf.MatchEvent(PinName)) || (bProxy && AffixesProxy.MatchEvent(PinName)) || (bObject && AffixesObject.MatchEvent(PinName));
}

bool UK2Neuron::MatchAffixesMember(const UEdGraphPin* InPin, bool bSelf, bool bProxy, bool bObject) const
{
	if (auto Info = FindPinExtraMeta(InPin))
	{
		return (Info->PinNeuronType == EPinNeuronType::Member) && MatchAffixes(InPin, bSelf, bProxy, bObject);
	}
	auto PinName = InPin->GetName();
	return (bSelf && AffixesSelf.MatchProp(PinName)) || (bProxy && AffixesProxy.MatchProp(PinName)) || (bObject && AffixesObject.MatchProp(PinName));
}

bool UK2Neuron::MatchAffixesInput(const UEdGraphPin* InPin, bool bSelf, bool bProxy, bool bObject) const
{
	if (auto Info = FindPinExtraMeta(InPin))
	{
		return (Info->PinNeuronType == EPinNeuronType::Param || Info->PinNeuronType == EPinNeuronType::Member) && MatchAffixes(InPin, bSelf, bProxy, bObject);
	}
	auto PinName = InPin->GetName();
	return (bSelf && AffixesSelf.MatchInput(PinName)) || (bProxy && AffixesProxy.MatchInput(PinName)) || (bObject && AffixesObject.MatchInput(PinName));
}

void UK2Neuron::GetRedirectPinNames(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const
{
	Super::GetRedirectPinNames(Pin, RedirectPinNames);
	// GetRedirectPinNamesImpl(Pin, RedirectPinNames);
}

void UK2Neuron::GetRedirectPinNamesImpl(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const
{
	auto PinName = Pin.GetName();
	if (!MatchAffixes(&Pin, true, true, true))
		return;

	auto PinMetaInfo = GetPinMetaInfo(&Pin, true);
	if (!PinMetaInfo.GetObjOrFunc())
		return;

	if (PinMetaInfo.OwnerClass && PinMetaInfo.ClassName != *PinMetaInfo.OwnerClass->GetName())
		PinName.ReplaceInline(*PinMetaInfo.ClassName, *PinMetaInfo.OwnerClass->GetName());
	if (PinMetaInfo.Prop && PinMetaInfo.MemberName != PinMetaInfo.Prop->GetName())
		PinName.ReplaceInline(*PinMetaInfo.MemberName, *PinMetaInfo.Prop->GetName());
	if (PinMetaInfo.SubFuncion && PinMetaInfo.FunctionName != PinMetaInfo.SubFuncion->GetName())
		PinName.ReplaceInline(*PinMetaInfo.FunctionName, *PinMetaInfo.SubFuncion->GetName());
	if (PinMetaInfo.SubDelegate && PinMetaInfo.DelegateName != PinMetaInfo.SubDelegate->GetName())
		PinName.ReplaceInline(*PinMetaInfo.DelegateName, *PinMetaInfo.SubDelegate->GetName());

	if (PinName != Pin.GetName())
		RedirectPinNames.Add(PinName);
}

UK2Node::ERedirectType UK2Neuron::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	ERedirectType RedirectType = Super::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
	if (RedirectType == ERedirectType::ERedirectType_None)
	{
		if (NewPin->Direction == OldPin->Direction && OldPin->PinName != NewPin->PinName && NewPin->PinName.ToString() == OldPin->PinFriendlyName.ToString())
		{
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
			if (K2Schema && OldPin->Direction == EGPD_Input && OldPin->LinkedTo.Num() == 0 && !OldPin->DoesDefaultValueMatchAutogenerated() && !K2Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType))
			{
				RedirectType = ERedirectType_None;
			}
			else
			{
				RedirectType = ERedirectType_Name;
			}
		}
#if 0
		if (OldPin->Direction == NewPin->Direction && !OldPin->PinFriendlyName.IsEmpty() && OldPin->PinName != NewPin->PinName && NewPin->PinFriendlyName.ToString() == OldPin->PinFriendlyName.ToString())
		{
			if (IsObjectClassPin(OldPin, false) && IsObjectClassPin(NewPin, false))
				return ERedirectType_Name;
		}

		if (OldPin->Direction == NewPin->Direction && !OldPin->PinFriendlyName.IsEmpty() && OldPin->PinName != NewPin->PinName && NewPin->PinFriendlyName.ToString() == OldPin->PinFriendlyName.ToString())
		{
			// If the old pin had a default value, only match the new pin if the type is compatible:
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
			if (K2Schema && OldPin->Direction == EGPD_Input && OldPin->LinkedTo.Num() == 0 && !OldPin->DoesDefaultValueMatchAutogenerated() && !K2Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType))
			{
				RedirectType = ERedirectType_None;
			}
			else
			{
				RedirectType = ERedirectType_Name;
			}
		}
#endif
	}
	return RedirectType;
}
void UK2Neuron::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionKey->HasAnyClassFlags(CLASS_Abstract) || !ActionRegistrar.IsOpenForRegistration(GetClass()))
		return;

	static TSet<FWeakObjectPtr> Registered;
	bool bExisted = false;
	Registered.Add(ActionKey, &bExisted);
	if (!bExisted)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.OnFilesLoaded().AddLambda([ActionKey] { FBlueprintActionDatabase::Get().RefreshClassActions(ActionKey); });
		}
	}
}

UK2Node_CustomEvent* UK2Neuron::GetMetaEventForClass(UClass* InClass, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, bool bCreate)
{
	auto StatbleName = FName(*FString::Printf(TEXT("%c%s"), MetaEventPrefix, *GetNameSafe(InClass)));
	auto Node = SourceGraph->Nodes.FindByPredicate([&](UEdGraphNode* Node) { return Node->IsA<UK2Node_CustomEvent>() && static_cast<UK2Node_CustomEvent*>(Node)->CustomFunctionName == StatbleName; });
	UK2Node_CustomEvent* StableEventNode = Node ? CastChecked<UK2Node_CustomEvent>(*Node) : nullptr;
	if (!StableEventNode)
	{
		if (bCreate)
		{
			StableEventNode = CompilerContext.SpawnIntermediateNode<UK2Node_CustomEvent>(this, SourceGraph);
			StableEventNode->CustomFunctionName = StatbleName;
			StableEventNode->AllocateDefaultPins();
		}
	}
	else
	{
		TArray<UEdGraphPin*> ToBeRemoved;
		for (UEdGraphPin* Pin : StableEventNode->Pins)
		{
			if (Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				ToBeRemoved.Add(Pin);
			}
		}
		for (UEdGraphPin* Pin : ToBeRemoved)
		{
			Pin->Modify();
			Pin->MarkAsGarbage();
			StableEventNode->DestroyPin(Pin);
		}
		RemoveUselessPinMetas();
		StableEventNode->UserDefinedPins.Empty();
	}
	return StableEventNode;
}

bool UK2Neuron::BindDelegateEvents(FKismetCompilerContext& CompilerContext,
								   UEdGraph* SourceGraph,
								   UEdGraphPin* InstPin,
								   UEdGraphPin*& LastAddDelegateThenPin,
								   UEdGraphPin*& LastRemoveDelegateThenPin,
								   UK2Neuron::FDelegateEventOptions& Options)
{
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);

	bool bIsErrorFree = true;
	FString LastLinkedPropertyName;
	TSet<FGuid> SkipPinGuids;
	for (int32 i = 0; i < Pins.Num(); i++)
	{
		UEdGraphPin* const CurPin = Pins[i];
		auto MetaInfo = GetPinMetaInfo(CurPin);
		if (!MetaInfo.SubDelegate)
			continue;
		if (CurPin->PinName.ToString()[0] != Options.Affixes.EventPrefix)
			continue;

		FString CurPinName = CurPin->PinName.ToString().Mid(1);
		if (CurPin->Direction == EGPD_Output && CurPin->LinkedTo.Num() && CurPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			if (!CurPinName.StartsWith(LastLinkedPropertyName))
			{
				CompilerContext.MessageLog.Error(TEXT("UK2Neuron : Exec Pin Not Connected. @@"), CurPin);
				BreakAllNodeLinks();
				return false;
			}
		}
		else if (CurPin->Direction == EGPD_Output && CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		{
			bool bAnyLinked = !IsRunningCommandlet() || HasAnyConnections(CurPin) || Options.ContainsSpecialAction(CurPin);
			FString ClassName = CurPinName;
			FString DelegatePart;
			CurPinName.Split(DelegateDelimiter, &ClassName, &DelegatePart);
			UClass* PinSubClass = FindObject<UClass>(ANY_PACKAGE_COMPATIABLE, *ClassName, false);
			if (!ensure(PinSubClass))
				continue;

			FString DelegateName = DelegatePart;
			FString EnumValue;
			DelegatePart.Split(MemberDelimiter, &DelegateName, &EnumValue);
			LastLinkedPropertyName = DelegateName;

			// Found Delegate Property
			FMulticastDelegateProperty* MCDProp = nullptr;
			for (TFieldIterator<FMulticastDelegateProperty> It(PinSubClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
			{
				if (DelegateName == It->GetName())
				{
					MCDProp = *It;
					break;
				}
			}
			if (!ensure(MCDProp))
				continue;

			if (!bAnyLinked)
			{
				FString ParamPrefix = FString::Printf(TEXT("%c%s%s"), Options.Affixes.EventPrefix, *DelegateName, *MemberDelimiter);
				for (auto Pin : Pins)
				{
					if (Pin && Pin->Direction == EGPD_Output && Pin->PinName.ToString().StartsWith(ParamPrefix) && Pin->LinkedTo.Num())
					{
						bAnyLinked = true;
						break;
					}
				}
				if (!bAnyLinked)
					continue;
			}

			UK2Node_AddDelegate* AddDelegateNode = CompilerContext.SpawnIntermediateNode<UK2Node_AddDelegate>(this, SourceGraph);
			StaticAssignProperty(AddDelegateNode, MCDProp, false);
			AddDelegateNode->AllocateDefaultPins();

			if (ensure(LastAddDelegateThenPin))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastAddDelegateThenPin, AddDelegateNode->GetExecPin());
			}
			LastAddDelegateThenPin = FindThenPin(AddDelegateNode, true);

			if (UEdGraphPin* TargetPin = AddDelegateNode->FindPin(UEdGraphSchema_K2::PN_Self))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, TargetPin);
			}

			UK2Node_CustomEvent* EventNode = Options.MakeEventNode(this, InstPin, CompilerContext, SourceGraph, *DelegateName);
			check(EventNode);

			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, AddDelegateNode->GetDelegatePin(), FindDelegatePin(EventNode));
			EventNode->AutowireNewNode(AddDelegateNode->GetDelegatePin());

			UK2Node_RemoveDelegate* RemoveDelegateNode = CompilerContext.SpawnIntermediateNode<UK2Node_RemoveDelegate>(this, SourceGraph);
			StaticAssignProperty(RemoveDelegateNode, MCDProp, false);
			RemoveDelegateNode->AllocateDefaultPins();
			if (ensure(LastRemoveDelegateThenPin))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, LastRemoveDelegateThenPin, RemoveDelegateNode->GetExecPin());
			}
			LastRemoveDelegateThenPin = FindThenPin(RemoveDelegateNode);
			bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, RemoveDelegateNode->GetDelegatePin(), FindDelegatePin(EventNode));

			if (UEdGraphPin* TargetPin = RemoveDelegateNode->FindPin(UEdGraphSchema_K2::PN_Self))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, TargetPin);
			}

			UEdGraphPin* EventThenPin = FindThenPin(EventNode, true);

			TArray<UEdGraphPin*> ParamPins;
			int32 ArrInputIndex = 0;

			for (int32 j = 0; j < EventNode->Pins.Num(); j++)
			{
				auto ParamPin = EventNode->Pins[j];
				if (ParamPin->Direction == EGPD_Output && ParamPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && ParamPin->PinName != UK2Node_Event::DelegateOutputName)
				{
					FName FuncParamName = *FString::Printf(TEXT("%c%s%s%s"), Options.Affixes.EventPrefix, *DelegateName, *MemberDelimiter, *ParamPin->PinName.ToString());
					UEdGraphPin* FuncParamPin = FindPin(FuncParamName);
					if (ensure(FuncParamPin))
					{
						SkipPinGuids.Add(GetPinGuid(FuncParamPin));
						if (!IsRunningCommandlet() || HasAnyConnections(FuncParamPin))
							bIsErrorFree &= AssignTempAndGet(CompilerContext, SourceGraph, EventThenPin, ParamPin, true, FuncParamPin);
						bIsErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FuncParamPin, *ParamPin).CanSafeConnect();
					}
				}
			}

			Options.NotifyIfNeeded(this, InstPin, CompilerContext, SourceGraph, EventNode, EventThenPin, DelegateName);

			FProperty* Prop = nullptr;
			UEnum* Enum = nullptr;
			if (!EnumValue.IsEmpty() && IsExpandEnumAsExec(MCDProp, &Enum, &Prop) && ensure(Enum->GetIndexByName(*EnumValue) != INDEX_NONE) && ensure(EventNode->FindPin(Prop->GetFName())))
			{
				UK2Node_SwitchEnum* SwitchEnumNode = CompilerContext.SpawnIntermediateNode<UK2Node_SwitchEnum>(this, SourceGraph);
				SwitchEnumNode->Enum = Enum;
				SwitchEnumNode->AllocateDefaultPins();
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventThenPin, SwitchEnumNode->GetExecPin());
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventNode->FindPin(Prop->GetFName()), SwitchEnumNode->GetSelectionPin());

				int32 NumExecs = (Enum->NumEnums() - 1);
				int32 NumExecsDisplayed = 0;
				for (int32 ExecIdx = 0; ExecIdx < NumExecs; ExecIdx++)
				{
					if (!Enum->HasMetaData(TEXT("Hidden"), ExecIdx) || Enum->HasMetaData(TEXT("Spacer"), ExecIdx))
					{
						const FString& ExecName = Enum->GetNameStringByIndex(ExecIdx);
						FName ExecOutName = *FString::Printf(TEXT("%c%s%s%s"), Options.Affixes.EventPrefix, *DelegateName, *MemberDelimiter, *ExecName);
						UEdGraphPin* ExecOutPin = FindPin(ExecOutName);
						if (ExecOutPin)
							SkipPinGuids.Add(GetPinGuid(ExecOutPin));

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

			if (CurPin->LinkedTo.Num() == 0 && Options.ContainsSpecialAction(CurPin))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, EventThenPin, Options.GetSpecialActionExec(this, InstPin, CompilerContext, SourceGraph));
			}
		}
	}
	return bIsErrorFree;
}

bool UK2Neuron::ConnectLocalFunctions(TSet<FGuid>& SkipPinGuids, TCHAR InParamPrefix, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin)
{
	bool bIsErrorFree = true;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	for (UEdGraphPin* CurPin : Pins)
	{
		if (CurPin->bOrphanedPin || SkipPinGuids.Contains(GetPinGuid(CurPin)))
			continue;

		if (CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && CurPin->PinName.ToString()[0] == InParamPrefix)
		{
			SkipPinGuids.Add(GetPinGuid(CurPin));
			auto PinMetaInfo = GetPinMetaInfo(CurPin);
			if (!ensure(PinMetaInfo.OwnerClass))
				continue;

			if (ensure(PinMetaInfo.SubFuncion))
			{
				bool bAnyLinked = HasAnyConnections(CurPin);
				FString ParamPrefix = FString::Printf(TEXT("%s%s"), *CurPin->PinName.ToString().LeftChop(PinMetaInfo.EnumExecName.Len()), *MemberDelimiter);
				if (!bAnyLinked)
				{
					for (auto Pin : Pins)
					{
						if (Pin && !SkipPinGuids.Contains(GetPinGuid(Pin)) && Pin->Direction == EGPD_Input && Pin->PinName.ToString().StartsWith(ParamPrefix))
						{
							SkipPinGuids.Add(GetPinGuid(Pin));
							if (HasAnyConnections(Pin))
							{
								bAnyLinked = true;
								break;
							}
						}
					}
				}
				if (!bAnyLinked)
					continue;

				UK2Node_CallFunction* MemFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				MemFuncNode->SetFromFunction(PinMetaInfo.SubFuncion);
				MemFuncNode->AllocateDefaultPins();
				if (UEdGraphPin* SelfPin = K2Schema->FindSelfPin(*MemFuncNode, EGPD_Input))
					bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, SelfPin);
				bIsErrorFree &= ConditionDo(CompilerContext, SourceGraph, InstPin, CurPin, MemFuncNode->GetExecPin());

				for (TFieldIterator<FProperty> PropIt(PinMetaInfo.SubFuncion); PropIt; ++PropIt)
				{
					if (!ensure(IsInputParameter(*PropIt)))
						continue;

					FName ParamName = *(ParamPrefix + PropIt->GetName());
					if (auto ParamPin = FindPin(ParamName, EGPD_Input))
					{
						SkipPinGuids.Add(GetPinGuid(ParamPin));
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParamPin, MemFuncNode->FindPinChecked(PropIt->GetFName()));
					}
				}
			}
		}
	}
	return bIsErrorFree;
}

bool UK2Neuron::ConnectRemoteFunctions(UFunction* InProxyFunc,
									   TSet<FGuid>& SkipPinGuids,
									   TCHAR InParamPrefix,
									   FKismetCompilerContext& CompilerContext,
									   UEdGraph* SourceGraph,
									   UEdGraphPin* InstPin,
									   TFunctionRef<void(UK2Node*, UK2Node_CustomEvent*)> NodeOpreation)
{
	bool bIsErrorFree = !!InProxyFunc;
	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	for (UEdGraphPin* CurPin : Pins)
	{
		if (CurPin->bOrphanedPin || SkipPinGuids.Contains(GetPinGuid(CurPin)))
			continue;

		if (CurPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && CurPin->PinName.ToString()[0] == InParamPrefix)
		{
			SkipPinGuids.Add(GetPinGuid(CurPin));
			auto PinMetaInfo = GetPinMetaInfo(CurPin);
			if (!ensure(PinMetaInfo.OwnerClass))
				continue;

			if (ensure(PinMetaInfo.SubFuncion))
			{
				bool bAnyLinked = HasAnyConnections(CurPin);
				FString ParamPrefix = FString::Printf(TEXT("%s%s"), *CurPin->PinName.ToString().LeftChop(PinMetaInfo.EnumExecName.Len()), *MemberDelimiter);
				if (!bAnyLinked)
				{
					for (auto Pin : Pins)
					{
						if (Pin && !SkipPinGuids.Contains(GetPinGuid(Pin)) && Pin->Direction == EGPD_Input && Pin->PinName.ToString().StartsWith(ParamPrefix))
						{
							SkipPinGuids.Add(GetPinGuid(Pin));
							if (HasAnyConnections(Pin))
							{
								bAnyLinked = true;
								break;
							}
						}
					}
				}
				if (!bAnyLinked)
					continue;

				UK2Node_CallFunction* MemFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				MemFuncNode->SetFromFunction(PinMetaInfo.SubFuncion);
				MemFuncNode->AllocateDefaultPins();
				if (UEdGraphPin* SelfPin = K2Schema->FindSelfPin(*MemFuncNode, EGPD_Input))
					bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, SelfPin);

				UK2Node_CustomEvent* ProxyEventNode = MakeEventNodeRemote(CompilerContext, SourceGraph, PinMetaInfo.SubFuncion->GetName());

				UEdGraphPin* FuncEventThenPin = FindThenPin(ProxyEventNode, true);

				TArray<UEdGraphPin*> ParamPins;
				for (TFieldIterator<FProperty> PropIt(PinMetaInfo.SubFuncion); PropIt; ++PropIt)
				{
					bIsErrorFree &= ensure(IsInputParameter(*PropIt));
					FName ParamName = *(ParamPrefix + PropIt->GetName());

					auto ParamPin = FindPin(ParamName, EGPD_Input);
					if (ParamPin)
						SkipPinGuids.Add(GetPinGuid(ParamPin));
					bIsErrorFree &= !!ParamPin;
					if (ensure(ParamPin))
					{
						auto ParmPin = ProxyEventNode->CreateUserDefinedPin(ParamPin->GetFName(), ParamPin->PinType, EGPD_Output);
						if (!IsRunningCommandlet() || HasAnyConnections(ParamPin))
							bIsErrorFree &= AssignTempAndGet(CompilerContext, SourceGraph, FuncEventThenPin, ParmPin, false, ParamPin);
						ParamPins.Add(ParamPin);
						bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, ParmPin, MemFuncNode->FindPin(PropIt->GetFName()));
					}
				}
				bIsErrorFree &= ConditionDo(CompilerContext, SourceGraph, InstPin, FuncEventThenPin, MemFuncNode->GetExecPin());

				UK2Node_CallFunction* const ProxyFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				ProxyFuncNode->SetFromFunction(InProxyFunc);
				ProxyFuncNode->AllocateDefaultPins();
				NodeOpreation(ProxyFuncNode, ProxyEventNode);
				bIsErrorFree &= ConnectMessagePins(CompilerContext, SourceGraph, ProxyFuncNode, ParamPins);
				bIsErrorFree &= ConditionDo(CompilerContext, SourceGraph, InstPin, CurPin, ProxyFuncNode->GetExecPin());
			}
		}
	}
	return bIsErrorFree;
}

UEdGraphPin* UK2Neuron::PureCastTo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin, UEdGraphPin* TargetPin, UEdGraphPin** BoolSuccessPin)
{
	auto InstClass = ClassFromPin(InstPin);
	auto TargetClass = ClassFromPin(TargetPin);
	if (!InstClass || !TargetClass)
		return nullptr;

	bool bIsErrorFree = true;
	if (InstClass->IsChildOf(TargetClass))
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, TargetPin);
		return InstPin;
	}
	else
	{
		UK2Node_DynamicCast* CastNode = CompilerContext.SpawnIntermediateNode<UK2Node_DynamicCast>(this, SourceGraph);
		CastNode->SetPurity(true);
		CastNode->TargetType = TargetClass;
		CastNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, CastNode->GetCastSourcePin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, CastNode->GetCastResultPin(), TargetPin);
		ensure(bIsErrorFree);
		if (BoolSuccessPin)
			*BoolSuccessPin = CastNode->GetBoolSuccessPin();
		return CastNode->GetCastResultPin();
	}
}

UEdGraphPin* UK2Neuron::PureCastClassTo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin, UEdGraphPin* TargetPin, UEdGraphPin** BoolSuccessPin /*= nullptr*/)
{
	auto InstClass = ClassFromPin(InstPin);
	auto TargetClass = ClassFromPin(TargetPin);
	if (!InstClass || !TargetClass)
		return nullptr;

	bool bIsErrorFree = true;
	if (InstClass == TargetClass)
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, TargetPin);
		return InstPin;
	}
	else
	{
		UK2Node_ClassDynamicCast* CastNode = CompilerContext.SpawnIntermediateNode<UK2Node_ClassDynamicCast>(this, SourceGraph);
		CastNode->SetPurity(true);
		CastNode->TargetType = TargetClass;
		CastNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, InstPin, CastNode->GetCastSourcePin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SourceGraph, CastNode->GetCastResultPin(), TargetPin);
		ensure(bIsErrorFree);
		if (BoolSuccessPin)
			*BoolSuccessPin = CastNode->GetBoolSuccessPin();
		return CastNode->GetCastResultPin();
	}
}

bool UK2Neuron::IsPinValueOnPropertyModified(UEdGraphPin* GraphPinObj, UObject* ObjOrFunc, FProperty* Prop, bool bReset)
{
	if (!bReset && HasAnyConnections(GraphPinObj))
		return false;

	if (!Prop || !ensure(ObjOrFunc))
		return false;

	FString DefaultValue;
	if (auto Func = Cast<UFunction>(ObjOrFunc))
	{
		if (!GetDefault<UEdGraphSchema_K2>()->FindFunctionParameterDefaultValue(Func, Prop, DefaultValue))
		{
			if (auto PinClass = ClassFromPin(GraphPinObj))
				DefaultValue = FSoftClassPath(PinClass).ToString();
		}
	}
	else
	{
		FBlueprintEditorUtils::PropertyValueToString(Prop, (uint8*)ObjOrFunc, DefaultValue);
	}
	FString PinValueString = GraphPinObj->GetDefaultAsString();
	if (Prop->IsA<FObjectPropertyBase>())
	{
		ConstructorHelpers::StripObjectClass(DefaultValue);
		ConstructorHelpers::StripObjectClass(PinValueString);
	}

	static auto IsEmptyOrNone = [](const FString& Str) { return Str.IsEmpty() || Str == TEXT("None"); };
	if (PinValueString != DefaultValue && !(IsEmptyOrNone(PinValueString) && IsEmptyOrNone(DefaultValue)))
	{
		if (bReset)
		{
			// FScopedTransaction Scope(LOCTEXT("ResetToDefault", "Reset To Default Value"));
			GraphPinObj->GetSchema()->TrySetDefaultValue(*GraphPinObj, DefaultValue);
			GraphPinObj->GetOwningNode()->GetGraph()->NotifyGraphChanged();
		}
		return true;
	}

	return false;
}

bool UK2Neuron::IsPinValueModified(UEdGraphPin* GraphPinObj, bool bReset) const
{
	if (!bReset && HasAnyConnections(GraphPinObj))
		return false;

	auto PinMetaInfo = GetPinMetaInfo(GraphPinObj);
	if (!PinMetaInfo.OwnerClass || !PinMetaInfo.OwnerClass->GetDefaultObject(false))
		return false;

	return IsPinValueOnPropertyModified(GraphPinObj, PinMetaInfo.GetObjOrFunc(), PinMetaInfo.Prop, bReset);
}

FName UK2Neuron::GetCornerIcon() const
{
	if (GetK2Schema()->GetGraphType(GetGraph()) == GT_Ubergraph)
		return TEXT("Graph.Latent.LatentIcon");
	return {};
}

void UK2Neuron::BindBlueprintCompiledEvent(UClass* InClass)
{
	if ((InClass != LastClass.Get()))
	{
		if (auto TargetBP = UBlueprint::GetBlueprintFromClass(LastClass.Get()))
		{
			K2Neuron::RemoveGlobalOnBlueprintCompiled(TargetBP, CompiledHandle);
		}

		if (UBlueprint* TargetBP = InClass ? UBlueprint::GetBlueprintFromClass(InClass) : nullptr)
		{
			auto ThisBP = GetBlueprint();
			auto WeakClass = MakeWeakObjectPtr(InClass);
			CompiledHandle = K2Neuron::AddGlobalOnBlueprintCompiled(TargetBP).Add(CreateWeakLambda(this, [this, WeakClass](UBlueprint* BP, UObject* OldCDO) {
				if (BP && WeakClass.IsValid() && BP == UBlueprint::GetBlueprintFromClass(WeakClass.Get()))
				{
					OnAssociatedBPCompiled(BP);
				}
			}));
		}
	}
	LastClass = InClass;
}

void UK2Neuron::BindBlueprintPreCompilingEvent()
{
	if (HasAnyFlags(RF_ClassDefaultObject))
		return;

	UBlueprint* ThisBP = GetBlueprint();
	K2Neuron::RemoveGlobalOnBlueprintCompiled(ThisBP, PreCompilingHandle);
	TWeakObjectPtr<UClass> WeakClass = Cast<UClass>(ThisBP->GeneratedClass);
	DataCDO = nullptr;

	PreCompilingHandle = K2Neuron::AddGlobalOnBlueprintPreCompiling(ThisBP).Add(CreateWeakLambda(this, [this, WeakClass](UBlueprint* BP) {
		if (BP && WeakClass.IsValid() && BP == UBlueprint::GetBlueprintFromClass(WeakClass.Get()))
		{
			// store object just before compiling
			DataCDO = BP->GeneratedClass->ClassDefaultObject;
			RemoteEventNodes.Empty();
		}
	}));
}

bool UK2Neuron::BindObjBlueprintCompiledEvent(UClass* InObjClass)
{
	if (InObjClass != LastObjClass.Get())
	{
		if (auto TargetBP = UBlueprint::GetBlueprintFromClass(LastObjClass.Get()))
		{
			K2Neuron::RemoveGlobalOnBlueprintCompiled(TargetBP, ObjHandle);
		}

		if (UBlueprint* TargetBP = InObjClass ? UBlueprint::GetBlueprintFromClass(InObjClass) : nullptr)
		{
			auto WeakClass = MakeWeakObjectPtr(InObjClass);
			ObjHandle = K2Neuron::AddGlobalOnBlueprintCompiled(TargetBP).Add(CreateWeakLambda(this, [this, WeakClass](UBlueprint* BP, UObject* OldCDO) {
				if (BP && WeakClass.IsValid() && BP == UBlueprint::GetBlueprintFromClass(WeakClass.Get()))
				{
					OnAssociatedBPCompiled(BP, OldCDO);
				}
			}));
		}
	}
	LastObjClass = InObjClass;
	return LastObjClass.IsValid();
}

void UK2Neuron::BindOwnerBlueprintCompiledEvent()
{
	if (OwnerCompiledHandle.IsValid())
		return;
	if (auto ThisBP = GetBlueprint())
	{
		K2Neuron::RemoveGlobalOnBlueprintCompiled(ThisBP, OwnerCompiledHandle);
		OwnerCompiledHandle = K2Neuron::AddGlobalOnBlueprintCompiled(ThisBP).Add(CreateWeakLambda(this, [this](UBlueprint* BP, UObject* OldCDO) { OnOwnerBPCompiled(); }));
	}
}

UK2Node_CustomEvent* UK2Neuron::MakeEventNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const FString& DescName, uint32* OutID)
{
	UK2Node_CustomEvent* SpawnedEventNode = CompilerContext.SpawnIntermediateNode<UK2Node_CustomEvent>(this, SourceGraph);
	const uint32 BPUniqueID = MakeShortEventName(SpawnedEventNode, DescName);
	if (OutID)
		*OutID = BPUniqueID;
	SpawnedEventNode->AllocateDefaultPins();
	return SpawnedEventNode;
}

UK2Node_CustomEvent* UK2Neuron::MakeEventNodeRemote(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const FString& DescName, uint32* OutID)
{
	auto EventNode = MakeEventNode(CompilerContext, SourceGraph, DescName, OutID);
	EventNode->FunctionFlags |= FUNC_NetRequest;
	RemoteEventNodes.Add(EventNode->CustomFunctionName);
	return EventNode;
}

#if WITH_EDITOR
static TAutoConsoleVariable<int> CVarNeuronEventNameFullDesc(TEXT("neuron.eventname.fulldesc"), 0, TEXT("neuron event name with full desc(only in editor build)"));
#endif

uint32 UK2Neuron::MakeShortEventName(UK2Node_CustomEvent* InEventNode, const FString& DescName)
{
	const uint32 BPUniqueID = K2Neuron::GetUniqueIDForBlueprint(GetBlueprint());
	if (CVarNeuronEventNameFullDesc.GetValueOnAnyThread() != 0 && !IsRunningCommandlet())
		InEventNode->CustomFunctionName = *FString::Printf(TEXT("%s_%X-T"), *DescName, BPUniqueID);
	else
		InEventNode->CustomFunctionName = *FString::Printf(TEXT("%X"), BPUniqueID);
	return BPUniqueID;
}

K2NEURON_API UObject* UK2Neuron::FPinMetaInfo::GetObjOrFunc() const
{
	if (SubFuncion)
		return SubFuncion;
	if (SubDelegate)
		return SubDelegate->SignatureFunction;
	if (OwnerClass)
		return OwnerClass->GetDefaultObject(false);
	return nullptr;
}

#if WITH_EDITOR

#include "SGraphPin.h"
#include "Widgets/Input/SCheckBox.h"

void SGraphNeuronBase::Construct(const FArguments& InArgs, UK2Neuron* InNode)
{
	SGraphNodeK2Default::Construct({}, InNode);
}

void SGraphNeuronBase::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	auto PinObj = PinToAdd->GetPinObj();
	auto PinName = PinObj->GetName();
	auto NeuronNode = CastChecked<UK2Neuron>(GraphNode);
	do
	{
		if (PinObj->Direction != EGPD_Input || PinObj->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || NeuronNode->GetK2Schema()->ShouldHidePinDefaultValue(PinObj)  //
			|| !ShouldShowResetButton() || !NeuronNode->MatchAffixesInput(PinObj, true, false, true))
			break;

		auto PinMetaInfo = NeuronNode->GetPinMetaInfo(PinObj);
		UObject* ObjOrFunc = PinMetaInfo.GetObjOrFunc();
		if (!ObjOrFunc)
			break;
		FProperty* Prop = PinMetaInfo.Prop;

		PinToAdd->SetOwner(SharedThis(this));
		auto ResetBtn = PropertyCustomizationHelpers::MakeResetButton(CreateWeakLambda(ObjOrFunc, [PinObj, ObjOrFunc, Prop] {
			UK2Neuron::IsPinValueOnPropertyModified(PinObj, ObjOrFunc, Prop, true);
			if (auto Node = Cast<UK2Neuron>(PinObj->GetOwningNode()))
			{
				Node->NeuronCheckableGuids.Remove(UK2Neuron::GetPinGuid(PinObj));
				Node->GetGraph()->NotifyGraphChanged();
			}
		}));

		ResetBtn->SetToolTipText(LOCTEXT("Reset", "Reset To Default Value"));
		ResetBtn->SetCursor(EMouseCursor::Default);
		ResetBtn->SetVisibility(
			TAttribute<EVisibility>::Create(CreateWeakLambda(ObjOrFunc, [PinObj, ObjOrFunc, Prop] { return UK2Neuron::IsPinValueOnPropertyModified(PinObj, ObjOrFunc, Prop, false) ? EVisibility::Visible : EVisibility::Collapsed; })));
		ResetBtn->SetEnabled(TAttribute<bool>(PinToAdd, &SGraphPin::IsEditingEnabled));
		TSharedPtr<SCheckBox> CheckBox;

		LeftNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(GetDefault<UGraphEditorSettings>()->GetInputPinPadding())
				[SNew(SHorizontalBox)
					 .Visibility(TAttribute<EVisibility>::Create(CreateWeakLambda(ObjOrFunc,
																				  [PinToAdd, ObjOrFunc, Prop] {
																					  auto Visibility = PinToAdd->IsPinVisibleAsAdvanced();
																					  if (Visibility == EVisibility::Collapsed && UK2Neuron::IsPinValueOnPropertyModified(PinToAdd->GetPinObj(), ObjOrFunc, Prop, false))
																						  Visibility = EVisibility::Visible;
																					  return Visibility;
																				  })))
				 + SHorizontalBox::Slot().AutoWidth()[PinToAdd] + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Bottom).HAlign(HAlign_Center)[ResetBtn]
				 + SHorizontalBox::Slot()
					   .AutoWidth()
					   .VAlign(VAlign_Bottom)
					   .HAlign(HAlign_Center)[SAssignNew(CheckBox, SCheckBox)
												  .Style(FGMPStyle::Get(), "Graph.Checkbox")
												  .IsChecked(this, &SGraphNeuronBase::IsDefaultValueChecked, PinObj)
												  .OnCheckStateChanged(this, &SGraphNeuronBase::OnDefaultValueCheckBoxChanged, PinObj)
												  .IsEnabled(TAttribute<bool>(PinToAdd, &SGraphPin::IsEditingEnabled))
												  .Visibility(MakeAttributeSP(this, &SGraphNeuronBase::GetCheckBoxVisibility, PinObj, TAttribute<bool>::Create(CreateWeakLambda(ObjOrFunc, [PinToAdd, ObjOrFunc, Prop] {
																				  return !UK2Neuron::IsPinValueOnPropertyModified(PinToAdd->GetPinObj(), ObjOrFunc, Prop, false);
																			  }))))]];
		CheckBox->SetCursor(EMouseCursor::Default);
		const auto& TipText = GetCheckBoxToolTipText(PinObj);
		if (!TipText.IsEmpty())
			CheckBox->SetToolTipText(TipText);

		InputPins.Add(PinToAdd);
		return;
	} while (0);

	do
	{
		if (PinObj->Direction != EGPD_Output || PinObj->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec  //
			|| (!NeuronNode->MatchAffixesEvent(PinObj, true, true, true)))
			break;

		TSharedPtr<SCheckBox> CheckBox;
		PinToAdd->SetOwner(SharedThis(this));

		RightNodeBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Center)
			.Padding(GetDefault<UGraphEditorSettings>()->GetOutputPinPadding())[SNew(SHorizontalBox).Visibility(this, &SGraphNeuronBase::GetOutputPinVisibility, PinObj)
																				+ SHorizontalBox::Slot()
																					  .AutoWidth()
																					  .Padding(2, 0)
																					  .VAlign(VAlign_Center)
																					  .HAlign(HAlign_Center)[SAssignNew(CheckBox, SCheckBox)
																												 .Style(FGMPStyle::Get(), "Graph.Checkbox")
																												 .IsChecked(this, &SGraphNeuronBase::IsDefaultValueChecked, PinObj)
																												 .OnCheckStateChanged(this, &SGraphNeuronBase::OnDefaultValueCheckBoxChanged, PinObj)
																												 .IsEnabled(TAttribute<bool>(PinToAdd, &SGraphPin::IsEditingEnabled))
																												 .Visibility(MakeAttributeSP(this, &SGraphNeuronBase::GetCheckBoxVisibility, PinObj, TAttribute<bool>()))]
																				+ SHorizontalBox::Slot().AutoWidth()[PinToAdd]];

		CheckBox->SetCursor(EMouseCursor::Default);
		const auto& TipText = GetCheckBoxToolTipText(PinObj);
		if (!TipText.IsEmpty())
			CheckBox->SetToolTipText(TipText);
		OutputPins.Add(PinToAdd);
		return;
	} while (0);

	SGraphNodeK2Default::AddPin(PinToAdd);
}

EVisibility SGraphNeuronBase::GetOutputPinVisibility(UEdGraphPin* PinObj) const
{
	bool bHideAdvancedPin = false;
	auto NeuronNode = CastChecked<UK2Neuron>(GraphNode);

	if (OwnerGraphPanelPtr.IsValid())
		bHideAdvancedPin = (ENodeAdvancedPins::Hidden == NeuronNode->AdvancedPinDisplay);
	const bool bIsAdvancedPin = PinObj && !PinObj->IsPendingKill() && PinObj->bAdvancedView && !PinObj->bOrphanedPin;
	const bool bCanBeHidden = !PinObj->LinkedTo.Num() && (!NeuronNode->NeuronCheckableGuids.Contains(UK2Neuron::GetPinGuid(PinObj)));
	return (bIsAdvancedPin && bHideAdvancedPin && bCanBeHidden) ? EVisibility::Collapsed : EVisibility::Visible;
}

ECheckBoxState SGraphNeuronBase::IsDefaultValueChecked(UEdGraphPin* PinObj) const
{
	auto NeuronNode = CastChecked<UK2Neuron>(GraphNode);
	return (NeuronNode->NeuronCheckableGuids.Contains(UK2Neuron::GetPinGuid(PinObj))) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SGraphNeuronBase::OnDefaultValueCheckBoxChanged(ECheckBoxState InIsChecked, UEdGraphPin* PinObj)
{
	if (auto NeuronNode = CastChecked<UK2Neuron>(GraphNode))
	{
		bool bChecked = InIsChecked == ECheckBoxState::Checked;
		if (bChecked != (NeuronNode->NeuronCheckableGuids.Contains(UK2Neuron::GetPinGuid(PinObj))))
		{
			const FScopedTransaction Transaction(TEXT("NeuronCheckableFlagsChanged"), LOCTEXT("NeuronCheckableFlags", "NeuronCheckableFlagsChanged"), NeuronNode->GetBlueprint());
			if (bChecked)
			{
				NeuronNode->NeuronCheckableGuids.Add(UK2Neuron::GetPinGuid(PinObj));
			}
			else
			{
				NeuronNode->NeuronCheckableGuids.Remove(UK2Neuron::GetPinGuid(PinObj));
			}
			NeuronNode->Modify();
			FBlueprintEditorUtils::MarkBlueprintAsModified(NeuronNode->GetBlueprint());
		}
	}
}
#endif

#undef LOCTEXT_NAMESPACE
