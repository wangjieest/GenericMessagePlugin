//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "K2Node_NotifyMessage.h"

#include "../Private/SMessageTagGraphPin.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "DataTableEditorUtils.h"
#include "EdGraphSchema_K2.h"
#include "EditorCategoryUtils.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "K2Node_AssignmentStatement.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_MakeArray.h"
#include "K2Node_PureAssignmentStatement.h"
#include "K2Node_Self.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_VariableSetRef.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "KismetCompiler.h"
#include "KismetNodes/KismetNodeInfoContext.h"
#include "ScopedTransaction.h"

#define LOCTEXT_NAMESPACE "GMPNotifyMessage"

namespace GMPNotifyMessage
{
FString GetNameForPin(int32 Index)
{
	return UK2Node_NotifyMessage::MessageParamPrefix + LexToString(Index);
}
FString GetNameForRspPin(int32 Index)
{
	return UK2Node_NotifyMessage::MessageResponsePrefix + LexToString(Index);
}
const FGraphPinNameType ResponseExecName = TEXT("OnResponse");
const FGraphPinNameType ExactObjName = TEXT("ExactObjName");
const FGraphPinNameType Sender = TEXT("Sender");
};  // namespace GMPNotifyMessage

#if WITH_EDITOR
TSharedPtr<class SGraphNode> UK2Node_NotifyMessage::CreateVisualWidget()
{
	class SGraphNodeNotifyMessage : public SGraphNodeMessageBase
	{
	public:
		SLATE_BEGIN_ARGS(SGraphNodeNotifyMessage) {}
		SLATE_END_ARGS()

		TWeakObjectPtr<UK2Node_NotifyMessage> Node;
		UEdGraphPin* SenderPin = nullptr;
		UEdGraphPin* FilterPin = nullptr;
		mutable TArray<FWeakObjectPtr> Listeners;
		void Construct(const FArguments& InArgs, UK2Node_NotifyMessage* InNode, UEdGraphPin* InSenderPin, UEdGraphPin* InFilterPin = nullptr)
		{
			Node = InNode;
			SenderPin = InSenderPin;
			FilterPin = InFilterPin;
			this->SetCursor(EMouseCursor::CardinalCross);
			SGraphNodeMessageBase::Construct({}, InNode);
		}
		virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override
		{
			SGraphNodeMessageBase::GetNodeInfoPopups(Context, Popups);

			FKismetNodeInfoContext* K2Context = (FKismetNodeInfoContext*)Context;
			UObject* ActiveObject = K2Context->ActiveObjectBeingDebugged;
			UWorld* World = ActiveObject ? ActiveObject->GetWorld() : nullptr;
			if (!IsValid(World) || !World->IsGameWorld() || !Node.IsValid())
				return;

			UObject* SenderObj = ActiveObject;
			do
			{
				if (!UBlueprintGeneratedClass::UsePersistentUberGraphFrame() || !SenderPin)
					break;

				auto BGClass = Cast<UBlueprintGeneratedClass>(K2Context->SourceBlueprint->GeneratedClass);
				if (!BGClass || !ensure(ActiveObject->IsA(BGClass)))
					break;

				auto* SenderProp = CastField<FObjectProperty>(FKismetDebugUtilities::FindClassPropertyForPin(K2Context->SourceBlueprint, SenderPin));
				if (!SenderProp)
					break;

				if (ActiveObject && BGClass->UberGraphFramePointerProperty)
				{
					FPointerToUberGraphFrame* PointerToUberGraphFrame = BGClass->UberGraphFramePointerProperty->ContainerPtrToValuePtr<FPointerToUberGraphFrame>(ActiveObject);
					check(PointerToUberGraphFrame);
					SenderObj = SenderProp->GetObjectPropertyValue(SenderProp->ContainerPtrToValuePtr<void>(PointerToUberGraphFrame->RawPointer));
				}
			} while (false);

			const auto Limitation = 10;
			Listeners.Reset(0);
			const bool bEllipsis = GMP::FMessageUtils::GetMessageHub()->GetListeners(SenderObj, Node->MsgTag.GetTagName(), Listeners, Limitation);
			if (Listeners.Num() == 0)
			{
				static const FString NoListener(TEXT("no listener "));
				new (Popups) FGraphInformationPopupInfo(nullptr, FLinearColor::Gray, NoListener);
			}
			else
			{
				for (auto& Listener : Listeners)
				{
					static const FString SelfListen(TEXT("self listening "));
					bool bSelf = Listener.Get() == ActiveObject;
					new (Popups) FGraphInformationPopupInfo(nullptr, bSelf ? FLinearColor::Yellow : FLinearColor::Green, bSelf ? SelfListen : GetNameSafe(Listener.Get()));
				}

				if (bEllipsis)
				{
					static const FString Ellipsis(TEXT("... "));
					new (Popups) FGraphInformationPopupInfo(nullptr, FLinearColor::Gray, Ellipsis);
				}
			}
		}
	};

	return SNew(SGraphNodeNotifyMessage, this, FindPinChecked(GMPNotifyMessage::Sender));
}
#endif

