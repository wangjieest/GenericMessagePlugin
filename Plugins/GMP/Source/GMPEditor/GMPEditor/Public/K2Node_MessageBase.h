//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "DetailLayoutBuilder.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphNodeUtils.h"
#include "EdGraph/EdGraphPin.h"
#include "GMPCore.h"
#include "IDetailCustomNodeBuilder.h"
#include "IDetailCustomization.h"
#include "K2Node.h"
#include "K2Node_AddPinInterface.h"
#include "MessageTagContainer.h"
#include "UObject/Class.h"
#include "UObject/NameTypes.h"
#include "UObject/UObjectGlobals.h"
#include "UnrealCompatibility.h"
#include "Widgets/Views/SListView.h"

#if WITH_EDITOR
#include "KismetNodes/SGraphNodeK2Base.h"
#include "SNodePanel.h"
#endif

// Controls how listener parameter modifications are written back to the notify caller
UENUM()
enum class EGMPWritebackMode : uint8
{
	None       UMETA(DisplayName = "None",  ToolTip = "Never write back — listener modifications are discarded"),
	All        UMETA(DisplayName = "All",   ToolTip = "Always write back all parameters to the notify caller"),
	Auto       UMETA(DisplayName = "Auto",  ToolTip = "Write back only parameters marked via WritebackPins or connected to VariableSetRef"),
};

// Controls how message parameters are passed to the listener callback
UENUM()
enum class EGMPParamPassingMode : uint8
{
	AlwaysCopy UMETA(DisplayName = "AlwaysCopy", ToolTip = "Always copy parameters into the callback frame (safe with Delay)"),
	Auto       UMETA(DisplayName = "Auto",       ToolTip = "Use reference for synchronous paths, copy only when Delay/Latent is detected"),
};

// Controls the compilation strategy for the listen message node
UENUM()
enum class EGMPNodeCompileMode : uint8
{
	ExpandNode UMETA(DisplayName = "ExpandNode", ToolTip = "Standard: expand to intermediate K2Nodes (CustomEvent + CallFunction)"),
	Handler    UMETA(DisplayName = "Handler",    ToolTip = "Advanced: CustomEvent with MsgArray only + UK2Node_GMPDerefParam with InlineGeneratedParameter for zero-copy param access"),
	Direct     UMETA(DisplayName = "Direct",     ToolTip = "Optimal: no-param CustomEvent + SharedVariable + FastCall, direct UberGraph invoke like Latent resume"),
};

#include "K2Node_MessageBase.generated.h"

class FText;
class SComboButton;
class SEditableTextBox;
class SGraphPin;
class UEdGraphSchema_K2;
struct FEdGraphPinType;
class UK2Node_MakeArray;
class UK2Node_CallFunction;

#define GMP_NODE_DETAIL 0

USTRUCT()
struct FMessagePinTypeInfo
{
	GENERATED_BODY()
public:
	UPROPERTY()
	int32 InnerVer = 0;

	UPROPERTY()
	int32 PinIndex = 0;

	UPROPERTY()
	FName PinFriendlyName;

	UPROPERTY()
	FEdGraphPinType PinType;

	UPROPERTY()
	FString PinDefaultValue;
	friend FArchive& operator<<(FArchive& Ar, FMessagePinTypeInfo& Self);
};

USTRUCT()
struct FMessagePinTypeInfoCell
{
	GENERATED_BODY()
public:
	TSharedRef<FMessagePinTypeInfo> Info = MakeShared<FMessagePinTypeInfo>();
	FORCEINLINE FMessagePinTypeInfo* operator->() const { return &Info.Get(); }
	FORCEINLINE bool Serialize(FArchive& Ar)
	{
		Ar << Info.Get();
		return true;
	}
};
template<>
struct TStructOpsTypeTraits<FMessagePinTypeInfoCell> : public TStructOpsTypeTraitsBase2<FMessagePinTypeInfoCell>
{
	enum
	{
		WithSerializer = true,
	};
};

// Intermediate K2Node for zero-copy message parameter dereference.
// Spawned by K2Node_ListenMessage::ExpandNode in Handler/Auto mode.
// Has its own FKCHandler that creates InlineGeneratedParameter terms
// to avoid copying values out of the message body.
UCLASS()
class UK2Node_GMPDerefParam : public UK2Node
{
	GENERATED_BODY()
public:
	// Index into the MsgArray
	UPROPERTY()
	int32 ParamIndex = 0;

