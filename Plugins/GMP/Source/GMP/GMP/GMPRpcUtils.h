//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Engine/World.h"
#include "GMPArchive.h"
#include "GMPClass2Prop.h"
#include "GMPSerializer.h"
#include "GMPUtils.h"

class APlayerController;
class UGMPRpcProxy;
class UPackageMap;
namespace GMP
{
class GMP_API FRpcMessageUtils
{
protected:
	static UPackageMap* GetPackageMap(APlayerController* PC);
	static const int32 GetMaxBytes();
	static void PostRPCMsg(APlayerController* PC, const UObject* Sender, const FString& MessageStr, TArray<uint8>& Buffer, bool Reliable = true);
	static FString ProxyGetNameSafe(APlayerController* PC);
	static APlayerController* GetLocalPC(const UObject* Obj);
	static int32 GetPlayerLocalSequence(const APlayerController& PC);

	static bool Z_VerifyRPC(APlayerController* PC, const UObject* Obj, const FMSGKEY& MessageKey, const TArray<FProperty*>& Props);

	template<typename T, typename... TArgs>
	static void Z_PostRPC(bool bReliable, APlayerController* PC, T* Sender, const FMSGKEY& MessageKey, TArgs&... InArgs)
	{
		using MyTraits = Class2Prop::TPropertiesTraits<std::decay_t<TArgs>...>;
		static auto Properties = MyTraits::GetProperties();
		auto Package = FRpcMessageUtils::GetPackageMap(PC);

#if WITH_EDITOR
		bool bSucc = Z_VerifyRPC(PC, Sender, MessageKey, Properties);
		if (!ensureAlways(PC && bSucc))
			return;

		// make standalone work
		if (!Package || Package->GetWorld()->GetNetMode() == NM_Standalone)
		{
			FMessageUtils::GetMessageHub()->SendObjectMessage(MessageKey, Sender, Forward<TArgs>(InArgs)...);
		}
		else
#endif
		{
#if !WITH_SERVER_CODE
			if (ensureAlwaysMsgf(Package, TEXT("null map in PostRPC: PC:%s Obj:%s Key:%s"), *ProxyGetNameSafe(PC), *GetNameSafe(Sender), *MessageKey.ToString()))
#endif
			{
				FGMPNetBitWriter Writer(Package, 0);
				Serializer::NetSerializeWithProps(Package, Writer, Properties, ((std::remove_cv_t<TArgs>&)InArgs)...);
				ensureWorld(PC, Writer.GetNumBits() <= GetMaxBytes() * 8);
				if (ensureAlways(!Writer.IsError()))
					PostRPCMsg(PC, Sender, MessageKey.ToString(), const_cast<TArray<uint8>&>(*Writer.GetBuffer()), bReliable);
			}
		}
	}

public:
	template<typename T, typename... TArgs>
	static FORCEINLINE void PostRPC(APlayerController* PC, T* Sender, const MSGKEY_TYPE& Key, const TArgs&... InArgs)
	{
		Z_PostRPC(true, PC, Sender, Key, const_cast<TArgs&>(InArgs)...);
	}

	template<typename T, typename F>
	static void RecvRPC(APlayerController* PC, const UObject* WatchedObj, const MSGKEY_TYPE& Key, T* Binder, F&& Func, int32 Times = -1)
	{
		using namespace GMP;
		using MyTraits = typename Class2Prop::TFunctionPropertiesTraits<F>::ResultTraits;
		static auto Properties = MyTraits::GetProperties();
#if WITH_EDITOR || WITH_SERVER_CODE
		bool bSucc = Z_VerifyRPC(PC, WatchedObj, Key, Properties);
		if (!ensureAlways(PC && bSucc))
			return;
#endif
		FMessageUtils::GetMessageHub()->ListenObjectMessage(Key, WatchedObj, Binder, Forward<F>(Func), Times);
	}

	template<typename F>
	friend struct TRpcMessageUtils;
};

//////////////////////////////////////////////////////////////////////////
template<typename F>
struct TRpcMessageUtils;

template<typename UserClass, typename... TArgs>
struct TRpcMessageUtils<void (UserClass::*)(TArgs...)>
{
	static auto CallRemote(bool bReliable, APlayerController* PC, const FMSGKEY& HashKey, UserClass* InUserObject, TArgs... InArgs)
	{
		if (!PC)
			PC = FRpcMessageUtils::GetLocalPC(InUserObject);

		if (ensureAlways(PC && InUserObject))
			FRpcMessageUtils::Z_PostRPC(bReliable, PC, InUserObject, HashKey, InArgs...);
	}

	static auto ReceiveRemote(APlayerController* PC, const FMSGKEY& HashKey, UserClass* InUserObject, void (UserClass::*Func)(TArgs...), int32 Times = -1)
	{
		if (ensureAlways(InUserObject))
		{
			using MyTraits = Class2Prop::TPropertiesTraits<std::decay_t<TArgs>...>;
			static auto Properties = MyTraits::GetProperties();

			bool bSucc = FRpcMessageUtils::Z_VerifyRPC(PC, InUserObject, HashKey, Properties);
			if (ensureAlways(bSucc))
				FMessageUtils::GetMessageHub()->ListenObjectMessage(HashKey, InUserObject, InUserObject, Func, Times);
		}
	}
};
}  // namespace GMP

struct GMP_API FGMPRpcBatchScope final
{
public:
	FGMPRpcBatchScope(APlayerController* PC);
	FGMPRpcBatchScope(UGMPRpcProxy* InProxy);
	~FGMPRpcBatchScope();

	FGMPRpcBatchScope& operator=(FGMPRpcBatchScope&& Other)
	{
		Proxy = nullptr;
		Swap(Proxy, Other.Proxy);
#if WITH_EDITOR
		VerifyFrameNumber = Other.VerifyFrameNumber;
#endif
		return *this;
	}
	FGMPRpcBatchScope(FGMPRpcBatchScope&& Other) { *this = MoveTemp(Other); }

private:
	static void* operator new(size_t) = delete;
	static void* operator new[](size_t) = delete;
	static void operator delete(void*) = delete;
	static void operator delete[](void*) = delete;
	FGMPRpcBatchScope(const FGMPRpcBatchScope&) = delete;
	FGMPRpcBatchScope& operator=(const FGMPRpcBatchScope&) = delete;

	UGMPRpcProxy* Proxy;
#if WITH_EDITOR
	uint32 VerifyFrameNumber;
#endif
};

#define BindRPC(PC, Obj, FuncName, ...) GMP::TRpcMessageUtils<decltype(FuncName)>::ReceiveRemote(PC, GMP_RPC_FUNC_NAME(#FuncName), Obj, FuncName, ##__VA_ARGS__)
#define Z_PlayerCallRemote(PC, Obj, FuncName, ...) GMP::TRpcMessageUtils<decltype(FuncName)>::CallRemote(true, PC, GMP_RPC_FUNC_NAME(#FuncName), Obj, ##__VA_ARGS__)
#define Z_PlayerCallRemoteUnreliable(PC, Obj, FuncName, ...) GMP::TRpcMessageUtils<decltype(FuncName)>::CallRemote(false, PC, GMP_RPC_FUNC_NAME(#FuncName), Obj, ##__VA_ARGS__)
#define PlayerCallRequest Z_PlayerCallRemote
#define PlayerCallNotify Z_PlayerCallRemote
#define PlayerCallNotifyUnreliable Z_PlayerCallRemoteUnreliable
