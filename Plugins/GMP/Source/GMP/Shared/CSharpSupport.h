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

// Inverse of ToUObject: bare UObject* -> managed handle (listen-side, native pushes an object arg back up to C#). The exact
// UnrealCSharp wrap entry differs by version; centralized here so real-compile only adjusts one spot.
FORCEINLINE IManagedHandle BindUObject(UObject* InObj)
{
	return InObj ? FCSharpEnvironment::GetEnvironment().Bind<UObject>(InObj) : IManagedHandle{};
}

// Fire path: raw function-pointer direct call into the C# [UnmanagedCallersOnly] entry (zero reflection/marshal).
inline void InvokeCallback(const FCSharpCb& Cb, const FGMPTypedAddr* Paddrs, int32 NumArgs)
{
	if (const FGMPOnFireFn Fn = GetOnFireFn())
		Fn(Cb.CbHandle, Paddrs, NumArgs);
}

// C# -> GMP notify. Sender + N param addresses (already UE-typed memory). Generic path uses FName key; the key-baked fast
// path is stage 3 (per-tag native .gen.cpp under GMP_CSHARP_STATIC_BIND).
inline bool NotifyObjectMessageImpl(IManagedHandle InSender, const FName& MsgKey, FGMPTypedAddr* Params, int32 NumArgs, const char* Loc = nullptr)
{
	UObject* Sender = ToUObject(InSender);
#if GMP_TRACE_SCRIPT_SRC && WITH_EDITOR
	if (Loc && *Loc)  // managed side passes "file:line" via [CallerFilePath]/[CallerLineNumber]
		GMP::TraceScriptMessageSource(MsgKey, UTF8_TO_TCHAR(Loc), /*bIsListen*/ false);
#endif
	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("CSharp"));
	GMP::FTypedAddresses Addrs;
	Addrs.Reserve(NumArgs);
	for (int32 i = 0; i < NumArgs; ++i)
		Addrs.Add(Params[i]);
	return FGMPHelper::ScriptNotifyMessage(MsgKey, Addrs, Sender);
}

// C# arg type tags (must mirror Interop.GMPBridge.EArgType). Scalar values arrive in Payloads[i], UObject args in ObjHandles[i].
enum class EGMPCSharpArg : uint8
{
	Object = 0,   // ObjHandles[i] = managed handle; Payloads[i] unused
	Int64 = 1,    // Payloads[i] = int64 value
	Double = 2,   // Payloads[i] = double bit-pattern
	Bool = 3,     // Payloads[i] = 0/1
	String = 4,   // Payloads[i] = (int64)utf8 char* (null-terminated)
};

// C# -> GMP notify with full per-arg type marshal. Native rebuilds typed FGMPTypedAddr[] via TClass2Prop (TypeName filled by
// FromHolderArray) -> unregistered tags get their signature inferred here exactly like the lua/js backends. Sender + N args as
// three parallel arrays (TypeTags[i], Payloads[i], ObjHandles[i]); scalars use Payloads, UObject args use ObjHandles.
inline bool NotifyObjectMessageTypedImpl(IManagedHandle InSender, const FName& MsgKey, int32 NumArgs, const uint8* TypeTags, const int64* Payloads, const IManagedHandle* ObjHandles, const char* Loc = nullptr)
{
	UObject* Sender = ToUObject(InSender);
#if GMP_TRACE_SCRIPT_SRC && WITH_EDITOR
	if (Loc && *Loc)
		GMP::TraceScriptMessageSource(MsgKey, UTF8_TO_TCHAR(Loc), /*bIsListen*/ false);
#endif
	GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("CSharp"));

	FGMPPropStackHolderArray PropHolders;
	PropHolders.Reserve(NumArgs);
	for (int32 i = 0; i < NumArgs; ++i)
	{
		FProperty* Prop = nullptr;
		switch (static_cast<EGMPCSharpArg>(TypeTags[i]))
		{
			case EGMPCSharpArg::Object: Prop = GMP::TClass2Prop<UObject*>::GetProperty(); break;
			case EGMPCSharpArg::Int64:  Prop = GMP::TClass2Prop<int64>::GetProperty(); break;
			case EGMPCSharpArg::Double: Prop = GMP::TClass2Prop<double>::GetProperty(); break;
			case EGMPCSharpArg::Bool:   Prop = GMP::TClass2Prop<bool>::GetProperty(); break;
			case EGMPCSharpArg::String: Prop = GMP::TClass2Prop<FString>::GetProperty(); break;
			default: break;
		}
		if (!Prop)
		{
			GMP_WARNING(TEXT("[GMPCSharp] cannot infer type for arg %d of tag %s"), i, *MsgKey.ToString());
			return false;
		}
		auto& Holder = PropHolders.Emplace_GetRef(Prop, FMemory_Alloca_Aligned(Prop->GetElementSize(), Prop->GetMinAlignment()));
		void* Dst = const_cast<void*>(Holder.GetAddr());
		switch (static_cast<EGMPCSharpArg>(TypeTags[i]))
		{
			case EGMPCSharpArg::Object: *reinterpret_cast<UObject**>(Dst) = ObjHandles ? ToUObject(ObjHandles[i]) : nullptr; break;
			case EGMPCSharpArg::Int64:  *reinterpret_cast<int64*>(Dst) = Payloads[i]; break;
			case EGMPCSharpArg::Double: *reinterpret_cast<double*>(Dst) = *reinterpret_cast<const double*>(&Payloads[i]); break;
			case EGMPCSharpArg::Bool:   static_cast<FBoolProperty*>(Prop)->SetPropertyValue(Dst, Payloads[i] != 0); break;
			case EGMPCSharpArg::String: *reinterpret_cast<FString*>(Dst) = Payloads[i] ? UTF8_TO_TCHAR(reinterpret_cast<const char*>(Payloads[i])) : TEXT(""); break;
			default: break;
		}
	}

	GMP::FTypedAddresses Addrs;
	Addrs.Reserve(NumArgs);
	return FGMPHelper::ScriptNotifyMessage(MsgKey, FGMPTypedAddr::FromHolderArray(Addrs, PropHolders), Sender);
}

