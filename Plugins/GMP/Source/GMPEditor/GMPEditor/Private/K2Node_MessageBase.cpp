//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "K2Node_MessageBase.h"

#include "Slate.h"

#if UE_4_24_OR_LATER
#include "ToolMenu.h"
#include "ToolMenuSection.h"
#endif

#include "../Private/SMessageTagGraphPin.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "BlueprintActionDatabaseRegistrar.h"
#include "BlueprintEditorModule.h"
#include "BlueprintNodeSpawner.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "EdGraphCompilerUtilities.h"
#include "EdGraphSchema_K2.h"
#include "Engine/BlueprintCore.h"
#include "GMP/GMPReflection.h"
#include "GMPCore.h"
#include "GraphEditorSettings.h"
#include "IDetailChildrenBuilder.h"
#include "IDetailCustomNodeBuilder.h"
#include "IPropertyUtilities.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_CastByteToEnum.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_Knot.h"
#include "K2Node_ListenMessage.h"
#include "K2Node_MakeArray.h"
#include "K2Node_MathExpression.h"
#include "K2Node_PureAssignmentStatement.h"
#include "K2Node_TemporaryVariable.h"
#include "K2Node_VariableSetRef.h"
#include "Kismet/KismetMathLibrary.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetDebugUtilities.h"
#include "KismetCompiledFunctionContext.h"
#include "KismetCompiler.h"
#include "KismetCompilerMisc.h"
#include "MessageTagContainer.h"
#include "MessageTagRedirectors.h"
#include "MessageTagsEditorModule.h"
#include "MessageTagsManager.h"
#include "Misc/PackageName.h"
#include "NodeFactory.h"
#include "PropertyCustomizationHelpers.h"
#include "SGraphPin.h"
#include "SPinTypeSelector.h"
#include "ScopedTransaction.h"
#include "UObject/UObjectGlobals.h"

#define LOCTEXT_NAMESPACE "GMPMessageBase"

FArchive& operator<<(FArchive& Ar, FMessagePinTypeInfo& Info)
{
	Ar.UsingCustomVersion(FFrameworkObjectVersion::GUID);
	Ar << Info.InnerVer;
	Ar << Info.PinIndex;
	Ar << Info.PinFriendlyName;
	Info.PinType.Serialize(Ar);
	Ar << Info.PinDefaultValue;
	return Ar;
}

//////////////////////////////////////////////////////////////////////////
namespace GMPMessageBase
{
UObject* FindReflectionImpl(const FString& TypeName, UClass* TypeClass)
{
	TypeClass = TypeClass ? TypeClass : UObject::StaticClass();
	bool bIsValidName = true;

	if (TypeName.Contains(TEXT(" ")))
	{
		bIsValidName = false;
	}
	else if (!FPackageName::IsShortPackageName(TypeName))
	{
		if (TypeName.Contains(TEXT(".")))
		{
			FString PackageName;
			FString ObjectName;
			TypeName.Split(TEXT("."), &PackageName, &ObjectName);

			if (!FPackageName::IsValidLongPackageName(PackageName, true))
			{
				bIsValidName = false;
			}
		}
		else
		{
			bIsValidName = false;
		}
	}

	if (bIsValidName)
	{
		UObject* NewReflection = nullptr;
		if (FPackageName::IsShortPackageName(TypeName))
		{
			NewReflection = StaticFindObject(TypeClass, ANY_PACKAGE_COMPATIABLE, *TypeName);
		}
		else
		{
			NewReflection = StaticFindObject(TypeClass, nullptr, *TypeName);
		}

		return NewReflection;
	}

	return nullptr;
}

template<typename T>
T* FindReflection(const FString& TypeName)
{
	return static_cast<T*>(FindReflectionImpl(TypeName, T::StaticClass()));
}

static const FGraphPinNameType EventName = TEXT("EventName");
static const FGraphPinNameType TimesName = TEXT("Times");
static const FGraphPinNameType OnMessageName = TEXT("OnMessage");
static const FGraphPinNameType DelegateName = TEXT("Delegate");
static const FGraphPinNameType OutputDelegateName = TEXT("OutputDelegate");
static const FGraphPinNameType OutEventName = TEXT("OutEventName");
static const FGraphPinNameType MessageIdName = TEXT("MessageId");
static const FGraphPinNameType MessageSeqName = TEXT("SeqId");
static const FGraphPinNameType SenderName = TEXT("Sender");
static const FGraphPinNameType MsgArrayName = TEXT("MsgArray");
static const FGraphPinNameType ParamsName = TEXT("Params");
static const FGraphPinNameType UnlistenName = TEXT("StopListen");
static const FGraphPinNameType AuthorityType = TEXT("Type");
static const FGraphPinNameType WatchedObj = TEXT("WatchedObj");
static const FGraphPinNameType ArgNames = TEXT("ArgNames");

static bool bIgnoreMetaOnRunningCommandlet = false;
static FAutoConsoleVariableRef CVar_IgnoreMetaOnRunningCommandlet(TEXT("gmp.IgnoreMetaOnRunningCommandlet"), bIgnoreMetaOnRunningCommandlet, TEXT(""));
}  // namespace GMPMessageBase

bool UK2Node_MessageBase::ShouldIgnoreMetaOnRunningCommandlet()
{
	return GMPMessageBase::bIgnoreMetaOnRunningCommandlet && IsRunningCommandlet();
}

//////////////////////////////////////////////////////////////////////////
class FKCHandler_MessageSharedVariable : public FNodeHandlingFunctor
{
public:
	FKCHandler_MessageSharedVariable(FKismetCompilerContext& InCompilerContext)
		: FNodeHandlingFunctor(InCompilerContext)
	{
	}

	virtual void RegisterNet(FKismetFunctionContext& Context, UEdGraphPin* Net) override
	{
		UK2Node_MessageSharedVariable* MessageShareNode = Cast<UK2Node_MessageSharedVariable>(Net->GetOwningNode());
		check(MessageShareNode);

		FString TermName = (MessageShareNode->SharedName.ToString()) + TEXT("_MessageSharedVariable");

		// First, try to see if we already have a term for this object
		FBPTerminal* Term = NULL;
		for (int32 i = 0; i < Context.EventGraphLocals.Num(); i++)
		{
			FBPTerminal& CurrentTerm = Context.EventGraphLocals[i];
			if (CurrentTerm.Name == TermName)
			{
				Term = &CurrentTerm;
				break;
			}
		}

		// If we didn't find one, then create a new term
		if (!Term)
		{
			Term = new FBPTerminal();
			Term->SetContextTypeStruct(true);
			Term->CopyFromPin(Net, Context.NetNameMap->MakeValidName(Net));
			Term->Name = TermName;
			Context.EventGraphLocals.Add(Term);
		}

		check(Term);
		Context.NetMap.Add(Net, Term);
	}
};
//////////////////////////////////////////////////////////////////////////
UK2Node_MessageSharedVariable::UK2Node_MessageSharedVariable()
{
	PinType.PinCategory = UEdGraphSchema_K2::PC_Object;
	PinType.PinSubCategoryObject = AActor::StaticClass();
}

void UK2Node_MessageSharedVariable::AllocateDefaultPins()
{
	CreatePin(EGPD_Output, PinType, TEXT("Variable"));
	Super::AllocateDefaultPins();
}

UEdGraphPin* UK2Node_MessageSharedVariable::GetVariablePin()
{
	return FindPin(TEXT("Variable"));
}

FNodeHandlingFunctor* UK2Node_MessageSharedVariable::CreateNodeHandler(FKismetCompilerContext& CompilerContext) const
{
	return new FKCHandler_MessageSharedVariable(CompilerContext);
}

void UK2Node_MessageBase::OnSignatureChanged(FName MsgKey)
{
	// 	if (!HasAnyFlags(RF_Transactional | RF_WasLoaded))
	// 		return;

	if (GetMessageKey() == MsgKey.ToString())
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(GetBlueprint());
#if 0  
		// auto fix
		const FMessageTag* NewTag = FMessageTagRedirectors::Get().RedirectTag(MsgKey);
		if (NewTag)
			MsgTag = *NewTag;
		DoRebuild(!!NewTag);
		RefreashMessagePin();
#endif
	}
}

UK2Node* UK2Node_MessageBase::GetConnectedNode(UEdGraphPin* Pin, TSubclassOf<UK2Node> NodeClass) const
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

bool UK2Node_MessageBase::IsExecPin(UEdGraphPin* Pin, EEdGraphPinDirection Direction) const
{
	return Pin && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec && (Direction == EGPD_MAX || ((Direction == Pin->Direction) != Pins.Contains(Pin)));
}

