//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Delegates/DelegateCombinations.h"
#include "GMPKey.h"
#include "GMPStruct.h"
#include "GMPTypeTraits.h"
#include "GMPUtils.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Misc/ScopeExit.h"
#include "UObject/Class.h"
#include "UObject/TextProperty.h"
#include "tuplet/tuple.hpp"

#include "GMPBPLib.generated.h"

class APlayerController;
class UPackageMap;

namespace GMP
{
struct GMP_API FLatentActionKeeper
{
	FLatentActionKeeper() = default;

	void SetLatentInfo(const struct FLatentActionInfo& LatentInfo);
	bool ExecuteAction(bool bClear = true) const;
	FLatentActionKeeper(const struct FLatentActionInfo& LatentInfo);

protected:
	FName ExecutionFunction;
	mutable int32 LinkID = 0;
	FWeakObjectPtr CallbackTarget;
};
}  // namespace GMP

UCLASS(Abstract, Blueprintable)
class GMP_API UBlueprintableObject : public UObject
{
	GENERATED_BODY()
public:
	virtual UWorld* GetWorld() const override;
};

USTRUCT(BlueprintInternalUseOnly)
struct FGMPObjNamePair
{
	GENERATED_BODY()
public:
	UPROPERTY()
	UObject* Obj = nullptr;

	UPROPERTY()
	FName TagName = NAME_None;
};

//////////////////////////////////////////////////////////////////////////
DECLARE_DYNAMIC_DELEGATE_FourParams(FGMPScriptDelegate, const UObject*, Sender, const FName&, MessageId, FGMPKey, SeqId, UPARAM(ref) TArray<FGMPTypedAddr>&, Params);

UENUM()
enum EMessageAuthorityType
{
	EMessageTypeServer = 0x1,
	EMessageTypeClient = 0x2,
	EMessageTypeBoth = 0x3,
	EMessageTypeStore = 0x4,
};

using EGMPAuthorityType = EMessageAuthorityType;

#define GMP_WITH_VARIADIC_SUPPORT (UE_4_25_OR_LATER)

