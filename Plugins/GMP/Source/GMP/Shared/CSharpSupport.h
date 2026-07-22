//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
// UnrealCSharp (crazytuzi) backend adapter. Gated on either runtime backend (they are mutually exclusive: desktop=CoreCLR
// / mobile=Mono, UnrealCSharpCore.build.cs). Only compiled when included from an UnrealCSharp-module TU. Callbacks/handles
// go through the domain-agnostic IScriptDomain/FDomain layer so both CoreCLR(.NET 10 primary) and Mono are covered.
//
#if defined(WITH_MONO) && WITH_MONO || defined(WITH_CORECLR) && WITH_CORECLR
#include "GMPCore.h"

#include "Domain/FDomain.h"
#include "Domain/Script/IScriptDomain.h"
#include "Domain/Script/IManagedHandle.h"
#include "Environment/FCSharpEnvironment.h"

namespace CSharpSupport
{
using FGMPOnFireFn = void (*)(int64 /*cbHandle*/, const FGMPTypedAddr* /*paddrs*/, int32 /*numArgs*/);
inline FGMPOnFireFn& GetOnFireFn() { static FGMPOnFireFn Fn = nullptr; return Fn; }

// Called from C# once at init to hand native the fire entry pointer (registered like UnrealCSharp bridge fns).
inline void SetOnFireFn(void* InFn) { GetOnFireFn() = reinterpret_cast<FGMPOnFireFn>(InFn); }

// Holds the C# listen callback identity for its lifetime. cbHandle is the managed-side id (native never derefs it, just
// passes it back to OnGMPFire). Pin the managed delegate on the C# side; native only stores the opaque id here.
struct FCSharpCb
{
	int64 CbHandle = 0;   // opaque managed-side id (C# handle->delegate)
	FCSharpCb() = default;
	explicit FCSharpCb(int64 InHandle) : CbHandle(InHandle) {}
};

// Resolve a C# object handle to a bare UObject* (WatchedObject/WeakObj/Sender arrive as IManagedHandle; map via UnrealCSharp).
FORCEINLINE UObject* ToUObject(IManagedHandle InObj)
{
	return IManagedHandleIsValid(InObj) ? FCSharpEnvironment::GetEnvironment().GetObject<UObject>(InObj) : nullptr;
}

// Fire path: raw function-pointer direct call into the C# [UnmanagedCallersOnly] entry (zero reflection/marshal).
inline void InvokeCallback(const FCSharpCb& Cb, const FGMPTypedAddr* Paddrs, int32 NumArgs)
{
	if (const FGMPOnFireFn Fn = GetOnFireFn())
		Fn(Cb.CbHandle, Paddrs, NumArgs);
}

// C# -> GMP notify. Sender + N param addresses (already UE-typed memory). Generic path uses FName key; the key-baked fast
// path is stage 3 (per-tag native .gen.cpp under GMP_CSHARP_STATIC_BIND).
inline bool NotifyObjectMessageImpl(IManagedHandle InSender, const FName& MsgKey, FGMPTypedAddr* Params, int32 NumArgs)
{
	UObject* Sender = ToUObject(InSender);
	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("CSharp"));
	GMP::FTypedAddresses Addrs;
	Addrs.Reserve(NumArgs);
	for (int32 i = 0; i < NumArgs; ++i)
		Addrs.Add(Params[i]);
	return FGMPHelper::ScriptNotifyMessage(MsgKey, Addrs, Sender);
}

// C# -> GMP listen. Holds the managed callback id, dispatches via raw fire fn on fire.
inline uint64 ListenObjectMessageImpl(IManagedHandle InWatched, const FName& MsgKey, IManagedHandle InWeak, int64 InCbHandle, int32 LeftTimes)
{
	UObject* WatchedObject = ToUObject(InWatched);
	UObject* WeakObj = ToUObject(InWeak);
	if (!ensure(!MsgKey.IsNone()))
		return 0;

	FCSharpCb Cb(InCbHandle);
#if GMP_WITH_DIRECT_SIGNAL
	return FGMPHelper::ScriptListenMessageRaw(
		WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource::NullSigSrc,
		MsgKey, WeakObj,
		[Cb{std::move(Cb)}](const FGMPTypedAddr* paddrs, const GMP::FGMPExtra* extra) { InvokeCallback(Cb, paddrs, extra->Size); },
		LeftTimes);
#else
	return FGMPHelper::ScriptListenMessage(
		WatchedObject ? FGMPSigSource(WatchedObject) : FGMPSigSource::NullSigSrc,
		MsgKey, WeakObj,
		[Cb{std::move(Cb)}](GMP::FMessageBody& MsgBody) {
			const auto A = MsgBody.GetParams();
			InvokeCallback(Cb, A.GetData(), A.Num());
		},
		LeftTimes);
#endif
}

inline void UnbindObjectMessageImpl(const FName& MsgKey, IManagedHandle InListened, uint64 InKey)
{
	if (UObject* Listened = ToUObject(InListened))
		FGMPHelper::ScriptUnbindMessage(MsgKey, Listened);
	else
		FGMPHelper::ScriptUnbindMessage(MsgKey, FGMPKey(InKey));
}

#if defined(GMP_CSHARP_STATIC_BIND) && GMP_CSHARP_STATIC_BIND && !WITH_EDITOR && GMP_WITH_DIRECT_SIGNAL
// C# init: resolve the process-stable static store pointer for a tag once, cache it C#-side (Shipping: never invalidated).
inline int64 GetStoreByKeyImpl(const char* InTag)
{
	GMP::FSignalStore* Store = FGMPHelper::GetMessageHub()->GetDirectStoreByKey(FName(UTF8_TO_TCHAR(InTag)));
	return static_cast<int64>(reinterpret_cast<UPTRINT>(Store));
}

// C# fire: direct by-store notify. Store is the cached pointer; key still needed for trace (verify compiled out in Shipping).
inline bool NotifyByStoreImpl(int64 InStore, const FName& MsgKey, FGMPTypedAddr* Params, int32 NumArgs, IManagedHandle InSender)
{
	UObject* Sender = ToUObject(InSender);
	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("CSharp"));
	GMP::FTypedAddresses Addrs;
	Addrs.Reserve(NumArgs);
	for (int32 i = 0; i < NumArgs; ++i)
		Addrs.Add(Params[i]);
	auto* Store = reinterpret_cast<GMP::FSignalStore*>(static_cast<UPTRINT>(InStore));
	return FGMPHelper::GetMessageHub()->ScriptNotifyMessageByStore(Store, MsgKey, Addrs, Sender);
}
#endif
}  // namespace CSharpSupport
#endif

// how to use:
// 1. add "GMP" to PrivateDependencyModuleNames in UnrealCSharp/UnrealCSharpCore .build.cs (+ PrivateIncludePaths to
//    GMP/Source/GMP/Shared if unseen).
// 2. include this header into an UnrealCSharp-module TU (e.g. a new FRegisterGMP.cpp), then register the three functions
//    via FClassBuilder(TEXT("FGMP"), NAMESPACE_LIBRARY).Function("NotifyObjectMessage", ...)... -- registered once, both
//    Mono (mono_add_internal_call) and CoreCLR (MethodBridge) backends are covered.
// 3. cleanup: on assembly unload (FUnrealCSharpModuleDelegates::OnUnrealCSharpModuleInActive) drop domain-keyed listens.