bool UK2Node_MessageBase::SequenceDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins)
{
	check(IsExecPin(InOutThenPin, EGPD_Output));
	bool bIsErrorFree = true;
	auto IdxFirst = ExecPins.IndexOfByPredicate([](auto Pin) { return Pin && !Pin->IsPendingKill(); });
	if (IdxFirst == INDEX_NONE)
		return bIsErrorFree;

	const UEdGraphSchema_K2* K2Schema = GetK2Schema(CompilerContext);
	auto FirstPin = ExecPins[IdxFirst];
	check(IsExecPin(FirstPin, EGPD_Input));
	if (ExecPins.Num() == 1 && !Pins.Contains(FirstPin) && FirstPin->GetOwningNode()->FindPin(UEdGraphSchema_K2::PN_Then))
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, InOutThenPin, FirstPin);
		InOutThenPin = FirstPin->GetOwningNode()->FindPin(UEdGraphSchema_K2::PN_Then);
	}
	else
	{
		UK2Node_ExecutionSequence* SeqNode = SeqNode = CompilerContext.SpawnIntermediateNode<UK2Node_ExecutionSequence>(this, SourceGraph);
		SeqNode->AllocateDefaultPins();
		bIsErrorFree &= TryCreateConnection(CompilerContext, InOutThenPin, SeqNode->GetExecPin());
		bIsErrorFree &= TryCreateConnection(CompilerContext, SeqNode->GetThenPinGivenIndex(0), FirstPin);

		int32 i = IdxFirst + 1;
		while (i < ExecPins.Num())
		{
			if (!ExecPins[i])
				continue;
			check(IsExecPin(ExecPins[i], EGPD_Input));
			bIsErrorFree &= TryCreateConnection(CompilerContext, SeqNode->GetThenPinGivenIndex(i), ExecPins[i]);
			SeqNode->AddInputPin();
			++i;
		}
		InOutThenPin = SeqNode->GetThenPinGivenIndex(i);
	}
	return bIsErrorFree;
}

bool UK2Node_MessageBase::LaterDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins)
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

	bIsErrorFree &= TryCreateConnection(CompilerContext, InOutThenPin, SeqNode->GetExecPin());
	bIsErrorFree &= TryCreateConnection(CompilerContext, SeqNode->GetThenPinGivenIndex(1), FirstPin);
	InOutThenPin = SeqNode->GetThenPinGivenIndex(0);

	int32 i = IdxFirst + 1;
	while (i < ExecPins.Num())
	{
		if (!ExecPins[i])
			continue;
		check(IsExecPin(ExecPins[i], EGPD_Input));
		SeqNode->AddInputPin();
		bIsErrorFree &= TryCreateConnection(CompilerContext, SeqNode->GetThenPinGivenIndex(i + 1), ExecPins[i]);
		++i;
	}
	return bIsErrorFree;
}

UEdGraphPin* UK2Node_MessageBase::GetThenPin() const
{
	return FindPinChecked(UEdGraphSchema_K2::PN_Then);
}