// C# arg type tag -> static FProperty (shared by notify marshal and listen-side signature registration).
inline FProperty* CSharpArgProp(EGMPCSharpArg Tag)
{
	switch (Tag)
	{
		case EGMPCSharpArg::Object: return GMP::TClass2Prop<UObject*>::GetProperty();
		case EGMPCSharpArg::Int64:  return GMP::TClass2Prop<int64>::GetProperty();
		case EGMPCSharpArg::Double: return GMP::TClass2Prop<double>::GetProperty();
		case EGMPCSharpArg::Bool:   return GMP::TClass2Prop<bool>::GetProperty();
		case EGMPCSharpArg::String: return GMP::TClass2Prop<FString>::GetProperty();
		default: return nullptr;
	}
}

// C# -> GMP listen. Holds the managed callback id, dispatches via raw fire fn on fire. TypeTags (from the strongly-typed
// C# generic callback) let a first-seen unregistered tag register its signature from the listen side (editor/dev only).
inline uint64 ListenObjectMessageImpl(IManagedHandle InWatched, const FName& MsgKey, IManagedHandle InWeak, int64 InCbHandle, int32 LeftTimes, const uint8* InTypeTags = nullptr, int32 InNumTypes = 0, const char* Loc = nullptr)
{
	UObject* WatchedObject = ToUObject(InWatched);
	UObject* WeakObj = ToUObject(InWeak);
	if (!ensure(!MsgKey.IsNone()))
		return 0;
#if GMP_TRACE_SCRIPT_SRC && WITH_EDITOR
	if (Loc && *Loc)  // managed side passes "file:line" via [CallerFilePath]/[CallerLineNumber]
		GMP::TraceScriptMessageSource(MsgKey, UTF8_TO_TCHAR(Loc), /*bIsListen*/ true);
#endif

#if GMP_WITH_DYNAMIC_CALL_CHECK
	// Strong-typed C# listen infers into the table: generic callback arg types (TypeTags) register the signature recv-side.
	if (InTypeTags && InNumTypes > 0)
	{
		GMP::FArrayTypeNames ArgNames;
		ArgNames.Reserve(InNumTypes);
		for (int32 i = 0; i < InNumTypes; ++i)
		{
			FProperty* P = CSharpArgProp(static_cast<EGMPCSharpArg>(InTypeTags[i]));
			ArgNames.Add(P ? GMP::Reflection::GetPropertyName(P) : NAME_None);
		}
		const GMP::FArrayTypeNames* OldParams = nullptr;
		GMP::FMessageHub::FTagTypeSetter SetMsgTagType(TEXT("CSharp"));
		if (!ensure(GMP::FMessageHub::IsSignatureCompatible(false, MsgKey, ArgNames, OldParams)))
		{
			GMP_WARNING(TEXT("SignatureMismatch On CSharp Listen %s"), *MsgKey.ToString());
			return 0;
		}
	}
#endif

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

// Listen-side typed unpack: OnGMPFire gets the raw paddrs; C# reads arg i as the registered type via these (no C#-side layout
// assumptions about FGMPTypedAddr). Scalar getters reinterpret the arg address; string returns a UTF8 buffer the caller copies.
inline int64 GetArgInt64Impl(const FGMPTypedAddr* Paddrs, int32 Index) { return *reinterpret_cast<const int64*>(Paddrs[Index].ToAddr()); }
inline double GetArgDoubleImpl(const FGMPTypedAddr* Paddrs, int32 Index) { return *reinterpret_cast<const double*>(Paddrs[Index].ToAddr()); }
inline bool GetArgBoolImpl(const FGMPTypedAddr* Paddrs, int32 Index) { return *reinterpret_cast<const bool*>(Paddrs[Index].ToAddr()); }
// UTF8-encode the FString arg into Buf (Cap bytes incl. null); returns bytes needed (excl. null) so C# can size/copy.
inline int32 GetArgStringUtf8Impl(const FGMPTypedAddr* Paddrs, int32 Index, char* Buf, int32 Cap)
{
	const FString& Str = *reinterpret_cast<const FString*>(Paddrs[Index].ToAddr());
	FTCHARToUTF8 Conv(*Str, Str.Len());
	const int32 Need = Conv.Length();
	if (Buf && Cap > 0)
	{
		const int32 N = FMath::Min(Need, Cap - 1);
		FMemory::Memcpy(Buf, Conv.Get(), N);
		Buf[N] = 0;
	}
	return Need;
}
// Object arg -> bare UObject*; C# wraps it back into a managed handle via UnrealCSharp's object table.
inline UObject* GetArgObjectImpl(const FGMPTypedAddr* Paddrs, int32 Index) { return *reinterpret_cast<UObject* const*>(Paddrs[Index].ToAddr()); }

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