	// The output type (set by ExpandNode to match the message parameter type)
	UPROPERTY()
	FEdGraphPinType OutputPinType;

	static FName MsgArrayPinName;
	static FName OutItemPinName;

	UEdGraphPin* GetMsgArrayPin() const { return FindPinChecked(MsgArrayPinName); }
	UEdGraphPin* GetOutItemPin() const { return FindPinChecked(OutItemPinName); }

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface.
	virtual bool IsNodePure() const override { return true; }
	virtual bool ShouldDrawCompact() const override { return true; }
	virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	//~ End UK2Node Interface.
};

UCLASS()
class UK2Node_MessageSharedVariable : public UK2Node
{
	GENERATED_BODY()
public:
	UK2Node_MessageSharedVariable();

	UPROPERTY()
	FEdGraphPinType PinType;

	FName SharedName;

	// get variable pin
	UEdGraphPin* GetVariablePin();

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface.
	virtual bool IsNodePure() const override { return true; }
	virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	//~ End UK2Node Interface.
};

// Parameter info for EventGraphFunction
USTRUCT()
struct FEventGraphFunctionParam
{
	GENERATED_BODY()

	UPROPERTY()
	FName ParamName;

	UPROPERTY()
	FEdGraphPinType ParamType;

	UPROPERTY()
	bool bIsOutput = false;  // true = ref output (writable by function body)
};

// Definition node for a pure-synchronous function that lives in the EventGraph.
// Uses SharedVariables as "registers" for input/output. Compiles to a no-param
// CustomEvent (FastCall eligible). No Latent allowed (compile-time checked).
UCLASS()
class GMPEDITOR_API UK2Node_EventGraphFunction : public UK2Node
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName FunctionName;

	UPROPERTY()
	TArray<FEventGraphFunctionParam> Params;

	// Get the SharedVariable name for a parameter
	FString GetSharedVarName(const FName& InParamName) const
	{
		return FString::Printf(TEXT("EGF_%s_%s"), *FunctionName.ToString(), *InParamName.ToString());
	}

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual void ExpandNode(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph) override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface.
	virtual bool IsNodePure() const override { return false; }
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.2f, 0.6f, 0.2f); }
	//~ End UK2Node Interface.
};

// Call-site node for EventGraphFunction. Compiles via FKCHandler:
// writes inputs to SharedVars → FastCall invoke → outputs read from SharedVars.
UCLASS()
class GMPEDITOR_API UK2Node_CallEventGraphFunction : public UK2Node
{
	GENERATED_BODY()
public:
	UPROPERTY()
	FName TargetFunctionName;

	// Populated from the matching UK2Node_EventGraphFunction in the same graph
	UPROPERTY()
	TArray<FEventGraphFunctionParam> Params;

	FString GetSharedVarName(const FName& InParamName) const
	{
		return FString::Printf(TEXT("EGF_%s_%s"), *TargetFunctionName.ToString(), *InParamName.ToString());
	}

	//~ Begin UEdGraphNode Interface.
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	//~ End UEdGraphNode Interface.

	//~ Begin UK2Node Interface.
	virtual bool IsNodePure() const override { return false; }
	virtual class FNodeHandlingFunctor* CreateNodeHandler(class FKismetCompilerContext& CompilerContext) const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FLinearColor(0.2f, 0.6f, 0.2f); }
	//~ End UK2Node Interface.
};