bool UK2Node_MessageBase::ExpandMessageCall(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const TArray<FMessagePinTypeInfoCell>& PinTypeInfos, UK2Node_MakeArray* MakeArrayNode, UK2Node_CallFunction* CallMessageNode)
{
	bool bIsErrorFree = true;
	auto K2Schema = CompilerContext.GetSchema();
	for (int32 idx = 0; idx < PinTypeInfos.Num(); ++idx)
	{
		auto* const OriginalInputPin = GetInputPinByIndex(idx);
		if (!OriginalInputPin)
			continue;
		auto ParamPin = OriginalInputPin;
		UEdGraphPin* LinkPin = ParamPin;
		bool bInputPinLinked = !!LinkPin->LinkedTo.Num();
		while (LinkPin && LinkPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
		{
			LinkPin = LinkPin->LinkedTo.Num() ? LinkPin->LinkedTo[0] : nullptr;
		}
		if (!ensure(LinkPin))
		{
			BreakAllNodeLinks();
			return false;
		}
		// Variadic
		if (!MakeArrayNode)
		{
			auto EnumPtr = Cast<UEnum>(ParamPin->PinType.PinSubCategoryObject.Get());
			FString DefaultValue = ParamPin->GetDefaultAsString();
			if (EnumPtr)
			{
				int64 EnumValue = EnumPtr->GetValueByNameString(DefaultValue);

				if (EnumValue > FMath::Min(255ll, EnumPtr->GetMaxEnumValue()) || !EnumPtr->IsValidEnumValue(static_cast<uint8>(EnumValue)))
					CompilerContext.MessageLog.Warning(*FString::Printf(TEXT("please use enum class instead : %s @@"), *EnumPtr->CppType), ParamPin);
				// work same as enum declared in ufunctions
				DefaultValue = LexToString(static_cast<uint8>(EnumValue));
			}

			if (EnumPtr && EnumPtr->GetCppForm() != UEnum::ECppForm::EnumClass && ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
			{
				// force as byte enum otherwise it would use int instead
				const bool bForceByte = (PinTypeInfos[idx]->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinTypeInfos[idx]->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte);
				FEdGraphPinType LiteralPinType;
				LiteralPinType.PinCategory = bForceByte ? UEdGraphSchema_K2::PC_Byte : UEdGraphSchema_K2::PC_Int;

				auto InputPin = CallMessageNode->CreatePin(EGPD_Input, LiteralPinType, OriginalInputPin->PinName);
				if (!bInputPinLinked)
				{
					auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					NodeMakeLiteral->SetFromFunction(bForceByte ? GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralByte) : GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
					NodeMakeLiteral->AllocateDefaultPins();
					K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);

					UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
					VariablePin->PinType = LiteralPinType;
					bIsErrorFree &= TryCreateConnection(CompilerContext, VariablePin, InputPin);
				}
				else
				{
					auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					NodeMakeLiteral->SetFromFunction(bForceByte ? GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralByte) : GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
					NodeMakeLiteral->AllocateDefaultPins();
					auto GenericValuePin = NodeMakeLiteral->FindPinChecked(TEXT("Value"));
					bIsErrorFree &= TryCreateConnection(CompilerContext, OriginalInputPin, GenericValuePin);
					NodeMakeLiteral->NotifyPinConnectionListChanged(GenericValuePin);

					UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
					VariablePin->PinType = LiteralPinType;
					bIsErrorFree &= TryCreateConnection(CompilerContext, VariablePin, InputPin);
				}
			}
			else
			{
				auto InputPin = CallMessageNode->CreatePin(EGPD_Input, ParamPin->PinType, OriginalInputPin->PinName);
				if (bInputPinLinked)
				{
					auto NewSelfPin = ConstCastIfSelfPin(OriginalInputPin, CompilerContext, SourceGraph);
					bIsErrorFree &= ensure(NewSelfPin);
					if (NewSelfPin != OriginalInputPin)
					{
						NewSelfPin->MakeLinkTo(InputPin);
					}
					else
					{
						auto NewPin = CastIfFloatType(OriginalInputPin, CompilerContext, SourceGraph);
						bIsErrorFree &= ensure(NewPin);
						if (NewPin != OriginalInputPin)
						{
							NewPin->MakeLinkTo(InputPin);
						}
						else
						{
							bIsErrorFree &= TryCreateConnection(CompilerContext, NewPin, InputPin);
						}
					}
				}
				else
				{
					UEdGraphPin* VariablePin = SpawnPureVariable(CompilerContext, SourceGraph, ParamPin, DefaultValue);
					bIsErrorFree &= TryCreateConnection(CompilerContext, VariablePin, InputPin);
				}
			}
		}
		else
		{
			EGMPPropertyClass ProperyType = GMPReflection::PropertyTypeInvalid;
			EGMPPropertyClass ElemPropType = GMPReflection::PropertyTypeInvalid;
			EGMPPropertyClass KeyPropType = GMPReflection::PropertyTypeInvalid;

			GMPReflection::GetPinPropertyName(OriginalInputPin->PinType, &ProperyType, &ElemPropType, &KeyPropType);
			if (OriginalInputPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				GMPReflection::GetPinPropertyName(LinkPin->PinType, &ProperyType, &ElemPropType, &KeyPropType);

			UK2Node_CallFunction* ConvertFunc = nullptr;
			UEdGraphPin* ValuePin = nullptr;

			if (LinkPin->PinType.IsArray())
			{
				// AddrFromArray
				ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromArray));
				ConvertFunc->AllocateDefaultPins();

				//AddrFromArray(uint8 ElementEnum, const TArray<int32>& InAny)
				ConvertFunc->FindPinChecked(TEXT("ElementEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
				ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
				ValuePin->PinType.PinCategory = LinkPin->PinType.PinCategory;
			}
			else if (LinkPin->PinType.IsSet())
			{
				// AddrFromSet
				ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromSet));
				ConvertFunc->AllocateDefaultPins();

				//AddrFromSet(uint8 ElementEnum, const TSet<int32>& InAny)
				ConvertFunc->FindPinChecked(TEXT("ElementEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
				ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
			}
			else if (LinkPin->PinType.IsMap())
			{
				// AddrFromMap
				ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromMap));
				ConvertFunc->AllocateDefaultPins();

				//AddrFromMap(uint8 ValueEnum, uint8 KeyEnum, const TMap<int32, int32>& InAny)
				ConvertFunc->FindPinChecked(TEXT("KeyEnum"))->DefaultValue = LexToString(uint8(KeyPropType));
				ConvertFunc->FindPinChecked(TEXT("ValueEnum"))->DefaultValue = LexToString(uint8(ElemPropType));
				ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));
			}
			else /*(!LinkPin->PinType.IsContainer())*/
			{
				// AddrFromWild
				ConvertFunc = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
				ConvertFunc->SetFromFunction(GMP_UFUNCTION_CHECKED(UGMPBPLib, AddrFromWild));
				ConvertFunc->AllocateDefaultPins();

				//AddrFromWild(uint8 PropertyEnum, const FGMPTypedAddr& InAny);
				ConvertFunc->FindPinChecked(TEXT("PropertyEnum"))->DefaultValue = LexToString(uint8(ProperyType));
				ValuePin = ConvertFunc->FindPinChecked(TEXT("InAny"));

				if (LinkPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && LinkPin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
				{
					// self must assign to a new TemporaryVariable
					if (auto NewPin = ConstCastIfSelfPin(OriginalInputPin, CompilerContext, SourceGraph, LinkPin))
					{
						ParamPin = NewPin;
						ValuePin->PinType = ParamPin->PinType;
						ValuePin->PinType.bIsReference = true;
					}
				}
				else
				{
					auto FloatPin = CastIfFloatType(OriginalInputPin, CompilerContext, SourceGraph);
					if (FloatPin != OriginalInputPin)
					{
						ParamPin = FloatPin;
						ValuePin->PinType = ParamPin->PinType;
					}
				}
			}

			auto Pin = MakeArrayNode->FindPinChecked(MakeArrayNode->GetPinName(idx));
			auto EnumPtr = Cast<UEnum>(ParamPin->PinType.PinSubCategoryObject.Get());
			FString DefaultValue = ParamPin->GetDefaultAsString();
			if (EnumPtr)
			{
				int64 EnumValue = EnumPtr->GetValueByNameString(DefaultValue);

				if (EnumValue > FMath::Min(255ll, EnumPtr->GetMaxEnumValue()) || !EnumPtr->IsValidEnumValue(static_cast<uint8>(EnumValue)))
					CompilerContext.MessageLog.Warning(*FString::Printf(TEXT("please use enum class instead : %s @@"), *EnumPtr->CppType), ParamPin);
				// work same as enum declared in ufunctions
				DefaultValue = LexToString(static_cast<uint8>(EnumValue));
			}

			if (EnumPtr && EnumPtr->GetCppForm() != UEnum::ECppForm::EnumClass && ParamPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte)
			{
				// force as byte enum otherwise it would use int instead
				const bool bForceByte = (PinTypeInfos[idx]->PinType.PinCategory == UEdGraphSchema_K2::PC_Enum || PinTypeInfos[idx]->PinType.PinCategory == UEdGraphSchema_K2::PC_Byte);
				FEdGraphPinType LiteralPinType;
				LiteralPinType.PinCategory = bForceByte ? UEdGraphSchema_K2::PC_Byte : UEdGraphSchema_K2::PC_Int;

				ConvertFunc->FindPinChecked(TEXT("PropertyEnum"))->DefaultValue = bForceByte ? LexToString(uint8(EGMPPropertyClass::Byte)) : LexToString(uint8(EGMPPropertyClass::Int));
				if (!bInputPinLinked)
				{
					auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					NodeMakeLiteral->SetFromFunction(bForceByte ? GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralByte) : GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
					NodeMakeLiteral->AllocateDefaultPins();
					K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);

					UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
					VariablePin->PinType = LiteralPinType;
					// VariablePin = SpawnPureVariable(CompilerContext, SourceGraph, VariablePin, DefaultValue);
					bIsErrorFree &= TryCreateConnection(CompilerContext, VariablePin, ValuePin);
				}
				else
				{
					auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
					NodeMakeLiteral->SetFromFunction(bForceByte ? GMP_UFUNCTION_CHECKED(UGMPBPLib, MakeLiteralByte) : GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
					NodeMakeLiteral->AllocateDefaultPins();
					auto GenericValuePin = NodeMakeLiteral->FindPinChecked(TEXT("Value"));
					if (OriginalInputPin == ParamPin)
						bIsErrorFree &= TryCreateConnection(CompilerContext, ParamPin, GenericValuePin);
					else
						bIsErrorFree &= TryCreateConnection(CompilerContext, ParamPin, GenericValuePin);
					NodeMakeLiteral->NotifyPinConnectionListChanged(GenericValuePin);

					UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
					VariablePin->PinType = LiteralPinType;
					bIsErrorFree &= TryCreateConnection(CompilerContext, VariablePin, ValuePin);
				}
			}
			else
			{
				if (bInputPinLinked)
				{
#if 0
					auto NewPin = CastIfFloatType(OriginalInputPin, CompilerContext, SourceGraph);
					bIsErrorFree &= ensure(NewPin);
					if (NewPin != OriginalInputPin)
					{
						NewPin->MakeLinkTo(ValuePin);
					}
					else
#endif
					{
						bIsErrorFree &= TryCreateConnection(CompilerContext, OriginalInputPin, ValuePin);
					}
				}
				else
				{
					UEdGraphPin* VariablePin = SpawnPureVariable(CompilerContext, SourceGraph, ParamPin, DefaultValue);
					bIsErrorFree &= TryCreateConnection(CompilerContext, VariablePin, ValuePin);
				}
			}

			UEdGraphPin* ResultPin = ConvertFunc->GetReturnValuePin();
			ConvertFunc->NotifyPinConnectionListChanged(ValuePin);
			// ResultPin->MakeLinkTo(Pin);
			bIsErrorFree &= TryCreateConnection(CompilerContext, ResultPin, Pin);
			ConvertFunc->PostReconstructNode();
			MakeArrayNode->NotifyPinConnectionListChanged(Pin);
		}
	}
	return bIsErrorFree;
}

UEdGraphPin* UK2Node_MessageBase::ConstCastIfSelfPin(UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin)
{
	if (!LinkPin)
		LinkPin = TestSelfPin;

	if (TestSelfPin == LinkPin && LinkPin->LinkedTo.Num() > 0)
		LinkPin = LinkPin->LinkedTo[0];

	if (LinkPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Object && LinkPin->PinType.PinSubCategory == UEdGraphSchema_K2::PSC_Self)
	{
		UK2Node_TemporaryVariable* LocalVariable = CompilerContext.SpawnIntermediateNode<UK2Node_TemporaryVariable>(this, SourceGraph);
		LocalVariable->VariableType = LinkPin->PinType;
		LocalVariable->VariableType.PinSubCategory = NAME_None;
		LocalVariable->VariableType.bIsReference = false;
		LocalVariable->VariableType.PinSubCategoryObject = GetBlueprint()->GeneratedClass.Get();
		LocalVariable->AllocateDefaultPins();

		UK2Node_PureAssignmentStatement* AssignDefaultValue = CompilerContext.SpawnIntermediateNode<UK2Node_PureAssignmentStatement>(this, SourceGraph);
		AssignDefaultValue->AllocateDefaultPins();
		const bool bPreviousInputSaved = TryCreateConnection(CompilerContext, LinkPin, AssignDefaultValue->GetValuePin());

		const bool bVariableConnected = TryCreateConnection(CompilerContext, AssignDefaultValue->GetVariablePin(), LocalVariable->GetVariablePin());
		AssignDefaultValue->NotifyPinConnectionListChanged(AssignDefaultValue->GetVariablePin());

		if (!bVariableConnected || !bPreviousInputSaved)
		{
			CompilerContext.MessageLog.Error(*LOCTEXT("AutoCreateRefTermPin_AssignmentError", "AutoCreateRefTerm Expansion: Assignment Error @@").ToString(), AssignDefaultValue);
			BreakAllNodeLinks();
			return nullptr;
		}
		return AssignDefaultValue->GetOutputPin();
	}
	return TestSelfPin;
}

UEdGraphPin* UK2Node_MessageBase::CastIfFloatType(UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin)
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
		ensure(TryCreateConnection(CompilerContext, TestSelfPin, InputRealPin));
		NodeMakeLiteral->NotifyPinConnectionListChanged(InputRealPin);
		UEdGraphPin* VariablePin = NodeMakeLiteral->GetReturnValuePin();
		return VariablePin;
#endif
	} while (false);
	return TestSelfPin;
}