UK2Node_NotifyMessage::UK2Node_NotifyMessage()
{
	NodeTooltip = LOCTEXT("NotifyMessage", "NotifyMessage To Blueprint And C++");
}

FName UK2Node_NotifyMessage::GetMessageSignature() const
{
	if (auto Pin = GetEventNamePin())
	{
		return *Pin->DefaultValue;
	}
	return NAME_None;
}

UEdGraphPin* UK2Node_NotifyMessage::GetMessagePin(int32 Index, TArray<UEdGraphPin*>* InPins, bool bEnsure) const
{
	return GetPinByName(GMPNotifyMessage::GetNameForPin(Index), InPins, bEnsure);
}

UEdGraphPin* UK2Node_NotifyMessage::GetResponsePin(int32 Index, TArray<UEdGraphPin*>* InPins, bool bEnsure) const
{
	return GetPinByName(GMPNotifyMessage::GetNameForRspPin(Index), InPins, bEnsure);
}

UEdGraphPin* UK2Node_NotifyMessage::CreateResponseExecPin()
{
	FEdGraphPinType PinType;
	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
	return CreatePin(EGPD_Output, PinType, TEXT("OnResponse"));
}

UEdGraphPin* UK2Node_NotifyMessage::GetResponseExecPin() const
{
	return FindPin(GMPNotifyMessage::ResponseExecName, EGPD_Output);
}

UEdGraphPin* UK2Node_NotifyMessage::AddMessagePin(int32 Index, bool bTransaction)
{
	if (bTransaction)
	{
		FScopedTransaction Transaction(LOCTEXT("AddPinTx", "AddPin"));

		auto Pin = AddParamPinImpl(Index, bTransaction);
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());

		return Pin;
	}
	else
	{
		auto Pin = AddParamPinImpl(Index, bTransaction);
		return Pin;
	}
}

UEdGraphPin* UK2Node_NotifyMessage::AddResponsePin(int32 Index, bool bTransaction)
{
	if (!IsAllowLatentFuncs())
		return nullptr;

	if (bTransaction)
	{
		FScopedTransaction Transaction(LOCTEXT("AddPinTx", "AddPin"));

		auto Pin = AddResponsePinImpl(Index, bTransaction);
		FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());

		return Pin;
	}
	else
	{
		auto Pin = AddResponsePinImpl(Index, bTransaction);
		return Pin;
	}
}

UEdGraphPin* UK2Node_NotifyMessage::AddParamPinImpl(int32 AdditionalPinIndex, bool bModify)
{
	FString InputPinName = GMPNotifyMessage::GetNameForPin(AdditionalPinIndex);
	UEdGraphPin* InputPin = nullptr;
	if (ensure(ParameterTypes.IsValidIndex(AdditionalPinIndex)))
	{
		InputPin = CreatePin(EGPD_Input, ParameterTypes[AdditionalPinIndex]->PinType, ToGraphPinNameType(InputPinName));
		if (InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
			InputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		InputPin->bDisplayAsMutableRef = false;
		InputPin->PinType.bIsReference = false;
		InputPin->PinFriendlyName = ParameterTypes[AdditionalPinIndex]->PinFriendlyName.ToString().IsEmpty() ? FText::FromString(InputPinName) : FText::FromName(ParameterTypes[AdditionalPinIndex]->PinFriendlyName);
		InputPin->AutogeneratedDefaultValue = ParameterTypes[AdditionalPinIndex]->PinDefaultValue;
		InputPin->DefaultValue = ParameterTypes[AdditionalPinIndex]->PinDefaultValue;
	}
	else
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		InputPin = CreatePin(EGPD_Input, PinType, ToGraphPinNameType(InputPinName));
		InputPin->bDisplayAsMutableRef = false;
		InputPin->PinType.bIsReference = false;
		InputPin->PinType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Wildcard;
		InputPin->PinFriendlyName = FText::FromString(InputPinName);
	}
	GetMessageCount() = AdditionalPinIndex + 1;
	if (bModify)
		Modify();
	return InputPin;
}

