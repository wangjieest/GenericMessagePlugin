//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "K2Node_GMPGenericInvoker.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "GMP/GMPBPLib.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_Self.h"
#include "KismetCompiler.h"
#include "Styling/AppStyle.h"

#define LOCTEXT_NAMESPACE "K2Node_GMPGenericInvoker"

const FName UK2Node_GMPGenericInvoker::PN_Target(TEXT("Target"));
const FName UK2Node_GMPGenericInvoker::PN_Result(TEXT("Result"));

UK2Node_GMPGenericInvoker::UK2Node_GMPGenericInvoker()
{
}

//////////////////////////////////////////////////////////////////////////
// Resolution helpers
//////////////////////////////////////////////////////////////////////////

UClass* UK2Node_GMPGenericInvoker::GetTargetPinClass() const
{
	const UEdGraphPin* TargetPin = FindPin(PN_Target);
	if (!TargetPin)
		return nullptr;

	// Connected: infer the class from the upstream pin (following knots).
	if (TargetPin->LinkedTo.Num() == 1)
	{
		const UEdGraphPin* Src = TargetPin->LinkedTo[0];
		while (auto* Knot = Cast<UK2Node_Knot>(Src->GetOwningNode()))
		{
			if (Knot->GetInputPin()->LinkedTo.Num() == 1)
				Src = Knot->GetInputPin()->LinkedTo[0];
			else
				break;
		}
		if (UClass* Linked = Cast<UClass>(Src->PinType.PinSubCategoryObject.Get()))
			return Linked;
	}

	// Unconnected self pin: default to the blueprint's own class.
	return GetBlueprintClassFromNode();
}

void UK2Node_GMPGenericInvoker::ClassifyAndCache(FGMPMemberChainLink& Link, FProperty* Prop) const
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	FEdGraphPinType PinType;
	K2Schema->ConvertPropertyToPinType(Prop, PinType);
	Link.CachedPinType = PinType;
	Link.Kind = GMPMemberChainUtils::ClassifyKind(Prop);
	if (UStruct* Owner = Prop->GetOwnerStruct())
		Link.OwnerTypeName = Owner->GetFName();
}

bool UK2Node_GMPGenericInvoker::ResolveChain(TArray<FProperty*>* OutProps)
{
	if (OutProps)
		OutProps->Reset();

	UStruct* CurOwner = GetTargetPinClass();
	bool bAllOk = (CurOwner != nullptr);

	for (int32 i = 0; i < MemberChain.Num(); ++i)
	{
		FGMPMemberChainLink& Link = MemberChain[i];
		FProperty* Prop = nullptr;
		if (CurOwner)
		{
			if (UClass* Cls = Cast<UClass>(CurOwner))
				Prop = Link.MemberRef.ResolveMember<FProperty>(Cls);
			else
				Prop = FindFProperty<FProperty>(CurOwner, Link.MemberRef.GetMemberName());
		}
		Link.bResolveFailed = (Prop == nullptr);
		bAllOk &= !Link.bResolveFailed;

		if (Prop)
		{
			ClassifyAndCache(Link, Prop);
			CurOwner = GMPMemberChainUtils::NextOwnerOf(Prop);
		}
		else
		{
			CurOwner = nullptr;
		}
		if (OutProps)
			OutProps->Add(Prop);
	}

	// GetMember mode: output type follows the last resolved link.
	if (MemberChain.Num() > 0 && !MemberChain.Last().bResolveFailed)
		CachedOutputType = MemberChain.Last().CachedPinType;
	else
		CachedOutputType = FEdGraphPinType();

	return bAllOk;
}

UStruct* UK2Node_GMPGenericInvoker::GetOwnerStructForLevel(int32 Level) const
{
	UStruct* CurOwner = GetTargetPinClass();
	for (int32 i = 0; i < Level && CurOwner; ++i)
	{
		if (!MemberChain.IsValidIndex(i))
			return nullptr;
		FProperty* Prop = nullptr;
		if (UClass* Cls = Cast<UClass>(CurOwner))
			Prop = const_cast<FGMPMemberChainLink&>(MemberChain[i]).MemberRef.ResolveMember<FProperty>(Cls);
		else
			Prop = FindFProperty<FProperty>(CurOwner, MemberChain[i].MemberRef.GetMemberName());
		CurOwner = Prop ? GMPMemberChainUtils::NextOwnerOf(Prop) : nullptr;
	}
	return CurOwner;
}