UCLASS(Abstract)
class GMPEDITOR_API UK2Node_MessageBase : public UK2Node
{
	GENERATED_BODY()
public:
	UK2Node_MessageBase(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
	UPROPERTY(VisibleAnywhere, Category = "MessageBase", AssetRegistrySearchable)
	FMessageTag MsgTag;

	UPROPERTY()
	bool bBindToGameInstance = false;

	TSharedPtr<FMessageTagContainer> TagHolder;

	static FString MessageParamPrefix;
	static FString MessageResponsePrefix;
	static FEdGraphPinType DefaultPinType;
	static FName GetFNameSender() { return FName(TEXT("Sender")); }
	static FName GetFNameWatchedObj() { return FName(TEXT("WatchedObj")); }

	static FName MakeParameterName(int32 In) { return FName(TEXT("p"), In); }

protected:

	static bool ShouldIgnoreMetaOnRunningCommandlet();

	static bool MatchPinTypes(const FEdGraphPinType& Lhs, const FEdGraphPinType& Rhs);
	bool TryCreateConnection(FKismetCompilerContext& CompilerContext, UEdGraphPin* InPinA, UEdGraphPin* InPinB, bool bMove = true);

	static const UEdGraphSchema_K2* GetK2Schema(const class FKismetCompilerContext& CompilerContext);
	static const UEdGraphSchema_K2* GetK2Schema(const UK2Node* Node);
	void SetAuthorityType(uint8 Type);
	virtual bool IsListenMessage() const { return false; }

	virtual void GetMenuActions(FBlueprintActionDatabaseRegistrar& ActionRegistrar) const override;

#if GMP_NODE_DETAIL
	virtual bool IsParameterIgnorable() const { return true; }
	void RemoveUserDefinedPinByInfo(TSharedRef<FMessagePinTypeInfo> Info);
#endif
	virtual int32& GetMessageCount() const;
	virtual UEdGraphPin* GetMessagePin(int32 Index, TArray<UEdGraphPin*>* InPins = nullptr, bool bEnsure = true) const { return nullptr; }
	virtual UEdGraphPin* GetResponsePin(int32 Index, TArray<UEdGraphPin*>* InPins = nullptr, bool bEnsure = true) const { return nullptr; }
	bool IsMessageSignatureRegistered() const { return false; }
	TWeakPtr<class IDetailLayoutBuilder> Details;
	void RefreshDetail() const;

	UEdGraphPin* SpawnPureVariable(class FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* ValuePin, const FString& Value, bool bConst = false);

	void DoRebuild(bool bNewTag, TArray<UEdGraphPin*>* InOldPins = nullptr);
	virtual void AllocateDefaultPins() final;
	virtual void ReallocatePinsDuringReconstruction(TArray<UEdGraphPin*>& OldPins) final;
	void AllocateMsgKeyTagPin();
	virtual void AllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins = nullptr) {}
	void CallAllocateDefaultPinsImpl(TArray<UEdGraphPin*>* InOldPins = nullptr);
	UEdGraphPin* GetEventNamePin(const TArray<UEdGraphPin*>* InOldPins = nullptr) const;
	virtual UEdGraphPin* AddMessagePin(int32 Index, bool bTransaction = true) { return nullptr; }
	virtual UEdGraphPin* AddResponsePin(int32 Index, bool bTransaction = true) { return nullptr; }
	virtual UEdGraphPin* CreateResponseExecPin() { return nullptr; }
	virtual UEdGraphPin* GetResponseExecPin() const { return nullptr; }

	virtual FName GetCornerIcon() const override;
	virtual bool CanJumpToDefinition() const override;
	virtual void JumpToDefinition() const override;

