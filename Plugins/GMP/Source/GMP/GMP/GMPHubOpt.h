//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPMacros.h"

#if GMP_WITH_DIRECT_SIGNAL

#include "GMPHub.h"
#include "GMPMessageKeySlot.h"
#include "GMPUtils.h"

namespace GMP
{
template<typename KeyT, typename U, typename F>
FORCEINLINE FGMPKey ListenObjectMessageDirect(const TKeySlot<KeyT>& KeySlot, FSigSource InSigSrc, U* Listener, F&& Func, FGMPListenOptions Options = {})
{
#if GMP_WITH_STATIC_STORE
	return FMessageUtils::GetMessageHub()->ListenObjectMessageByStore(KeySlot.GetSignal(), KeySlot.GetKey(), InSigSrc, Listener, Forward<F>(Func), Options);
#else
	FSignalStore* Store = KeySlot.GetStore();
	if (!Store)
		return {};
	FSignalBase ShellTmp;
	ShellTmp.Store = Store->AsShared();
	return FMessageUtils::GetMessageHub()->ListenObjectMessageByStore(&ShellTmp, KeySlot.GetKey(), InSigSrc, Listener, Forward<F>(Func), Options);
#endif
}

namespace DirectTyped
{
	template<typename Tup, std::size_t... Is>
	FORCEINLINE void SendDirectRaw(FSignalStore* Store, FSigSource InSigSrc, const FName& Key, Tup&& Args, std::index_sequence<Is...>)
	{
		constexpr int32 N = (int32)sizeof...(Is);
		FGMPTypedAddr paddrs[N == 0 ? 1 : N] = {FGMPTypedAddr::MakeMsg(std::get<Is>(Args))...};
		using TupleType = typename std::remove_reference<Tup>::type;
		const FArrayTypeNames& TypeNames = FMessageBody::MakeStaticNames((TupleType*)nullptr, std::index_sequence<Is...>{});
		const FGMPExtra Extra{N, 0.f, TypeNames.GetData(), InSigSrc, Key, FGMPKey{}};
#if GMP_WITH_INLINE_FIRE_ENABLED
		Store->ForEachMatchedRaw(InSigSrc, paddrs, &Extra);
#elif GMP_WITH_STATIC_STORE
		GMP::GMPFireWithSigSourceDirectRaw(Store, InSigSrc, paddrs, &Extra);
#else
		FMessageUtils::GetMessageHub()->NotifyMessageDirectRaw(Store, InSigSrc, paddrs, &Extra);
#endif
	}
}  // namespace DirectTyped

template<typename KeyT, typename... Args>
FORCEINLINE auto SendObjectMessageDirect(const TKeySlot<KeyT>& KeySlot, FSigSource InSigSrc, Args&&... InArgs)
	-> std::enable_if_t<!Hub::TSendArgumentsTraits<TypeTraits::TGetLastType<Args...>>::bIsSingleShot>
{
	FSignalStore* Store = KeySlot.GetStore();  // monolithic: direct field read of pre-bound Ptr; modular: lazy ResolvePtr
	if (!Store)
		return;
#if GMP_WITH_DYNAMIC_CALL_CHECK
	const auto& ArgNames = FMessageBody::MakeStaticNamesImpl<std::decay_t<Args>...>();
	const FArrayTypeNames* OldParams = nullptr;
	if (!FMessageUtils::GetMessageHub()->IsSignatureCompatible(true, KeySlot.GetKey(), ArgNames, OldParams, FMessageHub::GetNativeTagType()))
	{
		ensureAlwaysMsgf(false, TEXT("SignatureMismatch On SendDirect %s"), *KeySlot.GetKey().ToString());
		return;
	}
#endif
	FMessageUtils::GetMessageHub()->TraceDirectMessage(KeySlot.GetKey(), InSigSrc);
	auto ArgsTup = std::forward_as_tuple(Forward<Args>(InArgs)...);
	DirectTyped::SendDirectRaw(Store, InSigSrc, KeySlot.GetKey(), ArgsTup, std::make_index_sequence<sizeof...(Args)>{});
}

template<typename KeyT, typename... Args>
FORCEINLINE auto SendObjectMessageDirect(const TKeySlot<KeyT>& KeySlot, FSigSource InSigSrc, Args&&... InArgs)
	-> std::enable_if_t<Hub::TSendArgumentsTraits<TypeTraits::TGetLastType<Args...>>::bIsSingleShot, FGMPKey>
{
	return FMessageUtils::GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InSigSrc, Forward<Args>(InArgs)...);
}

#if GMP_WITH_MSG_HOLDER
template<typename KeyT, typename... Args>
FORCEINLINE FGMPKey StoreObjectMessageDirect(const TKeySlot<KeyT>& KeySlot, FSigSource InSigSrc, Args... InArgs)
{
	FSignalStore* Store = KeySlot.GetStore();  // monolithic: direct field read; modular: lazy ResolvePtr
	if (!Store)
		return {};
	return FMessageUtils::GetMessageHub()->template StoreObjectMessageDirectImpl<-1>(Store, KeySlot.GetKey(), InSigSrc, ForwardParam<Args>(InArgs)...);
}