UClass* UK2Node_GMPGenericInvoker::GetEndpointClass() const
{
	return Cast<UClass>(GetOwnerStructForLevel(MemberChain.Num()));
}

UFunction* UK2Node_GMPGenericInvoker::ResolveFunction() const
{
	UClass* EndClass = GetEndpointClass();
	if (!EndClass)
	{
		const_cast<UK2Node_GMPGenericInvoker*>(this)->bResolveFailed = !FunctionRef.GetMemberName().IsNone();
		return nullptr;
	}
	UFunction* Fn = FunctionRef.ResolveMember<UFunction>(EndClass);
	const_cast<UK2Node_GMPGenericInvoker*>(this)->bResolveFailed = (Fn == nullptr) && !FunctionRef.GetMemberName().IsNone();
	return Fn;
}

bool UK2Node_GMPGenericInvoker::IsFunctionPure() const
{
	UFunction* Fn = const_cast<UK2Node_GMPGenericInvoker*>(this)->ResolveFunction();
	return Fn && Fn->HasAnyFunctionFlags(FUNC_BlueprintPure);
}

bool UK2Node_GMPGenericInvoker::IsNodePure() const
{
	// GetMember is always pure. CallFunction follows the function's purity.
	if (EndpointMode == EGMPInvokeEndpoint::GetMember)
		return true;
	return IsFunctionPure();
}

void UK2Node_GMPGenericInvoker::RebuildParamCaches(UFunction* Fn)
{
	CachedParams.Reset();
	if (!Fn)
		return;
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	for (TFieldIterator<FProperty> It(Fn); It && (It->PropertyFlags & CPF_Parm); ++It)
	{
		FProperty* Param = *It;
		FGMPCallParamCache Cache;
		Cache.ParamName = Param->GetFName();
		K2Schema->ConvertPropertyToPinType(Param, Cache.PinType);
		Cache.bIsReturn = Param->HasAnyPropertyFlags(CPF_ReturnParm);
		Cache.bIsOutput = !Cache.bIsReturn && Param->HasAnyPropertyFlags(CPF_OutParm) && !Param->HasAnyPropertyFlags(CPF_ConstParm);
		CachedParams.Add(Cache);
	}
}

//////////////////////////////////////////////////////////////////////////
// Pins
//////////////////////////////////////////////////////////////////////////

void UK2Node_GMPGenericInvoker::AllocateDefaultPins()
{
	// exec / then only when impure.
	if (!IsNodePure())
	{
		CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Execute);
		CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, UEdGraphSchema_K2::PN_Then);
	}

	// Target object input as a "self" pin: unconnected => the blueprint itself.
	CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Object, UEdGraphSchema_K2::PSC_Self, nullptr, PN_Target);

	ResolveChain();

	if (EndpointMode == EGMPInvokeEndpoint::GetMember)
	{
		UEdGraphPin* ResultPin = CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Wildcard, PN_Result);
		if (CachedOutputType.PinCategory != NAME_None && CachedOutputType.PinCategory != UEdGraphSchema_K2::PC_Wildcard)
			ResultPin->PinType = CachedOutputType;
	}
	else
	{
		UFunction* Fn = ResolveFunction();
		RebuildParamCaches(Fn);
		for (const FGMPCallParamCache& Cache : CachedParams)
		{
			const EEdGraphPinDirection Dir = (Cache.bIsOutput || Cache.bIsReturn) ? EGPD_Output : EGPD_Input;
			UEdGraphPin* Pin = CreatePin(Dir, NAME_None, Cache.ParamName);
			Pin->PinType = Cache.PinType;
		}
	}

	Super::AllocateDefaultPins();
}

void UK2Node_GMPGenericInvoker::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	ResolveChain();
	if (EndpointMode == EGMPInvokeEndpoint::CallFunction)
		RebuildParamCaches(ResolveFunction());
	Super::ReallocatePinsDuringReconstruction(OldPins);
}