UEdGraphPin* UK2Node_NotifyMessage::AddResponsePinImpl(int32 AdditionalPinIndex, bool bModify)
{
	FString OutputPinName = GMPNotifyMessage::GetNameForRspPin(AdditionalPinIndex);
	UEdGraphPin* OutputPin = nullptr;
	if (ensure(ResponseTypes.IsValidIndex(AdditionalPinIndex)))
	{
		OutputPin = CreatePin(EGPD_Output, ResponseTypes[AdditionalPinIndex]->PinType, ToGraphPinNameType(OutputPinName));
		if (OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
			OutputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		OutputPin->bDisplayAsMutableRef = false;
		OutputPin->PinType.bIsReference = false;
		OutputPin->PinFriendlyName = ResponseTypes[AdditionalPinIndex]->PinFriendlyName.ToString().IsEmpty() ? FText::FromString(OutputPinName) : FText::FromName(ResponseTypes[AdditionalPinIndex]->PinFriendlyName);
		OutputPin->AutogeneratedDefaultValue = ResponseTypes[AdditionalPinIndex]->PinDefaultValue;
		OutputPin->DefaultValue = ResponseTypes[AdditionalPinIndex]->PinDefaultValue;
	}
	else
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		OutputPin = CreatePin(EGPD_Output, PinType, ToGraphPinNameType(OutputPinName));
		OutputPin->bDisplayAsMutableRef = true;
		OutputPin->PinType.bIsReference = true;
		OutputPin->PinFriendlyName = FText::FromString(OutputPinName);
		OutputPin->PinType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Wildcard;
	}
	if (bModify)
		Modify();
	return OutputPin;
}