//////////////////////////////////////////////////////////////////////////
UK2Node_MessageBase::UK2Node_MessageBase(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	if (!HasAnyFlags(RF_ClassDefaultObject) && HasAnyFlags(RF_NeedLoad | RF_NeedPostLoad | RF_WillBeLoaded | RF_WasLoaded))
		UMessageTagsManager::OnMessageTagSignatureChanged().AddUObject(this, &UK2Node_MessageBase::OnSignatureChanged);
}

#if GMP_NODE_DETAIL
void UK2Node_MessageBase::RemoveUserDefinedPinByInfo(TSharedRef<FMessagePinTypeInfo> Info)
{
	auto Pin = GetMessagePin(Info->PinIndex);
	Pin->Modify();

	Pins.Remove(Pin);
	Pin->MarkAsGarbage();

	if (UBlueprint* Blueprint = GetBlueprint())
	{
		FKismetDebugUtilities::RemovePinWatch(Blueprint, Pin);
	}
	ParameterTypes.RemoveAt(Info->PinIndex);
	for (auto i = 0; i < ParameterTypes.Num(); ++i)
	{
		ParameterTypes[i]->PinIndex = i;
	}
	if (ensure(GetMessageCount() > 0))
		--GetMessageCount();
}
#endif

void UK2Node_MessageBase::AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector)
{
	UK2Node_MessageBase* This = CastChecked<UK2Node_MessageBase>(InThis);
	for (int32 Index = 0; Index < This->ParameterTypes.Num(); ++Index)
	{
#if UE_5_04_OR_LATER
		Collector.AddReferencedObject(This->ParameterTypes[Index]->PinType.PinSubCategoryObject, This);
		Collector.AddReferencedObject(This->ParameterTypes[Index]->PinType.PinSubCategoryMemberReference.MemberParent, This);
#else
		UObject* PinSubCategoryObject = This->ParameterTypes[Index]->PinType.PinSubCategoryObject.Get();
		Collector.AddReferencedObject(PinSubCategoryObject, This);
#endif
	}

	for (int32 Index = 0; Index < This->ResponseTypes.Num(); ++Index)
	{
#if UE_5_04_OR_LATER
		Collector.AddReferencedObject(This->ResponseTypes[Index]->PinType.PinSubCategoryObject, This);
		Collector.AddReferencedObject(This->ResponseTypes[Index]->PinType.PinSubCategoryMemberReference.MemberParent, This);
#else
		UObject* PinSubCategoryObject = This->ResponseTypes[Index]->PinType.PinSubCategoryObject.Get();
		Collector.AddReferencedObject(PinSubCategoryObject, This);
#endif
	}
	Super::AddReferencedObjects(This, Collector);
}

void UK2Node_MessageBase::PinDefaultValueChanged(UEdGraphPin* ChangedPin)
{
	if (bRecursivelyChangingDefaultValue)
	{
		return;
	}

	if (ChangedPin == GetEventNamePin())
	{
		CachedNodeTitle.SetCachedText(FText::FromString(GetMessageTitle()), this);
		DoRebuild(true);
		GetGraph()->NotifyGraphChanged();
		return;
	}

	// 	if (ChangedPin->Direction == EGPD_Input)
	// 	{
	// 		auto PinIdx = GetPinIndex(ChangedPin);
	// 		if (PinIdx != INDEX_NONE)
	// 		{
	// 			auto PinInfo = ParameterTypes[PinIdx].Info;
	// 			FString DefaultsString = ChangedPin->GetDefaultAsString();
	// 			if (DefaultsString != PinInfo->PinDefaultValue)
	// 			{
	// 				TGuardValue<bool> CircularGuard(bRecursivelyChangingDefaultValue, true);
	// 				ModifyUserDefinedPinDefaultValue(PinInfo, ChangedPin->GetDefaultAsString());
	// 			}
	// 		}
	// 	}
}

#if GMP_NODE_DETAIL
bool UK2Node_MessageBase::ModifyUserDefinedPinDefaultValue(TSharedRef<FMessagePinTypeInfo> Info, const FString& InDefaultValue)
{
	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	FString NewDefaultValue = InDefaultValue;

	UEdGraphPin* OldPin = GetMessagePin(Info->PinIndex);
	FString SavedDefaultValue = OldPin->DefaultValue;
	K2Schema->SetPinAutogeneratedDefaultValue(OldPin, NewDefaultValue);

	FString ErrorString = K2Schema->IsCurrentPinDefaultValid(OldPin);

	if (!ErrorString.IsEmpty())
	{
		NewDefaultValue = SavedDefaultValue;
		K2Schema->SetPinAutogeneratedDefaultValue(OldPin, SavedDefaultValue);

		return false;
	}

	Info->PinDefaultValue = NewDefaultValue;
	return true;
}

UEdGraphPin* UK2Node_MessageBase::CreateUserDefinedPin(const FName InPinName, const FEdGraphPinType& InPinType)
{
	FMessagePinTypeInfoCell NewPinInfo;
	NewPinInfo->PinFriendlyName = InPinName;
	NewPinInfo->PinType = InPinType;
	ParameterTypes.Add(NewPinInfo);

	UEdGraphPin* NewPin = AddMessagePin(GetMessageCount());
	if (NewPin)
	{
		NewPinInfo->PinIndex = GetMessageCount() - 1;
		NewPin->PinType = NewPinInfo->PinType;
#if WITH_EDITORONLY_DATA
		NewPin->PinFriendlyName = FText::FromName(NewPinInfo->PinFriendlyName);
#endif
		const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
		K2Schema->SetPinAutogeneratedDefaultValue(NewPin, NewPinInfo->PinDefaultValue);
	}
	return NewPin;
}
#endif

FEdGraphPinType UK2Node_MessageBase::DefaultPinType = [] {
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_Wildcard;
	PinType.bIsReference = true;
	return PinType;
}();

int UK2Node_MessageBase::GetPinIndex(UEdGraphPin* Pin) const
{
	int32 Index = INDEX_NONE;
	auto Str = ToString(Pin->PinName);
	if (Str.RemoveFromStart(MessageParamPrefix) && Str.IsNumeric())
	{
		Index = FCString::Atoi(*Str);
	}
	if (Str.RemoveFromStart(MessageResponsePrefix) && Str.IsNumeric())
	{
		Index = FCString::Atoi(*Str);
	}
	return Index;
}

FString UK2Node_MessageBase::MessageParamPrefix = TEXT("ParamName");
FString UK2Node_MessageBase::MessageResponsePrefix = TEXT("ResponeName");
FName UK2Node_MessageBase::MessageKeyName = TEXT("EventName");

FText UK2Node_MessageBase::GetMenuCategory() const
{
	return LOCTEXT("GMP_Message_Category", "GMP|Message");
}

void UK2Node_MessageBase::PinConnectionListChanged(UEdGraphPin* ChangedPin)
{
	Super::PinConnectionListChanged(ChangedPin);
	auto Index = GetPinIndex(ChangedPin);
	if (Index != INDEX_NONE)
	{
		RefreshDetail();
	}
}

FName UK2Node_MessageBase::GetCornerIcon() const
{
	if (AuthorityType == EMessageTypeServer)
	{
		return TEXT("Graph.Replication.AuthorityOnly");
	}
	else if (AuthorityType == EMessageTypeClient)
	{
		return TEXT("Graph.Replication.ClientEvent");
	}

	// return TEXT("Graph.Message.MessageIcon");
	return Super::GetCornerIcon();
}

void EditorSearchNodeTitleInBlueprints(const FString& InStr, UBlueprint* Blueprint = nullptr);
void EditorSearchMessageReferences(const FMessageTag& InStr);

bool UK2Node_MessageBase::CanJumpToDefinition() const
{
	return true;
}

void UK2Node_MessageBase::JumpToDefinition() const
{
	FindInBlueprint(true);
}