void UK2Node_GMPGenericInvoker::PostReconstructNode()
{
	Super::PostReconstructNode();
}

void UK2Node_GMPGenericInvoker::PostLoad()
{
	Super::PostLoad();
	ResolveChain();
	if (EndpointMode == EGMPInvokeEndpoint::CallFunction)
		ResolveFunction();
}

void UK2Node_GMPGenericInvoker::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);
	if (Pin && Pin->PinName == PN_Target)
		ReconstructNode();
}

//////////////////////////////////////////////////////////////////////////
// Editing actions (details panel)
//////////////////////////////////////////////////////////////////////////

void UK2Node_GMPGenericInvoker::RemoveLevelsFrom(int32 Level)
{
	if (MemberChain.IsValidIndex(Level))
		MemberChain.RemoveAt(Level, MemberChain.Num() - Level);
}

void UK2Node_GMPGenericInvoker::SetMemberAtLevel(int32 Level, FProperty* Picked)
{
	if (!Picked)
		return;
	Modify();
	RemoveLevelsFrom(Level + 1);
	if (!MemberChain.IsValidIndex(Level))
		MemberChain.SetNum(Level + 1);

	FGMPMemberChainLink& Link = MemberChain[Level];
	UStruct* Owner = GetOwnerStructForLevel(Level);
	UClass* OwnerClass = Cast<UClass>(Owner);
	Link.MemberRef.SetFromField<FProperty>(Picked, /*bIsConsideredSelfContext=*/false, OwnerClass);
	ClassifyAndCache(Link, Picked);

	if (GMPMemberChainUtils::ClassifyKind(Picked) != EGMPChainNodeKind::Leaf)
		MemberChain.AddDefaulted();   // descendable -> offer next level

	ReconstructNode();
}

void UK2Node_GMPGenericInvoker::SetEndpointMode(EGMPInvokeEndpoint Mode)
{
	if (EndpointMode == Mode)
		return;
	Modify();
	EndpointMode = Mode;
	ReconstructNode();
}

void UK2Node_GMPGenericInvoker::SetFunction(UFunction* Fn)
{
	if (!Fn)
		return;
	Modify();
	EndpointMode = EGMPInvokeEndpoint::CallFunction;
	UClass* OwnerClass = Cast<UClass>(Fn->GetOuterUClass());
	FunctionRef.SetFromField<UFunction>(Fn, /*bIsConsideredSelfContext=*/false, OwnerClass);
	if (UClass* End = GetEndpointClass())
		EndpointClassName = End->GetFName();
	RebuildParamCaches(Fn);
	ReconstructNode();
}

//////////////////////////////////////////////////////////////////////////
// Cosmetic
//////////////////////////////////////////////////////////////////////////

FText UK2Node_GMPGenericInvoker::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (TitleType == ENodeTitleType::MenuTitle)
		return LOCTEXT("NodeTitle_Menu", "Generic Invoker (GMP)");

	TArray<FString> Parts;
	for (const FGMPMemberChainLink& Link : MemberChain)
	{
		const FName N = Link.MemberRef.GetMemberName();
		if (!N.IsNone())
			Parts.Add(Link.bResolveFailed ? (N.ToString() + TEXT("(?)")) : N.ToString());
	}
	FString Path = Parts.Num() ? FString::Join(Parts, TEXT(".")) : FString();

	if (EndpointMode == EGMPInvokeEndpoint::CallFunction)
	{
		const FName Fn = FunctionRef.GetMemberName();
		const FString Call = Fn.IsNone() ? TEXT("<func>") : Fn.ToString();
		return FText::Format(LOCTEXT("Title_Call", "Call {0}{1}{2}"),
			FText::FromString(Path),
			FText::FromString(Path.IsEmpty() ? TEXT("") : TEXT(".")),
			FText::FromString(Call));
	}
	return FText::Format(LOCTEXT("Title_Get", "Get {0}"), FText::FromString(Path.IsEmpty() ? TEXT("<member>") : Path));
}

FText UK2Node_GMPGenericInvoker::GetTooltipText() const
{
	return LOCTEXT("Tooltip", "Walks a member chain off the target object, then reads the final member or calls a function -- all by name reflection. No hard reference to the endpoint class at runtime.");
}