#if GMP_NODE_DETAIL
void UK2Node_NotifyMessage::RemoveInputPin(UEdGraphPin* Pin)
{
	if (CanRemovePin(Pin))
	{
		FScopedTransaction Transaction(LOCTEXT("RemovePinTx", "RemovePin"));
		Modify();

		int32 PinRemovalIndex = INDEX_NONE;
		if (Pins.Find(Pin, /*out*/ PinRemovalIndex))
		{
			int32 NameIndex = 0;
			for (int32 PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
			{
				UEdGraphPin* LocalPin = Pins[PinIndex];
				if (LocalPin && ToString(LocalPin->PinName).Find(MessageParamPrefix) != INDEX_NONE && LocalPin->Direction == EGPD_Input && LocalPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && PinIndex != PinRemovalIndex)
				{
					FString InputPinName = GMPNotifyMessage::GetNameForPin(NameIndex);  // FIXME
					if (ToGraphPinNameType(InputPinName) != LocalPin->PinName)
					{
						LocalPin->Modify();
						LocalPin->PinName = ToGraphPinNameType(InputPinName);
						LocalPin->PinFriendlyName = FText::FromString(InputPinName);
					}
					NameIndex++;
				}
			}

			Pins.RemoveAt(PinRemovalIndex);
			Pin->MarkAsGarbage();
			--GetMessageCount();

			FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
		}
	}
}

bool UK2Node_NotifyMessage::CanRemovePin(const UEdGraphPin* Pin) const
{
	return (GetMessageCount() && Pin && Pin->Direction == EGPD_Input && (INDEX_NONE != Pins.IndexOfByKey(Pin)) && (ToString(Pin->PinName).Find(MessageParamPrefix) != INDEX_NONE));
}
#endif

void UK2Node_NotifyMessage::AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins)
{
	AdvancedPinDisplay = ENodeAdvancedPins::Hidden;
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
	auto Pin = CreatePin(EGPD_Input, PinType, UEdGraphSchema_K2::PN_Execute);
	Pin = CreatePin(EGPD_Output, PinType, UEdGraphSchema_K2::PN_Then);
	Super::AllocateMsgKeyTagPin();

	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	// PinType.PinSubCategory = UEdGraphSchema_K2::PSC_Self;
	PinType.PinSubCategoryObject = UObject::StaticClass();
	Pin = CreatePin(EGPD_Input, PinType, GMPNotifyMessage::Sender);
	Pin->bAdvancedView = true;
	Pin->bDefaultValueIsIgnored = true;

	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	Pin = CreatePin(EGPD_Input, PinType, GMPNotifyMessage::ExactObjName);
	static FString Str_None = FName(NAME_None).ToString();
	Pin->DefaultValue = Str_None;
	Pin->PinToolTip = TEXT("combine a special signal source with object and name");
	Pin->bAdvancedView = [InOldPins] {
		if (InOldPins)
		{
			for (auto Pin : *InOldPins)
			{
				if (Pin->GetFName() == GMPNotifyMessage::ExactObjName)
				{
					return Pin->DefaultValue.IsEmpty() || Pin->DefaultValue == Str_None;
				}
			}
		}
		return true;
	}();

	for (int32 i = 0; i < ParameterTypes.Num(); ++i)
	{
		AddParamPinImpl(i, false);
	}

	if (IsAllowLatentFuncs())
	{
		if (ResponseTypes.Num() > 0)
		{
			CreateResponseExecPin();
			for (int32 i = 0; i < ResponseTypes.Num(); ++i)
			{
				AddResponsePinImpl(i, false);
			}
		}
	}
	else
	{
	}

	// 	if (InOldPins)
	// 	{
	// 		for (auto OldPin : *InOldPins)
	// 		{
	// 			if (OldPin->Direction == EGPD_Input && !OldPin->HasAnyConnections())
	// 			{
	// 				if (auto NewPin = FindPin(OldPin->PinName))
	// 				{
	// 					NewPin->DefaultObject = OldPin->DefaultObject;
	// 					NewPin->DefaultTextValue = OldPin->DefaultTextValue;
	// 					NewPin->DefaultValue = OldPin->DefaultValue;
	// 				}
	// 			}
	// 		}
	// 	}
}

void UK2Node_NotifyMessage::Refresh()
{
	UEdGraph* Graph = GetGraph();
	Graph->NotifyGraphChanged();
}

FString UK2Node_NotifyMessage::GetTitleHead() const
{
	if (ResponseTypes.Num() > 0)
		return TEXT("RequestMessage");
	else
		return TEXT("NotifyMessage");
}

void UK2Node_NotifyMessage::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	Super::PinDefaultValueChanged(ChangedPin);
	if (bRecursivelyChangingDefaultValue)
	{
		return;
	}

	if (ChangedPin == GetEventNamePin())
	{
		CachedNodeTitle.SetCachedText(FText::FromString(GetMessageTitle()), this);
		GetGraph()->NotifyGraphChanged();
	}
	auto Index = GetPinIndex(ChangedPin);
	if (ParameterTypes.IsValidIndex(Index))
	{
		ParameterTypes[Index]->PinDefaultValue = ChangedPin->DefaultValue;
		ChangedPin->AutogeneratedDefaultValue = ChangedPin->DefaultValue;
		GetGraph()->NotifyGraphChanged();
	}
}

UEdGraphPin* UK2Node_NotifyMessage::GetPinByName(const FString& Index, TArray<UEdGraphPin*>* InPins, bool bEnsure) const
{
	UEdGraphPin* RetPin = nullptr;
	auto PinName = ToGraphPinNameType(Index);
	for (UEdGraphPin* Pin : InPins ? *InPins : Pins)
	{
		if (Pin->PinName == PinName)
		{
			RetPin = Pin;
			break;
		}
	}

	ensure(!bEnsure || RetPin);
	return RetPin;
}