#if UE_4_24_OR_LATER
void UK2Node_MessageBase::GetMenuEntries(struct FGraphContextMenuBuilder& Context) const
{
	Super::GetMenuEntries(Context);
#if 0
	if (!Context.FromPin)
	{
		struct FBlueprintAction_Lambda : public FEdGraphSchemaAction
		{
			static FName StaticGetTypeId()
			{
				static FName Type("FBlueprintAction_Lambda");
				return Type;
			}
			virtual FName GetTypeId() const { return StaticGetTypeId(); }

			FBlueprintAction_Lambda(const UK2Node_MessageBase* InNode, TFunction<void(UK2Node_MessageBase*)> Lambda, const FText& InMenuDesc, const FText& InToolTip, const FText& InNodeCategory = FText::GetEmpty())
				: FEdGraphSchemaAction(InNodeCategory, InMenuDesc, InToolTip, 1)
				, Node(InNode)
				, Func(Lambda)
			{
			}
			virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override
			{
				if (Node.IsValid() && Func)
					Func(Node.Get());
				return NULL;
			}
			TWeakObjectPtr<UK2Node_MessageBase> Node;
			TFunction<void(UK2Node_MessageBase*)> Func;
		};

		Context.AddAction(MakeShareable(new FBlueprintAction_Lambda(
			this,
			[](auto* Node) { EditorFindMessageInBlueprints(Node->GetMessageKey(), Node->GetBlueprint()); },
			LOCTEXT("SearchSame", "SearchSame"),
			LOCTEXT("SearchSameTips", "SearchSameMessage"))));

		if (AuthorityType != EMessageTypeClient)
		{
			Context.AddAction(MakeShareable(new FBlueprintAction_Lambda(
				this,
				[](auto* Node) { Node->SetAuthorityType(EMessageTypeClient); },
				LOCTEXT("SetClient", "Set Client Only"),
				LOCTEXT("SetClientTooltip", "Set Client Only"))));
		}
		if (AuthorityType != EMessageTypeServer)
		{
			Context.AddAction(MakeShareable(new FBlueprintAction_Lambda(
				this,
				[](auto* Node) { Node->SetAuthorityType(EMessageTypeServer); },
				LOCTEXT("SetServer", "Set Server Only"),
				LOCTEXT("SetServerTooltip", "Set Server Only"))));
		}
		if (AuthorityType != EMessageTypeBoth)
		{
			Context.AddAction(MakeShareable(new FBlueprintAction_Lambda(
				this,
				[](auto* Node) { Node->SetAuthorityType(EMessageTypeBoth); },
				LOCTEXT("SetBoth", "Set Both Side"),
				LOCTEXT("SetBothTooltip", "Set Both Side"))));
		}
	}
#endif
}

void UK2Node_MessageBase::GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	Super::GetNodeContextMenuActions(Menu, Context);
	if (!Context->bIsDebugging && !Context->Pin)
	{
		FToolMenuSection& Section = Menu->AddSection("K2Node_MessageBase", LOCTEXT("K2NodeMessageMenu", "Message Op"));
		TWeakObjectPtr<UK2Node_MessageBase> MutableThis = const_cast<UK2Node_MessageBase*>(this);
		if (!GetMessageKey().IsEmpty())
		{
			Section.AddMenuEntry("FindWithinBlueprint",
								 LOCTEXT("FindWithinBlueprint", "FindWithinBlueprint"),
								 LOCTEXT("FindWithinBlueprintTips", "Find In This Blueprint"),
								 FSlateIcon(),
								 FUIAction(FExecuteAction::CreateLambda([MutableThis] {
									 if (MutableThis.IsValid())
										 EditorSearchNodeTitleInBlueprints(MutableThis->GetMessageKey(), MutableThis->GetBlueprint());
								 })));

			Section.AddMenuEntry("SearchReferences", LOCTEXT("SearchReferences", "SearchReferences"), LOCTEXT("SearchReferencesTips", "Search All Tags References"), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([MutableThis] {
									 if (MutableThis.IsValid())
										 EditorSearchMessageReferences(MutableThis->MsgTag);
								 })));
		}

		if (AuthorityType != EMessageTypeClient)
		{
			Section.AddMenuEntry("SetClient", LOCTEXT("SetClient", "Set Client Only"), LOCTEXT("SetClientTooltip", "Set Client Only"), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([MutableThis] {
									 if (MutableThis.IsValid())
									 {
										 MutableThis->SetAuthorityType(EMessageTypeClient);
										 MutableThis->GetGraph()->NotifyGraphChanged();
									 }
								 })));
		}
		if (AuthorityType != EMessageTypeServer)
		{
			Section.AddMenuEntry("SetServer", LOCTEXT("SetServer", "Set Server Only"), LOCTEXT("SetServerTooltip", "Set Server Only"), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([MutableThis] {
									 if (MutableThis.IsValid())
									 {
										 MutableThis->SetAuthorityType(EMessageTypeServer);
										 MutableThis->GetGraph()->NotifyGraphChanged();
									 }
								 })));
		}
		if (AuthorityType != EMessageTypeBoth)
		{
			Section.AddMenuEntry("SetBoth", LOCTEXT("SetBoth", "Set Both Side"), LOCTEXT("SetBothTooltip", "Set Both Side"), FSlateIcon(), FUIAction(FExecuteAction::CreateLambda([MutableThis] {
									 if (MutableThis.IsValid())
									 {
										 MutableThis->SetAuthorityType(EMessageTypeBoth);
										 MutableThis->GetGraph()->NotifyGraphChanged();
									 }
								 })));
		}
	}
}

#else
void UK2Node_MessageBase::GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const
{
	Super::GetContextMenuActions(Context);
	if (!Context.bIsDebugging && !Context.Pin)
	{
		if (!GetMessageKey().IsEmpty())
		{
			static FName CommutativeAssociativeBinaryOperatorNodeName = FName("SearchBlueprintOperatorNode");
			FText CommutativeAssociativeBinaryOperatorStr = LOCTEXT("SearchBlueprintOperatorNode", "SearchBlueprint");
			Context.MenuBuilder->BeginSection(CommutativeAssociativeBinaryOperatorNodeName, CommutativeAssociativeBinaryOperatorStr);
			Context.MenuBuilder->AddMenuEntry(LOCTEXT("FindInBlueprint", "FindInBlueprint"),
											  LOCTEXT("FindInBlueprintTips", "Find In This or All Blueprint"),
											  FSlateIcon(),
											  FUIAction(FExecuteAction::CreateUObject(this, &UK2Node_MessageBase::FindInBlueprint, true)));
			Context.MenuBuilder->AddMenuEntry(LOCTEXT("SearchReferences", "SearchReferences"),
											  LOCTEXT("SearchReferencesTips", "Search All Tags References"),
											  FSlateIcon(),
											  FUIAction(FExecuteAction::CreateUObject(this, &UK2Node_MessageBase::SearchReferences)));
			Context.MenuBuilder->EndSection();
		}

		static FName NodeName = FName("UK2Node_MessageBase");
		FText NodeStr = LOCTEXT("UK2Node_MessageBase", "SetAuthorityType");
		Context.MenuBuilder->BeginSection(NodeName, NodeStr);
		if (AuthorityType != EMessageTypeClient)
		{
			Context.MenuBuilder->AddMenuEntry(LOCTEXT("SetClient", "Set Client Only"),
											  LOCTEXT("SetClientTooltip", "Set Client Only"),
											  FSlateIcon(),
											  FUIAction(FExecuteAction::CreateUObject(this, &UK2Node_MessageBase::SetAuthorityType, (uint8)EMessageTypeClient)));
		}
		if (AuthorityType != EMessageTypeServer)
		{
			Context.MenuBuilder->AddMenuEntry(LOCTEXT("SetServer", "Set Server Only"),
											  LOCTEXT("SetServerTooltip", "Set Server Only"),
											  FSlateIcon(),
											  FUIAction(FExecuteAction::CreateUObject(this, &UK2Node_MessageBase::SetAuthorityType, (uint8)EMessageTypeServer)));
		}
		if (AuthorityType != EMessageTypeBoth)
		{
			Context.MenuBuilder->AddMenuEntry(LOCTEXT("SetBoth", "Set Both Side"),
											  LOCTEXT("SetBothTooltip", "Set Both Side"),
											  FSlateIcon(),
											  FUIAction(FExecuteAction::CreateUObject(this, &UK2Node_MessageBase::SetAuthorityType, (uint8)EMessageTypeBoth)));
		}
		Context.MenuBuilder->EndSection();
	}
}
#endif

FText UK2Node_MessageBase::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	if (CachedNodeTitle.IsOutOfDate(this))
	{
		CachedNodeTitle.SetCachedText(FText::FromString(GetMessageTitle()), this);
	}
	return CachedNodeTitle;
}

FText UK2Node_MessageBase::GetPinDisplayName(const UEdGraphPin* Pin) const
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
			DisplayName = GetK2Schema(this)->UEdGraphSchema::GetPinDisplayName(Pin);

			if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				FName DisplayFName(*DisplayName.ToString(), FNAME_Find);
				if ((ToGraphPinNameType(DisplayFName) == UEdGraphSchema_K2::PN_Execute) || (ToGraphPinNameType(DisplayFName) == UEdGraphSchema_K2::PN_Then))
				{
					DisplayName = FText::GetEmpty();
				}
			}
		}
	}
	return DisplayName;
}

FString UK2Node_MessageBase::GetTitleHead() const
{
	return FString("MessageBase");
}

