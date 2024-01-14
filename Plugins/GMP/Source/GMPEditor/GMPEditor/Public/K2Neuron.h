// Copyright K2Neuron, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#include "EdGraph/EdGraphNodeUtils.h"
#include "K2Node.h"
#include "Templates/SubclassOf.h"
#include "UObject/ObjectMacros.h"
#include "UnrealCompatibility.h"
#include "Engine/MemberReference.h"

#if WITH_EDITOR
#include "KismetNodes/SGraphNodeK2Default.h"
#endif

#include "K2Neuron.generated.h"

class FBlueprintActionDatabaseRegistrar;
class FKismetCompilerContext;
class UEdGraph;
class UEdGraphPin;
class UEdGraphSchema_K2;
class UK2Node_CustomEvent;
class UK2Node_TemporaryVariable;
class UK2Node_BaseMCDelegate;
class UK2Node_Event;
class UK2Node_CallFunction;
struct FEdGraphPinType;

#ifndef K2NEURON_API
#define K2NEURON_API GMPEDITOR_API
#endif

USTRUCT()
struct K2NEURON_API FEdPinExtraMeta
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FMemberReference MemberRef;

	UPROPERTY()
	TMap<FString, FString> MemberMetas;


	UPROPERTY()
	TArray<FMemberReference> MemberRefs;
};

UCLASS(abstract)
class K2NEURON_API UK2Neuron : public UK2Node
{
	GENERATED_BODY()
public:
	static void FindInBlueprint(const FString& InStr, UBlueprint* Blueprint = nullptr);

	static const bool HasVariadicSupport;