void UK2Node_NotifyMessage::SetPinToolTip(UEdGraphPin& Pin, bool bModify) const
{
	if (Pin.PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
		return;

	if (bModify)
		Pin.Modify();

	if (Pin.LinkedTo.Num() && Pin.LinkedTo[0])
	{
		Pin.PinToolTip = UEdGraphSchema_K2::TypeToText(Pin.LinkedTo[0]->PinType).ToString();
		if (auto K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema()))
		{
			Pin.PinToolTip += TEXT("\n Connected To ");
			Pin.PinToolTip += K2Schema->GetPinDisplayName(Pin.LinkedTo[0]).ToString();
		}
	}
	else
	{
		Pin.PinToolTip = UEdGraphSchema_K2::TypeToText(Pin.PinType).ToString();
	}
}

void UK2Node_NotifyMessage::PinConnectionListChanged(UEdGraphPin* ChangedPin)
{
	Super::PinConnectionListChanged(ChangedPin);
	auto Index = GetPinIndex(ChangedPin);
	if (ChangedPin && !IsMessageSignatureRegistered() && Index != INDEX_NONE)
	{
		if (ChangedPin->LinkedTo.Num() > 0)
		{
			ChangedPin->PinType.bIsReference = true;
			if (ChangedPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
			{
				ChangedPin->PinType = ChangedPin->LinkedTo[0]->PinType;
				if (ParameterTypes.IsValidIndex(Index))
				{
					ParameterTypes[Index]->PinType = ChangedPin->LinkedTo[0]->PinType;
				}
			}
		}
		else
		{
			if (ParameterTypes.IsValidIndex(Index))
			{
				ChangedPin->PinType = ParameterTypes[Index]->PinType;
				if (ChangedPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
					ChangedPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
			}
			ChangedPin->PinType.bIsReference = false;
		}
	}
	GetGraph()->NotifyGraphChanged();
}

FNodeHandlingFunctor* UK2Node_NotifyMessage::CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const
{
	return nullptr;
}

bool UK2Node_NotifyMessage::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
			return true;
	}
	else if (MyPin == GetEventNamePin())
	{
		return true;
	}
	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

void UK2Node_NotifyMessage::FixupPinDefaultValues()
{
	Super::FixupPinDefaultValues();
}

void UK2Node_NotifyMessage::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	bool bIsErrorFree = true;
	auto OnRspExecPin = GetResponseExecPin();
	const bool bHasResponse = OnRspExecPin && OnRspExecPin->LinkedTo.Num() > 0;
	bool bAllValidated = true;
	for (int32 Index = 0; Index < ResponseTypes.Num(); ++Index)
	{
		bAllValidated &= ensure(ResponseTypes[Index]->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard);
	}

	UFunction* NotifyMessageFunc = (UE_4_25_OR_LATER) ? GMP_UFUNCTION_CHECKED(UGMPBPLib, NotifyMessageByKeyVariadic) : GMP_UFUNCTION_CHECKED(UGMPBPLib, NotifyMessageByKey);
	UFunction* RequestMessageFunc = (UE_4_25_OR_LATER) ? GMP_UFUNCTION_CHECKED(UGMPBPLib, RequestMessageVariadic) : GMP_UFUNCTION_CHECKED(UGMPBPLib, RequestMessage);

	UK2Node_CallFunction* InvokeMessageNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	InvokeMessageNode->SetFromFunction(bHasResponse ? RequestMessageFunc : NotifyMessageFunc);
	InvokeMessageNode->AllocateDefaultPins();

	UK2Node_MakeArray* MakeArrayNode = nullptr;
	UEdGraphPin* PinParams = InvokeMessageNode->FindPin(TEXT("Params"));
	if (PinParams)
	{
		MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
		MakeArrayNode->NumInputs = GetMessageCount();
		MakeArrayNode->AllocateDefaultPins();
		CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeArrayNode, this);
	}

	auto ExecutePin = GetExecPin();

	bIsErrorFree &= ExpandMessageCall(CompilerContext, SourceGraph, ParameterTypes, MakeArrayNode, InvokeMessageNode);

	{
		// FGMPObjNamePair Sender, const FString& MessageId, const TArray<FGMPTypedAddr>& Params
		// PinCategory PinSubCategoryObject

		{
			UEdGraphPin* PinSender = InvokeMessageNode->FindPinChecked(GMPNotifyMessage::Sender);
			UK2Node_CallFunction* MakeObjNamePairNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			MakeObjNamePairNode->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeObjNamePair));
			MakeObjNamePairNode->AllocateDefaultPins();
			bIsErrorFree &= TryCreateConnection(CompilerContext, MakeObjNamePairNode->GetReturnValuePin(), PinSender);
			if (auto SenderPin = FindPinChecked(GMPNotifyMessage::Sender))
			{
				if (SenderPin->LinkedTo.Num() == 1)
				{
					bIsErrorFree &= TryCreateConnection(CompilerContext, SenderPin, MakeObjNamePairNode->FindPinChecked(TEXT("InObj")));
				}
				else
				{
					UK2Node_Self* SelfNode = CompilerContext.SpawnIntermediateNode<UK2Node_Self>(this, SourceGraph);
					SelfNode->AllocateDefaultPins();
					bIsErrorFree &= TryCreateConnection(CompilerContext, SelfNode->FindPinChecked(UEdGraphSchema_K2::PN_Self), MakeObjNamePairNode->FindPinChecked(TEXT("InObj")));
				}
			}
			if (auto TagNamePin = FindPin(GMPNotifyMessage::ExactObjName))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, TagNamePin, MakeObjNamePairNode->FindPinChecked(TEXT("InName")));
			}
		}

		if (UEdGraphPin* PinMessageId = InvokeMessageNode->FindPinChecked(TEXT("MessageId")))
		{
			PinMessageId->DefaultValue = GetMessageKey();
			// bIsErrorFree &= TryCreateConnection(CompilerContext, *FindPinChecked(MessageKeyName), *PinMessageId);
		}

		if (UEdGraphPin* PinMgr = InvokeMessageNode->FindPin(TEXT("Mgr")))
		{
			if (auto MgrData = FindPin(TEXT("Mgr")))
				bIsErrorFree &= TryCreateConnection(CompilerContext, MgrData, PinMgr, false);
		}

		if (MakeArrayNode)
		{
			UEdGraphPin* MakeArrayOut = MakeArrayNode->GetOutputPin();
			check(MakeArrayOut);
			bIsErrorFree &= TryCreateConnection(CompilerContext, MakeArrayOut, PinParams);
		}

		if (ExecutePin == GetExecPin())
		{
			bIsErrorFree &= TryCreateConnection(CompilerContext, ExecutePin, InvokeMessageNode->GetExecPin());
		}
		else
		{
			bIsErrorFree &= TryCreateConnection(CompilerContext, ExecutePin, InvokeMessageNode->GetExecPin());
		}
		bIsErrorFree &= TryCreateConnection(CompilerContext, FindPinChecked(UEdGraphSchema_K2::PN_Then), InvokeMessageNode->GetThenPin());
	}

	if (auto TypePin = InvokeMessageNode->FindPin(TEXT("Type")))
	{
		TypePin->DefaultValue = LexToString((uint8)AuthorityType);
	}

	if (bAllowLatentFuncs && bHasResponse)
	{
		auto EventNamePin = InvokeMessageNode->FindPin(TEXT("EventName"), EGPD_Input);
		auto RspKeyPin = InvokeMessageNode->FindPin(TEXT("RspKey"), EGPD_Output);

		UK2Node_CustomEvent* CustomEventNode = CompilerContext.SpawnIntermediateNode<UK2Node_CustomEvent>(this, SourceGraph);
		FString EventNodeName;
		{
			int32 NodeIndex = 0;
			TArray<UK2Node_CustomEvent*> Nodes;
			SourceGraph->GetNodesOfClass(Nodes);
			do
			{
				EventNodeName = FString::Printf(TEXT("OnRsp[%s][%d]%d"), *GetMessageKey(), ResponseTypes.Num(), ++NodeIndex);

			} while (Nodes.FindByPredicate([&](UK2Node_CustomEvent* Node) { return Node->CustomFunctionName.ToString() == EventNodeName; }));
		}

		CustomEventNode->CustomFunctionName = FName(*EventNodeName);
		CustomEventNode->AllocateDefaultPins();
		EventNamePin->DefaultValue = EventNodeName;

		if (bAllValidated)
		{
			auto CustomEventThenPin = CustomEventNode->FindPinChecked(UEdGraphSchema_K2::PN_Then);
			bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, CustomEventThenPin, {OnRspExecPin});

			for (int32 Index = 0; Index < ResponseTypes.Num(); ++Index)
			{
				auto OutputPin = GetResponsePin(Index);
				UEdGraphPin* EventParamPin = nullptr;
				auto EnumPtr = Cast<UEnum>(OutputPin->PinType.PinSubCategoryObject.Get());
				if (EnumPtr && EnumPtr->GetCppForm() != UEnum::ECppForm::EnumClass && OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
				{
					const bool bIsFromByte = (ParameterTypes[Index]->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || ParameterTypes[Index]->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte);
					const auto LiteralFunc = bIsFromByte ? GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralByte) : GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralByte);

					FEdGraphPinType ThisPinType;
					ThisPinType.PinCategory = bIsFromByte ? UEdGraphSchema_K2::PC_Byte : UEdGraphSchema_K2::PC_Int;
					EventParamPin = CustomEventNode->CreateUserDefinedPin(MakeParameterName(Index), ThisPinType, EGPD_Output, false);

					if (IsRunningCommandlet() && !OutputPin->LinkedTo.Num())
						continue;

					auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);

					NodeMakeLiteral->SetFromFunction(LiteralFunc);
					NodeMakeLiteral->AllocateDefaultPins();
					auto GenericValuePin = NodeMakeLiteral->FindPinChecked(TEXT("Value"));
					bIsErrorFree &= TryCreateConnection(CompilerContext, GenericValuePin, EventParamPin);
					NodeMakeLiteral->NotifyPinConnectionListChanged(GenericValuePin);

					EventParamPin = NodeMakeLiteral->GetReturnValuePin();
					EventParamPin->PinType = OutputPin->PinType;
				}
				else
				{
					EventParamPin = CustomEventNode->CreateUserDefinedPin(MakeParameterName(Index), OutputPin->PinType, EGPD_Output, false);
					if (IsRunningCommandlet() && !OutputPin->LinkedTo.Num())
						continue;
				}
			}
		}
	}

	// Break all links to the Select node so it goes away for at scheduling time
	OnNodeExpanded(CompilerContext, SourceGraph, InvokeMessageNode);
	BreakAllNodeLinks();
}

