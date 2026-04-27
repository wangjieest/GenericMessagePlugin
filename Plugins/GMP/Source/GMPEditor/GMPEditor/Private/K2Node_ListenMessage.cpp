//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "K2Node_ListenMessage.h"

#include "../Private/SMessageTagGraphPin.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "DataTableEditorUtils.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "EditorCategoryUtils.h"
#include "Engine/Blueprint.h"
#include "Engine/DataTable.h"
#include "Engine/World.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "GraphEditorSettings.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Knot.h"
#include "K2Node_MakeArray.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_VariableSetRef.h"
#include "Kismet/DataTableFunctionLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/StructureEditorUtils.h"
#include "Kismet/GameplayStatics.h"
#include "KismetCompiler.h"
#include "KismetNodes/KismetNodeInfoContext.h"
#include "PropertyCustomizationHelpers.h"
#include "SNodePanel.h"
#include "ScopedTransaction.h"
#include "Shared/GMPCore.h"
#include "GMP/GMPReflection.h"
#include "EdGraph/EdGraphPin.h"
#include "Misc/MessageDialog.h"
#include "UnrealCompatibility.h"
#include "K2Node_AssignmentStatement.h"

#define LOCTEXT_NAMESPACE "GMPListenMessage"

// =====================================================================
// UK2Node_GMPDerefParam implementation + FKCHandler
// =====================================================================

FName UK2Node_GMPDerefParam::MsgArrayPinName = TEXT("MsgArray");
FName UK2Node_GMPDerefParam::OutItemPinName = TEXT("OutItem");

void UK2Node_GMPDerefParam::AllocateDefaultPins()
{
	// Input: MsgArray (TArray<FGMPTypedAddr>&, hidden — connected by ExpandNode)
	FEdGraphPinType ArrType;
	ArrType.PinCategory = UEdGraphSchema_K2::PC_Struct;
	ArrType.PinSubCategoryObject = FGMPTypedAddr::StaticStruct();
	ArrType.ContainerType = EPinContainerType::Array;
	ArrType.bIsReference = true;
	auto* ArrPin = CreatePin(EGPD_Input, ArrType, MsgArrayPinName);
	ArrPin->bHidden = true;

	// Output: OutItem (typed by OutputPinType, set by ExpandNode)
	auto* OutPin = CreatePin(EGPD_Output, OutputPinType, OutItemPinName);
	OutPin->PinType.bIsReference = true;
}

FText UK2Node_GMPDerefParam::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return FText::Format(LOCTEXT("GMPDerefTitle", "Deref Param [{0}]"), FText::AsNumber(ParamIndex));
}

// FKCHandler that uses InlineGeneratedParameter for zero-copy access.
// Pattern follows BlueprintOutputReference: creates a PtrTerm (int64 local)
// and a RefTerm (InlineGenerated) that calls GMPDerefPtr to dereference on demand.
class FKCHandler_GMPDerefParam : public FNodeHandlingFunctor
{
public:
	FKCHandler_GMPDerefParam(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
	}

	virtual void RegisterNets(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		auto* DerefNode = CastChecked<UK2Node_GMPDerefParam>(Node);

		// Register input pin (MsgArray) normally
		for (auto* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Input)
			{
				// Let the default handler register input pins as normal terms
				if (Pin->LinkedTo.Num() > 0)
				{
					// Input is connected — its term is already registered by the source node
				}
				else
				{
					FNodeHandlingFunctor::RegisterNet(Context, Pin);
				}
			}
		}

		// For the output pin: create InlineGenerated term (no local variable)
		UEdGraphPin* OutPin = DerefNode->GetOutItemPin();
		if (OutPin->LinkedTo.Num() == 0)
			return;  // Not connected — skip

		// 1. Create PtrTerm: a real int64 local variable that holds the pointer value
		//    (GMPGetParamPtr writes the address here)
		FBPTerminal* PtrTerm = new FBPTerminal();
		Context.Locals.Add(PtrTerm);
		FString PtrName = Context.NetNameMap->MakeValidName(OutPin) + TEXT("_Ptr");
		PtrTerm->Name = PtrName;
		PtrTerm->Type.PinCategory = UEdGraphSchema_K2::PC_Int64;
		PtrTerm->Source = Node;
		PtrTerm->SourcePin = OutPin;

		// 2. Create RefTerm: an InlineGenerated term for the output pin
		//    (consumers of this pin will trigger the inline DereferencePtr)
		FBPTerminal* RefTerm = new FBPTerminal();
		Context.InlineGeneratedValues.Add(RefTerm);
		RefTerm->CopyFromPin(OutPin, Context.NetNameMap->MakeValidName(OutPin));
		Context.NetMap.Add(OutPin, RefTerm);

		// Store for Compile phase
		PtrTermMap.Add(Node, {PtrTerm, RefTerm});
	}

	virtual void Compile(FKismetFunctionContext& Context, UEdGraphNode* Node) override
	{
		auto* DerefNode = CastChecked<UK2Node_GMPDerefParam>(Node);

		auto* TermPair = PtrTermMap.Find(Node);
		if (!TermPair)
			return;  // Output not connected

		FBPTerminal* PtrTerm = TermPair->PtrTerm;
		FBPTerminal* RefTerm = TermPair->RefTerm;

		// Find the MsgArray input term (registered by the CustomEvent node's handler)
		UEdGraphPin* MsgArrayPin = DerefNode->GetMsgArrayPin();
		FBPTerminal** MsgArrayTermPtr = nullptr;
		if (MsgArrayPin->LinkedTo.Num() > 0)
		{
			MsgArrayTermPtr = Context.NetMap.Find(MsgArrayPin->LinkedTo[0]);
		}
		if (!MsgArrayTermPtr)
		{
			CompilerContext.MessageLog.Error(TEXT("MsgArray not connected for @@"), Node);
			return;
		}

		// Statement 1: PtrTerm = GMPGetParamPtr(MsgArray, ParamIndex)
		// This extracts the raw pointer from MsgArray[Index].Value into an int64 local
		UFunction* GetPtrFunc = UGMPBPLib::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UGMPBPLib, GMPGetParamPtr));
		check(GetPtrFunc);

		FBlueprintCompiledStatement& GetPtrStmt = Context.AppendStatementForNode(Node);
		GetPtrStmt.Type = KCST_CallFunction;
		GetPtrStmt.FunctionToCall = GetPtrFunc;
		GetPtrStmt.LHS = PtrTerm;
		GetPtrStmt.RHS.Add(*MsgArrayTermPtr);

		// Create a literal term for the index
		FBPTerminal* IndexTerm = new FBPTerminal();
		Context.Literals.Add(IndexTerm);
		IndexTerm->bIsLiteral = true;
		IndexTerm->Name = FString::Printf(TEXT("%d"), DerefNode->ParamIndex);
		IndexTerm->Type.PinCategory = UEdGraphSchema_K2::PC_Int;
		GetPtrStmt.RHS.Add(IndexTerm);

		// Statement 2 (inline): RefTerm = GMPDerefPtr(PtrTerm)
		// This is NOT appended to the node's statement list — it's an InlineGeneratedParameter
		// that executes only when something reads from RefTerm.
		UFunction* DerefPtrFunc = UGMPBPLib::StaticClass()->FindFunctionByName(
			GET_FUNCTION_NAME_CHECKED(UGMPBPLib, GMPDerefPtr));
		check(DerefPtrFunc);

		FBlueprintCompiledStatement* DerefStmt = new FBlueprintCompiledStatement();
		Context.AllGeneratedStatements.Add(DerefStmt);
		DerefStmt->Type = KCST_CallFunction;
		DerefStmt->FunctionToCall = DerefPtrFunc;
		DerefStmt->RHS.Add(PtrTerm);

		// Hook: when the VM needs RefTerm's value, it executes DerefStmt first,
		// which calls GMPDerefPtr → sets MostRecentPropertyAddress to the original data.
		// No copy — the VM uses that address directly.
		RefTerm->InlineGeneratedParameter = DerefStmt;
	}

private:
	struct FTermPair
	{
		FBPTerminal* PtrTerm;
		FBPTerminal* RefTerm;
	};
	TMap<UEdGraphNode*, FTermPair> PtrTermMap;
};

FNodeHandlingFunctor* UK2Node_GMPDerefParam::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_GMPDerefParam(CompilerContext);
}

// =====================================================================

namespace GMPListenMessage
{
const FGraphPinNameType EventName = TEXT("EventName");
const FGraphPinNameType TimesName = TEXT("Times");
const FGraphPinNameType OrderName = TEXT("Order");
const FGraphPinNameType ExactObjName = TEXT("ExactObjName");
const FGraphPinNameType OnMessageName = TEXT("OnMessage");
const FGraphPinNameType DelegateName = TEXT("Delegate");
const FGraphPinNameType OutputDelegateName = TEXT("OutputDelegate");
const FGraphPinNameType OutEventName = TEXT("OutEventName");
const FGraphPinNameType MessageIdName = TEXT("MessageId");
const FGraphPinNameType MessageSeqName = TEXT("SeqId");
const FGraphPinNameType SenderName = TEXT("SenderObj");
const FGraphPinNameType MsgArrayName = TEXT("MsgArray");
const FGraphPinNameType ParamsName = TEXT("Params");
const FGraphPinNameType UnlistenName = TEXT("StopListen");
const FGraphPinNameType AuthorityType = TEXT("Type");
const FGraphPinNameType ArgNames = TEXT("ArgNames");
const FGraphPinNameType CallbackEventName = TEXT("Callback");
const FGraphPinNameType ResponseExecName = TEXT("Response");

FString GetNameForMsgPin(int32 Index)
{
	return FString::Printf(TEXT("%s%d"), *UK2Node_MessageBase::MessageParamPrefix, Index);
}
FString GetNameForRspPin(int32 Index)
{
	return FString::Printf(TEXT("%s%d"), *UK2Node_MessageBase::MessageResponsePrefix, Index);
}
}  // namespace GMPListenMessage