	UK2Neuron(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	static void StaticAssignProperty(UK2Node_BaseMCDelegate* InNode, const FProperty* Property, bool bSelfContext = false, UClass* OwnerClass = nullptr);
	static const UEdGraphSchema_K2* GetK2Schema(const FKismetCompilerContext& CompilerContext);
	static const UEdGraphSchema_K2* GetK2Schema(const UK2Node* InNode);
	const UEdGraphSchema_K2* GetK2Schema() const { return GetK2Schema(this); }
	static UClass* GetBlueprintClass(const UK2Node* InNode);
	UClass* GetBlueprintClass() const { return GetBlueprintClass(this); }

	static bool IsInputParameter(const FProperty* FuncParam, bool bEnsure = false);
	bool IsExecPin(UEdGraphPin* Pin, EEdGraphPinDirection Direction = EGPD_MAX);

	bool IsExpandEnumAsExec(UFunction* Function, UEnum** OutEnum = nullptr, FProperty** OutProp = nullptr, FName ExecParamName = NAME_None) const;
	bool IsExpandEnumAsExec(FMulticastDelegateProperty* MCDProp, UEnum** OutEnum = nullptr, FProperty** OutProp = nullptr) const;

	bool IsExpandBoolAsExec(UFunction* Function, FBoolProperty** OutProp = nullptr, FName ExecParamName = NAME_None) const;
	bool IsExpandBoolAsExec(FMulticastDelegateProperty* MCDProp, FBoolProperty** OutProp = nullptr) const;

	static UEdGraphPin* FindThenPin(const UK2Node* InNode, bool bChecked = true);
	UEdGraphPin* FindThenPin(bool bChecked = true) const { return FindThenPin(this, bChecked); }
	static UEdGraphPin* FindDelegatePin(const UK2Node_Event* InNode, bool bChecked = true);

	static UEdGraphPin* FindExecPin(const UK2Node* InNode, EEdGraphPinDirection Dir = EGPD_MAX, bool bChecked = true);
	UEdGraphPin* FindExecPin(EEdGraphPinDirection Dir = EGPD_MAX, bool bChecked = true) const { return FindExecPin(this, Dir, bChecked); }

	static UEdGraphPin* FindValuePin(const UK2Node* InNode, EEdGraphPinDirection Dir = EGPD_MAX, FName PinCategory = NAME_None);
	UEdGraphPin* FindValuePin(FName PinCategory = NAME_None, EEdGraphPinDirection Dir = EGPD_MAX) const { return FindValuePin(this, Dir, PinCategory); }

	static UEdGraphPin* FindObjectPin(const UK2Node* InNode, UClass* ObjClass, EEdGraphPinDirection Dir = EGPD_MAX);
	UEdGraphPin* SearchPin(const FName PinName, const TArray<UEdGraphPin*>* InPinsToSearch = nullptr, const EEdGraphPinDirection Direction = EEdGraphPinDirection::EGPD_MAX) const;

	static UClass* ClassFromPin(UEdGraphPin* ClassPin, bool bFallback = true);
	UEdGraphPin* GetSpecialClassPin(const TArray<UEdGraphPin*>& InPinsToSearch, FName PinName, UClass** OutClass = nullptr) const;
	UClass* GetSpecialPinClass(const TArray<UEdGraphPin*>& InPinsToSearch, FName PinName, UEdGraphPin** OutPin = nullptr) const;

	static bool IsTypePickerPin(UEdGraphPin* Pin);

	static bool HasAnyConnections(const UEdGraphPin* InPin);

	static void FillCustomStructureParameterNames(const UFunction* Function, TArray<FString>& OutNames);

	static void UpdateCustomStructurePins(const UFunction* Function, UK2Node* Node, UEdGraphPin* SinglePin = nullptr);

	void UpdateCustomStructurePin(UEdGraphPin* SinglePin);
	void UpdateCustomStructurePin(TArray<UEdGraphPin*>* InOldPins = nullptr);

private:
	static void HandleSinglePinWildStatus(UEdGraphPin* Pin);

public:
	const FName MetaHidePins;
	UPROPERTY()
	FName MetaTagFlag;
	UPROPERTY()
	FString MetaTagPostfix;

	bool HasSpecialTag(const FFieldVariant& Field);
	bool HasSpecialImportTag(const FFieldVariant& Field);
	bool HasSpecialExportTag(const FFieldVariant& Field);
	FString GetPinFriendlyName(FString Name);
	FString GetDisplayString(const FFieldVariant& Field, const TCHAR* Prefix = nullptr);
	FText GetDisplayText(const FFieldVariant& Field, const TCHAR* Prefix = nullptr);
	FText GetCustomDisplayName(const FString& ClassName, const FFieldVariant& Field, const FString& Postfix = {});

	// Tasks can hide spawn parameters by doing meta = (HideSpawnParms="PropertyA,PropertyB")
	// (For example, hide Instigator in situations where instigator is not relevant to your task)
	TSet<FString> GetHideProperties(const FFieldVariant& InFieldVariant, FField* InFieldOptional = nullptr);
	FName NameHideProperties;
	FName NameShowProperties;

public:
	bool DoOnce(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin* OnceExecPin);
	bool LaterDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins);
	FORCEINLINE bool LaterDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin* ExecPin) { return LaterDo(CompilerContext, SourceGraph, InOutThenPin, TArray<UEdGraphPin*>{ExecPin}); }
	bool SequenceDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins);
	FORCEINLINE bool SequenceDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin* Exec) { return SequenceDo(CompilerContext, SourceGraph, InOutThenPin, TArray<UEdGraphPin*>{Exec}); }
	bool ConditionDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* ConditionPin, UEdGraphPin*& InOutThenPin, UEdGraphPin* ExecPin, UEdGraphPin* ElsePin = nullptr);
	UEdGraphPin* BranchExec(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* ConditionPin, UEdGraphPin* ExecPin, UEdGraphPin* ElsePin = nullptr);
	bool BranchThen(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin* ConditionPin, UEdGraphPin*& ElsePin);

	TMap<UEdGraphPin*, UEdGraphPin*> TemporaryVariables;
	bool AssignTempAndGet(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin*& InOutVarPin, bool bPure = false, UEdGraphPin* ConnectingPin = nullptr);
	UEdGraphPin* AssignValueAndGet(UK2Node_TemporaryVariable* InVarNode, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& LastThenPin, const FString& InValue);

	bool CastAssignAndGet(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, UEdGraphPin*& InOutVarPin, UClass* InClass, bool bPure = false);
	UEdGraphPin* PureValidateObjectOrClass(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InObjectPin);
	UEdGraphPin* SpawnPureVariableTo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* SourceTypePin, const FString& Value = {});
	UEdGraphPin* SpawnPureVariable(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InVariablePin, const FString& DefaultValue, bool bConst = true);

	UEdGraphPin* MakeTemporaryVariable(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const FEdGraphPinType& PinType);
	TMap<UEdGraphPin*, UEdGraphPin*> LiteralVariables;
	UEdGraphPin* MakeLiteralVariable(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* SrcPin, const FString& Value = {});
	bool TryCreateConnection(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InPinA, UEdGraphPin* InPinB, bool bMove = true);