FSlateIcon UK2Node_NotifyMessage::GetIconAndTint(FLinearColor& OutColor) const
{
	OutColor = GetNodeTitleColor();
	static FSlateIcon Icon("EditorStyle", "Kismet.AllClasses.FunctionIcon");
	return Icon;
}

void UK2Node_NotifyMessage::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	if (FindPinChecked(UEdGraphSchema_K2::PN_Execute)->LinkedTo.Num())
	{
		if (auto Pin = GetEventNamePin())
		{
			if (!Pin->LinkedTo.Num() && GetMessageKey().IsEmpty())
			{
				MessageLog.Error(TEXT("Error Name Null for Pin: @@"), Pin);
				return;
			}
		}

		for (int32 Index = 0; Index < GetMessageCount(); ++Index)
		{
			auto InputPin = GetPinByName(GMPNotifyMessage::GetNameForPin(Index));
			if (ensure(InputPin))
			{
				UEdGraphPin* LinkPin = InputPin;
				while (LinkPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				{
					if (!LinkPin->LinkedTo.Num())
					{
						MessageLog.Error(TEXT("Pin Error : @@"), InputPin);
						return;
					}
					LinkPin = LinkPin->LinkedTo[0];
				}
			}
		}

		auto OnRspPin = GetResponseExecPin();
		if (OnRspPin)
		{
			if (!IsAllowLatentFuncs())
			{
				MessageLog.Error(TEXT("Does not support Latent : @@"), OnRspPin);
				return;
			}
			if (!OnRspPin->LinkedTo.Num())
			{
				MessageLog.Warning(TEXT("OnRsponse should has comsumed : @@"), OnRspPin);
				return;
			}

			for (int32 idx = 0; idx < ResponseTypes.Num(); ++idx)
			{
				auto OutputPin = GetOutputPinByIndex(idx);
				if (ensure(OutputPin))
				{
					UEdGraphPin* LinkPin = OutputPin;
					while (LinkPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
					{
						if (!LinkPin->LinkedTo.Num())
						{
							MessageLog.Error(TEXT("Pin Error : @@"), OutputPin);
							return;
						}
						LinkPin = LinkPin->LinkedTo[0];
					}
				}
			}
		}
	}
}
#undef LOCTEXT_NAMESPACE