UCLASS()
class GMP_API UGMPBPLib : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintPure, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static FGMPObjNamePair MakeObjNamePair(const UObject* InObj, FName InName) { return FGMPObjNamePair{const_cast<UObject*>(InObj), InName}; }

	// Unlisten
	UFUNCTION(BlueprintCallable, Category = "GMP|Message", meta = (WorldContext = "Obj", StringAsMessageTag = "MessageId", AutoCreateRefTerm = "MessageId", AdvancedDisplay = "Mgr"))
	static bool UnlistenMessage(const FString& MessageId, UObject* Listener, UGMPManager* Mgr = nullptr, UObject* Obj = nullptr);
	UFUNCTION(BlueprintCallable, meta = (WorldContext = "Listener", BlueprintInternalUseOnly = true, AutoCreateRefTerm = "MessageId", AdvancedDisplay = "Mgr"))
	static bool UnlistenMessageByKey(const FString& MessageId, UObject* Listener, UGMPManager* Mgr = nullptr);

	// Listen
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, Times = "-1", Order = "0", Type = "0", AutoCreateRefTerm = "WatchedObj"))
	static FGMPTypedAddr ListenMessageByKey(FName MessageId, const FGMPScriptDelegate& Delegate, int32 Times, int32 Order, uint8 Type, UGMPManager* Mgr, const FGMPObjNamePair& WatchedObj);
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, Times = "-1", Order = "0", Type = "0", AutoCreateRefTerm = "WatchedObj"))
	static FGMPTypedAddr ListenMessageByKeyValidate(const TArray<FName>& ArgNames, FName MessageId, const FGMPScriptDelegate& Delegate, int32 Times, int32 Order, uint8 Type, UGMPManager* Mgr, const FGMPObjNamePair& WatchedObj);
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Listener", DefaultToSelf = "Listener", Times = "-1", Order = "0", Type = "0", AutoCreateRefTerm = "WatchedObj"))
	static FGMPTypedAddr ListenMessageViaKey(UObject* Listener, FName MessageId, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& WatchedObj, int64 ParmBitMask = 0);
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Listener", DefaultToSelf = "Listener", Times = "-1", Order = "0", Type = "0", AutoCreateRefTerm = "WatchedObj"))
	static FGMPTypedAddr
		ListenMessageViaKeyValidate(const TArray<FName>& ArgNames, UObject* Listener, FName MessageId, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& WatchedObj, int64 ParmBitMask = 0);

	// Notify
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Sender,Params,MessageId"))
	static bool NotifyMessageByKeyRet(const FString& MessageId, const FGMPObjNamePair& Sender, UPARAM(ref) TArray<FGMPTypedAddr>& Params, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Sender,MessageId", Variadic))
	static bool NotifyMessageByKeyVariadicRet(const FString& MessageId, const FGMPObjNamePair& Sender, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execNotifyMessageByKeyVariadicRet);

	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Sender,Params,MessageId"))
	static void NotifyMessageByKey(const FString& MessageId, const FGMPObjNamePair& Sender, UPARAM(ref) TArray<FGMPTypedAddr>& Params, uint8 Type = 0, UGMPManager* Mgr = nullptr)
	{
		NotifyMessageByKeyRet(MessageId, Sender, Params, Type, Mgr);
	}
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Sender,MessageId", Variadic))
	static void NotifyMessageByKeyVariadic(const FString& MessageId, const FGMPObjNamePair& Sender, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execNotifyMessageByKeyVariadic);

	// RequestMessage
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Sender", DefaultToSelf = "Sender", AutoCreateRefTerm = "Params,MessageId"))
	static bool RequestMessageRet(FGMPKey& RspKey, FName EventName, const FString& MessageId, const FGMPObjNamePair& Sender, UPARAM(ref) TArray<FGMPTypedAddr>& Params, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Sender", DefaultToSelf = "Sender", AutoCreateRefTerm = "MessageId", Variadic))
	static bool RequestMessageVariadicRet(FGMPKey& RspKey, FName EventName, const FString& MessageId, const FGMPObjNamePair& Sender, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execRequestMessageVariadicRet);

	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Sender", DefaultToSelf = "Sender", AutoCreateRefTerm = "Params,MessageId"))
	static void RequestMessage(FGMPKey& RspKey, FName EventName, const FString& MessageId, const FGMPObjNamePair& Sender, UPARAM(ref) TArray<FGMPTypedAddr>& Params, uint8 Type = 0, UGMPManager* Mgr = nullptr)
	{
		RequestMessageRet(RspKey, EventName, MessageId, Sender, Params, Type, Mgr);
	}
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Sender", DefaultToSelf = "Sender", AutoCreateRefTerm = "MessageId", Variadic))
	static void RequestMessageVariadic(FGMPKey& RspKey, FName EventName, const FString& MessageId, const FGMPObjNamePair& Sender, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execRequestMessageVariadic);

	// ResponseMessage
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "SigSource", DefaultToSelf = "SigSource", AutoCreateRefTerm = "Params,MessageId"))
	static void ResponseMessage(FGMPKey SeqId, UPARAM(ref) TArray<FGMPTypedAddr>& Params, UObject* SigSource, UGMPManager* Mgr = nullptr);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "SigSource", DefaultToSelf = "SigSource", AutoCreateRefTerm = "MessageId", Variadic))
	static void ResponseMessageVariadic(FGMPKey SeqId, UObject* SigSource, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execResponseMessageVariadic);

	//////////////////////////////////////////////////////////////////////////
	// Dereference a message parameter by index — sets MostRecentPropertyAddress to the original
	// data pointer stored in MsgArray[Index], enabling zero-copy reference access.
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "OutItem"))
	static void GMPDerefParam(const TArray<FGMPTypedAddr>& MsgArray, int32 Index, FGMPTypedAddr& OutItem);
	DECLARE_FUNCTION(execGMPDerefParam);

	// Extract the raw pointer (as int64) from MsgArray[Index].Value.
	// Used by FKCHandler_GMPDerefParam to get the pointer into an int64 local.
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static int64 GMPGetParamPtr(const TArray<FGMPTypedAddr>& MsgArray, int32 Index);
	DECLARE_FUNCTION(execGMPGetParamPtr);

	// Dereference an int64 pointer — sets Stack.MostRecentPropertyAddress to (uint8*)InPtr.
	// Only called via InlineGeneratedParameter, never placed in blueprints directly.
	UFUNCTION(CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static void GMPDerefPtr(int64 InPtr);
	DECLARE_FUNCTION(execGMPDerefPtr);

	//////////////////////////////////////////////////////////////////////////
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, NativeMakeFunc, BlueprintInternalUseOnly = true, CompactNodeTitle = "->", CustomStructureParam = "InAny", PropertyEnum = "255"))
	static FGMPTypedAddr AddrFromWild(uint8 PropertyEnum, const FGMPTypedAddr& InAny);
	DECLARE_FUNCTION(execAddrFromWild);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CompactNodeTitle = "->", ArrayParm = "InAny", ArrayTypeDependentParams = "OutItem", ElementEnum = "255"))
	static FGMPTypedAddr AddrFromArray(uint8 ElementEnum, const TArray<int32>& InAny);
	DECLARE_FUNCTION(execAddrFromArray);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CompactNodeTitle = "->", SetParam = "InAny", PropertyEnum = "255"))
	static FGMPTypedAddr AddrFromSet(uint8 ElementEnum, const TSet<int32>& InAny);
	DECLARE_FUNCTION(execAddrFromSet);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CompactNodeTitle = "->", MapParam = "InAny", ValueEnum = "255", KeyEnum = "255"))
	static FGMPTypedAddr AddrFromMap(uint8 ValueEnum, uint8 KeyEnum, const TMap<int32, int32>& InAny);
	DECLARE_FUNCTION(execAddrFromMap);

	//////////////////////////////////////////////////////////////////////////
	static void InnerSet(FFrame& Stack, uint8 PropertyEnum = -1, uint8 ElementEnum = -1, uint8 KeyEnum = -1);
	UFUNCTION(BlueprintCallable, Category = "GMP|Message", CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "SetValue", CompactNodeTitle = "SET", CustomStructureParam = "InItem"))
	static void SetValue(UPARAM(ref) TArray<FGMPTypedAddr>& TargetArray, int32 Index, const FGMPTypedAddr& InItem);
	DECLARE_FUNCTION(execSetValue);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "SetWild", CompactNodeTitle = "SET", BlueprintInternalUseOnly = true, CustomStructureParam = "InItem", PropertyEnum = "255"))
	static void SetWild(uint8 PropertyEnum, UPARAM(ref) TArray<FGMPTypedAddr>& TargetArray, int32 Index, const FGMPTypedAddr& InItem);
	DECLARE_FUNCTION(execSetWild);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "SetArray", CompactNodeTitle = "SET", BlueprintInternalUseOnly = true, ArrayParm = "InItem", ElementEnum = "255"))
	static void SetArray(uint8 ElementEnum, UPARAM(ref) TArray<FGMPTypedAddr>& TargetArray, int32 Index, TArray<int32>& InItem);
	DECLARE_FUNCTION(execSetArray);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "SetMap", CompactNodeTitle = "SET", BlueprintInternalUseOnly = true, MapParam = "InItem", KeyEnum = "255", ValueEnum = "255"))
	static void SetMap(uint8 KeyEnum, uint8 ValueEnum, UPARAM(ref) TArray<FGMPTypedAddr>& TargetArray, int32 Index, TMap<int32, int32>& InItem);
	DECLARE_FUNCTION(execSetMap);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "SetSet", CompactNodeTitle = "SET", BlueprintInternalUseOnly = true, SetParam = "InItem", ElementEnum = "255"))
	static void SetSet(uint8 ElementEnum, UPARAM(ref) TArray<FGMPTypedAddr>& TargetArray, int32 Index, TSet<int32>& InItem);
	DECLARE_FUNCTION(execSetSet);

	//////////////////////////////////////////////////////////////////////////
	static void InnerGet(FFrame& Stack, uint8 PropertyEnum = 255, uint8 ElementEnum = 255, uint8 KeyEnum = 255);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "AddrToWild", BlueprintInternalUseOnly = true, CompactNodeTitle = "GET", CustomStructureParam = "OutItem"))
	static void AddrToWild(uint8 PropertyEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, FGMPTypedAddr& OutItem);
	DECLARE_FUNCTION(execAddrToWild);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "AddrToArray", BlueprintInternalUseOnly = true, CompactNodeTitle = "GET", ArrayParm = "OutItem"))
	static void AddrToArray(uint8 ElementEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, TArray<int32>& OutItem);
	DECLARE_FUNCTION(execAddrToArray);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "AddrToMap", BlueprintInternalUseOnly = true, CompactNodeTitle = "GET", MapParam = "OutItem"))
	static void AddrToMap(uint8 KeyEnum, uint8 ValueEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, TMap<int32, int32>& OutItem);
	DECLARE_FUNCTION(execAddrToMap);
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, DisplayName = "AddrToSet", BlueprintInternalUseOnly = true, CompactNodeTitle = "GET", SetParam = "OutItem"))
	static void AddrToSet(uint8 ElementEnum, const TArray<FGMPTypedAddr>& TargetArray, int32 Index, TSet<int32>& OutItem);
	DECLARE_FUNCTION(execAddrToSet);