FString UK2Node_MessageBase::GetMessageKey(bool bWithModifies) const
{
	auto GetKey = [&](auto Pin) {
		if (Pin)
		{
			auto Key = Pin->GetDefaultAsString();
			if (bWithModifies && !Key.IsEmpty())
			{
				return FString::Printf(TEXT("('%s')"), *Key);
			}
			else
			{
				return Key;
			}
		}
		return FString();
	};

	auto Key = GetKey(GetEventNamePin());
	return Key;
}

FString UK2Node_MessageBase::GetMessageTitle(bool bWithMessageKey) const
{
	return FString::Printf(TEXT("%s%s"), *GetTitleHead(), !bWithMessageKey ? TEXT("") : *GetMessageKey(true));
}

void UK2Node_MessageBase::FindInBlueprint(bool bWithinBlueprint) const
{
	auto MessageKey = GetMessageKey(true);
	if (MessageKey.IsEmpty())
		MessageKey = GetTitleHead();
	EditorSearchNodeTitleInBlueprints(FString::Printf(TEXT("('%s')"), *MessageKey), bWithinBlueprint ? GetBlueprint() : nullptr);
}

void UK2Node_MessageBase::SearchReferences() const
{
	EditorSearchMessageReferences(MsgTag);
}

void UK2Node_MessageBase::PostPasteNode()
{
	Super::PostPasteNode();
	RefreashMessagePin(true);
	DoRebuild(false);
	GetGraph()->NotifyGraphChanged();
}

void UK2Node_MessageBase::PostLoad()
{
	Super::PostLoad();
	RefreashMessagePin();
}

void UK2Node_MessageBase::PostReconstructNode()
{
	Super::PostReconstructNode();
	RefreashMessagePin();
}

void UK2Node_MessageBase::EarlyValidation(class FCompilerResultsLog& MessageLog) const
{
	Super::EarlyValidation(MessageLog);
	do
	{
		if (!IsAllowLatentFuncs() && ResponseTypes.Num() > 0)
		{
			MessageLog.Error(TEXT("unsopported MSGKEY type @@"), GetEventNamePin());
			break;
		}

		auto Node = UMessageTagsManager::Get().FindTagNode(MsgTag.GetTagName());
		if (Node)
		{
			if (Node->Parameters.Num() != ParameterTypes.Num() || Node->ResponseTypes.Num() != ResponseTypes.Num())
			{
				MessageLog.Error(TEXT("parameter count error @@"), GetEventNamePin());
				break;
			}

			for (auto i = 0; i < Node->Parameters.Num(); ++i)
			{
				FEdGraphPinType DesiredPinType;
				bool bMatch = GMPReflection::PinTypeFromString(Node->Parameters[i].Type.ToString(), DesiredPinType) && MatchPinTypes(DesiredPinType, ParameterTypes[i]->PinType);
				if (!bMatch)
				{
					MessageLog.Error(*FString::Printf(TEXT("PinType:%s mismatch @@"), *Node->Parameters[i].Type.ToString()), GetMessagePin(i, const_cast<TArray<UEdGraphPin*>*>(&Pins), false));
				}
			}

			for (auto i = 0; i < Node->ResponseTypes.Num(); ++i)
			{
				FEdGraphPinType DesiredPinType;
				bool bMatch = GMPReflection::PinTypeFromString(Node->ResponseTypes[i].Type.ToString(), DesiredPinType) && MatchPinTypes(DesiredPinType, ResponseTypes[i]->PinType);
				if (!bMatch)
				{
					MessageLog.Error(*FString::Printf(TEXT("PinType:%s mismatch @@"), *Node->ResponseTypes[i].Type.ToString()), GetResponsePin(i, const_cast<TArray<UEdGraphPin*>*>(&Pins), false));
				}
			}
		}
		else if (!ShouldIgnoreMetaOnRunningCommandlet())
		{
			MessageLog.Error(*FString::Printf(TEXT("missing messagetag [%s] @@"), *MsgTag.GetTagName().ToString()), GetEventNamePin());
			break;
		}

		if (IsAllowLatentFuncs() || !Cast<UK2Node_ListenMessage>(this))
		{
			if (GetMessageCount() != ParameterTypes.Num())
			{
				MessageLog.Error(TEXT("parameter count error @@"), GetEventNamePin());
				break;
			}

			for (auto i = 0; i < ParameterTypes.Num(); ++i)
			{
				auto& Parameter = ParameterTypes[i];
				auto TestPin = GetMessagePin(i, const_cast<TArray<UEdGraphPin*>*>(&Pins), false);
				if (!TestPin || !MatchPinTypes(Parameter->PinType, TestPin->PinType))
				{
					MessageLog.Error(TEXT("PinType mismatch @@"), TestPin);
				}
			}

			for (auto i = 0; i < ResponseTypes.Num(); ++i)
			{
				auto& Response = ResponseTypes[i];
				auto TestPin = GetResponsePin(i, const_cast<TArray<UEdGraphPin*>*>(&Pins), false);
				if (!TestPin || !MatchPinTypes(Response->PinType, TestPin->PinType))
				{
					MessageLog.Error(TEXT("PinType mismatch @@"), TestPin);
				}
			}
		}
	} while (false);
}

bool UK2Node_MessageBase::IsCompatibleWithGraph(UEdGraph const* TargetGraph) const
{
	UBlueprint* MyBlueprint = FBlueprintEditorUtils::FindBlueprintForGraph(TargetGraph);
	bool bIsCompatible = MyBlueprint && FBlueprintEditorUtils::DoesSupportEventGraphs(MyBlueprint);
	return bIsCompatible && (IsAllowLatentFuncs(TargetGraph) || (GetK2Schema(this)->GetGraphType(TargetGraph) == GT_Function && ResponseTypes.Num() == 0));
}

bool UK2Node_MessageBase::IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const
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

bool UK2Node_MessageBase::RefreashMessagePin(bool bClearError)
{
	if (!MsgTag.IsValid())
	{
		auto KeyName = GetMessageKey();
		MsgTag = FMessageTag::RequestMessageTag(*KeyName, false);
		bMatchTag = KeyName.IsEmpty() || MsgTag.IsValid();
		if (MsgTag.IsValid())
			FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
		if (!bMatchTag && bClearError)
			GetEventNamePin()->ResetDefaultValue();
	}
	else
	{
		bMatchTag = true;
		if (auto Pin = GetEventNamePin())
		{
			if (Pin->DefaultValue != MsgTag.ToString())
			{
				GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Pin, MsgTag.ToString());
				Pin->Modify();
				FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
				GetGraph()->NotifyGraphChanged();
			}
		}
	}
	return bMatchTag;
}

bool UK2Node_MessageBase::IsAllowLatentFuncs(const UEdGraph* InGraph) const
{
	InGraph = InGraph ? InGraph : GetGraph();
	bAllowLatentFuncs = GetK2Schema(this)->GetGraphType(InGraph) == GT_Ubergraph;
	return bAllowLatentFuncs;
}

UEdGraphPin* UK2Node_MessageBase::SpawnPureVariable(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InVariablePin, const FString& DefaultValue, bool bConst)
{
	if (!SourceGraph)
		return nullptr;
	auto VarType = InVariablePin->PinType;
	const UEdGraphSchema_K2* K2Schema = CompilerContext.GetSchema();
	if (!VarType.IsContainer())
	{
		if (VarType.PinCategory == UEdGraphSchema_K2::PC_Int)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
#if UE_5_00_OR_LATER
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Int64)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralInt64));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Double)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralDouble));
			NodeMakeLiteral->AllocateDefaultPins();
			K2Schema->TrySetDefaultValue(*NodeMakeLiteral->FindPinChecked(TEXT("Value")), DefaultValue);
			return NodeMakeLiteral->GetReturnValuePin();
		}
#endif
		else if (VarType.PinCategory == UEdGraphSchema_K2::PC_Float)
		{
			auto NodeMakeLiteral = CompilerContext.SpawnIntermediateNode<UK2Node_CallFunction>(this, SourceGraph);
#if UE_5_01_OR_LATER
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralDouble));
#else
			NodeMakeLiteral->SetFromFunction(GMP_UFUNCTION_CHECKED(UKismetSystemLibrary, MakeLiteralFloat));
#endif
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
	bool bIsErrorFree = TryCreateConnection(CompilerContext, PureAssignNode->GetVariablePin(), VariablePin);
	PureAssignNode->PinConnectionListChanged(PureAssignNode->GetVariablePin());

	if (Pins.Contains(InVariablePin))
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, InVariablePin, PureAssignNode->GetValuePin());
	}
	else
	{
		bIsErrorFree &= TryCreateConnection(CompilerContext, InVariablePin, PureAssignNode->GetValuePin());
	}

	if (!bIsErrorFree)
	{
		CompilerContext.MessageLog.Error(*LOCTEXT("AutoCreateRefTermPin_AssignmentError", "SpawnPureVariable Expansion: Assignment Error @@").ToString(), PureAssignNode);
		BreakAllNodeLinks();
		return nullptr;
	}

	return PureAssignNode->GetOutputPin();
}

