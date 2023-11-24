// Copyright Epic Games, Inc. All Rights Reserved.

#include "K2Node_FormatStr.h"

#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintNodeSpawner.h"
#include "EdGraphSchema_K2.h"
#include "EdGraphSchema_K2_Actions.h"
#include "EditorCategoryUtils.h"
#include "GMP/GMPBPLib.h"
#include "K2Node_CallFunction.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MakeMap.h"
#include "K2Node_MakeStruct.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetStringLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "KismetCompiler.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "K2Node_FormatStr"

/////////////////////////////////////////////////////
// UK2Node_FormatStr

struct FFormatStrNodeHelper
{
	static const FName FormatPinName;

	static const FName GetFormatPinName() { return FormatPinName; }
};

const FName FFormatStrNodeHelper::FormatPinName(TEXT("Format"));

UK2Node_FormatStr::UK2Node_FormatStr(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
	, CachedFormatPin(NULL)
{
	NodeTooltip = LOCTEXT("NodeTooltip", "Builds a formatted string using available format argument values.");
}

void UK2Node_FormatStr::AllocateDefaultPins()
{
	Super::AllocateDefaultPins();

	CachedFormatPin = CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_String, FFormatStrNodeHelper::GetFormatPinName());
	CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_String, TEXT("Result"));

	for (const FName& PinName : PinNames)
	{
		CreatePin(EGPD_Input, bInputAstWild ? UEdGraphSchema_K2::PC_Wildcard : UEdGraphSchema_K2::PC_String, PinName);
	}
}

void UK2Node_FormatStr::SynchronizeArgumentPinType(UEdGraphPin* Pin)
{
	if (!bInputAstWild)
		return;

	const UEdGraphPin* FormatPin = GetFormatPin();
	if (Pin != FormatPin && Pin->Direction == EGPD_Input)
	{
		const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());

		bool bPinTypeChanged = false;
		if (Pin->LinkedTo.Num() == 0)
		{
			static const FEdGraphPinType WildcardPinType = FEdGraphPinType(UEdGraphSchema_K2::PC_Wildcard, NAME_None, nullptr, EPinContainerType::None, false, FEdGraphTerminalType());

			// Ensure wildcard
			if (Pin->PinType != WildcardPinType)
			{
				Pin->PinType = WildcardPinType;
				bPinTypeChanged = true;
			}
		}
		else
		{
			UEdGraphPin* ArgumentSourcePin = Pin->LinkedTo[0];

			// Take the type of the connected pin
			if (Pin->PinType != ArgumentSourcePin->PinType)
			{
				Pin->PinType = ArgumentSourcePin->PinType;
				bPinTypeChanged = true;
			}
		}

		if (bPinTypeChanged)
		{
			// Let the graph know to refresh
			GetGraph()->NotifyGraphChanged();

			UBlueprint* Blueprint = GetBlueprint();
			if (!Blueprint->bBeingCompiled)
			{
				FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
				Blueprint->BroadcastChanged();
			}
		}
	}
}

FText UK2Node_FormatStr::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return LOCTEXT("FormatStr_Title", "Format String");
}

FText UK2Node_FormatStr::GetPinDisplayName(const UEdGraphPin* Pin) const
{
	return FText::FromName(Pin->PinName);
}

FName UK2Node_FormatStr::GetUniquePinName()
{
	FName NewPinName;
	int32 i = 0;
	while (true)
	{
		NewPinName = *FString::FromInt(i++);
		if (!FindPin(NewPinName))
		{
			break;
		}
	}
	return NewPinName;
}

void UK2Node_FormatStr::PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = (PropertyChangedEvent.Property ? PropertyChangedEvent.Property->GetFName() : NAME_None);
	if (PropertyName == GET_MEMBER_NAME_CHECKED(UK2Node_FormatStr, PinNames))
	{
		ReconstructNode();
		GetGraph()->NotifyGraphChanged();
	}
	Super::PostEditChangeProperty(PropertyChangedEvent);
}

void UK2Node_FormatStr::PinConnectionListChanged(UEdGraphPin* Pin)
{
	UEdGraphPin* FormatPin = GetFormatPin();

	Modify();

	// Clear all pins.
	if (Pin == FormatPin && !FormatPin->DefaultValue.IsEmpty())
	{
		PinNames.Empty();
		GetSchema()->TrySetDefaultValue(*FormatPin, TEXT(""));

		for (auto It = Pins.CreateConstIterator(); It; ++It)
		{
			UEdGraphPin* CheckPin = *It;
			if (CheckPin != FormatPin && CheckPin->Direction == EGPD_Input)
			{
				CheckPin->Modify();
				CheckPin->MarkAsGarbage();
				Pins.Remove(CheckPin);
				--It;
			}
		}

		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	}

	// Potentially update an argument pin type
	SynchronizeArgumentPinType(Pin);
}