template<typename KeyT, typename... Args>
FORCEINLINE FGMPKey OnceObjectMessageDirect(const TKeySlot<KeyT>& KeySlot, FSigSource InSigSrc, Args... InArgs)
{
	FSignalStore* Store = KeySlot.GetStore();  // monolithic: direct field read; modular: lazy ResolvePtr
	if (!Store)
		return {};
	return FMessageUtils::GetMessageHub()->template StoreObjectMessageDirectImpl<1>(Store, KeySlot.GetKey(), InSigSrc, ForwardParam<Args>(InArgs)...);
}

template<typename KeyT>
FORCEINLINE int32 RemoveStoredObjectMessageDirect(const TKeySlot<KeyT>& KeySlot, FSigSource InSigSrc)
{
	FSignalStore* Store = KeySlot.GetStore();  // monolithic: direct field read; modular: lazy ResolvePtr
	if (!Store)
		return 0;
	auto* Hub = FMessageUtils::GetMessageHub();
	const bool bHad = (Hub->FindStoredMessageDirect(Store, InSigSrc) != nullptr);
	Hub->RemoveStoredMessageDirect(Store, InSigSrc);
	return bHad ? 1 : 0;
}
#endif  // GMP_WITH_MSG_HOLDER

template<typename KeyT>
FORCEINLINE void UnbindMessageDirect(const TKeySlot<KeyT>& KeySlot, FGMPKey Key)
{
	FMessageUtils::GetMessageHub()->UnbindMessage(KeySlot.GetKey(), Key);
}
template<typename KeyT>
FORCEINLINE void UnbindMessageDirect(const TKeySlot<KeyT>& KeySlot, const UObject* Listener)
{
	FMessageUtils::GetMessageHub()->UnbindMessage(KeySlot.GetKey(), Listener);
}
template<typename KeyT>
FORCEINLINE void UnbindMessageDirect(const TKeySlot<KeyT>& KeySlot, const UObject* Listener, FSigSource InSigSrc)
{
	FMessageUtils::GetMessageHub()->UnbindMessage(KeySlot.GetKey(), Listener, InSigSrc);
}

#if GMP_WITH_STATIC_STORE
// ---- Out-of-line definitions of FMessageUtils' compile-time-key entries (declared in GMPUtils.h). Only present in
// the monolithic static-store build (GMP_WITH_STATIC_STORE = DIRECT && MONOLITHIC); in modular/editor or non-DIRECT
// builds these overloads do not exist and MSGKEY implicitly converts to the by-name API. Here the full slot/store
// (GMPMessageKeySlot.h) and the *Direct helpers above are visible. Each forwards straight to the per-type static
// store via GetKeySlot<KeyT>(). World forms resolve the world first.
//
// IMPORTANT (return-type compatibility): the by-name FMessageUtils::SendObjectMessage returns FGMPKey, and user
// code relies on it (e.g. `if (FGMPHelper::SendObjectMessage(...))`). So the typed entries route through
// SendObjectMessageByStore (returns FGMPKey, keeps full by-name semantics -- signature check / trace / holder --
// while skipping ONLY the GetSig TMap<FName> lookup via the slot's resolved store). They do NOT route through the
// void-returning SendObjectMessageDirect typed-fire fast path (that would change the return contract and break
// `if(Send(...))`). The fire-core three-arg fast path stays the job of the explicit A-class SendObjectMessageDirect
// entry; the everyday transparent upgrade only needs to kill the TMap lookup.
template<typename KeyT, typename... TArgs>
auto FMessageUtils::SendObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(InSigSrc);
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InSigSrc, Forward<TArgs>(Args)...);
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::NotifyObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(InSigSrc);
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InSigSrc, NoRef(Args)...);
}

template<typename KeyT, typename... TArgs>
auto FMessageUtils::SendWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InWorld);
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InWorld, Forward<TArgs>(Args)...);
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::SendWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	return SendWorldMessage(WorldContext->GetWorld(), K, Forward<TArgs>(Args)...);
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::NotifyWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InWorld);
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InWorld, NoRef(Args)...);
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::NotifyWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	return NotifyWorldMessage(WorldContext->GetWorld(), K, NoRef(Args)...);
}

template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenMessage(const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	return GMP::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	GMP_CHECK_SLOW(InSigSrc);
	return GMP::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InSigSrc, Listener, Forward<F>(f), Options);
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	GMP_CHECK_SLOW(!!InWorld);
	return GMP::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InWorld, Listener, Forward<F>(f), Options);
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	return ListenWorldMessage(WorldContext->GetWorld(), K, Listener, Forward<F>(f), Options);
}

#if GMP_WITH_MSG_HOLDER
template<typename KeyT, typename... TArgs>
auto FMessageUtils::StoreObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InObj);
	return GMP::StoreObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InObj, Forward<TArgs>(Args)...);
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::OnceObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InObj);
	return GMP::OnceObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InObj, Forward<TArgs>(Args)...);
}
#endif  // GMP_WITH_MSG_HOLDER
#endif  // GMP_WITH_STATIC_STORE

}  // namespace GMP

#endif  // GMP_WITH_DIRECT_SIGNAL