FSlateIcon UK2Node_GMPGenericInvoker::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = FLinearColor::White;
	static FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

FText UK2Node_GMPGenericInvoker::GetMenuCategory() const
{
	return LOCTEXT("MenuCategory", "GMP|Reflection");
}

void UK2Node_GMPGenericInvoker::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	if (ActionRegistrar.IsOpenForRegistration(ActionKey))
	{
		UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(GetClass());
		check(NodeSpawner);
		ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
	}
}

void UK2Node_GMPGenericInvoker::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);

	for (const FGMPMemberChainLink& Link : MemberChain)
	{
		if (Link.bResolveFailed)
			MessageLog.Error(*FText::Format(LOCTEXT("ChainFail", "Member '{0}' could not be resolved @@"), FText::FromName(Link.MemberRef.GetMemberName())).ToString(), this);
	}

	if (EndpointMode == EGMPInvokeEndpoint::GetMember)
	{
		if (MemberChain.Num() == 0)
			MessageLog.Error(*LOCTEXT("NoChain", "Member chain is empty @@").ToString(), this);
	}
	else
	{
		if (FunctionRef.GetMemberName().IsNone())
			MessageLog.Error(*LOCTEXT("NoFunc", "No function selected @@").ToString(), this);
		else if (bResolveFailed)
			MessageLog.Error(*FText::Format(LOCTEXT("FuncFail", "Function '{0}' could not be resolved @@"), FText::FromName(FunctionRef.GetMemberName())).ToString(), this);
	}
}

void UK2Node_GMPGenericInvoker::HandleVariableRenamed(UBlueprint* InBlueprint, UClass* InVariableClass, UEdGraph* InGraph, const FName& InOldVarName, const FName& InNewVarName)
{
	bool bAffected = (FunctionRef.GetMemberName() == InOldVarName);
	for (const FGMPMemberChainLink& Link : MemberChain)
	{
		if (Link.MemberRef.GetMemberName() == InOldVarName)
		{
			bAffected = true;
			break;
		}
	}
	if (bAffected)
		ReconstructNode();
}

//////////////////////////////////////////////////////////////////////////
// Expansion
//////////////////////////////////////////////////////////////////////////