	virtual FName GetMessageSignature() const { return NAME_None; }
	virtual bool ShouldShowNodeProperties() const override { return false; }
	virtual void PinDefaultValueChanged(UEdGraphPin* Pin) override;
	virtual void PinConnectionListChanged(UEdGraphPin* ChangedPin) override;
	virtual FText GetMenuCategory() const override;
#if UE_4_24_OR_LATER
	virtual void GetMenuEntries(struct FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual void GetNodeContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const override;
#else
	virtual void GetContextMenuActions(const FGraphNodeContextMenuBuilder& Context) const override;
#endif
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;

	virtual FText GetPinDisplayName(const UEdGraphPin* Pin) const override;
	virtual void GetPinHoverText(const UEdGraphPin& Pin, FString& HoverTextOut) const override;

	virtual FString GetMessageTitle(bool bWithMessageKey = true) const;
	virtual FString GetMessageKey(bool bWithModifies = false) const;
	virtual FString GetTitleHead() const;

	virtual void PostPasteNode() override;
	virtual void PostLoad() override;
	virtual void PostReconstructNode() override;
	virtual void EarlyValidation(class FCompilerResultsLog& MessageLog) const override;

	virtual bool IsCompatibleWithGraph(UEdGraph const* TargetGraph) const override;
	virtual bool IsConnectionDisallowed(const UEdGraphPin* MyPin, const UEdGraphPin* OtherPin, FString& OutReason) const;

	virtual void OnDefaultAsGameInstance(bool bAs) {}
	bool IsPinSupportDefaultGameInstance(const UEdGraphPin* InPin) const;

	void FindInBlueprint(bool bWithinBlueprint) const;
	void SearchReferences() const;
#if GMP_NODE_DETAIL
	bool ModifyUserDefinedPinDefaultValue(TSharedRef<FMessagePinTypeInfo> Info, const FString& InDefaultValue);
	UEdGraphPin* CreateUserDefinedPin(const FName InPinName, const FEdGraphPinType& InPinType);
#endif
	int32 GetPinIndex(UEdGraphPin* Pin) const;
	static void AddReferencedObjects(UObject* InThis, FReferenceCollector& Collector);

	bool RefreashMessagePin(bool bClearError = false);

	bool bMatchTag = false;

	bool IsAllowLatentFuncs(const UEdGraph* InGraph = nullptr) const;
	UPROPERTY()
	mutable bool bAllowLatentFuncs = false;

	UPROPERTY()
	TArray<FMessagePinTypeInfoCell> ParameterTypes;

	UPROPERTY()
	TArray<FMessagePinTypeInfoCell> ResponseTypes;

	UPROPERTY()
	uint8 AuthorityType = 0;
	UPROPERTY()
	TSet<FName> WritebackPins;

	UPROPERTY()
	EGMPWritebackMode WritebackMode = EGMPWritebackMode::All;
	UPROPERTY()
	EGMPParamPassingMode ParamPassingMode = EGMPParamPassingMode::AlwaysCopy;
	UPROPERTY()
	EGMPNodeCompileMode NodeCompileMode = EGMPNodeCompileMode::ExpandNode;

	static FName MessageKeyName;
	FNodeTextCache CachedNodeTitle;
	friend class SGraphNodeMessageBase;
	bool bRecursivelyChangingDefaultValue = false;

	UK2Node* GetConnectedNode(UEdGraphPin* Pin, TSubclassOf<UK2Node> NodeClass) const;
	template<typename T>
	bool GetConnectedNode(UEdGraphPin* Pin, T*& OutNode) const
	{
		static_assert(TIsDerivedFrom<T, UK2Node>::IsDerived, "err");
		OutNode = Cast<T>(GetConnectedNode(Pin, T::StaticClass()));
		return !!OutNode;
	}

	virtual void OnNodeExpanded(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UK2Node_CallFunction* MessageFunc) {}
	void OnSignatureChanged(FName MsgKey, bool);
	bool IsExecPin(UEdGraphPin* Pin, EEdGraphPinDirection Direction = EGPD_MAX) const;
	bool SequenceDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins);
	bool LaterDo(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin*& InOutThenPin, const TArray<UEdGraphPin*>& ExecPins);
	UEdGraphPin* GetThenPin() const;

	virtual UEdGraphPin* GetInputPinByIndex(int32 PinIndex) const { return nullptr; }
	virtual UEdGraphPin* GetOutputPinByIndex(int32 PinIndex) const { return nullptr; }
	virtual int32 IsMessageParamPin(UEdGraphPin* InPin) const { return 0; }

	UEdGraphPin* ConstCastIfSelfPin(UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin = nullptr);

	UEdGraphPin* CastIfFloatType(UEdGraphPin* TestSelfPin, FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, UEdGraphPin* LinkPin = nullptr);

	bool ExpandMessageCall(FKismetCompilerContext& CompilerContext, UEdGraph* SourceGraph, const TArray<FMessagePinTypeInfoCell>& PinTypeInfos, UK2Node_MakeArray* MakeArrayNode, UK2Node_CallFunction* CallMessageNode);
};

#if WITH_EDITOR
class SGraphNodeMessageBase : public SGraphNodeK2Base
{
public:
	SLATE_BEGIN_ARGS(SGraphNodeMessageBase) {}
	SLATE_END_ARGS()

	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	void Construct(const FArguments& InArgs, UK2Node_MessageBase* InNode);
	virtual void CreateStandardPinWidget(UEdGraphPin* Pin) override;
};
#endif