#if WITH_EDITOR
TSharedPtr<class SGraphNode> UK2Node_ListenMessage::CreateVisualWidget()
{
	class SGraphNodeListenMessage : public SGraphNodeMessageBase
	{
	public:
		SLATE_BEGIN_ARGS(SGraphNodeListenMessage) {}
		SLATE_END_ARGS()

		TWeakObjectPtr<UK2Node_ListenMessage> Node;
		UEdGraphPin* WatchedPin = nullptr;
		UEdGraphPin* FilterPin = nullptr;

		bool bIsSelectedExclusively = false;
		void Construct(const FArguments& InArgs, UK2Node_ListenMessage* InNode, UEdGraphPin* InWatchedPin, UEdGraphPin* InFilterPin = nullptr)
		{
			Node = InNode;
			WatchedPin = InWatchedPin;
			FilterPin = InFilterPin;
			this->SetCursor(EMouseCursor::CardinalCross);
			SGraphNodeMessageBase::Construct({}, InNode);
		}

		virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override
		{
			auto PinObj = PinToAdd->GetPinObj();
			auto PinName = PinObj->GetFName();
			if (PinObj->Direction == EGPD_Input && ToGraphPinNameType(PinName) == GMPListenMessage::CallbackEventName)
			{
				PinToAdd->SetOwner(SharedThis(this));
				static auto GetEventName = [](UK2Node_ListenMessage* ListenNode) { return FString::Printf(TEXT("OnMsg.%s"), *ListenNode->GetMessageKey()); };
				static auto GetEventNode = [](UK2Node_ListenMessage* ListenNode) {
					UK2Node_CustomEvent* TargetNode = nullptr;
					do
					{
						auto Pin = ListenNode->FindPinChecked(GMPListenMessage::CallbackEventName);
						auto BP = ListenNode->GetBlueprint();
						if (ListenNode->GetMessageKey().IsEmpty())
							break;

						const FName EvtName = *Pin->DefaultValue;

						for (auto UberPage = 0; UberPage < BP->UbergraphPages.Num(); ++UberPage)
						{
							auto EventGraph = BP->UbergraphPages[UberPage];
							TArray<UK2Node_CustomEvent*> Nodes;
							EventGraph->GetNodesOfClass(Nodes);
							auto Idx = Nodes.IndexOfByPredicate([&](auto EvtNode) { return (EvtNode->CustomFunctionName == EvtName); });
							if (INDEX_NONE != Idx)
							{
								TargetNode = Nodes[Idx];
								break;
							}
						}
					} while (false);
					return TargetNode;
				};
				static auto MakeEventNode = [](UK2Node_ListenMessage* ListenNode) {
					UK2Node_CustomEvent* NewEventNode = nullptr;
					do
					{
						auto Pin = ListenNode->FindPinChecked(GMPListenMessage::CallbackEventName);
						Pin->DefaultValue = GetEventName(ListenNode);
						const FName EvtName = *Pin->DefaultValue;

						auto BP = ListenNode->GetBlueprint();
						if (ensure(FBlueprintEditorUtils::DoesSupportEventGraphs(BP)))
						{
							auto EventGraph = BP->UbergraphPages[0];
							const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();

							// Add the event
							NewEventNode = NewObject<UK2Node_CustomEvent>(EventGraph);
							NewEventNode->CustomFunctionName = EvtName;

							NewEventNode->CreateNewGuid();
							NewEventNode->PostPlacedNewNode();
							NewEventNode->SetFlags(RF_Transactional);
							NewEventNode->bInternalEvent = true;
							NewEventNode->bIsEditable = false;
							UEdGraphSchema_K2::SetNodeMetaData(NewEventNode, FNodeMetadata::DefaultGraphNode);
							NewEventNode->AllocateDefaultPins();
							EventGraph->AddNode(NewEventNode);

							for (int32 Index = 0; Index < ListenNode->ParameterTypes.Num(); ++Index)
							{
								auto PinInfo = ListenNode->ParameterTypes[Index].Info;
								auto PinType = PinInfo->PinType;
								PinType.bIsReference = false;

								static TSet<FName> AllowRefSets = {
									UEdGraphSchema_K2::PC_Struct,
									//UEdGraphSchema_K2::PC_String,
									//UEdGraphSchema_K2::PC_Name,
									//UEdGraphSchema_K2::PC_Text,
									//UEdGraphSchema_K2::PC_Class,
									//UEdGraphSchema_K2::PC_Object,
									//UEdGraphSchema_K2::PC_SoftObject,
									//UEdGraphSchema_K2::PC_SoftClass,
								};
								if (PinType.ContainerType != EPinContainerType::None || AllowRefSets.Contains(PinType.PinCategory))
									PinType.bIsReference = true;

								NewEventNode->CreateUserDefinedPin(PinInfo->PinFriendlyName, PinType, EGPD_Output);
							}
							NewEventNode->GetGraph()->NotifyNodeChanged(NewEventNode);
						}
					} while (false);
					return NewEventNode;
				};
				auto IsEditingEnabledAttr = TAttribute<bool>(PinToAdd, &SGraphPin::IsEditingEnabled);
				auto ClickLambda = CreateWeakLambda(Node.Get(), [ListenNode{Node.Get()}, IsEditingEnabledAttr] {
					if (UK2Node_CustomEvent* TargetNode = GetEventNode(ListenNode))
					{
						FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(TargetNode);
					}
					else if (IsEditingEnabledAttr.Get(false))
					{
						if (FMessageDialog::Open(EAppMsgType::YesNo, LOCTEXT("AutoCreateEventNode?", "Function cannot contains event node, create it in UberGraph?")) == EAppReturnType::Yes)
						{
							if (UK2Node_CustomEvent* NewEventNode = MakeEventNode(ListenNode))
								FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(NewEventNode);
						}
					}
				});
				auto GotoCallbackEventBtn = PropertyCustomizationHelpers::MakeBrowseButton(ClickLambda);
				GotoCallbackEventBtn->SetToolTipText(LOCTEXT("GotoCallbackEvent", "GotoCallbackEvent"));
				GotoCallbackEventBtn->SetCursor(EMouseCursor::Default);
				GotoCallbackEventBtn->SetVisibility(TAttribute<EVisibility>::Create(CreateWeakLambda(Node.Get(), [ListenNode{Node.Get()}] {
					auto Pin = ListenNode->FindPin(GMPListenMessage::CallbackEventName);
					return (ListenNode->GetMessageKey().IsEmpty() || !Pin || Pin->HasAnyConnections()) ? EVisibility::Hidden : EVisibility::Visible;
				})));

				LeftNodeBox
				->AddSlot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				.Padding(GetDefault<UGraphEditorSettings>()->GetInputPinPadding())
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						PinToAdd
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					.HAlign(HAlign_Center)
					[
						GotoCallbackEventBtn
					]
				];

				OutputPins.Add(PinToAdd);
				return;
			}
			else
			{
				// Show writeback checkbox when WritebackMode is Auto
				do
				{
					if (PinObj->Direction != EGPD_Output || !Node->IsAllowLatentFuncs() || !PinName.ToString().StartsWith(UK2Node_MessageBase::MessageParamPrefix))
						break;
					if (Node->WritebackMode != EGMPWritebackMode::Auto)
						break;

					TSharedPtr<SCheckBox> CheckBox;
					PinToAdd->SetOwner(SharedThis(this));

					RightNodeBox->AddSlot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						.VAlign(VAlign_Center)
						.Padding(GetDefault<UGraphEditorSettings>()->GetOutputPinPadding())
					[
						SNew(SHorizontalBox)
						.Visibility(this, &SGraphNodeListenMessage::GetOutputPinVisibility, PinObj)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.Padding(2, 0)
						.VAlign(VAlign_Center)
						.HAlign(HAlign_Center)
						[
							SAssignNew(CheckBox, SCheckBox)
							.Style(FAppStyle::Get(), "Graph.Checkbox")
							.IsChecked(this, &SGraphNodeListenMessage::IsDefaultValueChecked, PinObj)
							.OnCheckStateChanged(this, &SGraphNodeListenMessage::OnDefaultValueCheckBoxChanged, PinObj)
							.IsEnabled(TAttribute<bool>(PinToAdd, &SGraphPin::IsEditingEnabled))
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							PinToAdd
						]
					];

					CheckBox->SetCursor(EMouseCursor::Default);
					CheckBox->SetToolTipText(FText::FromString(TEXT("Mark this parameter for writeback to the notify caller")));
					OutputPins.Add(PinToAdd);
					return;
				} while (0);
				SGraphNodeMessageBase::AddPin(PinToAdd);
			}
		}
		EVisibility GetOutputPinVisibility(UEdGraphPin* PinObj) const
		{
			bool bHideAdvancedPin = false;
			auto ListenNode = CastChecked<UK2Node_ListenMessage>(GraphNode);

			if (OwnerGraphPanelPtr.IsValid())
				bHideAdvancedPin = (ENodeAdvancedPins::Hidden == ListenNode->AdvancedPinDisplay);
			const bool bIsAdvancedPin = PinObj && !PinObj->IsPendingKill() && PinObj->bAdvancedView && !PinObj->bOrphanedPin;
			const bool bCanBeHidden = !PinObj->LinkedTo.Num() && !ListenNode->WritebackPins.Contains(PinObj->GetFName());
			return (bIsAdvancedPin && bHideAdvancedPin && bCanBeHidden) ? EVisibility::Collapsed : EVisibility::Visible;
		}
		ECheckBoxState IsDefaultValueChecked(UEdGraphPin* PinObj) const
		{
			auto ListenNode = CastChecked<UK2Node_ListenMessage>(GraphNode);
			return ListenNode->WritebackPins.Contains(PinObj->GetFName()) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
		void OnDefaultValueCheckBoxChanged(ECheckBoxState InIsChecked, UEdGraphPin* PinObj)
		{
			if (auto ListenNode = CastChecked<UK2Node_ListenMessage>(GraphNode))
			{
				bool bChecked = InIsChecked == ECheckBoxState::Checked;
				if (!bChecked)
					ListenNode->WritebackPins.Remove(PinObj->GetFName());
				else
					ListenNode->WritebackPins.Add(PinObj->GetFName());
				FBlueprintEditorUtils::MarkBlueprintAsModified(ListenNode->GetBlueprint());
			}
		}

		virtual void GetNodeInfoPopups(FNodeInfoContext* Context, TArray<FGraphInformationPopupInfo>& Popups) const override
		{
			SGraphNodeMessageBase::GetNodeInfoPopups(Context, Popups);

			FKismetNodeInfoContext* K2Context = (FKismetNodeInfoContext*)Context;
			UObject* ActiveObject = K2Context->ActiveObjectBeingDebugged;
			UWorld* World = ActiveObject ? ActiveObject->GetWorld() : nullptr;
			if (!IsValid(World) || !World->IsGameWorld() || !Node.IsValid())
				return;

			UObject* HandlerObj = ActiveObject;
			do
			{
				if (!UBlueprintGeneratedClass::UsePersistentUberGraphFrame() || !WatchedPin)
					break;

				auto BGClass = Cast<UBlueprintGeneratedClass>(K2Context->SourceBlueprint->GeneratedClass);
				if (!BGClass || !ensure(ActiveObject->IsA(BGClass)))
					break;

				auto* Prop = CastField<FObjectProperty>(FKismetDebugUtilities::FindClassPropertyForPin(K2Context->SourceBlueprint, WatchedPin));
				if (!Prop)
					break;

				if (ActiveObject && BGClass->UberGraphFramePointerProperty)
				{
					FPointerToUberGraphFrame* PointerToUberGraphFrame = BGClass->UberGraphFramePointerProperty->ContainerPtrToValuePtr<FPointerToUberGraphFrame>(ActiveObject);
					check(PointerToUberGraphFrame);
					HandlerObj = Prop->GetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(PointerToUberGraphFrame->RawPointer));
				}
			} while (false);

			TArray<FString> Arr;
			bool bActive = GMP::FMessageUtils::GetMessageHub()->GetCallInfos(HandlerObj, Node->MsgTag.GetTagName(), Arr);
			static const FString Listening(TEXT("Listening"));
			static const FString Stopped(TEXT("Stopped"));
			new (Popups) FGraphInformationPopupInfo(nullptr, bActive ? FLinearColor::Blue : FLinearColor::Gray, bActive ? Listening : Stopped);

			const auto Limitation = 10;
			auto StartIdx = 0;
			if (Arr.Num() >= Limitation)
			{
				Arr.Add(TEXT("..."));
				StartIdx = Arr.Num() - Limitation;
			}

			for (int32 i = StartIdx; i < Arr.Num(); ++i)
				new (Popups) FGraphInformationPopupInfo(nullptr, Arr[i][0] == TEXT('+') ? FLinearColor::Blue : FLinearColor::Gray, Arr[i]);
		}
	};

	return SNew(SGraphNodeListenMessage, this, FindPinChecked(UK2Node_MessageBase::GetFNameWatchedObj()));
}
#endif