void UK2Node_FormatStr::PinDefaultValueChanged(UEdGraphPin* Pin)
{
	const UEdGraphPin* FormatPin = GetFormatPin();
	if (Pin == FormatPin && FormatPin->LinkedTo.Num() == 0)
	{
		TArray<FString> ArgumentParams;
		FText::GetFormatPatternParameters(FTextFormat::FromString(FormatPin->DefaultValue), ArgumentParams);

		PinNames.Reset();

		for (const FString& Param : ArgumentParams)
		{
			const FName ParamName(*Param);
			if (!FindArgumentPin(ParamName))
			{
				CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Wildcard, ParamName);
			}
			PinNames.Add(ParamName);
		}

		for (auto It = Pins.CreateIterator(); It; ++It)
		{
			UEdGraphPin* CheckPin = *It;
			if (CheckPin != FormatPin && CheckPin->Direction == EGPD_Input)
			{
				const bool bIsValidArgPin = ArgumentParams.ContainsByPredicate([&CheckPin](const FString& InPinName) { return InPinName.Equals(CheckPin->PinName.ToString(), ESearchCase::CaseSensitive); });

				if (!bIsValidArgPin)
				{
					CheckPin->MarkAsGarbage();
					It.RemoveCurrent();
				}
			}
		}

		GetGraph()->NotifyGraphChanged();
	}
}

void UK2Node_FormatStr::PinTypeChanged(UEdGraphPin* Pin)
{
	// Potentially update an argument pin type
	SynchronizeArgumentPinType(Pin);

	Super::PinTypeChanged(Pin);
}

FText UK2Node_FormatStr::GetTooltipText() const
{
	return NodeTooltip;
}

UEdGraphPin* FindOutputStructPinChecked(UEdGraphNode* Node)
{
	check(NULL != Node);
	UEdGraphPin* OutputPin = NULL;
	for (int32 PinIndex = 0; PinIndex < Node->Pins.Num(); ++PinIndex)
	{
		UEdGraphPin* Pin = Node->Pins[PinIndex];
		if (Pin && (EGPD_Output == Pin->Direction))
		{
			OutputPin = Pin;
			break;
		}
	}
	check(NULL != OutputPin);
	return OutputPin;
}

void UK2Node_FormatStr::PostReconstructNode()
{
	Super::PostReconstructNode();

	// We need to upgrade any non-connected argument pins with valid literal text data to use a "Make Literal String" node as an input (argument pins used to be PC_String and they're now PC_Wildcard)
	if (!IsTemplate())
	{
		// Make sure we're not dealing with a menu node
		UEdGraph* OuterGraph = GetGraph();
		if (OuterGraph && OuterGraph->Schema)
		{
			int32 NumPinsFixedUp = 0;

			const UEdGraphPin* FormatPin = GetFormatPin();
			for (UEdGraphPin* CurrentPin : Pins)
			{
				if (CurrentPin != FormatPin && CurrentPin->Direction == EGPD_Input && CurrentPin->LinkedTo.Num() == 0 && !CurrentPin->DefaultTextValue.IsEmpty())
				{
					// Create a new "Make Literal String" function and add it to the graph
					const FVector2D SpawnLocation = FVector2D(NodePosX - 300, NodePosY + (60 * (NumPinsFixedUp + 1)));
					UK2Node_CallFunction* MakeLiteralString = FEdGraphSchemaAction_K2NewNode::SpawnNode<UK2Node_CallFunction>(GetGraph(), SpawnLocation, EK2NewNodeFlags::None, [](UK2Node_CallFunction* NewInstance) {
						NewInstance->SetFromFunction(UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UKismetSystemLibrary, MakeLiteralString)));
					});

					// Set the new value and clear it on this pin to avoid it ever attempting this upgrade again (eg, if the "Make Literal String" node was disconnected)
					UEdGraphPin* LiteralValuePin = MakeLiteralString->FindPinChecked(TEXT("Value"));
					LiteralValuePin->DefaultTextValue = CurrentPin->DefaultTextValue;  // Note: Uses assignment rather than TrySetDefaultValue to ensure we keep the existing localization identity
					CurrentPin->DefaultTextValue = FText::GetEmpty();

					// Connect the new node to the existing pin
					UEdGraphPin* LiteralReturnValuePin = MakeLiteralString->FindPinChecked(TEXT("ReturnValue"));
					GetSchema()->TryCreateConnection(LiteralReturnValuePin, CurrentPin);

					++NumPinsFixedUp;
				}

				// Potentially update an argument pin type
				SynchronizeArgumentPinType(CurrentPin);
			}

			if (NumPinsFixedUp > 0)
			{
				GetGraph()->NotifyGraphChanged();
				FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
			}
		}
	}
}

