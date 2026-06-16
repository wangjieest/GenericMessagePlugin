//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPMacros.h"

#if GMP_WITH_DIRECT_SIGNAL

#include "GMPHub.h"
#include "GMPMessageKeySlot.h"
#include "GMPUtils.h"

namespace GMP
{
namespace DirectTyped
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
}  // namespace DirectTyped

template<typename KeyT, typename... TArgs>
auto FMessageUtils::SendObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(InSigSrc);
#if GMP_WITH_STATIC_STORE
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InSigSrc, Forward<TArgs>(Args)...);
#else
	return GetMessageHub()->SendObjectMessage(FMSGKEYFind(FMSGKEY(K.GetKey())), InSigSrc, Forward<TArgs>(Args)...);
#endif
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::NotifyObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(InSigSrc);
#if GMP_WITH_STATIC_STORE
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InSigSrc, NoRef(Args)...);
#else
	return GetMessageHub()->SendObjectMessage(FMSGKEYFind(FMSGKEY(K.GetKey())), InSigSrc, NoRef(Args)...);
#endif
}

template<typename KeyT, typename... TArgs>
auto FMessageUtils::SendWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InWorld);
#if GMP_WITH_STATIC_STORE
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InWorld, Forward<TArgs>(Args)...);
#else
	return GetMessageHub()->SendObjectMessage(FMSGKEYFind(FMSGKEY(K.GetKey())), InWorld, Forward<TArgs>(Args)...);
#endif
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
#if GMP_WITH_STATIC_STORE
	const auto KeySlot = GMP::GetKeySlot<KeyT>();
	return GetMessageHub()->SendObjectMessageByStore(KeySlot.GetStore(), KeySlot.GetKey(), InWorld, NoRef(Args)...);
#else
	return GetMessageHub()->SendObjectMessage(FMSGKEYFind(FMSGKEY(K.GetKey())), InWorld, NoRef(Args)...);
#endif
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::NotifyWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	return NotifyWorldMessage(WorldContext->GetWorld(), K, NoRef(Args)...);
}

template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenMessage(const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
#else
	return GetMessageHub()->ListenObjectMessage(FMSGKEY(K.GetKey()), FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
#endif
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenObjectMessage(FSigSource InSigSrc, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	GMP_CHECK_SLOW(InSigSrc);
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InSigSrc, Listener, Forward<F>(f), Options);
#else
	return GetMessageHub()->ListenObjectMessage(FMSGKEY(K.GetKey()), InSigSrc, Listener, Forward<F>(f), Options);
#endif
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenWorldMessage(const UWorld* InWorld, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	GMP_CHECK_SLOW(!!InWorld);
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InWorld, Listener, Forward<F>(f), Options);
#else
	return GetMessageHub()->ListenObjectMessage(FMSGKEY(K.GetKey()), InWorld, Listener, Forward<F>(f), Options);
#endif
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::ListenWorldMessage(const UObject* WorldContext, const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
	return ListenWorldMessage(WorldContext->GetWorld(), K, Listener, Forward<F>(f), Options);
}

template<typename KeyT, typename F>
FGMPKey FMessageUtils::UnsafeListenMessage(const TMSGKEYTyped<KeyT>& K, F&& f, GMP::FGMPListenOptions Options)
{
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), FSigSource::NullSigSrc, GMP_LISTENER_ANY(), Forward<F>(f), Options);
#else
	return GetMessageHub()->ListenObjectMessage(FMSGKEY(K.GetKey()), FSigSource::NullSigSrc, GMP_LISTENER_ANY(), Forward<F>(f), Options);
#endif
}
template<typename KeyT, typename T, typename F>
FGMPKey FMessageUtils::UnsafeListenMessage(const TMSGKEYTyped<KeyT>& K, T* Listener, F&& f, GMP::FGMPListenOptions Options)
{
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::ListenObjectMessageDirect(GMP::GetKeySlot<KeyT>(), FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
#else
	return GetMessageHub()->ListenObjectMessage(FMSGKEY(K.GetKey()), FSigSource::NullSigSrc, Listener, Forward<F>(f), Options);
#endif
}

#if GMP_WITH_MSG_HOLDER
template<typename KeyT, typename... TArgs>
auto FMessageUtils::StoreObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InObj);
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::StoreObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InObj, Forward<TArgs>(Args)...);
#else
	return GetMessageHub()->StoreObjectMessage(FMSGKEYFind(FMSGKEY(K.GetKey())), InObj, Forward<TArgs>(Args)...);
#endif
}
template<typename KeyT, typename... TArgs>
auto FMessageUtils::OnceObjectMessage(const UObject* InObj, const TMSGKEYTyped<KeyT>& K, TArgs&&... Args)
{
	GMP_CHECK_SLOW(!!InObj);
#if GMP_WITH_STATIC_STORE
	return GMP::DirectTyped::OnceObjectMessageDirect(GMP::GetKeySlot<KeyT>(), InObj, Forward<TArgs>(Args)...);
#else
	return GetMessageHub()->OnceObjectMessage(FMSGKEY(K.GetKey()), InObj, Forward<TArgs>(Args)...);
#endif
}
#endif  // GMP_WITH_MSG_HOLDER

}  // namespace GMP

#endif  // GMP_WITH_DIRECT_SIGNAL