UK2Node_ListenMessage::UK2Node_ListenMessage()
{
	NodeTooltip = LOCTEXT("ListenMessage", "ListenMessage From Blueprint And C++");
}

UEdGraphPin* UK2Node_ListenMessage::AddMessagePin(int32 Index, bool bTransaction)
{
	if (!IsAllowLatentFuncs())
		return nullptr;

	if (bTransaction)
	{
		FScopedTransaction Transaction(LOCTEXT("AddMessagePinTx", "AddMessagePin"));
		Modify();

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

UEdGraphPin* UK2Node_ListenMessage::AddResponsePin(int32 Index, bool bTransaction /*= true*/)
{
	if (!IsAllowLatentFuncs())
		return nullptr;

	if (bTransaction)
	{
		FScopedTransaction Transaction(LOCTEXT("AddResponsePinTx", "AddResponsePin"));
		Modify();

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

FName UK2Node_ListenMessage::GetMessageSignature() const
{
	if (auto Pin = GetEventNamePin())
	{
		return *Pin->DefaultValue;
	}
	return NAME_None;
}

UEdGraphPin* UK2Node_ListenMessage::GetMessagePin(int32 Index, TArray<UEdGraphPin*>* InPins, bool bEnsure) const
{
	return GetPinByName(GMPListenMessage::GetNameForMsgPin(Index), InPins, bEnsure ? EGPD_Output : EGPD_MAX);
}

UEdGraphPin* UK2Node_ListenMessage::GetResponsePin(int32 Index, TArray<UEdGraphPin*>* InPins, bool bEnsure) const
{
	return GetPinByName(GMPListenMessage::GetNameForRspPin(Index), InPins, bEnsure ? EGPD_Input : EGPD_MAX);
}

UEdGraphPin* UK2Node_ListenMessage::CreateResponseExecPin()
{
	FEdGraphPinType PinType;
	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
	return CreatePin(EGPD_Input, PinType, GMPListenMessage::ResponseExecName);
}

UEdGraphPin* UK2Node_ListenMessage::GetResponseExecPin() const
{
	return FindPin(GMPListenMessage::ResponseExecName, EGPD_Input);
}

UEdGraphPin* UK2Node_ListenMessage::AddParamPinImpl(int32 AdditionalPinIndex, bool bModify)
{
	auto OutputPinName = GMPListenMessage::GetNameForMsgPin(AdditionalPinIndex);
	UEdGraphPin* OutputPin = nullptr;
	if (ParameterTypes.IsValidIndex(AdditionalPinIndex))
	{
		OutputPin = CreatePin(EGPD_Output, ParameterTypes[AdditionalPinIndex]->PinType, ToGraphPinNameType(OutputPinName));
		if (OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
			OutputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		OutputPin->PinType.bIsReference = false;
		OutputPin->bDisplayAsMutableRef = false;
		OutputPin->PinFriendlyName = ParameterTypes[AdditionalPinIndex]->PinFriendlyName.ToString().IsEmpty() ? FText::FromString(OutputPinName) : FText::FromName(ParameterTypes[AdditionalPinIndex]->PinFriendlyName);
		OutputPin->DefaultValue = ParameterTypes[AdditionalPinIndex]->PinDefaultValue;
		OutputPin->AutogeneratedDefaultValue = ParameterTypes[AdditionalPinIndex]->PinDefaultValue;
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
	GetMessageCount() = AdditionalPinIndex + 1;
	if (bModify)
		Modify();
	return OutputPin;
}

UEdGraphPin* UK2Node_ListenMessage::AddResponsePinImpl(int32 AdditionalPinIndex, bool bModify)
{
	auto InputPinName = GMPListenMessage::GetNameForRspPin(AdditionalPinIndex);
	UEdGraphPin* InputPin = nullptr;
	if (ResponseTypes.IsValidIndex(AdditionalPinIndex))
	{
		InputPin = CreatePin(EGPD_Input, ResponseTypes[AdditionalPinIndex]->PinType, ToGraphPinNameType(InputPinName));
		if (InputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
			InputPin->PinType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		InputPin->PinType.bIsReference = false;
		InputPin->bDisplayAsMutableRef = false;
		InputPin->PinFriendlyName = ResponseTypes[AdditionalPinIndex]->PinFriendlyName.ToString().IsEmpty() ? FText::FromString(InputPinName) : FText::FromName(ResponseTypes[AdditionalPinIndex]->PinFriendlyName);
		InputPin->DefaultValue = ResponseTypes[AdditionalPinIndex]->PinDefaultValue;
		InputPin->AutogeneratedDefaultValue = ResponseTypes[AdditionalPinIndex]->PinDefaultValue;
	}
	else
	{
		FEdGraphPinType PinType;
		PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
		InputPin = CreatePin(EGPD_Input, PinType, ToGraphPinNameType(InputPinName));
		InputPin->bDisplayAsMutableRef = false;
		InputPin->PinType.bIsReference = false;
		InputPin->PinFriendlyName = FText::FromString(InputPinName);
		InputPin->PinType.PinValueType.TerminalCategory = UEdGraphSchema_K2::PC_Wildcard;
	}

	if (bModify)
		Modify();
	return InputPin;
}

#if GMP_NODE_DETAIL
void UK2Node_ListenMessage::RemoveInputPin(UEdGraphPin* Pin)
{
	if (CanRemovePin(Pin))
	{
		FScopedTransaction Transaction(LOCTEXT("RemoveInputPinTx", "RemoveInputPin"));
		Modify();

		int32 PinRemovalIndex = INDEX_NONE;
		if (Pins.Find(Pin, /*out*/ PinRemovalIndex))
		{
			int32 NameIndex = 0;
			for (int32 PinIndex = 0; PinIndex < Pins.Num(); ++PinIndex)
			{
				UEdGraphPin* LocalPin = Pins[PinIndex];
				if (LocalPin && ToString(LocalPin->PinName).Find(MessageParamPrefix) != INDEX_NONE && LocalPin->Direction == EGPD_Output && LocalPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && PinIndex != PinRemovalIndex)
				{
					auto InputPinName = GMPListenMessage::GetNameForMsgPin(NameIndex);  // FIXME
					if (LocalPin->PinName != ToGraphPinNameType(InputPinName))
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

bool UK2Node_ListenMessage::CanRemovePin(const UEdGraphPin* Pin) const
{
	return (Pin && GetMessageCount() && (INDEX_NONE != Pins.IndexOfByKey(Pin)) && (ToString(Pin->PinName).Find(MessageParamPrefix) != INDEX_NONE));
}
#endif

void UK2Node_ListenMessage::AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins)
{
	FEdGraphPinType PinType;
	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
	auto Pin = CreatePin(EGPD_Input, PinType, UEdGraphSchema_K2::PN_Execute);

	Pin = CreatePin(EGPD_Output, PinType, UEdGraphSchema_K2::PN_Then);
	Pin->bAdvancedView = true;
	Pin->bHidden = true;

	Super::AllocateMsgKeyTagPin();
	AdvancedPinDisplay = ENodeAdvancedPins::Hidden;

	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	PinType.PinSubCategoryObject = UObject::StaticClass();
	Pin = CreatePin(EGPD_Input, PinType, UK2Node_MessageBase::GetFNameWatchedObj());
	Pin->bDefaultValueIsIgnored = true;
	Pin->bAdvancedView = true;

	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
	Pin = CreatePin(EGPD_Input, PinType, GMPListenMessage::ExactObjName);
	Pin->DefaultValue = TEXT("None");
	Pin->PinFriendlyName = LOCTEXT("ObjNameFilter", "ObjNameFilter");
	Pin->PinToolTip = TEXT("filter special signal source with WatchedObj and Name");
	Pin->bAdvancedView = [InOldPins] {
		if (InOldPins)
		{
			for (auto OldPin : *InOldPins)
			{
				if (OldPin->GetFName() == GMPListenMessage::ExactObjName)
				{
					return OldPin->DefaultValue.IsEmpty() || OldPin->DefaultValue == TEXT("None");
				}
			}
		}
		return true;
	}();

	PinType.ResetToDefaults();
	PinType.PinCategory = UEdGraphSchema_K2::PC_Int;
	Pin = CreatePin(EGPD_Input, PinType, GMPListenMessage::TimesName);
	Pin->DefaultValue = TEXT("-1");
	Pin->PinToolTip = TEXT("Trigger Counts, Negative forever");
	Pin->bAdvancedView = [InOldPins] {
		if (InOldPins)
		{
			for (auto OldPin : *InOldPins)
			{
				if (OldPin->GetFName() == GMPListenMessage::TimesName)
				{
					int32 OldTimes = -1;
					LexFromString(OldTimes, *OldPin->DefaultValue);
					return OldTimes == -1;
				}
			}
		}
		return true;
	}();
	Pin = CreatePin(EGPD_Input, PinType, GMPListenMessage::OrderName);
	Pin->DefaultValue = TEXT("0");
	Pin->PinToolTip = TEXT("Trigger Order, Lower is earlier, same value will notify by listen order");
	Pin->bAdvancedView = [InOldPins] {
		if (InOldPins)
		{
			for (auto OldPin : *InOldPins)
			{
				if (OldPin->GetFName() == GMPListenMessage::OrderName)
				{
					int32 OldOrder = 0;
					LexFromString(OldOrder, *OldPin->DefaultValue);
					return OldOrder == 0;
				}
			}
		}
		return true;
	}();

	if (IsAllowLatentFuncs())
	{
		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
		auto UnlistenPin = CreatePin(EGPD_Input, PinType, GMPListenMessage::UnlistenName);
		UnlistenPin->PinToolTip = TEXT("Stop");
		UnlistenPin->bAdvancedView = true;

		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Exec;
		Pin = CreatePin(EGPD_Output, PinType, GMPListenMessage::OnMessageName);
		if (ResponseTypes.Num() > 0)
		{
#if WITH_EDITORONLY_DATA
			Pin->PinFriendlyName = FText::FromString(("OnRequest"));
#endif
			Pin->PinToolTip = TEXT("OnRequest");
		}
		else
		{
			Pin->PinToolTip = TEXT("OnMessage");
		}

		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		PinType.PinSubCategoryObject = FGMPTypedAddr::StaticStruct();
		Pin = CreatePin(EGPD_Output, PinType, GMPListenMessage::MsgArrayName);
		Pin->PinToolTip = TEXT("MessageBody");
		Pin->PinType.ContainerType = EPinContainerType::Array;
		Pin->PinType.bIsReference = true;
		Pin->bAdvancedView = true;
		Pin->bHidden = true;

		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
		PinType.PinSubCategoryObject = UObject::StaticClass();
		Pin = CreatePin(EGPD_Output, PinType, GMPListenMessage::SenderName);
		Pin->PinToolTip = TEXT("Sender Object");
		Pin->bAdvancedView = true;

		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		Pin = CreatePin(EGPD_Output, PinType, GMPListenMessage::OutEventName);
		Pin->bAdvancedView = true;

		for (int32 i = 0; i < ParameterTypes.Num(); ++i)
		{
			AddParamPinImpl(i, false);
		}

		if (ResponseTypes.Num() > 0)
		{
			auto ResponePin = CreateResponseExecPin();
			for (int32 i = 0; i < ResponseTypes.Num(); ++i)
			{
				AddResponsePinImpl(i, false);
			}
		}

		auto PinNames = MoveTemp(WritebackPins);
		for (auto Name : PinNames)
		{
			if (FindPin(Name))
				WritebackPins.Add(Name);
		}
	}
	else
	{
		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		Pin = CreatePin(EGPD_Input, PinType, GMPListenMessage::CallbackEventName);
		Pin->PinFriendlyName = LOCTEXT("EventName", "EventName");
		Pin->bNotConnectable = true;
		Pin->bDefaultValueIsReadOnly = true;

		PinType.ResetToDefaults();
		PinType.PinCategory = UEdGraphSchema_K2::PC_Name;
		Pin = CreatePin(EGPD_Output, PinType, GMPListenMessage::OutEventName);
		Pin->PinFriendlyName = LOCTEXT("OutEventName", "OutEventName");
		Pin->bAdvancedView = true;
	}
}

FString UK2Node_ListenMessage::GetTitleHead() const
{
	if (ResponseTypes.Num() > 0)
		return TEXT("MessageService");
	else
		return TEXT("ListenMessage");
}

void UK2Node_ListenMessage::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	bool bOldAdvancedView = ChangedPin->bAdvancedView;
	Super::PinDefaultValueChanged(ChangedPin);
	if (ChangedPin == GetEventNamePin())
	{
		CachedNodeTitle.SetCachedText(FText::FromString(GetMessageTitle()), this);
		if (auto CallBackPin = FindPin(GMPListenMessage::CallbackEventName))
			CallBackPin->DefaultValue = TEXT("");
		GetGraph()->NotifyNodeChanged(this);
	}
	else if (ChangedPin == FindPin(GMPListenMessage::TimesName))
	{
		int32 Times = -1;
		LexFromString(Times, *ChangedPin->DefaultValue);
		ChangedPin->bAdvancedView = Times == -1;
	}
	else if (ChangedPin == FindPin(GMPListenMessage::OrderName))
	{
		int32 Order = 0;
		LexFromString(Order, *ChangedPin->DefaultValue);
		ChangedPin->bAdvancedView = (Order == 0);
	}
	else if (ChangedPin == FindPin(GMPListenMessage::ExactObjName))
	{
		ChangedPin->bAdvancedView = (ChangedPin->DefaultValue.IsEmpty() || ChangedPin->DefaultValue == TEXT("None"));
	}
	if (bOldAdvancedView != ChangedPin->bAdvancedView)
	{
		GetGraph()->NotifyNodeChanged(this);
	}
}

UEdGraphPin* UK2Node_ListenMessage::GetPinByName(const FString& PinIndex, TArray<UEdGraphPin*>* InPins, EEdGraphPinDirection PinDir) const
{
	UEdGraphPin* RetPin = nullptr;
	auto PinName = ToGraphPinNameType(PinIndex);
	for (UEdGraphPin* Pin : InPins ? *InPins : Pins)
	{
		if (Pin->PinName == PinName && (PinDir == EGPD_MAX || PinDir == Pin->Direction))
		{
			RetPin = Pin;
			break;
		}
	}

	ensure(RetPin || PinDir == EGPD_MAX);
	return RetPin;
}

void UK2Node_ListenMessage::SetPinToolTip(UEdGraphPin& Pin, bool bModify) const
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

FName UK2Node_ListenMessage::GetCornerIcon() const
{
	if (AuthorityType == EMessageTypeServer)
	{
		return TEXT("Graph.Replication.AuthorityOnly");
	}
	else if (AuthorityType == EMessageTypeClient)
	{
		return TEXT("Graph.Replication.ClientEvent");
	}

	return TEXT("Graph.Latent.LatentIcon");
}

void UK2Node_ListenMessage::PinConnectionListChanged(UEdGraphPin* ChangedPin)
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
					ParameterTypes[Index]->PinType = ChangedPin->PinType;
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

	GetGraph()->NotifyNodeChanged(this);
}

bool UK2Node_ListenMessage::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	if (MyPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
	{
		if (OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || OtherPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
			return true;

		// hack auto cast
		if (MyPin->Direction == EGPD_Output && OtherPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard && OtherPin->PinType.PinValueType.TerminalCategory != UEdGraphSchema_K2::PC_Wildcard)
		{
			const_cast<UEdGraphPin*>(MyPin)->PinType.ContainerType = OtherPin->PinType.ContainerType;
			return false;
		}
	}
	else if (MyPin == GetEventNamePin())
	{
		return true;
	}
	return Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
}

// Checks if an exec pin's downstream path contains any latent (Delay, etc.) nodes
static bool HasLatentDownstream(UEdGraphPin* ExecPin)
{
	if (!ExecPin)
		return false;

	TSet<UEdGraphNode*> Visited;
	TArray<UEdGraphNode*> WorkStack;

	for (auto* Link : ExecPin->LinkedTo)
	{
		if (Link)
			WorkStack.Push(Link->GetOwningNode());
	}

	while (WorkStack.Num())
	{
		auto* Node = WorkStack.Pop();
		if (!Node || Visited.Contains(Node))
			continue;
		Visited.Add(Node);

		if (auto* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (auto* Func = CallNode->GetTargetFunction())
			{
				if (Func->HasMetaData(FBlueprintMetadata::MD_Latent))
					return true;
			}
		}

		for (auto* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				for (auto* Next : Pin->LinkedTo)
				{
					if (Next)
						WorkStack.Push(Next->GetOwningNode());
				}
			}
		}
	}
	return false;
}

// Collects all nodes reachable after any latent node in the exec chain
static void CollectNodesAfterLatent(UEdGraphPin* ExecPin, TSet<UEdGraphNode*>& OutNodesAfterLatent)
{
	if (!ExecPin)
		return;

	TSet<UEdGraphNode*> Visited;
	// pair: <Node, bAlreadyPassedLatent>
	TArray<TPair<UEdGraphNode*, bool>> WorkStack;

	for (auto* Link : ExecPin->LinkedTo)
	{
		if (Link)
			WorkStack.Push({Link->GetOwningNode(), false});
	}

	while (WorkStack.Num())
	{
		auto [Node, bAfterLatent] = WorkStack.Pop();
		if (!Node || Visited.Contains(Node))
			continue;
		Visited.Add(Node);

		bool bIsLatent = false;
		if (auto* CallNode = Cast<UK2Node_CallFunction>(Node))
		{
			if (auto* Func = CallNode->GetTargetFunction())
				bIsLatent = Func->HasMetaData(FBlueprintMetadata::MD_Latent);
		}

		const bool bAfterThis = bAfterLatent || bIsLatent;
		if (bAfterThis)
			OutNodesAfterLatent.Add(Node);

		for (auto* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				for (auto* Next : Pin->LinkedTo)
				{
					if (Next)
						WorkStack.Push({Next->GetOwningNode(), bAfterThis});
				}
			}
		}
	}
}

// Checks if a data pin has any consumer node that executes after a latent boundary
static bool IsPinUsedAfterLatent(UEdGraphPin* DataPin, const TSet<UEdGraphNode*>& NodesAfterLatent)
{
	if (!DataPin)
		return false;
	for (auto* Link : DataPin->LinkedTo)
	{
		if (Link && NodesAfterLatent.Contains(Link->GetOwningNode()))
			return true;
	}
	return false;
}

// Checks if a data pin has any WRITE connection (Set Members, VariableSetRef, etc.) after latent
static bool IsPinWrittenAfterLatent(UEdGraphPin* DataPin, const TSet<UEdGraphNode*>& NodesAfterLatent)
{
	if (!DataPin)
		return false;
	for (auto* Link : DataPin->LinkedTo)
	{
		if (!Link || !NodesAfterLatent.Contains(Link->GetOwningNode()))
			continue;
		// Check if the connected pin is an input (write target)
		if (Link->Direction == EGPD_Input)
		{
			// Common write patterns: VariableSetRef, SetMembers, assignment nodes
			auto* OwnerNode = Link->GetOwningNode();
			if (OwnerNode->IsA<UK2Node_VariableSetRef>()
				|| Link->PinType.bIsReference)
			{
				return true;
			}
		}
	}
	return false;
}

FNodeHandlingFunctor* UK2Node_ListenMessage::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	// The original ListenMessage node is always fully handled by ExpandNode.
	// The real FKCHandler optimization lives on the intermediate UK2Node_GMPDerefParam nodes
	// that ExpandNode spawns — each has its own CreateNodeHandler returning FKCHandler_GMPDerefParam.
	return nullptr;
}

void UK2Node_ListenMessage::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	bool bAllValidated = true;
	if (bAllowLatentFuncs)
	{
		for (int32 Index = 0; Index < ParameterTypes.Num(); ++Index)
		{
			bAllValidated &= ParameterTypes[Index]->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard;
		}
	}

	bool bIsErrorFree = true;
	UFunction* ListenMessageFunc = nullptr;

	if (bAllValidated)
		ListenMessageFunc = (GMP_WITH_DYNAMIC_CALL_CHECK && bAllowLatentFuncs) ? GMP_UFUNCTION_CHECKED(UGMPBPLib, ListenMessageViaKeyValidate) : GMP_UFUNCTION_CHECKED(UGMPBPLib, ListenMessageViaKey);
	else
		ListenMessageFunc = (GMP_WITH_DYNAMIC_CALL_CHECK) ? GMP_UFUNCTION_CHECKED(UGMPBPLib, ListenMessageByKeyValidate) : GMP_UFUNCTION_CHECKED(UGMPBPLib, ListenMessageByKey);

	UK2Node_CallFunction* ListenMessageFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	ListenMessageFuncNode->SetFromFunction(ListenMessageFunc);
	ListenMessageFuncNode->AllocateDefaultPins();

	if (auto RetrunPin = ListenMessageFuncNode->GetReturnValuePin())
	{
		if (auto UnlistenPin = FindPin(GMPListenMessage::UnlistenName))
		{
			if (UnlistenPin->LinkedTo.Num())
			{
				UFunction* UnListenMessageFunc = GMP_UFUNCTION_CHECKED(UGMPBPLib, UnlistenMessageByKey);
				UK2Node_CallFunction* UnListenMessageFuncNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				UnListenMessageFuncNode->SetFromFunction(UnListenMessageFunc);
				UnListenMessageFuncNode->AllocateDefaultPins();

				bIsErrorFree &= TryCreateConnection(CompilerContext, UnlistenPin, UnListenMessageFuncNode->GetExecPin());

				if (auto MsgIdPin = UnListenMessageFuncNode->FindPin(GMPListenMessage::MessageIdName))
				{
					MsgIdPin->DefaultValue = GetMessageKey();
				}
			}
		}
	}

	if (auto MsgIdPin = ListenMessageFuncNode->FindPin(GMPListenMessage::MessageIdName))
	{
		// TryCreateConnection(CompilerContext, FindPinChecked(MessageKeyName), MsgIdPin, false);
		MsgIdPin->DefaultValue = GetMessageKey();
	}

	if (UEdGraphPin* TimesPin = FindPin(GMPListenMessage::TimesName))
	{
		if (UEdGraphPin* PinTimes = ListenMessageFuncNode->FindPin(GMPListenMessage::TimesName))
			bIsErrorFree &= TryCreateConnection(CompilerContext, TimesPin, PinTimes);
	}
	if (UEdGraphPin* OrderPin = FindPin(GMPListenMessage::OrderName))
	{
		if (UEdGraphPin* PinOrder = ListenMessageFuncNode->FindPin(GMPListenMessage::OrderName))
			bIsErrorFree &= TryCreateConnection(CompilerContext, OrderPin, PinOrder);
	}
	if (auto ThenPin = FindPin(UEdGraphSchema_K2::PN_Then))
		bIsErrorFree &= TryCreateConnection(CompilerContext, ThenPin, ListenMessageFuncNode->GetThenPin());
	bIsErrorFree &= TryCreateConnection(CompilerContext, FindPinChecked(UEdGraphSchema_K2::PN_Execute), ListenMessageFuncNode->GetExecPin());

	if (bAllowLatentFuncs)
	{
		auto RspPin = GetResponseExecPin();
		const bool bHasResponse = RspPin && RspPin->LinkedTo.Num() > 0;
		// Make CustomEvent To Delegate
		UK2Node_CustomEvent* CustomEventNode = CompilerContext.SpawnIntermediateNode<UK2Node_CustomEvent>(this, SourceGraph);

		const auto MsgCnt = FMath::Min(GetMessageCount(), ParameterTypes.Num());
		FString EventNodeName;
		{
			int32 NodeIndex = 0;
			TArray<UK2Node_CustomEvent*> Nodes;
			SourceGraph->GetNodesOfClass(Nodes);
			do
			{
				EventNodeName = FString::Printf(TEXT("OnMsg[%s][%d]%d"), *GetMessageKey(), MsgCnt, ++NodeIndex);

			} while (Nodes.FindByPredicate([&](UK2Node_CustomEvent* Node) { return Node->CustomFunctionName.ToString() == EventNodeName; }));
		}

		CustomEventNode->CustomFunctionName = FName(*EventNodeName);
		CustomEventNode->AllocateDefaultPins();

		UK2Node_MakeArray* ArgNamesNode = nullptr;
		if (auto ArgNamesPin = ListenMessageFuncNode->FindPin(GMPListenMessage::ArgNames))
		{
			ArgNamesNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
			ArgNamesNode->NumInputs = MsgCnt;
			ArgNamesNode->AllocateDefaultPins();

			UEdGraphPin* MakeArrayOut = ArgNamesNode->GetOutputPin();
			check(MakeArrayOut && ArgNamesPin);
			bIsErrorFree &= TryCreateConnection(CompilerContext, MakeArrayOut, ArgNamesPin);

			for (auto i = 0; i < MsgCnt; ++i)
			{
				auto& DefaultValue = ArgNamesNode->FindPinChecked(ArgNamesNode->GetPinName(i))->DefaultValue;
				DefaultValue = GMPReflection::GetPinPropertyName(ParameterTypes[i]->PinType).ToString();
			}
		}

		if (auto TypePin = ListenMessageFuncNode->FindPin(GMPListenMessage::AuthorityType))
		{
			TypePin->DefaultValue = LexToString((uint8)AuthorityType);
		}

		if (auto PinWatchObj = ListenMessageFuncNode->FindPin(UK2Node_MessageBase::GetFNameWatchedObj()))
		{
			UK2Node_CallFunction* MakeObjNamePairNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			MakeObjNamePairNode->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeObjNamePair));
			MakeObjNamePairNode->AllocateDefaultPins();
			bIsErrorFree &= TryCreateConnection(CompilerContext, MakeObjNamePairNode->GetReturnValuePin(), PinWatchObj);

			if (auto WatchObj = FindPin(UK2Node_MessageBase::GetFNameWatchedObj()))
			{
				ensure(WatchObj->LinkedTo.Num() <= 1);
				if (WatchObj->LinkedTo.Num() > 0)
				{
					bIsErrorFree &= TryCreateConnection(CompilerContext, WatchObj, MakeObjNamePairNode->FindPinChecked(TEXT("InObj")));
				}
				else if (bBindToGameInstance)
				{
					auto NodeGameInstance = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					NodeGameInstance->SetFromFunction(GET_UFUNCTION_CHECKED(UGameplayStatics, GetGameInstance));
					NodeGameInstance->AllocateDefaultPins();
					auto PinGameInstance = NodeGameInstance->GetReturnValuePin();
					bIsErrorFree &= TryCreateConnection(CompilerContext, PinGameInstance, MakeObjNamePairNode->FindPinChecked(TEXT("InObj")));
				}
			}
			if (auto TagNamePin = FindPin(GMPListenMessage::ExactObjName))
			{
				bIsErrorFree &= TryCreateConnection(CompilerContext, TagNamePin, MakeObjNamePairNode->FindPinChecked(TEXT("InName")));
			}
		}

		if (bAllValidated && NodeCompileMode == EGMPNodeCompileMode::Direct)
		{
			// ===== DIRECT MODE =====
			// CustomEvent has NO params → enables FastCall (EventGraphCallOffset).
			// MsgArray is stored in a SharedVariable (EventGraphLocal in PersistentFrame).
			// Native side caches UbergraphFunc + EntryOffset + SharedVar offset at registration time,
			// then invokes UbergraphFunc->Invoke(Listener, Frame, &Offset) directly.
			auto CustomEventThenPin = CustomEventNode->FindPinChecked(UEdGraphSchema_K2::PN_Then);
			bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, CustomEventThenPin, {FindPinChecked(GMPListenMessage::OnMessageName)});
			auto EventNamePin = ListenMessageFuncNode->FindPinChecked(GMPListenMessage::EventName);
			EventNamePin->DefaultValue = CustomEventNode->CustomFunctionName.ToString();

			// Create SharedVariable for MsgArray (EventGraphLocal in PersistentFrame)
			FString SharedVarName = FString::Printf(TEXT("GMPMsgArr_%s"), *GetMessageKey());
			UK2Node_MessageSharedVariable* MsgArrayVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_MessageSharedVariable>(this, SourceGraph);
			MsgArrayVarNode->SharedName = FName(*SharedVarName);
			MsgArrayVarNode->PinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
			MsgArrayVarNode->PinType.PinSubCategoryObject = FGMPTypedAddr::StaticStruct();
			MsgArrayVarNode->PinType.ContainerType = EPinContainerType::Array;
			MsgArrayVarNode->PinType.bIsReference = true;
			MsgArrayVarNode->AllocateDefaultPins();

			// Compute ParmBitMask
			uint64 ParmBitMask = 0;
			if (WritebackMode == EGMPWritebackMode::All)
				ParmBitMask = (GetMessageCount() > 0) ? ((1ull << GetMessageCount()) - 1) : 0;
			else if (WritebackMode == EGMPWritebackMode::Auto)
			{
				for (int32 Index = 0; Index < GetMessageCount(); ++Index)
				{
					auto Pin = GetMessagePin(Index);
					UK2Node_VariableSetRef* Ref = nullptr;
					if (WritebackPins.Contains(Pin->GetFName()) || GetConnectedNode(Pin, Ref))
						ParmBitMask |= 1ull << Index;
				}
			}

			// Pass SharedVarName to native side so it can cache the property offset at registration time
			// We encode it in BodyDataMask + use a dedicated pin if available
			uint8 BodyDataMask = 0;  // No Sender/MsgId/SeqId/MsgArray in CustomEvent params
			ListenMessageFuncNode->FindPinChecked(TEXT("BodyDataMask"))->DefaultValue = LexToString(BodyDataMask);
			if (auto PinParmBitMask = ListenMessageFuncNode->FindPin(TEXT("ParmBitMask")))
				PinParmBitMask->DefaultValue = LexToString(ParmBitMask);

			// Latent analysis
			auto* OnMessageExecPin = FindPinChecked(GMPListenMessage::OnMessageName);
			TSet<UEdGraphNode*> NodesAfterLatent;
			CollectNodesAfterLatent(OnMessageExecPin, NodesAfterLatent);

			// Generate per-parameter deref nodes from SharedVariable
			for (int32 Index = 0; Index < GetMessageCount(); ++Index)
			{
				if (!ParameterTypes.IsValidIndex(Index))
				{
					ensure(IsRunningCommandlet());
					CompilerContext.MessageLog.Error(TEXT("Less Parameters @@"), this);
					BreakAllNodeLinks();
					return;
				}

				auto OutputPin = GetMessagePin(Index);
				if (!OutputPin || !OutputPin->LinkedTo.Num())
					continue;

				const bool bUsedAfterLatent = IsPinUsedAfterLatent(OutputPin, NodesAfterLatent);

				if (bUsedAfterLatent && IsPinWrittenAfterLatent(OutputPin, NodesAfterLatent))
				{
					CompilerContext.MessageLog.Warning(
						*FText::Format(
							LOCTEXT("DirectAsyncWriteWarning", "Parameter '{0}' is modified after Delay — changes will NOT propagate back. @@"),
							FText::FromString(OutputPin->PinFriendlyName.ToString())
						).ToString(), OutputPin);
				}

				if (!bUsedAfterLatent)
				{
					// Zero-copy reference via UK2Node_GMPDerefParam → InlineGeneratedParameter
					auto* DerefNode = CompilerContext.SpawnIntermediateNode<UK2Node_GMPDerefParam>(this, SourceGraph);
					DerefNode->ParamIndex = Index;
					DerefNode->OutputPinType = OutputPin->PinType;
					DerefNode->AllocateDefaultPins();
					bIsErrorFree &= TryCreateConnection(CompilerContext, DerefNode->GetMsgArrayPin(), MsgArrayVarNode->GetVariablePin());
					bIsErrorFree &= TryCreateConnection(CompilerContext, OutputPin, DerefNode->GetOutItemPin(), true);
				}
				else
				{
					// Latent-accessed param: copy from SharedVariable MsgArray
					auto* ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrToWild));
					ConvertFunc->AllocateDefaultPins();
					ConvertFunc->FindPinChecked(TEXT("PropertyEnum"))->DefaultValue = LexToString(uint8(GMPReflection::PropertyTypeInvalid));
					ConvertFunc->FindPinChecked(TEXT("Index"))->DefaultValue = LexToString(Index);
					bIsErrorFree &= TryCreateConnection(CompilerContext, ConvertFunc->FindPinChecked(TEXT("TargetArray")), MsgArrayVarNode->GetVariablePin());
					auto* OutItemPin = ConvertFunc->FindPinChecked(TEXT("OutItem"));
					OutItemPin->PinType = OutputPin->PinType;
					bIsErrorFree &= TryCreateConnection(CompilerContext, OutputPin, OutItemPin);
					ConvertFunc->PinConnectionListChanged(OutItemPin);
					ConvertFunc->PostReconstructNode();
				}
			}

			OnNodeExpanded(CompilerContext, SourceGraph, ListenMessageFuncNode);
			BreakAllNodeLinks();
		}
		else if (bAllValidated)
		{
			auto CustomEventThenPin = CustomEventNode->FindPinChecked(UEdGraphSchema_K2::PN_Then);
			bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, CustomEventThenPin, {FindPinChecked(GMPListenMessage::OnMessageName)});
			auto EventNamePin = ListenMessageFuncNode->FindPinChecked(GMPListenMessage::EventName);
			EventNamePin->DefaultValue = CustomEventNode->CustomFunctionName.ToString();

			uint8 BodyDataMask = 0;

			auto PinSender = FindPin(GMPListenMessage::SenderName, EGPD_Output);
			if (PinSender && PinSender->LinkedTo.Num() > 0)
			{
				BodyDataMask |= 0x1;
				FEdGraphPinType ObjectPinType;
				ObjectPinType.PinCategory = UEdGraphSchema_K2::PC_Object;
				ObjectPinType.PinSubCategoryObject = UObject::StaticClass();
				auto SenderPin = CustomEventNode->CreateUserDefinedPin(UK2Node_MessageBase::GetFNameSender(), ObjectPinType, EGPD_Output, false);
				bIsErrorFree &= TryCreateConnection(CompilerContext, PinSender, SenderPin, !bHasResponse);
			}

			auto PinKey = FindPin(GMPListenMessage::MessageIdName, EGPD_Output);
			if (PinKey && PinKey->LinkedTo.Num())
			{
				BodyDataMask |= 0x2;
				FEdGraphPinType NamePinType;
				NamePinType.PinCategory = UEdGraphSchema_K2::PC_Name;
				auto KeyPin = CustomEventNode->CreateUserDefinedPin(GMPListenMessage::MessageIdName, NamePinType, EGPD_Output, false);
				bIsErrorFree &= TryCreateConnection(CompilerContext, PinKey, KeyPin);
			}

			auto PinSeqId = FindPin(GMPListenMessage::MessageSeqName, EGPD_Output);
			if (bHasResponse || (PinSeqId && PinSeqId->LinkedTo.Num() > 0))
			{
				BodyDataMask |= 0x4;
				FEdGraphPinType SeqIdPinType;
				SeqIdPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				SeqIdPinType.PinSubCategoryObject = FGMPKey::StaticStruct();
				auto SeqIdPin = CustomEventNode->CreateUserDefinedPin(GMPListenMessage::MessageSeqName, SeqIdPinType, EGPD_Output, false);
				if ((PinSeqId && PinSeqId->LinkedTo.Num()))
					bIsErrorFree &= TryCreateConnection(CompilerContext, PinSeqId, SeqIdPin);
			}

			// ===== Compute ParmBitMask based on WritebackMode =====
			uint64 ParmBitMask = 0;
			switch (WritebackMode)
			{
			case EGMPWritebackMode::All:
				ParmBitMask = (GetMessageCount() > 0) ? ((1ull << GetMessageCount()) - 1) : 0;
				break;
			case EGMPWritebackMode::Auto:
				for (int32 Index = 0; Index < GetMessageCount(); ++Index)
				{
					auto Pin = GetMessagePin(Index);
					UK2Node_VariableSetRef* Ref = nullptr;
					if (WritebackPins.Contains(Pin->GetFName()) || GetConnectedNode(Pin, Ref))
						ParmBitMask |= 1ull << Index;
				}
				break;
			case EGMPWritebackMode::None:
			default:
				break;
			}

			// ===== Latent analysis for ParamPassingMode::Auto =====
			auto* OnMessageExecPin = FindPinChecked(GMPListenMessage::OnMessageName);
			TSet<UEdGraphNode*> NodesAfterLatent;
			// Handler/Direct modes force reference path for all params; Auto mode analyzes per-pin
			const bool bUseAutoParamPassing = (ParamPassingMode == EGMPParamPassingMode::Auto)
				|| (NodeCompileMode == EGMPNodeCompileMode::Handler)
				|| (NodeCompileMode == EGMPNodeCompileMode::Direct);
			if (bUseAutoParamPassing)
			{
				CollectNodesAfterLatent(OnMessageExecPin, NodesAfterLatent);
			}
			// When Auto passing mode uses reference path, we need MsgArray in the CustomEvent
			bool bNeedsMsgArrayForDeref = false;

			auto PinMsgArray = FindPin(GMPListenMessage::MsgArrayName, EGPD_Output);
			// Determine if MsgArray param is needed in the CustomEvent:
			// - User explicitly connected MsgArray output pin, OR
			// - Auto param passing mode may use GMPDerefParam which reads from MsgArray
			if (bUseAutoParamPassing)
			{
				// Any connected pin uses the reference path (even if also used after Latent,
				// the sync portion still uses GMPDerefParam which needs MsgArray)
				for (int32 Index = 0; Index < GetMessageCount(); ++Index)
				{
					auto Pin = GetMessagePin(Index);
					if (Pin && Pin->LinkedTo.Num())
					{
						bNeedsMsgArrayForDeref = true;
						break;
					}
				}
			}
			if (bNeedsMsgArrayForDeref || (PinMsgArray && PinMsgArray->LinkedTo.Num()))
			{
				BodyDataMask |= 0x8;
				FEdGraphPinType ArrPinType;
				ArrPinType.PinCategory = UEdGraphSchema_K2::PC_Struct;
				ArrPinType.PinSubCategoryObject = FGMPTypedAddr::StaticStruct();
				ArrPinType.ContainerType = EPinContainerType::Array;
				ArrPinType.bIsReference = true;
				auto MsgArrPin = CustomEventNode->CreateUserDefinedPin(GMPListenMessage::MsgArrayName, ArrPinType, EGPD_Output, false);
				if (PinMsgArray && PinMsgArray->LinkedTo.Num())
					bIsErrorFree &= TryCreateConnection(CompilerContext, PinMsgArray, MsgArrPin);
			}
			ListenMessageFuncNode->FindPinChecked(TEXT("BodyDataMask"))->DefaultValue = LexToString(BodyDataMask);
			if (auto PinParmBitMask = ListenMessageFuncNode->FindPin(TEXT("ParmBitMask")))
				PinParmBitMask->DefaultValue = LexToString(ParmBitMask);

			// ===== Generate per-parameter code =====
			for (int32 Index = 0; Index < GetMessageCount(); ++Index)
			{
				if (!ParameterTypes.IsValidIndex(Index))
				{
					ensure(IsRunningCommandlet());
					CompilerContext.MessageLog.Error(TEXT("Less Parameters @@"), this);
					BreakAllNodeLinks();
					return;
				}

				auto OutputPin = GetMessagePin(Index);
				const bool bPinConnected = OutputPin && OutputPin->LinkedTo.Num() > 0;

				// Per-pin analysis: which consumers are before vs after Latent?
				const bool bUsedAfterLatent = bUseAutoParamPassing && IsPinUsedAfterLatent(OutputPin, NodesAfterLatent);
				const bool bUsedBeforeLatent = bPinConnected;  // any connection implies sync usage
				const bool bUseReferencePath = bUseAutoParamPassing && bPinConnected;
				// Note: even if used after Latent, we still create a reference for sync consumers.
				// A separate TemporaryVariable snapshot is created for async consumers.

				// Warn if pin is written after a latent boundary
				if (bUsedAfterLatent && IsPinWrittenAfterLatent(OutputPin, NodesAfterLatent))
				{
					CompilerContext.MessageLog.Warning(
						*FText::Format(
							LOCTEXT("AsyncWriteWarning", "Parameter '{0}' is modified after a Delay/Latent node — changes will NOT propagate back to the notify caller. @@"),
							FText::FromString(OutputPin->PinFriendlyName.ToString())
						).ToString(), OutputPin);
				}

				if (bUseReferencePath)
				{
					// ===== Always create a zero-copy reference for sync access =====
					UEdGraphPin* RefOutPin = nullptr;

					if (NodeCompileMode == EGMPNodeCompileMode::Handler || NodeCompileMode == EGMPNodeCompileMode::Direct)
					{
						// Handler/Direct: UK2Node_GMPDerefParam with InlineGeneratedParameter
						auto* DerefNode = CompilerContext.SpawnIntermediateNode<UK2Node_GMPDerefParam>(this, SourceGraph);
						DerefNode->ParamIndex = Index;
						DerefNode->OutputPinType = OutputPin->PinType;
						DerefNode->AllocateDefaultPins();

						bIsErrorFree &= TryCreateConnection(CompilerContext,
							DerefNode->GetMsgArrayPin(),
							CustomEventNode->FindPinChecked(GMPListenMessage::MsgArrayName));

						RefOutPin = DerefNode->GetOutItemPin();
					}
					else
					{
						// ExpandNode: UK2Node_CallFunction(GMPDerefParam)
						auto* DerefNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
						DerefNode->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, GMPDerefParam));
						DerefNode->AllocateDefaultPins();
						DerefNode->FindPinChecked(TEXT("Index"))->DefaultValue = LexToString(Index);

						bIsErrorFree &= TryCreateConnection(CompilerContext,
							DerefNode->FindPinChecked(TEXT("MsgArray")),
							CustomEventNode->FindPinChecked(GMPListenMessage::MsgArrayName));

						auto* OutItemPin = DerefNode->FindPinChecked(TEXT("OutItem"));
						OutItemPin->PinType = OutputPin->PinType;
						OutItemPin->PinType.bIsReference = true;
						RefOutPin = OutItemPin;
					}

					if (ArgNamesNode)
					{
						ArgNamesNode->FindPinChecked(ArgNamesNode->GetPinName(Index))->DefaultValue = GMPReflection::GetPinPropertyName(OutputPin->PinType).ToString();
					}

					if (!bUsedAfterLatent)
					{
						// Pin only used synchronously — all connections go to the reference
						bIsErrorFree &= TryCreateConnection(CompilerContext, OutputPin, RefOutPin, true);
					}
					else
					{
						// ===== Pin used BOTH sync AND async: split connections =====
						// Sync consumers → RefOutPin (zero-copy reference)
						// Async consumers → TemporaryVariable snapshot (safe after Delay)

						// Create a TemporaryVariable for the async copy
						auto* TempVarNode = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
						TempVarNode->VariableType = OutputPin->PinType;
						TempVarNode->VariableType.bIsReference = false;
						TempVarNode->AllocateDefaultPins();
						auto* TempVarPin = TempVarNode->GetVariablePin();

						// Snapshot assignment: TempVar = *RefPtr
						// MUST be exec-based (not pure) so it runs at a deterministic point
						// BEFORE any Latent node, not lazily when TempVar is first read.
						auto* AssignNode = CompilerContext.SpawnIntermediateNode<UK2Node_AssignmentStatement>(this, SourceGraph);
						AssignNode->AllocateDefaultPins();
						// Variable pin = TempVar (destination)
						bIsErrorFree &= TryCreateConnection(CompilerContext, AssignNode->GetVariablePin(), TempVarPin);
						// Value pin = RefOutPin (source — triggers GMPDerefPtr at exec time, reads from original)
						auto* ValuePin = AssignNode->GetValuePin();
						ValuePin->PinType = OutputPin->PinType;
						bIsErrorFree &= TryCreateConnection(CompilerContext, ValuePin, RefOutPin);

						// Insert the snapshot assignment into the exec flow
						// It runs right after CustomEvent fires, before user logic
						bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, CustomEventThenPin, {AssignNode->GetExecPin()});

						// Split connections: rewire each LinkedTo based on pre/post Latent
						TArray<UEdGraphPin*> OriginalLinks = OutputPin->LinkedTo;
						OutputPin->BreakAllPinLinks(true);

						for (auto* LinkedTo : OriginalLinks)
						{
							if (NodesAfterLatent.Contains(LinkedTo->GetOwningNode()))
							{
								// Post-Latent consumer → connect to snapshot (TemporaryVariable)
								bIsErrorFree &= TryCreateConnection(CompilerContext, TempVarPin, LinkedTo);
							}
							else
							{
								// Pre-Latent consumer → connect to reference (zero-copy)
								bIsErrorFree &= TryCreateConnection(CompilerContext, RefOutPin, LinkedTo);
							}
						}
					}
				}
				else/* if (bPinConnected)*/
				{
					// ===== AlwaysCopy mode: standard CustomEvent parameter =====
					UEdGraphPin* EventParamPin = nullptr;
					auto EnumPtr = Cast<UEnum>(OutputPin->PinType.PinSubCategoryObject.Get());
					if (EnumPtr && EnumPtr->GetCppForm() != UEnum::ECppForm::EnumClass && OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
					{
						const bool bIsFromByte = (ParameterTypes[Index]->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || ParameterTypes[Index]->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte);
						const auto LiteralFunc = bIsFromByte ? GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralByte) : GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralByte);

						FEdGraphPinType ThisPinType;
						ThisPinType.PinCategory = bIsFromByte ? UEdGraphSchema_K2::PC_Byte : UEdGraphSchema_K2::PC_Int;
						ThisPinType.bIsReference = true;
						EventParamPin = CustomEventNode->CreateUserDefinedPin(MakeParameterName(Index), ThisPinType, EGPD_Output, false);

						if (ArgNamesNode)
						{
							ArgNamesNode->FindPinChecked(ArgNamesNode->GetPinName(Index))->DefaultValue = GMPReflection::GetPinPropertyName(ThisPinType).ToString();
						}

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
						auto PinType = OutputPin->PinType;
						PinType.bIsReference = true;
						EventParamPin = CustomEventNode->CreateUserDefinedPin(MakeParameterName(Index), PinType, EGPD_Output, false);
						if (ArgNamesNode)
						{
							auto& DefaultVal = ArgNamesNode->FindPinChecked(ArgNamesNode->GetPinName(Index))->DefaultValue;
							DefaultVal = GMPReflection::GetPinPropertyName(EventParamPin->PinType).ToString();
						}
						if (IsRunningCommandlet() && !OutputPin->LinkedTo.Num())
							continue;
					}

					// Connect output pin to event parameter — writeback via OutParm/explicit in CallMessageFunction
					UK2Node_VariableSetRef* SetByRefNode = nullptr;
					GetConnectedNode(OutputPin, SetByRefNode);

					if (SetByRefNode)
					{
						bIsErrorFree &= TryCreateConnection(CompilerContext, OutputPin, EventParamPin);
						bIsErrorFree &= TryCreateConnection(CompilerContext, SetByRefNode->FindPinChecked(TEXT("Target")), EventParamPin);
					}
					else
					{
						bIsErrorFree &= TryCreateConnection(CompilerContext, OutputPin, EventParamPin, true);
					}
				}
			}

			OnNodeExpanded(CompilerContext, SourceGraph, ListenMessageFuncNode);
			BreakAllNodeLinks();
		}
		else
		{
			// DelegateVersion
			UEdGraphPin* EventPin = CustomEventNode->FindPin(GMPListenMessage::OutputDelegateName);
			UEdGraphPin* PinDelegate = ListenMessageFuncNode->FindPinChecked(GMPListenMessage::DelegateName);
			bIsErrorFree &= TryCreateConnection(CompilerContext, EventPin, PinDelegate);
			CustomEventNode->AutowireNewNode(PinDelegate);

			UEdGraphPin* MessageEndPin = CustomEventNode->FindPinChecked(UEdGraphSchema_K2::PN_Then);
			bIsErrorFree &= SequenceDo(CompilerContext, SourceGraph, MessageEndPin, {FindPinChecked(GMPListenMessage::OnMessageName)});

			// (UObject* Sender, const FString& MessageId,const TArray<FGMPTypedAddr>& Params)
			UEdGraphPin* EventParamsPin = nullptr;
			for (int32 j = 0; j < CustomEventNode->Pins.Num(); j++)
			{
				auto CurPin = CustomEventNode->Pins[j];
				if (CurPin->Direction == EGPD_Output && CurPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec && CurPin->PinType.PinCategory != UEdGraphSchema_K2::PC_Delegate)
				{
					if (CurPin->PinName == GMPListenMessage::SenderName)
					{
						if (auto PinSender = FindPin(GMPListenMessage::SenderName, EGPD_Output))
							bIsErrorFree &= TryCreateConnection(CompilerContext, PinSender, CurPin);
					}
					else if (CurPin->PinName == GMPListenMessage::MessageIdName)
					{
						if (auto MessageIdPin = FindPin(GMPListenMessage::OutEventName, EGPD_Output))
							bIsErrorFree &= TryCreateConnection(CompilerContext, MessageIdPin, CurPin);
					}
					else if (CurPin->PinName == GMPListenMessage::MsgArrayName)
					{
					}
					else if (CurPin->PinName == GMPListenMessage::MessageSeqName)
					{
					}
					else if (CurPin->PinName == GMPListenMessage::ParamsName)
					{
						EventParamsPin = CurPin;
						if (auto MsgArray = FindPin(GMPListenMessage::MsgArrayName, EGPD_Output))
						{
							if (MsgArray->LinkedTo.Num() > 0)
								bIsErrorFree &= TryCreateConnection(CompilerContext, MsgArray, CurPin);
						}
					}
					else
					{
						check(false);
					}
				}
			}
			check(EventParamsPin);

			for (int32 idx = 0; idx < GetMessageCount(); ++idx)
			{
				auto OutputPin = GetMessagePin(idx);
				if (!OutputPin->LinkedTo.Num())
					continue;

				UEdGraphPin* LinkPin = OutputPin->LinkedTo[0];
				while (LinkPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				{
					if (!LinkPin->LinkedTo.Num())
					{
						LinkPin = nullptr;
						break;
					}
					LinkPin = LinkPin->LinkedTo[0];
				}
				if (!LinkPin)
					continue;

				EGMPPropertyClass ProperyType = GMPReflection::PropertyTypeInvalid;
				EGMPPropertyClass ElemPropType = GMPReflection::PropertyTypeInvalid;
				EGMPPropertyClass KeyPropType = GMPReflection::PropertyTypeInvalid;

				if (ArgNamesNode)
				{
					auto ArgName = GMPReflection::GetPinPropertyName(OutputPin->PinType, &ProperyType, &ElemPropType, &KeyPropType);
					if (OutputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
						ArgName = GMPReflection::GetPinPropertyName(LinkPin->PinType, &ProperyType, &ElemPropType, &KeyPropType);

					ArgNamesNode->FindPinChecked(ArgNamesNode->GetPinName(idx))->DefaultValue = ArgName.ToString();
				}

				UK2Node_CallFunction* ConvertFunc = nullptr;

				auto PostReconstruct = [&](UFunction* SetValueFunc) {
					ConvertFunc->FindPinChecked(TEXT("Index"))->DefaultValue = LexToString(idx);
					UEdGraphPin* ArrPin = ConvertFunc->FindPinChecked(TEXT("TargetArray"));
					bIsErrorFree &= TryCreateConnection(CompilerContext, EventParamsPin, ArrPin);

					UEdGraphPin* OutValuePin = ConvertFunc->FindPinChecked(TEXT("OutItem"));
					bIsErrorFree &= TryCreateConnection(CompilerContext, OutputPin, OutValuePin);
					ConvertFunc->PinConnectionListChanged(OutValuePin);
					ConvertFunc->PostReconstructNode();

					if (WritebackPins.Contains(OutputPin->GetFName()))
					{
						ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
						ConvertFunc->SetFromFunction(SetValueFunc);
						ConvertFunc->AllocateDefaultPins();
						switch (LinkPin->PinType.ContainerType)
						{
							case EPinContainerType::Array:
							case EPinContainerType::Set:
							{
								ConvertFunc->FindPinChecked(TEXT("ElementEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
							}
							break;
							case EPinContainerType::Map:
							{
								ConvertFunc->FindPinChecked(TEXT("KeyEnum"))->DefaultValue = LexToString(uint8(KeyPropType));
								ConvertFunc->FindPinChecked(TEXT("ValueEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
							}
							break;
							default:
							{
								ConvertFunc->FindPinChecked(TEXT("PropertyEnum"))->DefaultValue = LexToString(uint8(ProperyType));
							}
							break;
						}
						ConvertFunc->FindPinChecked(TEXT("Index"))->DefaultValue = LexToString(idx);
						bIsErrorFree &= TryCreateConnection(CompilerContext, EventParamsPin, ConvertFunc->FindPinChecked(TEXT("TargetArray")));
						bIsErrorFree &= TryCreateConnection(CompilerContext, MessageEndPin, ConvertFunc->GetExecPin());
						MessageEndPin = ConvertFunc->GetThenPin();
						UEdGraphPin* SetValuePin = ConvertFunc->FindPinChecked(TEXT("InItem"));
						SetValuePin->PinType.PinCategory = LinkPin->PinType.PinCategory;
						bIsErrorFree &= TryCreateConnection(CompilerContext, OutValuePin, SetValuePin);
						ConvertFunc->PinConnectionListChanged(SetValuePin);
						ConvertFunc->PostReconstructNode();
					}
				};

				if (LinkPin->PinType.IsArray())
				{
					// AddrToArray
					ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrToArray));
					ConvertFunc->AllocateDefaultPins();

					//AddrToArray(uint8 ElementEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, TArray<int32>& OutItem)
					ConvertFunc->FindPinChecked(TEXT("ElementEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
					UEdGraphPin* ValuePin = ConvertFunc->FindPinChecked(TEXT("OutItem"));
					ValuePin->PinType.PinCategory = LinkPin->PinType.PinCategory;
					PostReconstruct(GMP_UFUNCTION_CHECKED(UGMPBPLib, SetArray));
				}
				else if (LinkPin->PinType.IsSet())
				{
					ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrToSet));
					ConvertFunc->AllocateDefaultPins();

					//AddrToSet(uint8 ElementEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, TSet<int32>& OutItem)
					ConvertFunc->FindPinChecked(TEXT("ElementEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
					UEdGraphPin* ValuePin = ConvertFunc->FindPinChecked(TEXT("OutItem"));
					ValuePin->PinType.PinCategory = LinkPin->PinType.PinCategory;
					PostReconstruct(GMP_UFUNCTION_CHECKED(UGMPBPLib, SetSet));
				}
				else if (LinkPin->PinType.IsMap())
				{
					ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrToMap));
					ConvertFunc->AllocateDefaultPins();

					//AddrToMap(uint8 KeyEnum, uint8 ValueEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, TMap<int32, int32>& OutItem)
					ConvertFunc->FindPinChecked(TEXT("KeyEnum"))->DefaultValue = LexToString(uint8(KeyPropType));
					ConvertFunc->FindPinChecked(TEXT("ValueEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
					UEdGraphPin* ValuePin = ConvertFunc->FindPinChecked(TEXT("OutItem"));
					ValuePin->PinType.PinCategory = LinkPin->PinType.PinCategory;
					ValuePin->PinType.PinValueType = LinkPin->PinType.PinValueType;
					PostReconstruct(GMP_UFUNCTION_CHECKED(UGMPBPLib, SetMap));
				}
				else /*(!LinkPin->PinType.IsContainer())*/
				{
					// AddrToWild
					ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrToWild));
					ConvertFunc->AllocateDefaultPins();

					//AddrToWild(uint8 PropertyEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, FGMPTypedAddr& OutItem)
					ConvertFunc->FindPinChecked(TEXT("PropertyEnum"))->DefaultValue = LexToString(uint8(ProperyType));
					PostReconstruct(GMP_UFUNCTION_CHECKED(UGMPBPLib, SetWild));
				}
			}
			OnNodeExpanded(CompilerContext, SourceGraph, ListenMessageFuncNode);
		}

		// Response
		if (!bHasResponse)
			return;

		for (int32 Index = 0; Index < ResponseTypes.Num(); ++Index)
		{
			bAllValidated &= ResponseTypes[Index]->PinType.PinCategory != UEdGraphSchema_K2::PC_Wildcard;
		}

		UFunction* ResponseMessageFunc = (UE_4_25_OR_LATER) ? GMP_UFUNCTION_CHECKED(UGMPBPLib, ResponseMessageVariadic) : GMP_UFUNCTION_CHECKED(UGMPBPLib, ResponseMessage);
		UK2Node_CallFunction* ResponseMessageNode = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
		ResponseMessageNode->SetFromFunction(ResponseMessageFunc);
		ResponseMessageNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, RspPin, ResponseMessageNode->GetExecPin());

		UK2Node_MakeArray* MakeArrayNode = nullptr;
		UEdGraphPin* PinParams = ResponseMessageNode->FindPin(TEXT("Params"));
		if (PinParams)
		{
			MakeArrayNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeArray>(this, SourceGraph);
			MakeArrayNode->NumInputs = GetMessageCount();
			MakeArrayNode->AllocateDefaultPins();
			CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeArrayNode, this);
		}

		bIsErrorFree &= ExpandMessageCall(CompilerContext, SourceGraph, ResponseTypes, MakeArrayNode, ResponseMessageNode);

		UEdGraphPin* PinSender = ResponseMessageNode->FindPinChecked(UK2Node_MessageBase::GetFNameSender());
		if (auto SenderPin = FindPinChecked(GMPListenMessage::SenderName))
		{
			if (SenderPin->LinkedTo.Num() == 1)
				bIsErrorFree &= TryCreateConnection(CompilerContext, SenderPin, PinSender, false);
		}

		if (UEdGraphPin* PinMessageId = ResponseMessageNode->FindPinChecked(GMPListenMessage::MessageIdName))
		{
			PinMessageId->DefaultValue = GetMessageKey();
			// bIsErrorFree &= TryCreateConnection(CompilerContext,FindPinChecked(MessageKeyName), PinMessageId);
		}

		if (UEdGraphPin* PinSeqId = ResponseMessageNode->FindPinChecked(GMPListenMessage::MessageSeqName))
		{
			auto PinData = CustomEventNode->FindPin(GMPListenMessage::MessageSeqName);
			bIsErrorFree &= ensure(PinData);
			if (bIsErrorFree)
				bIsErrorFree &= TryCreateConnection(CompilerContext, PinData, PinSeqId);
		}

		if (UEdGraphPin* PinMgr = ResponseMessageNode->FindPin(TEXT("Mgr")))
		{
			if (auto MgrData = FindPin(TEXT("Mgr")))
				bIsErrorFree &= TryCreateConnection(CompilerContext, MgrData, PinMgr, false);
		}

		if (MakeArrayNode)
		{
			UEdGraphPin* MakeArrayOut = MakeArrayNode->GetOutputPin();
			check(MakeArrayOut);
			bIsErrorFree &= TryCreateConnection(CompilerContext, MakeArrayOut, PinParams);
			MakeArrayNode->PostReconstructNode();
		}
	}
	else if (auto CallBackPin = FindPin(GMPListenMessage::CallbackEventName))
	{
		auto EventNamePin = ListenMessageFuncNode->FindPinChecked(GMPListenMessage::EventName);
		EventNamePin->DefaultValue = CallBackPin->GetDefaultAsString();
	}

	// Break all links to the Select node so it goes away for at scheduling time
	BreakAllNodeLinks();
}