void UK2Node_FormatStr::ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph)
{
	Super::ExpandNode(CompilerContext, SourceGraph);

	/**
		At the end of this, the UK2Node_FormatStr will not be a part of the Blueprint, it merely handles connecting
		the other nodes into the Blueprint.
	*/

	const UEdGraphSchema_K2* Schema = CompilerContext.GetSchema();

	// Create a "Make Array" node to compile the list of arguments into an array for the Format function being called
	UK2Node_MakeMap* MakeMapNode = CompilerContext.SpawnIntermediateNode<UK2Node_MakeMap>(this, SourceGraph);
	MakeMapNode->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(MakeMapNode, this);

	UEdGraphPin* MapOut = MakeMapNode->GetOutputPin();

	// This is the node that does all the Format work.
	UK2Node_CallFunction* CallFormatFunction = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
	if (!bInputAstWild)
	{
		CallFormatFunction->SetFromFunction(UGMPBPLib::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UGMPBPLib, FormatStringByName)));
	}
	else
	{
		CallFormatFunction->SetFromFunction(UGMPBPLib::StaticClass()->FindFunctionByName(GET_MEMBER_NAME_CHECKED(UGMPBPLib, FormatStringByName)));
	}
	CallFormatFunction->AllocateDefaultPins();
	CompilerContext.MessageLog.NotifyIntermediateObjectCreation(CallFormatFunction, this);

	// Connect the output of the "Make Array" pin to the function's "InArgs" pin
	MapOut->MakeLinkTo(CallFormatFunction->FindPinChecked(TEXT("InArgs")));
	MakeMapNode->PinConnectionListChanged(MapOut);

	for (int32 ArgIdx = 0; ArgIdx < PinNames.Num(); ++ArgIdx)
	{
		UEdGraphPin* ArgumentPin = FindArgumentPin(PinNames[ArgIdx]);
		if (ArgIdx > 0)
		{
			MakeMapNode->AddInputPin();
		}
		auto MapKeyPin = MakeMapNode->FindPinChecked(MakeMapNode->GetPinName(ArgIdx * 2));
		Schema->TrySetDefaultValue(*MapKeyPin, PinNames[ArgIdx].ToString());

		auto MapValuePin = MakeMapNode->FindPinChecked(MakeMapNode->GetPinName(ArgIdx * 2 + 1));
		Schema->CreateAutomaticConversionNodeAndConnections(ArgumentPin, MapValuePin);
	}

	// Move connection of FormatStr's "Result" pin to the call function's return value pin.
	CompilerContext.MovePinLinksToIntermediate(*FindPinChecked(TEXT("Result")), *CallFormatFunction->GetReturnValuePin());
	// Move connection of FormatStr's "Format" pin to the call function's "InPattern" pin
	CompilerContext.MovePinLinksToIntermediate(*GetFormatPin(), *CallFormatFunction->FindPinChecked(TEXT("InFmtStr")));

	BreakAllNodeLinks();
}

UEdGraphPin* UK2Node_FormatStr::FindArgumentPin(const FName InPinName) const
{
	const UEdGraphPin* FormatPin = GetFormatPin();
	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin != FormatPin && Pin->Direction != EGPD_Output && Pin->PinName.ToString().Equals(InPinName.ToString(), ESearchCase::CaseSensitive))
		{
			return Pin;
		}
	}

	return nullptr;
}

UK2Node::ERedirectType UK2Node_FormatStr::DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const
{
	ERedirectType RedirectType = ERedirectType_None;

	// if the pin names do match
	if (NewPin->PinName.ToString().Equals(OldPin->PinName.ToString(), ESearchCase::CaseSensitive))
	{
		// Make sure we're not dealing with a menu node
		UEdGraph* OuterGraph = GetGraph();
		if (OuterGraph && OuterGraph->Schema)
		{
			const UEdGraphSchema_K2* K2Schema = Cast<const UEdGraphSchema_K2>(GetSchema());
			if (!K2Schema || K2Schema->IsSelfPin(*NewPin) || K2Schema->ArePinTypesCompatible(OldPin->PinType, NewPin->PinType))
			{
				RedirectType = ERedirectType_Name;
			}
			else
			{
				RedirectType = ERedirectType_None;
			}
		}
	}
	else
	{
		// try looking for a redirect if it's a K2 node
		if (UK2Node* Node = Cast<UK2Node>(NewPin->GetOwningNode()))
		{
			// if you don't have matching pin, now check if there is any redirect param set
			TArray<FString> OldPinNames;
			GetRedirectPinNames(*OldPin, OldPinNames);

			FName NewPinName;
			RedirectType = ShouldRedirectParam(OldPinNames, /*out*/ NewPinName, Node);

			// make sure they match
			if ((RedirectType != ERedirectType_None) && (!NewPin->PinName.ToString().Equals(NewPinName.ToString(), ESearchCase::CaseSensitive)))
			{
				RedirectType = ERedirectType_None;
			}
		}
	}

	return RedirectType;
}