void UK2Node_MessageBase::DoRebuild(bool bNewTag, TArray<UEdGraphPin*>* InOldPins)
{
	auto EventNamePin = GetEventNamePin(InOldPins);
	MsgTag = FMessageTag::RequestMessageTag(*EventNamePin->GetDefaultAsString(), false);
	auto Node = UMessageTagsManager::Get().FindTagNode(MsgTag.GetTagName());
	bool bNodeValid = Node.IsValid();

	ParameterTypes.Empty(bNodeValid ? Node->Parameters.Num() : 0);
	ResponseTypes.Empty(bNodeValid ? Node->ResponseTypes.Num() : 0);

	int32 ParamIndex = 0;
	int32 ResponeIndex = 0;

	if (!bNodeValid)
		EventNamePin->PinToolTip = TEXT("select a MessageTag as message key");
	else
		EventNamePin->PinToolTip = FString::Printf(TEXT("KEY : %s \n\n%s"), *MsgTag.ToString(), Node->GetComment().IsEmpty() ? *Node->GetComment() : TEXT("No Comment"));

	bool bChanged = false;
	if (bNodeValid)
	{
		for (auto& Parameter : Node->Parameters)
		{
			auto OldPin = GetMessagePin(ParamIndex, InOldPins, false);
			//ensureAlways(OldPin);
			auto NewPin = GetMessagePin(ParamIndex, &Pins, false);
			auto& Ref = Add_GetRef(ParameterTypes);
			Ref->PinIndex = ParamIndex++;
			Ref->PinFriendlyName = Parameter.Name;
			if (GMPReflection::PinTypeFromString(Parameter.Type.ToString(), Ref->PinType))
			{
				if (OldPin && OldPin->Direction == EGPD_Input)
				{
					Ref->PinDefaultValue = OldPin->GetDefaultAsString();
				}
				else
				{
					Ref->PinDefaultValue = GMPReflection::GetDefaultValueOnType(Ref->PinType);
				}
			}
			else
			{
				Ref->PinType = DefaultPinType;
			}

			bChanged = !(OldPin && NewPin) || (!(MatchPinTypes(NewPin->PinType, OldPin->PinType)) || OldPin->PinToolTip != NewPin->PinToolTip || OldPin->PinFriendlyName.EqualTo(NewPin->PinFriendlyName));
		}
		for (auto& Respone : Node->ResponseTypes)
		{
			auto OldPin = GetResponsePin(ResponeIndex, InOldPins, false);
			//ensureAlways(OldPin);
			auto NewPin = GetResponsePin(ResponeIndex, &Pins, false);
			auto& Ref = Add_GetRef(ResponseTypes);
			Ref->PinIndex = ResponeIndex++;
			Ref->PinFriendlyName = Respone.Name;
			if (GMPReflection::PinTypeFromString(Respone.Type.ToString(), Ref->PinType))
			{
				if (OldPin && OldPin->Direction == EGPD_Input)
				{
					Ref->PinDefaultValue = OldPin->GetDefaultAsString();
				}
				else
				{
					Ref->PinDefaultValue = GMPReflection::GetDefaultValueOnType(Ref->PinType);
				}
			}
			else
			{
				Ref->PinType = DefaultPinType;
			}

			bChanged = !(OldPin && NewPin) || (!(MatchPinTypes(NewPin->PinType, OldPin->PinType)) || OldPin->PinToolTip != NewPin->PinToolTip || OldPin->PinFriendlyName.EqualTo(NewPin->PinFriendlyName));
		}
	}

	if (InOldPins && !bChanged)
	{
		return;
	}

	WritebackPins.Empty();
	TArray<UEdGraphPin*> OldPins = Pins;
	TArray<UEdGraphPin*> RemovedPins;
	for (int32 PinIdx = Pins.Num() - 1; PinIdx > 0; PinIdx--)
	{
		if (!Pins[PinIdx])
			continue;
		auto Str = ToString(Pins[PinIdx]->PinName);
		if (Str.StartsWith(MessageParamPrefix) || Str.StartsWith(MessageResponsePrefix))
		{
			RemovedPins.Add(Pins[PinIdx]);
			Pins.RemoveAt(PinIdx);
		}
		else if (auto RspPin = FindPin(TEXT("Response"), EGPD_Input))
		{
			RemovedPins.Add(Pins[PinIdx]);
			Pins.RemoveAt(PinIdx);
		}
		else if (auto OnRspPin = FindPin(TEXT("OnResponse"), EGPD_Output))
		{
			RemovedPins.Add(Pins[PinIdx]);
			Pins.RemoveAt(PinIdx);
		}
	}

	GetMessageCount() = 0;
	RestoreSplitPins(OldPins);

	const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
	for (int32 i = 0; i < ParameterTypes.Num(); ++i)
	{
		if (UEdGraphPin* NewPin = AddMessagePin(i, false))
		{
			K2Schema->SetPinAutogeneratedDefaultValue(NewPin, ParameterTypes[i]->PinDefaultValue);
#if WITH_EDITORONLY_DATA
			if (ParameterTypes[i]->PinFriendlyName.IsValid() && !ParameterTypes[i]->PinFriendlyName.IsNone())
			{
				NewPin->PinFriendlyName = FText::FromName(ParameterTypes[i]->PinFriendlyName);
			}

			if (NewPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
				NewPin->PinToolTip = FString::Printf(TEXT("TypeError : %s"), *Node->Parameters[i].Type.ToString());
		}
#endif
	}

	if (ResponseTypes.Num() > 0)
	{
		auto ResponePin = CreateResponseExecPin();
		for (int32 i = 0; i < ResponseTypes.Num(); ++i)
		{
			if (UEdGraphPin* NewPin = AddResponsePin(i, false))
			{
				K2Schema->SetPinAutogeneratedDefaultValue(NewPin, ResponseTypes[i]->PinDefaultValue);
#if WITH_EDITORONLY_DATA
				if (ResponseTypes[i]->PinFriendlyName.IsValid() && !ResponseTypes[i]->PinFriendlyName.IsNone())
				{
					NewPin->PinFriendlyName = FText::FromName(ResponseTypes[i]->PinFriendlyName);
				}

				if (NewPin->PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard)
					NewPin->PinToolTip = FString::Printf(TEXT("TypeError : %s"), *Node->ResponseTypes[i].Type.ToString());
			}
#endif
		}
	}

#if !UE_4_20_OR_LATER
#define GMP_TAIL_NULLPTR
#else
#define GMP_TAIL_NULLPTR , nullptr
#endif
	RewireOldPinsToNewPins(RemovedPins, Pins GMP_TAIL_NULLPTR);
#undef GMP_TAIL_NULLPTR

	if (!bNewTag)
	{
		for (auto OldPin : RemovedPins)
		{
			if (OldPin->Direction == EGPD_Input && !OldPin->HasAnyConnections())
			{
				if (auto NewPin = FindPin(OldPin->PinName))
				{
					NewPin->DefaultObject = OldPin->DefaultObject;
					NewPin->DefaultTextValue = OldPin->DefaultTextValue;
					NewPin->DefaultValue = OldPin->DefaultValue;
				}
			}
		}
	}
	// GetGraph()->NotifyGraphChanged();
}

void UK2Node_MessageBase::RefreshDetail() const
{
	auto Pined = Details.Pin();
	if (Pined.IsValid())
	{
		if (IPropertyUtilities* Ptr = &Pined->GetPropertyUtilities().Get())
			Ptr->ForceRefresh();
	}
}

void UK2Node_MessageBase::AllocateMsgKeyTagPin()
{
	FEdGraphPinType PinType;
	PinType.PinCategory = UEdGraphSchema_K2::PC_String;
	auto Pin = CreatePin(EGPD_Input, PinType, ToGraphPinNameType(MessageKeyName));
	Pin->PinToolTip = TEXT("select a MessageTag as message key");
#if WITH_EDITORONLY_DATA
	Pin->PinFriendlyName = LOCTEXT("Event ID", "MSGKEY");
#endif
	Pin->bNotConnectable = true;
}

void UK2Node_MessageBase::AllocateDefaultPins()
{
	CallAllocateDefaultPinsImpl();
}

void UK2Node_MessageBase::CallAllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins)
{
	AllocateDefaultPinsImpl(InOldPins);
}

void UK2Node_MessageBase::ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins)
{
	CallAllocateDefaultPinsImpl(&OldPins);
	DoRebuild(false, &OldPins);
	RestoreSplitPins(OldPins);
}

UEdGraphPin* UK2Node_MessageBase::GetEventNamePin(const TArray<UEdGraphPin*>* InOldPins) const
{
	static auto PinName = ToGraphPinNameType(MessageKeyName);
	auto ArrPins = InOldPins ? InOldPins : &Pins;
	auto Ret = ArrPins->FindByPredicate([](auto&& a) { return a->PinName == PinName; });
	return Ret ? *Ret : nullptr;
}

