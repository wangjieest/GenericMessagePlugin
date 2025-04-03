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
	EMessageTypeBoth,
	EMessageTypeServer,
	EMessageTypeClient,
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
	static FGMPTypedAddr ListenMessageViaKey(UObject* Listener, FName MessageId, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& WatchedObj);
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Listener", DefaultToSelf = "Listener", Times = "-1", Order = "0", Type = "0", AutoCreateRefTerm = "WatchedObj"))
	static FGMPTypedAddr ListenMessageViaKeyValidate(const TArray<FName>& ArgNames, UObject* Listener, FName MessageId, FName EventName, int32 Times, int32 Order, uint8 Type, uint8 BodyDataMask, UGMPManager* Mgr, const FGMPObjNamePair& WatchedObj);

	// Notify
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Sender,Params,MessageId"))
	static void NotifyMessageByKey(const FString& MessageId, const FGMPObjNamePair& Sender, UPARAM(ref) TArray<FGMPTypedAddr>& Params, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, AutoCreateRefTerm = "Sender,MessageId", Variadic))
	static void NotifyMessageByKeyVariadic(const FString& MessageId, const FGMPObjNamePair& Sender, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execNotifyMessageByKeyVariadic);

	// RequestMessage
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Sender", DefaultToSelf = "Sender", AutoCreateRefTerm = "Params,MessageId"))
	static void RequestMessage(FGMPKey& RspKey, FName EventName, const FString& MessageId, UObject* Sender, UPARAM(ref) TArray<FGMPTypedAddr>& Params, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "Sender", DefaultToSelf = "Sender", AutoCreateRefTerm = "MessageId", Variadic))
	static void RequestMessageVariadic(FGMPKey& RspKey, FName EventName, const FString& MessageId, UObject* Sender, uint8 Type = 0, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execRequestMessageVariadic);

	// ResponseMessage
	UFUNCTION(BlueprintCallable, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "SigSource", DefaultToSelf = "SigSource", AutoCreateRefTerm = "Params,MessageId"))
	static void ResponseMessage(FGMPKey SeqId, UPARAM(ref) TArray<FGMPTypedAddr>& Params, UObject* SigSource, UGMPManager* Mgr = nullptr);
	UFUNCTION(BlueprintCallable, CustomThunk, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true, HidePin = "SigSource", DefaultToSelf = "SigSource", AutoCreateRefTerm = "MessageId", Variadic))
	static void ResponseMessageVariadic(FGMPKey SeqId, UObject* SigSource, UGMPManager* Mgr = nullptr);
	DECLARE_FUNCTION(execResponseMessageVariadic);

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

	UFUNCTION(BlueprintPure, meta = (CallableWithoutWorldContext, BlueprintInternalUseOnly = true))
	static FString FormatStringByName(const FString& InFmtStr, const TMap<FString, FString>& InArgs);
	
	UFUNCTION(BlueprintPure, meta = (WorldContext = "InCtx"))
	static bool IsListenServer(UObject* InCtx);
};

template<typename F, typename = void>
struct TGMPBPFastCall;

class FGMPBPFastCallImpl
{
	template<typename T, typename A, typename... Ts, std::size_t... Is, typename F>
	static void TransParametersImpl(TArray<T, A>& OutRecs, std::tuple<Ts...>& OutArgs, std::index_sequence<Is...>, F& Func)
	{
		static_assert(sizeof...(Is) <= sizeof...(Ts), "err");
		const int Temp[] = {0, (Func(OutRecs[Is], &std::get<Is>(OutArgs)), 0)...};
		(void)(Temp);
	}

	template<typename T, typename A, typename... Ts, typename F>
	static void TransParameters(TArray<T, A>& OutRecs, std::tuple<Ts...>& OutArgs, F& Func)
	{
		TransParametersImpl(OutRecs, OutArgs, std::make_index_sequence<sizeof...(Ts)>{}, Func);
	}