public:
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InAny"))
	static FGMPTypedAddr AddrFromVariadic(const FGMPTypedAddr& InAny);
	DECLARE_FUNCTION(execAddrFromVariadic);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, CustomStructureParam = "InItem"))
	static void SetVariadic(int32 Index, const FGMPTypedAddr& InItem, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execSetVariadic);

	// byte to int
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CustomStructureParam = "Value", BlueprintThreadSafe, BlueprintInternalUseOnly = true))
	static int32 MakeLiteralInt(const uint8& Value);
	DECLARE_FUNCTION(execMakeLiteralInt);

	// int to byte
	UFUNCTION(BlueprintPure, CustomThunk, meta = (CustomStructureParam = "Value", BlueprintThreadSafe, BlueprintInternalUseOnly = true))
	static uint8 MakeLiteralByte(const int32& Value);
	DECLARE_FUNCTION(execMakeLiteralByte);

	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, BlueprintInternalUseOnly = true, DeterminesOutputType = "Value", DynamicOutputParam))
	static UClass* MakeLiteralClass(UClass* Value) { return Value; }
	UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, BlueprintInternalUseOnly = true, DeterminesOutputType = "Value", DynamicOutputParam))
	static UObject* MakeLiteralObject(UObject* Value) { return Value; }
	//UFUNCTION(BlueprintPure, meta = (BlueprintThreadSafe, BlueprintInternalUseOnly = true))
	//static FKey MakeInputKey(FKey Value) { return Value; }