void UK2Node_GMPGenericInvoker::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	bool bErrorFree = true;

	// Wires the Target value into an intermediate object input pin. If Target is
	// connected, its links are moved; otherwise (self pin) a UK2Node_Self supplies
	// the blueprint instance -- so the default really is "self".
	auto WireTargetTo = [&](UEdGraphPin* DestObjPin) -> bool
	{
		UEdGraphPin* TargetPin = FindPinChecked(PN_Target);
		if (TargetPin->LinkedTo.Num() > 0)
			return CompilerContext.MovePinLinksToIntermediate(*TargetPin, *DestObjPin).CanSafeConnect();

		UK2Node_Self* SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(this, SourceGraph);
		SelfNode->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(SelfNode, this);
		return K2Schema->TryCreateConnection(SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self), DestObjPin);
	};

	if (EndpointMode == EGMPInvokeEndpoint::GetMember)
	{
		// Whole chain (including the leaf) resolved by one GetMemberByChain call.
		TArray<FProperty*> Props;
		if (!ResolveChain(&Props) || MemberChain.Num() == 0)
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ExpandChainFail", "GMPGenericInvoker: unresolved member chain @@").ToString(), this);
			BreakAllNodeLinks();
			return;
		}

		UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		CallNode->SetFromFunction(UGMPBPLib::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UGMPBPLib, GetMemberByChain)));
		CallNode->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(CallNode, this);

		bErrorFree &= WireTargetTo(CallNode->FindPinChecked(TEXT("InObject")));

		UK2Node_MakeArray* MakeArr = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeArr->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeArr, this);
		UEdGraphPin* ArrayOut = MakeArr->GetOutputPin();
		ArrayOut->MakeLinkTo(CallNode->FindPinChecked(TEXT("Chain")));
		MakeArr->PinConnectionListChanged(ArrayOut);
		for (int32 i = 0; i < MemberChain.Num(); ++i)
		{
			if (i > 0)
				MakeArr->AddInputPin();
			UEdGraphPin* ElemPin = MakeArr->FindPinChecked(MakeArr->GetPinName(i));
			K2Schema->TrySetDefaultValue(*ElemPin, MemberChain[i].MemberRef.GetMemberName().ToString());
		}

		UEdGraphPin* OutValuePin = CallNode->FindPinChecked(TEXT("OutValue"));
		OutValuePin->PinType = CachedOutputType;
		bErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(PN_Result), *OutValuePin).CanSafeConnect();
	}
	else
	{
		// CallFunction: optionally walk the chain to the endpoint object first.
		UFunction* Fn = ResolveFunction();
		if (!Fn)
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("ExpandFuncFail", "GMPGenericInvoker: unresolved function @@").ToString(), this);
			BreakAllNodeLinks();
			return;
		}

		UEdGraphPin* EndpointObjPin = nullptr;
		if (MemberChain.Num() > 0)
		{
			UK2Node_CallFunction* ChainNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			ChainNode->SetFromFunction(UGMPBPLib::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UGMPBPLib, GetMemberByChain)));
			ChainNode->AllocateDefaultPins();
			CompilerContext.MessageLog.NotifyIntermediateObjectCreation(ChainNode, this);

			bErrorFree &= WireTargetTo(ChainNode->FindPinChecked(TEXT("InObject")));

			UK2Node_MakeArray* MakeChain = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
			MakeChain->AllocateDefaultPins();
			CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeChain, this);
			UEdGraphPin* ChainArrayOut = MakeChain->GetOutputPin();
			ChainArrayOut->MakeLinkTo(ChainNode->FindPinChecked(TEXT("Chain")));
			MakeChain->PinConnectionListChanged(ChainArrayOut);
			for (int32 i = 0; i < MemberChain.Num(); ++i)
			{
				if (i > 0)
					MakeChain->AddInputPin();
				UEdGraphPin* ElemPin = MakeChain->FindPinChecked(MakeChain->GetPinName(i));
				K2Schema->TrySetDefaultValue(*ElemPin, MemberChain[i].MemberRef.GetMemberName().ToString());
			}

			EndpointObjPin = ChainNode->FindPinChecked(TEXT("OutValue"));
			EndpointObjPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
			EndpointObjPin->PinType.PinSubCategoryObject = UObject::StaticClass();
		}

		UK2Node_CallFunction* CallNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		CallNode->SetFromFunction(UGMPBPLib::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UGMPBPLib, CallObjectFunctionByName)));
		CallNode->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(CallNode, this);

		// exec chain only if this node is impure.
		if (!IsNodePure())
		{
			bErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Execute), *CallNode->GetExecPin()).CanSafeConnect();
			bErrorFree &= CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(UEdGraphSchema_K2::PN_Then), *CallNode->GetThenPin()).CanSafeConnect();
		}

		if (EndpointObjPin)
			EndpointObjPin->MakeLinkTo(CallNode->FindPinChecked(TEXT("Obj")));
		else
			bErrorFree &= WireTargetTo(CallNode->FindPinChecked(TEXT("Obj")));

		K2Schema->TrySetDefaultValue(*CallNode->FindPinChecked(TEXT("FuncName")), Fn->GetName());

		for (const FGMPCallParamCache& Cache : CachedParams)
		{
			const EEdGraphPinDirection Dir = (Cache.bIsOutput || Cache.bIsReturn) ? EGPD_Output : EGPD_Input;
			UEdGraphPin* VarPin = CallNode->CreatePin(Dir, Cache.PinType.PinCategory, Cache.ParamName);
			VarPin->PinType = Cache.PinType;

			UEdGraphPin* NodePin = FindPinChecked(Cache.ParamName);
			bErrorFree &= CompilerContext.MovePinLinksToIntermediate(*NodePin, *VarPin).CanSafeConnect();
		}
	}

	if (!bErrorFree)
		CompilerContext.MessageLog.Error(*LOCTEXT("ExpandWireFail", "GMPGenericInvoker expansion failed @@").ToString(), this);

	BreakAllNodeLinks();
}

TSharedPtr<class SGraphNode> UK2Node_GMPGenericInvoker::CreateVisualWidget()
{
	return nullptr;
}

#undef LOCTEXT_NAMESPACE