	template<typename... TArgs>
	static void InvokeBlueprintEvent(UObject* InObj, UFunction* Function, TArgs... Args)
	{
		using namespace GMP;
		GMP_CHECK_SLOW(InObj && Function);

		const bool bReturnVoid = Function->ReturnValueOffset == MAX_uint16;
		std::tuple<std::decay_t<TArgs>...> LocalsOnStack(Args...);
		uint8* InLocals = (uint8*)&LocalsOnStack;

#if GMP_WITH_DYNAMIC_CALL_CHECK
		const auto& ArgNames = Hub::DefaultTraits::MakeNames(LocalsOnStack);
		if (!ensure(ArgNames.Num() == Function->NumParms))
			return;

		auto FuncFirstProp = Reflection::GetFunctionChildProperties(Function);
		auto FuncProp = FuncFirstProp;
		for (auto& TypeName : ArgNames)
		{
			if (!ensure(FuncProp && Reflection::EqualPropertyName(FuncProp, TypeName)))
				return;
			FuncProp = (FProperty*)FuncProp->Next;
		}
		const auto* TypeName = &ArgNames[0];
#else
		auto FuncFirstProp = Reflection::GetFunctionChildProperties(Function);
#endif

		FFrame Frame(InObj, Function, InLocals, nullptr, FuncFirstProp);
		uint8* ReturnValueAddress = !bReturnVoid ? ((uint8*)InLocals + Function->ReturnValueOffset) : nullptr;

		const bool bHasOutParms = Function->HasAnyFunctionFlags(FUNC_HasOutParms);
		if (bHasOutParms)
		{
			// if the function has out parameters, fill the stack frame's out parameter info with the info for those params
			constexpr auto ArgsCnt = sizeof...(TArgs);
			TArray<void*, TFixedAllocator<ArgsCnt>> OutRetruns;

			OutRetruns.AddZeroed(ArgsCnt);
			TransParametersImpl(OutRetruns, std::tuple<std::decay_t<TArgs>&...>(Args...), std::make_index_sequence<ArgsCnt>{}, [](auto& Elm, auto& ValRef) { Elm = &ValRef; });
			int32 Idx = 0;
			FOutParmRec** LastOut = &Frame.OutParms;
			for (FProperty* Property = FuncFirstProp; Property && (Property->PropertyFlags & (CPF_Parm)) == CPF_Parm; Property = (FProperty*)Property->Next)
			{
#if GMP_WITH_DYNAMIC_CALL_CHECK
				if (!ensure(Reflection::EqualPropertyName(Property, *TypeName++)))
				{
					return;
				}
#endif
				if (Property->HasAnyPropertyFlags(CPF_OutParm))
				{
					FOutParmRec* Out = (FOutParmRec*)FMemory_Alloca(sizeof(FOutParmRec));
					Out->PropAddr = Property->ContainerPtrToValuePtr<uint8>(LocalsOnStack);
					Out->Property = Property;

					ensure(Out->PropAddr == OutRetruns[Idx]);

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
				else
				{
					OutRetruns[Idx] = nullptr;
				}
				++Idx;
			}

			if (*LastOut)
			{
				(*LastOut)->NextOutParm = nullptr;
			}

			Function->Invoke(InObj, Frame, ReturnValueAddress);

			TransParametersImpl(OutRetruns, std::tuple<std::decay_t<TArgs>&...>(Args...), std::make_index_sequence<ArgsCnt>{}, [](auto& Elm, auto& ValRef) {
				if (Elm)
					ValRef = *reinterpret_cast<decltype(ValRef)*>(Elm);
			});
		}
		else
		{
			Function->Invoke(InObj, Frame, ReturnValueAddress);
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
		FGMPBPFastCallImpl::InvokeBlueprintEvent<TArgs&..., R&>(InObj, Function, Args..., ReturnVal);  //TODO
	}
};
template<typename... TArgs>
struct TGMPBPFastCall<void(TArgs...), void>
{
	static void FastInvoke(UObject* InObj, UFunction* Function, TArgs&... Args)
	{
		FGMPBPFastCallImpl::InvokeBlueprintEvent<TArgs&...>(InObj, Function, Args...);  //TODO
	}
};