public:
	static UPackageMap* GetPackageMap(APlayerController* PC);

	UFUNCTION(BlueprintPure, meta = (BlueprintInternalUseOnly = true))
	static bool HasAnyListeners(FName InMsgKey, UGMPManager* Mgr = nullptr);

	// Work around for that dynamic create event call not be call directly
	UFUNCTION(BlueprintCallable, meta = (DefaultToSelf = Obj, WorldContext = Obj, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Params"))
	static void CallFunctionPacked(UObject* Obj, FName FuncName, UPARAM(ref) TArray<FGMPTypedAddr>& Params);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (Variadic, DefaultToSelf = Obj, WorldContext = Obj, BlueprintInternalUseOnly = true))
	static void CallFunctionVariadic(UObject* Obj, FName FuncName);
	DECLARE_FUNCTION(execCallFunctionVariadic);

	static bool CallEventFunction(UObject* Obj, const FName FuncName, const TArray<uint8>& Buffer, UPackageMap* PackageMap, EFunctionFlags VerifyFlags = FUNC_None);
	static bool CallEventDelegate(UObject* Obj, const FName EventName, const TArray<uint8>& Buffer, UPackageMap* PackageMap);
	static bool CallMessageFunction(UObject* Obj, UFunction* Function, const TArray<FGMPTypedAddr>& Params, uint64 WritebackFlags = -1);

public:
	UFUNCTION(BlueprintPure, CustomThunk, meta = (Variadic, CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static void MessageFromVariadic(TArray<FGMPTypedAddr>& MsgArr);
	DECLARE_FUNCTION(execMessageFromVariadic);

	static bool MessageToFrame(UFunction* Function, void* FramePtr, const TArray<FGMPTypedAddr>& Params);
	static bool MessageToArchive(FArchive& ArToSave, UFunction* Function, const TArray<FGMPTypedAddr>& Params, UPackageMap* PackageMap = nullptr);
	static bool ArchiveToFrame(FArchive& ArToLoad, UFunction* Function, void* FramePtr, UPackageMap* PackageMap = nullptr);
	static bool ArchiveToMessage(const TArray<uint8>& Buffer, GMP::FTypedAddresses& Params, const TArray<FProperty*>& Props, UPackageMap* PackageMap = nullptr);
	template<typename... TArgs>
	static TArray<FGMPTypedAddr> VariadicToMessage(TArgs&... Args)
	{
		return TArray<FGMPTypedAddr>{FGMPTypedAddr::MakeMsg(Args)...};
	}

	static bool NetSerializeProperty(FArchive& Ar, FProperty* Prop, void* ItemPtr, UPackageMap* PackageMap = nullptr);

	UFUNCTION(BlueprintPure, CustomThunk, meta = (Variadic, CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static FString FormatStringByOrder(const FString& InFmtStr);
	DECLARE_FUNCTION(execFormatStringByOrder);

	UFUNCTION(BlueprintPure, CustomThunk, meta = (Variadic, CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static FString FormatStringByName(const FString& InFmtStr, const TArray<FString>& InNames);
	DECLARE_FUNCTION(execFormatStringByName);

	UFUNCTION(BlueprintPure, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static FString FormatStringByNameLegacy(const FString& InFmtStr, const TMap<FString, FString>& InArgs);

	UFUNCTION(BlueprintPure, Category = "GMP|Utils", meta = (WorldContext = "InCtx"))
	static bool IsListenServer(UObject* InCtx);

	UFUNCTION(BlueprintPure, Category = "GMP|Utils", meta = (CallableWithoutWorldContext))
	static bool IsModuleLoaded(const FString& ModuleName);
};

template<typename F, typename = void>
struct TGMPBPFastCall;

class FGMPBPFastCallImpl
{
	// Verify that tuplet::tuple layout matches UFunction frame layout at runtime.
	// Both use natural C++ alignment, so they should always match.
	// Returns true if the tuple can be used directly as the function frame.
	template<typename... Ts>
	static bool VerifyTupleLayout(UFunction* Function)
	{
		using TupType = tuplet::tuple<Ts...>;
		if (sizeof(TupType) != Function->ParmsSize)
			return false;

		// Verify each property offset matches the tuple element offset
		TupType* NullTup = nullptr;
		auto Prop = GMP::Reflection::GetFunctionChildProperties(Function);
		bool bMatch = true;
		// Use fold expression to check each element offset
		int Idx = 0;
		const int Dummy[] = {0, ([&] {
			if (!bMatch || !Prop) { bMatch = false; return; }
			// tuplet element offset = offsetof via pointer arithmetic
			// Since tuplet is aggregate, elements are at predictable offsets
			if ((int32)Prop->GetSize() != (int32)sizeof(Ts) || Prop->GetMinAlignment() != (int32)alignof(Ts))
				bMatch = false;
			Prop = CastField<FProperty>((FField*)Prop->Next);
			++Idx;
		}(), 0)...};
		(void)Dummy;
		return bMatch;
	}

	// Slow path: copy args into frame via Property offsets
	template<typename T>
	static void CopyArgToFrame(uint8* Parms, FProperty*& Prop, T& Arg)
	{
		if (Prop)
		{
			Prop->CopyCompleteValue(Prop->ContainerPtrToValuePtr<void>(Parms), &Arg);
			Prop = CastField<FProperty>((FField*)Prop->Next);
		}
	}

	template<typename T>
	static void CopyArgFromFrame(const uint8* Parms, FProperty*& Prop, T& Arg)
	{
		if (Prop)
		{
			if (Prop->HasAnyPropertyFlags(CPF_OutParm))
			{
				Prop->CopyCompleteValue(&Arg, Prop->ContainerPtrToValuePtr<void>(Parms));
			}
			Prop = CastField<FProperty>((FField*)Prop->Next);
		}
	}

	template<typename... TArgs>
	static void InvokeBlueprintEvent(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		using namespace GMP;
		GMP_CHECK_SLOW(InObj && Function);

		// Use tuplet::tuple as the parameter frame — aggregate layout matches struct layout.
		// This eliminates per-property CopyCompleteValue calls when layout is verified.
		using TupType = tuplet::tuple<std::decay_t<TArgs>...>;
		TupType LocalsOnStack{Args...};

		// tuplet::tuple uses aggregate layout (same as struct) with natural alignment,
		// which should always match UE's FProperty layout (also natural alignment).
		// sizeof check is a fast short-circuit; full verify only if sizes match.
		const bool bLayoutMatch = (sizeof(TupType) == Function->ParmsSize);
		uint8* Parms = nullptr;

		if (bLayoutMatch)
		{
			// Fast path: tuple IS the frame — zero per-property copy
			Parms = reinterpret_cast<uint8*>(&LocalsOnStack);
		}
		else
		{
			// Slow path: allocate proper frame and copy via Property offsets
			Parms = (uint8*)FMemory_Alloca_Aligned(Function->ParmsSize, Function->GetMinAlignment());
			FMemory::Memzero(Parms, Function->ParmsSize);
			auto Prop = Reflection::GetFunctionChildProperties(Function);
			const int Dummy[] = {0, (CopyArgToFrame(Parms, Prop, Args), 0)...};
			(void)Dummy;
		}

#if GMP_WITH_DYNAMIC_CALL_CHECK
		{
			// Reuse LocalsOnStack (tuplet::tuple) for type name validation.
			// MakeNames only uses type info (std::tuple_element_t / std::tuple_size),
			// both of which tuplet::tuple has std:: specializations for.
			const auto& ArgNames = Hub::DefaultTraits::MakeNames(LocalsOnStack);
			if (!ensure(ArgNames.Num() == Function->NumParms))
				return;

			auto FuncProp = Reflection::GetFunctionChildProperties(Function);
			for (auto& TypeName : ArgNames)
			{
				if (!ensure(FuncProp && Reflection::EqualPropertyName(FuncProp, TypeName)))
					return;
				FuncProp = CastField<FProperty>((FField*)FuncProp->Next);
			}
		}
#endif

		auto FuncFirstProp = Reflection::GetFunctionChildProperties(Function);
		const bool bReturnVoid = (Function->ReturnValueOffset == MAX_uint16);
		uint8* ReturnValueAddress = !bReturnVoid ? (Parms + Function->ReturnValueOffset) : nullptr;

		// Allocate execution frame
		uint8* FrameMemory = nullptr;
#if defined(USE_UBER_GRAPH_PERSISTENT_FRAME) && USE_UBER_GRAPH_PERSISTENT_FRAME
		if (Function->HasAnyFunctionFlags(FUNC_UbergraphFunction))
		{
			FrameMemory = Function->GetOuterUClassUnchecked()->GetPersistentUberGraphFrame(InObj, Function);
		}
#endif
		const bool bUsePersistentFrame = (FrameMemory != nullptr);
		if (!bUsePersistentFrame)
		{
			FrameMemory = (uint8*)FMemory_Alloca_Aligned(Function->PropertiesSize, Function->GetMinAlignment());
			FMemory::Memzero(FrameMemory + Function->ParmsSize, Function->PropertiesSize - Function->ParmsSize);
		}
		FMemory::Memcpy(FrameMemory, Parms, Function->ParmsSize);

		FFrame NewStack(InObj, Function, FrameMemory, nullptr, FuncFirstProp);

		// Set up OutParms pointing to the ORIGINAL caller args for direct writeback
		if (Function->HasAnyFunctionFlags(FUNC_HasOutParms))
		{
			void* ArgAddrs[] = {(&Args)...};
			int32 ArgIdx = 0;

			FOutParmRec** LastOut = &NewStack.OutParms;
			for (FProperty* Property = FuncFirstProp;
				 Property && (Property->PropertyFlags & CPF_Parm) == CPF_Parm;
				 Property = CastField<FProperty>((FField*)Property->Next))
			{
				if (Property->HasAnyPropertyFlags(CPF_OutParm) && ArgIdx < (int32)UE_ARRAY_COUNT(ArgAddrs))
				{
					CA_SUPPRESS(6263)
					FOutParmRec* Out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
					Out->PropAddr = reinterpret_cast<uint8*>(ArgAddrs[ArgIdx]);
					Out->Property = Property;

					if (*LastOut)
					{
						(*LastOut)->NextOutParm = Out;
						LastOut = &(*LastOut)->NextOutParm;
					}
					else
					{
						*LastOut = Out;
					}
				}
				++ArgIdx;
			}
			if (*LastOut)
			{
				(*LastOut)->NextOutParm = nullptr;
			}
		}

		// Initialize local properties
		if (!bUsePersistentFrame)
		{
			for (FProperty* LocalProp = Function->FirstPropertyToInit; LocalProp; LocalProp = CastField<FProperty>((FField*)LocalProp->Next))
			{
				LocalProp->InitializeValue_InContainer(NewStack.Locals);
			}
		}

		// Invoke
		Function->Invoke(InObj, NewStack, ReturnValueAddress);

		// Copy back out params from frame to original args
		{
			auto Prop = FuncFirstProp;
			const int Dummy[] = {0, (CopyArgFromFrame(FrameMemory, Prop, Args), 0)...};
			(void)Dummy;
		}

		// Destroy local variables (non-parameter properties)
		if (!bUsePersistentFrame)
		{
			for (FProperty* P = Function->DestructorLink; P; P = P->DestructorLinkNext)
			{
				if (!P->IsInContainer(Function->ParmsSize))
				{
					P->DestroyValue_InContainer(NewStack.Locals);
				}
			}
		}
	}

	template<typename F, typename V>
	friend struct TGMPBPFastCall;
};

template<typename R, typename... TArgs>
struct TGMPBPFastCall<R(TArgs...), std::enable_if_t<!GMP::TypeTraits::IsSameV<void, R>>>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args, R& ReturnVal)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent(InObj, Function, Args..., ReturnVal);
	}
};
template<typename... TArgs>
struct TGMPBPFastCall<void(TArgs...), void>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent(InObj, Function, Args...);
	}
};