bool UK2Node_MessageBase::MatchPinTypes(const FEdGraphPinType& Lhs, const FEdGraphPinType& Rhs)
{
	return (Lhs.ContainerType == Rhs.ContainerType && Rhs.PinCategory == Rhs.PinCategory && Lhs.PinSubCategory == Rhs.PinSubCategory && Lhs.PinSubCategoryObject == Rhs.PinSubCategoryObject && Lhs.PinValueType == Rhs.PinValueType);
}

bool UK2Node_MessageBase::TryCreateConnection(FKismetCompilerContext& CompilerContext, UEdGraphPin* InPinA, UEdGraphPin* InPinB, bool bMove /*= true*/)
{
	bool bIsErrorFree = ensureAlways(InPinA && InPinB && !(Pins.Contains(InPinA) && Pins.Contains(InPinB)));
	if (bIsErrorFree)
	{
		if (Pins.Contains(InPinA))
		{
			bIsErrorFree &= bMove ? CompilerContext.MovePinLinksToIntermediate(*InPinA, *InPinB).CanSafeConnect() : CompilerContext.CopyPinLinksToIntermediate(*InPinA, *InPinB).CanSafeConnect();
		}
		else if (Pins.Contains(InPinB))
		{
			bIsErrorFree &= bMove ? CompilerContext.MovePinLinksToIntermediate(*InPinB, *InPinA).CanSafeConnect() : CompilerContext.CopyPinLinksToIntermediate(*InPinB, *InPinA).CanSafeConnect();
		}
		else
		{
			bIsErrorFree &= CompilerContext.GetSchema()->TryCreateConnection(InPinA, InPinB);
		}
	}
	return ensure(bIsErrorFree);
}

const UEdGraphSchema_K2* UK2Node_MessageBase::GetK2Schema(const FKismetCompilerContext& CompilerContext)
{
	return CompilerContext.GetSchema();
}

const UEdGraphSchema_K2* UK2Node_MessageBase::GetK2Schema(const UK2Node* Node)
{
	return (Node && Cast<UEdGraph>(Node->GetOuter()) && Node->GetGraph()->GetSchema()) ? Cast<const UEdGraphSchema_K2>(Node->GetGraph()->GetSchema()) : GetDefault<UEdGraphSchema_K2>();
}

void UK2Node_MessageBase::SetAuthorityType(uint8 Type)
{
	if (AuthorityType == Type)
		return;

	if (Type == EMessageTypeServer)
		AuthorityType = EMessageTypeServer;
	else if (Type == EMessageTypeClient)
		AuthorityType = EMessageTypeClient;
	else
		AuthorityType = EMessageTypeBoth;

	FBlueprintEditorUtils::MarkBlueprintAsModified(GetBlueprint());
}

// void UK2Node_MessageBase::AddInputPin()
// {
// 	// 	if (!IsMessageSignatureRegistered())
// 	// 	{
// 	// 		auto NewPin = AddMessagePin();
// 	// 	}
// 	// 	else
// 	{
// 		const FName NewPinName = *FString::Printf(TEXT("Param%d"), ParameterTypes.Num());
// 		auto NewPin = CreateUserDefinedPin(NewPinName, DefaultPinType);
// 	}
// }
//
// bool UK2Node_MessageBase::CanAddPin() const
// {
// 	return !IsMessageSignatureRegistered();
// }

void UK2Node_MessageBase::GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const
{
	UClass* ActionKey = GetClass();
	// Do a first time registration using the node's class to pull in all existing actions
	if (ActionKey->HasAnyClassFlags(CLASS_Abstract) || !ActionRegistrar.IsOpenForRegistration(ActionKey))
		return;

	static TSet<FWeakObjectPtr> Registered;
	bool bExisted = false;
	Registered.Add(ActionKey, &bExisted);
	if (!bExisted)
	{
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		if (AssetRegistry.IsLoadingAssets())
		{
			AssetRegistry.OnFilesLoaded().AddLambda([WeakCls{MakeWeakObjectPtr(ActionKey)}]() { if(WeakCls.IsValid()) FBlueprintActionDatabase::Get().RefreshClassActions(WeakCls.Get()); });
		}
	}

	UBlueprintNodeSpawner* NodeSpawner = UBlueprintNodeSpawner::Create(ActionKey);
	check(NodeSpawner != nullptr);
	NodeSpawner->DefaultMenuSignature.Category = GetMenuCategory();

	ActionRegistrar.AddBlueprintAction(ActionKey, NodeSpawner);
}

int32& UK2Node_MessageBase::GetMessageCount() const
{
	static int32 s_count = 0;
	check(false);
	return s_count;
}
#if WITH_EDITOR
void SGraphNodeMessageBase::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	auto PinObj = PinToAdd->GetPinObj();
	auto PinName = PinObj->GetFName();
	auto Node = CastChecked<UK2Node_MessageBase>(GraphNode);
	if (PinObj->Direction == EGPD_Input && ToGraphPinNameType(PinName) == UK2Node_MessageBase::MessageKeyName && !Node->RefreashMessagePin() && !Node->GetMessageKey().IsEmpty())
	{
		PinToAdd->SetOwner(SharedThis(this));
		auto AddMsgKeyBtn = PropertyCustomizationHelpers::MakeAddButton(CreateWeakLambda(Node, [Node] {
			do
			{
				if (Node->RefreashMessagePin() && !Node->GetMessageKey().IsEmpty())
					break;

				const UEdGraphSchema_K2* K2Schema = GetDefault<UEdGraphSchema_K2>();
				auto EventNamePin = Node->GetEventNamePin();
				auto MsgTag = FMessageTag::RequestMessageTag(*EventNamePin->GetDefaultAsString(), false);

				TArray<FMessageParameter> ParamterData;
				ParamterData.Empty(Node->ParameterTypes.Num());
				for (auto& a : Node->ParameterTypes)
				{
					ParamterData.Add(FMessageParameter{a->PinFriendlyName, GMPReflection::GetPinPropertyName(a->PinType)});
				}
				TArray<FMessageParameter> ResponseData;
				ResponseData.Empty(Node->ResponseTypes.Num());
				for (auto& a : Node->ResponseTypes)
				{
					ResponseData.Add(FMessageParameter{a->PinFriendlyName, GMPReflection::GetPinPropertyName(a->PinType)});
				}
				IMessageTagsEditorModule::Get().AddNewMessageTagToINI(*EventNamePin->GetDefaultAsString(), TEXT(""), FMessageTagSource::GetDefaultName(), false, true, ParamterData, ResponseData);
				Node->DoRebuild(true);
				Node->GetGraph()->NotifyGraphChanged();
			} while (false);
		}));
		AddMsgKeyBtn->SetToolTipText(LOCTEXT("CreateMissingMessageKey", "CreateMissingMessageKey"));
		AddMsgKeyBtn->SetCursor(EMouseCursor::Default);
		AddMsgKeyBtn->SetVisibility(TAttribute<EVisibility>::Create(CreateWeakLambda(Node, [Node] { return (!Node->GetMessageKey().IsEmpty() && !Node->RefreashMessagePin()) ? EVisibility::Hidden : EVisibility::Visible; })));
		AddMsgKeyBtn->SetEnabled(TAttribute<bool>(PinToAdd, &SGraphPin::IsEditingEnabled));

		LeftNodeBox->AddSlot()
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
				AddMsgKeyBtn
			]
		];

		OutputPins.Add(PinToAdd);
		return;
	}
	SGraphNodeK2Base::AddPin(PinToAdd);
}

void SGraphNodeMessageBase::Construct(const FArguments& InArgs, UK2Node_MessageBase* InNode)
{
	this->GraphNode = InNode;
	this->UpdateGraphNode();
}

void SGraphNodeMessageBase::CreateStandardPinWidget(UEdGraphPin* Pin)
{
	auto RealNode = Cast<UK2Node_MessageBase>(GraphNode);

	if (Pin == RealNode->GetEventNamePin())
	{
		if (RealNode->RefreashMessagePin())
		{
			const bool bShowPin = ShouldPinBeHidden(Pin);
			if (bShowPin)
			{
				RealNode->TagHolder = MakeShared<FMessageTagContainer>();
				TSharedPtr<SGraphPin> NewPin = SNew(SMessageTagGraphPin, Pin)
												.bRawName(true)
												.TagContainer(RealNode->TagHolder);
				check(NewPin.IsValid());
				this->AddPin(NewPin.ToSharedRef());
			}
			return;
		}
	}
	SGraphNodeK2Base::CreateStandardPinWidget(Pin);
}
#endif
#undef LOCTEXT_NAMESPACE