public:
	UEdGraphPin* ParamsToArrayPin(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const TArray<UEdGraphPin*>& InPins);
	bool ConnectAdditionalPins(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* FuncNode, const TArray<UEdGraphPin*>& InPins, bool bWithCnt = false);
	bool ConnectMessagePins(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* FuncNode, const TArray<UEdGraphPin*>& InPins);

public:
	TCHAR DelimiterChar;
	FString DelimiterStr;

	FString DelegateDelimiter;
	FString FunctionDelimiter;
	FString MemberDelimiter;
	FString AdditionalDelimiter;
	FName RequiresConnection;
	FName DisableConnection;
	FName RequiresReference;
	FString ExecEnumPrefix;

	bool bHasAdvancedViewPins = false;
	bool IsAllocWithOldPins() const { return bAllocWithOldPins; }

	UEdGraphPin* CreatePinFromInnerProp(const UClass* InDerivedCls, FProperty* Property, const FString& InPrefix, const FString& InDisplayPrefix = TEXT("."), EEdGraphPinDirection Direction = EGPD_MAX);
	UEdGraphPin* CreatePinFromInnerProp(const UFunction* InFunc, FProperty* Property, const FString& InPrefix, const FString& InDisplayPrefix = TEXT("."), EEdGraphPinDirection Direction = EGPD_MAX);

	UEdGraphPin* CreatePinFromInnerProp(const UObject* ClsOrFunc, FProperty* Property, const FString& InPrefix, const FString& InDisplayPrefix = TEXT("."), EEdGraphPinDirection Direction = EGPD_MAX);
	UEdGraphPin* CreatePinFromInnerProp(const UObject* ClsOrFunc, FProperty* Property, TCHAR InPrefix, TCHAR InDisplayPrefix = TEXT('.'), EEdGraphPinDirection Direction = EGPD_MAX)
	{
		return CreatePinFromInnerProp(ClsOrFunc, Property, FString::Printf(TEXT("%c"), InPrefix), FString::Printf(TEXT("%c"), InDisplayPrefix), Direction);
	}

	TCHAR MetaEventPrefix;
	UK2Node_CustomEvent* GetMetaEventForClass(UClass* InClass, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, bool bCreate);