UK2Node::ERedirectType UK2Node_ListenMessage::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	// Temp work around: remove whitespaces from pin names before doing string comparison.

	// 	FString NewName = NewPin->PinName.ToString();
	// 	FString OldName = OldPin->PinName.ToString();
	//
	// 	NewName.ReplaceInline(TEXT(" "), TEXT(""));
	// 	OldName.ReplaceInline(TEXT(" "), TEXT(""));
	//
	// 	if (NewName == OldName)
	// 	{
	// 		// Make sure we're not dealing with a menu node
	// 		UEdGraph* OuterGraph = GetGraph();
	// 		if (OuterGraph && OuterGraph->Schema)
	// 		{
	// 			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
	// 			if (!K2Schema || K2Schema->IsSelfPin(*NewPin) || K2Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType))
	// 			{
	// 				return ERedirectType_Name;
	// 			}
	// 		}
	// 	}

	return Super::DoPinsMatchForReconstruction(NewPin, NewPinIndex, OldPin, OldPinIndex);
}

void UK2Node_ListenMessage::EarlyValidation(class FCompilerResultsLog& MessageLog) const
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

		if (!IsAllowLatentFuncs())
		{
			if (auto CallBackPin = FindPin(GMPListenMessage::CallbackEventName))
			{
				TArray<UK2Node_CustomEvent*> Nodes;
				GetBlueprint()->UbergraphPages[0]->GetNodesOfClass(Nodes);
				const FName EventName = *CallBackPin->GetDefaultAsString();
				auto Find = Nodes.FindByPredicate([&](auto EvtNode) { return EvtNode->CustomFunctionName == EventName; });
				if (!Find || !*Find)
				{
					MessageLog.Error(TEXT("event not found for Pin: @@"), CallBackPin);
					return;
				}

				auto& UserPins = (*Find)->UserDefinedPins;
				if (UserPins.Num() > ParameterTypes.Num())
				{
					MessageLog.Error(TEXT("event signature mismatch : @@"), *Find);
					return;
				}

				for (auto i = 0; i < UserPins.Num(); ++i)
				{
					if (UserPins[i]->PinType != ParameterTypes[i]->PinType)
					{
						MessageLog.Error(TEXT("event signature mismatch : @@"), (*Find)->FindPinChecked(UserPins[i]->PinName));
						return;
					}
				}
			}
		}

		auto RspExecPin = GetResponseExecPin();
		if (RspExecPin)
		{
			if (!IsAllowLatentFuncs())
			{
				MessageLog.Error(TEXT("Does not support Latent : @@"), RspExecPin);
				return;
			}
			if (!RspExecPin->LinkedTo.Num())
			{
				MessageLog.Warning(TEXT("Rsponse should has comsumed : @@"), RspExecPin);
				return;
			}

			for (int32 idx = 0; idx < ResponseTypes.Num(); ++idx)
			{
				auto OutputPin = GetResponsePin(idx);
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