bool UK2Node_FormatStr::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
{
	bool bDisallowed = Super::IsConnectionDisallowed(MyPin, OtherPin, OutReason);
	if (!bDisallowed)
	{
		auto K2Schema = GetDefault<UEdGraphSchema_K2>();

		static auto StrPinType = [] {
			FEdGraphPinType PinType;
			PinType.PinCategory = UEdGraphSchema_K2::PC_String;
			return PinType;
		}();
		TGuardValue<FEdGraphPinType>(const_cast<FEdGraphPinType&>(MyPin->PinType), StrPinType);

#if UE_5_02_OR_LATER
		if (!K2Schema->SearchForAutocastFunction(OtherPin->PinType, MyPin->PinType))
		{
			if (!K2Schema->FindSpecializedConversionNode(OtherPin->PinType, *MyPin, true))
			{
				bDisallowed = true;
				OutReason = LOCTEXT("Error_InvalidArgumentType", "Format arguments not supported").ToString();
			}
		}
#else
		FName TargetFunctionName;
		UClass* ClassContainingConversionFunction = nullptr;
		if (!K2Schema->SearchForAutocastFunction(OtherPin, MyPin, TargetFunctionName, ClassContainingConversionFunction))
		{
			UK2Node* TemplateConversionNode = nullptr;
			K2Schema->FindSpecializedConversionNode(OtherPin, MyPin, true, TemplateConversionNode);
			if (!TemplateConversionNode)
			{
				bDisallowed = true;
				OutReason = LOCTEXT("Error_InvalidArgumentType", "Format arguments not supported").ToString();
			}
		}
#endif
	}
	return bDisallowed;
}

FText UK2Node_FormatStr::GetArgumentName(int32 InIndex) const
{
	if (InIndex < PinNames.Num())
	{
		return FText::FromName(PinNames[InIndex]);
	}
	return FText::GetEmpty();
}

void UK2Node_FormatStr::AddArgumentPin()
{
	const FScopedTransaction Transaction(NSLOCTEXT("Kismet", "AddArgumentPin", "Add Argument Pin"));
	Modify();

	const FName PinName(GetUniquePinName());
	CreatePin(EGPD_Input, bInputAstWild ? UEdGraphSchema_K2::PC_Wildcard : UEdGraphSchema_K2::PC_String, PinName);
	PinNames.Add(PinName);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	GetGraph()->NotifyGraphChanged();
}

void UK2Node_FormatStr::RemoveArgument(int32 InIndex)
{
	const FScopedTransaction Transaction(NSLOCTEXT("Kismet", "RemoveArgumentPin", "Remove Argument Pin"));
	Modify();

	if (UEdGraphPin* ArgumentPin = FindArgumentPin(PinNames[InIndex]))
	{
		Pins.Remove(ArgumentPin);
		ArgumentPin->MarkAsGarbage();
	}
	PinNames.RemoveAt(InIndex);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
	GetGraph()->NotifyGraphChanged();
}

void UK2Node_FormatStr::SetArgumentName(int32 InIndex, FName InName)
{
	PinNames[InIndex] = InName;

	ReconstructNode();

	FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
}

void UK2Node_FormatStr::SwapArguments(int32 InIndexA, int32 InIndexB)
{
	check(InIndexA < PinNames.Num());
	check(InIndexB < PinNames.Num());
	PinNames.Swap(InIndexA, InIndexB);

	ReconstructNode();
	GetGraph()->NotifyGraphChanged();

	FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
}

UEdGraphPin* UK2Node_FormatStr::GetFormatPin() const
{
	if (!CachedFormatPin)
	{
		const_cast<UK2Node_FormatStr*>(this)->CachedFormatPin = FindPinChecked(FFormatStrNodeHelper::GetFormatPinName());
	}
	return CachedFormatPin;
}

void UK2Node_FormatStr::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	// actions get registered under specific object-keys; the idea is that
	// actions might have to be updated (or deleted) if their object-key is
	// mutated (or removed)... here we use the node's class (so if the node
	// type disappears, then the action should go with it)
	UClass* ActionKey = GetClass();
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

FText UK2Node_FormatStr::GetMenuCategory() const
{
	return FEditorCategoryUtils::GetCommonCategory(FCommonEditorCategory::String);
}

#undef LOCTEXT_NAMESPACE