public:
	struct FPinNameAffixes
	{
		TCHAR PropPrefix;
		TCHAR ParamPrefix;
		TCHAR EventPrefix;
		bool MatchAll(const FString& Str) const { return !Str.IsEmpty() && (Str[0] == PropPrefix || Str[0] == ParamPrefix || Str[0] == EventPrefix); }
		bool MatchAll(const FName& Name) const { return MatchAll(Name.ToString()); }

		bool MatchInput(const FString& Str) const { return !Str.IsEmpty() && (Str[0] == PropPrefix || Str[0] == ParamPrefix); }
		bool MatchInput(const FName& Name) const { return MatchInput(Name.ToString()); }

		bool MatchEvent(const FString& Str) const { return !Str.IsEmpty() && (Str[0] == EventPrefix); }
		bool MatchEvent(const FName& Name) const { return MatchEvent(Name.ToString()); }

		bool MatchParam(const FString& Str) const { return !Str.IsEmpty() && (Str[0] == ParamPrefix); }
		bool MatchParam(const FName& Name) const { return MatchParam(Name.ToString()); }

		bool MatchProp(const FString& Str) const { return !Str.IsEmpty() && (Str[0] == PropPrefix); }
		bool MatchProp(const FName& Name) const { return MatchProp(Name.ToString()); }
	};
	FPinNameAffixes AffixesSelf;
	FPinNameAffixes AffixesProxy;

	virtual bool GetInstancedFlag(UClass* InClass) const { return true; }
	// we need this info to create pins
	virtual UFunction* GetAlternativeAction(UClass* InClass) const { return nullptr; }
	TArray<FName> CreateImportPinsForClass(UClass* InClass, const UK2Neuron::FPinNameAffixes& Affixes, bool bImportFlag = true, TArray<UEdGraphPin*>* OldPins = nullptr);
	bool ConnectImportPinsForClass(const TArray<FName>& PinNames,
								   UClass* InClass,
								   FKismetCompilerContext& CompilerContext,
								   UEdGraph* SourceGraph,
								   UEdGraphPin*& LastThenPin,
								   UEdGraphPin* InstancePin,
								   bool bOverrideDefault = true,
								   bool bOverrideRemote = false);

	struct FPinMetaInfo
	{
		UClass* OwnerClass = nullptr;

		UFunction* SubFuncion = nullptr;
		FDelegateProperty* FuncDelegate = nullptr;

		FMulticastDelegateProperty* SubDelegate = nullptr;

		FProperty* Prop = nullptr;

		FString ClassName;
		FString FunctionName;
		FString DelegateName;
		FString MemberName;
		FString EnumName;

		bool HasSubStructure() const { return SubFuncion || SubDelegate; }
		K2NEURON_API UObject* GetObjOrFunc() const;
	};

	FPinMetaInfo GetPinMetaInfo(FString PinNameString, bool bRedirect = false, bool bEnsure = true) const;
	virtual void GetRedirectPinNames(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const override;

	void GetRedirectPinNamesImpl(const UEdGraphPin& Pin, TArray<FString>& RedirectPinNames) const;
	virtual ERedirectType DoPinsMatchForReconstruction(const UEdGraphPin* NewPin, int32 NewPinIndex, const UEdGraphPin* OldPin, int32 OldPinIndex) const override;

public:
	bool CreateOutPinsForDelegate(const FString& InPrefix, UFunction* Function, bool bAdvanceView, UEdGraphPin** OutParamPin = nullptr);
	bool CreateDelegatesForClass(UClass* InClass, const UK2Neuron::FPinNameAffixes& Affixes, UClass* StopClass = nullptr, TArray<UEdGraphPin*>* OldPins = nullptr);
	bool CreateEventsForClass(UClass* InClass, const UK2Neuron::FPinNameAffixes& Affixes, UClass* StopClass = nullptr, TArray<UEdGraphPin*>* OldPins = nullptr);

	static bool ValidDataPin(const UEdGraphPin* Pin, EEdGraphPinDirection Direction);
	static bool CopyEventSignature(UK2Node_CustomEvent* CENode, UFunction* Function, const UEdGraphSchema_K2* Schema);
	static bool CreateDelegateForNewFunction(UEdGraphPin* DelegateInputPin, FName FunctionName, UK2Node* CurrentNode, UEdGraph* SourceGraph, FKismetCompilerContext& CompilerContext);

	struct FDelegateEventOptions
	{
		UK2Neuron::FPinNameAffixes Affixes;

		virtual UK2Node_CustomEvent* MakeEventNode(UK2Node* InNode, UEdGraphPin* InstPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const TCHAR* Prefix, const TCHAR* Postfix = nullptr) = 0;
		virtual void NotifyIfNeeded(UK2Node* InNode, UEdGraphPin* InstPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CustomEvent* EventNode, UEdGraphPin*& EventThenPin, const FString& DelegateName) {}

		virtual bool ContainsSpecialAction(const UEdGraphPin* ExecPin) { return false; }
		virtual UEdGraphPin* GetSpecialActionExec(UK2Node* InNode, UEdGraphPin* InstPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) { return nullptr; }
	};

	bool BindDelegateEvents(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin, UEdGraphPin*& LastAddDelegateThenPin, UEdGraphPin*& LastRemoveDelegateThenPin, FDelegateEventOptions& Options);

	bool ConnectLocalFunctions(TSet<FName>& SkipPinNames, TCHAR InParamPrefix, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin);
	bool ConnectRemoteFunctions(UFunction* InProxyFunc,
								TSet<FName>& SkipPinNames,
								TCHAR InParamPrefix,
								FKismetCompilerContext& CompilerContext,
								UEdGraph* SourceGraph,
								UEdGraphPin* InstPin,
								TFunctionRef<void(UK2Node* /*FuncNode*/, UK2Node_CustomEvent* /*EventNode*/)> NodeOpreation);
	UEdGraphPin* PureCastTo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin, UEdGraphPin* TargetPin, UEdGraphPin** BoolSuccessPin = nullptr);
	UEdGraphPin* PureCastClassTo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* InstPin, UEdGraphPin* TargetPin, UEdGraphPin** BoolSuccessPin = nullptr);

	static bool IsPinValueOnPropertyModified(UEdGraphPin* GraphPinObj, UObject* ObjOrFunc, FProperty* Prop, bool bReset = false);
	bool IsPinValueModified(UEdGraphPin* GraphPinObj, bool bReset = false) const;

	TArray<UEdGraphPin*> RemovePinsByName(const TSet<FName>& Names, bool bAffixes = true);

	bool IsInUbergraph() const;

public:
	FPinNameAffixes AffixesObject;

	FName BeginSpawningFuncName;
	FName PostSpawningFuncName;
	FName SpawnedSucceedName;
	FName SpawnedObjectName;
	FName SpawnedFailedName;

	FName SpawnedObjectPropName;
	FName ObjectClassPropName;
	FName SpawnedDelegatePropName;

	FName UnlinkObjEventName;
	FName MetaSplitStructPin;

	UPROPERTY()
	FName ObjClassPinName;

	UPROPERTY()
	int32 NodeVersion = 0;

	UPROPERTY()
	TArray<FName> ImportedPinNames;
	UPROPERTY()
	TArray<FName> SpawnedPinNames;
	UPROPERTY()
	TSet<FName> NeuronCheckableFlags;

	UPROPERTY()
	TSet<FName> CustomStructurePinNames;
	UPROPERTY()
	TSet<FName> AutoCreateRefTermPinNames;

	TArray<TPair<UEdGraphPin*, UEdGraphPin*>> ObjectPreparedPins;
	TWeakObjectPtr<UK2Node_CallFunction> ProxySpawnFuncNode;
	bool ConnectObjectPreparedPins(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* FuncNode, bool bUseKey = true);

	bool IsObjectClassPin(const UEdGraphPin* Pin, bool bExact = true) const;
	virtual UEdGraphPin* GetObjectFactoryClassPin(const TArray<UEdGraphPin*>* InPinsToSearch = nullptr) const;
	UEdGraphPin* GetSpawnedObjectClassPin(const TArray<UEdGraphPin*>* InPinsToSearch = nullptr, UClass** OutClass = nullptr) const;
	UClass* GetSpawnedObjectClass(const TArray<UEdGraphPin*>* InPinsToSearch = nullptr, UEdGraphPin** OutPin = nullptr) const;
	bool CreateObjectSpawnPins(UClass* OwnerClass, TArray<UEdGraphPin*>* InPinsToSearch = nullptr, UClass* InOverrideClass = nullptr);
	UClass* ValidateObjectSpawning(UClass* OwnerClass, FKismetCompilerContext* CompilerContext, const TArray<UEdGraphPin*>& InPinsToSearch, UClass** OutClass = nullptr);
	FProperty* DetectObjectSpawning(UClass* OwnerClass, FKismetCompilerContext* CompilerContext, FName DetectPinName = NAME_None);
	UEdGraphPin* ConnectObjectSpawnPins(UClass* OwnerClass, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& LastThenPin, UEdGraphPin* InstancePin, UFunction* ProxyFunc = nullptr);

	void OnSpawnedObjectClassChanged(UClass* OwnerClass, const TArray<UEdGraphPin*>* InPinsToSearch = nullptr);

	bool BindObjBlueprintCompiledEvent(UClass* InClass);

private:
	TWeakObjectPtr<UClass> LastObjClass;
	FDelegateHandle ObjHandle;

	TWeakObjectPtr<class UK2Node_Self> CtxSelfNode;
	UEdGraphPin* GetCtxPin() const;
	bool ShouldDefaultToCtx(UEdGraphPin* InPin) const;
	bool ShouldDefaultToCtx(FProperty* InProp) const;
	bool bAllocWithOldPins = false;

protected:
	virtual FNodeHandlingFunctor* CreateNodeHandler(FKismetCompilerContext& CompilerContext) const override;
	virtual FName GetCornerIcon() const override;
	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
	virtual void PostPasteNode() override;
	virtual bool HasExternalDependencies(TArray<class UStruct*>* OptionalOutput) const override;
	virtual bool CanSplitPin(const UEdGraphPin* Pin) const;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void AllocateDefaultPins() override;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) override;
	virtual bool CanJumpToDefinition() const override;
	virtual void JumpToDefinition() const override;
	void JumpToDefinitionClass(UClass* InClass) const;
	virtual void AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins = nullptr) {}
	void CallAllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins = nullptr);
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void ExpandNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	virtual void ClearCachedBlueprintData(UBlueprint* Blueprint) override;
	virtual void PostReconstructNode() override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const override;
	UK2Node* GetConnectedNode(UEdGraphPin* Pin, TSubclassOf<UK2Node> NodeClass) const;
	UEdGraphPin* CastIfFloatType(UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin = nullptr);

protected:
	mutable bool bClassChanged = false;
	TWeakObjectPtr<UClass> LastClass;
	FDelegateHandle CompiledHandle;
	FDelegateHandle PreCompilingHandle;
	void BindBlueprintCompiledEvent(UClass* InClass);
	void BindBlueprintPreCompilingEvent();
	virtual void OnAssociatedBPCompiled(UBlueprint* ClassBP, UObject* OldCDO = nullptr) {}

	FDelegateHandle OwnerCompiledHandle;
	virtual void OnOwnerBPCompiled() {}
	void BindOwnerBlueprintCompiledEvent();
	UK2Node_CustomEvent* MakeEventNode(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const FString& DescName, uint32* OutID = nullptr);
	UK2Node_CustomEvent* MakeEventNodeRemote(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const FString& DescName, uint32* OutID = nullptr);
	TArray<FName> RemoteEventNodes;

	uint32 MakeShortEventName(UK2Node_CustomEvent* Event, const FString& DescName);

	// reference to old object when compiling
	TWeakObjectPtr<UObject> DataCDO;

public:
	template<typename T = UObject>
	const T* FindDefaultObject(UClass* InClass) const
	{
		check(InClass && InClass->IsChildOf<T>());
		const T* Ret = Cast<T>(InClass->ClassDefaultObject);
		if (!Ret && IsBeingCompiled())
			Ret = Cast<T>(DataCDO.Get());
		return Ret;
	}
	bool IsBeingCompiled() const;

private:
	UPROPERTY()
	uint8 NodeUniqueID = 0;
	uint8 GenerateUniqueNodeID(TArray<UK2Neuron*>& Neurons, TSubclassOf<UK2Neuron> NeuronClass, bool bCompact = true);

	UPROPERTY()
	TMap<FGuid, FEdPinExtraMeta> PinExtraMetas;

protected:
	TArray<UK2Neuron*> GetOtherNodesOfClass(TSubclassOf<UK2Neuron> NeuronClass = nullptr) const;
	bool VerifyNodeID(FCompilerResultsLog* MessageLog) const;

	virtual uint8 GetNodeUniqueID() const { return NodeUniqueID; }
	template<typename T>
	uint8 GenerateUniqueNodeID(TArray<T*>& Neurons, bool bCompact = true)
	{
		static_assert(TIsDerivedFrom<T, UK2Neuron>::IsDerived, "err");
		return GenerateUniqueNodeID(*reinterpret_cast<TArray<UK2Neuron*>*>(&Neurons), T::StaticClass(), bCompact);
	}
	FNodeTextCache CachedNodeTitle;

	static bool ShouldMarkBlueprintDirtyBeforeOpen();
};

#if WITH_EDITOR
class K2NEURON_API SGraphNeuronBase : public SGraphNodeK2Default
{
public:
	SLATE_BEGIN_ARGS(SGraphNeuronBase) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UK2Neuron* InNode);

protected:
	virtual bool ShouldShowResetButton() const { return true; }
	virtual bool ShouldShowCheckBox(const UEdGraphPin* PinObj, TAttribute<bool> Attr = nullptr) const { return false; }
	EVisibility GetCheckBoxVisibility(const UEdGraphPin* PinObj, TAttribute<bool> Attr = nullptr) const { return ShouldShowCheckBox(PinObj, Attr) ? EVisibility::Visible : EVisibility::Hidden; }
	virtual const FText& GetCheckBoxToolTipText(UEdGraphPin* PinObj) const { return FText::GetEmpty(); }
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	EVisibility GetOutputPinVisibility(UEdGraphPin* PinObj) const;
	ECheckBoxState IsDefaultValueChecked(UEdGraphPin* PinObj) const;
	void OnDefaultValueCheckBoxChanged(ECheckBoxState InIsChecked, UEdGraphPin* PinObj);
};
#endif
