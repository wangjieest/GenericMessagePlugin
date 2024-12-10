//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "CoreUObject.h"

#include "Algo/AnyOf.h"
#include "GMPSignals.inl"
#include "Logging/LogMacros.h"
#include "Misc/AssertionMacros.h"
#include "Misc/ScopeExit.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Templates/TypeCompatibleBytes.h"
#include "Templates/UnrealTemplate.h"
#include "UObject/WeakObjectPtr.h"
#include "UnrealCompatibility.h"

#include <atomic>

namespace GMP
{
class FMessageHub;
// clang-format off
namespace Details
{
template<typename... FuncArgs>
struct Invoker
{
private:
	static constexpr auto FuncArgsCnt = sizeof... (FuncArgs);

	template<typename F, typename... TArgs>
	FORCEINLINE static decltype(auto) ApplyImpl(const F& f, TArgs&&... Args)
	{
		auto ftor = [&](ForwardParam<FuncArgs>... InArgs, auto&&...) { return f(static_cast<FuncArgs>(InArgs)...); };
		return ftor(std::forward<TArgs>(Args)...);
	}
	template<typename T, typename R, typename... TArgs>
	FORCEINLINE static decltype(auto) ApplyImpl(R(T::* f)(FuncArgs...), T* const Obj, TArgs&&... Args)
	{
		auto ftor = [&](ForwardParam<FuncArgs>... InArgs, auto&&...) { return (Obj->*f)(static_cast<FuncArgs>(InArgs)...); };
		return ftor(std::forward<TArgs>(Args)...);
	}
	template<typename T, typename R, typename... TArgs>
	FORCEINLINE static decltype(auto) ApplyImpl(R(T::* f)(FuncArgs...) const, T const* const Obj, TArgs&&... Args)
	{
		auto ftor = [&](ForwardParam<FuncArgs>... InArgs, auto&&...) { return (Obj->*f)(static_cast<FuncArgs>(InArgs)...); };
		return ftor(std::forward<TArgs>(Args)...);
	}

#if !defined(__clang__) && defined(_MSC_VER) && _MSC_VER < 1925   // replace auto&&... with ... in the lambda
	template<size_t Cnt, size_t I, typename T>
	static FORCEINLINE auto Conv(T&& t, typename std::enable_if<(I < Cnt)>::type* tag = nullptr) ->decltype(auto) { return std::forward<T>(t); }
	template<size_t Cnt, size_t I, typename T>
	static FORCEINLINE auto Conv(const T& t, typename std::enable_if<(I >= Cnt)>::type* tag = nullptr) -> auto { return nullptr; }

	template<size_t... Is, typename F, typename... TArgs>
	FORCEINLINE static decltype(auto) ApplyImpl2(std::index_sequence<Is...>*, const F& f, TArgs&&... Args)
	{
		auto ftor = [&](ForwardParam<FuncArgs>... InArgs, ...) { return f(static_cast<FuncArgs>(InArgs)...); };
		return ftor(Conv<FuncArgsCnt, Is, TArgs>(Args)...);
	}
	template<size_t... Is, typename T, typename R, typename... TArgs>
	FORCEINLINE static decltype(auto) ApplyImpl2(std::index_sequence<Is...>*, R(T::* f)(FuncArgs...), T* const Obj, TArgs&&... Args)
	{
		auto ftor = [&](ForwardParam<FuncArgs>... InArgs, ...) { return (Obj->*f)(static_cast<FuncArgs>(InArgs)...); };
		return ftor(Conv<FuncArgsCnt, Is, TArgs>(Args)...);
	}
	template<size_t... Is, typename T, typename R, typename... TArgs>
	FORCEINLINE static decltype(auto) ApplyImpl2(std::index_sequence<Is...>*, R(T::* f)(FuncArgs...) const, T const* const Obj, TArgs&&... Args)
	{
		auto ftor = [&](ForwardParam<FuncArgs>... InArgs, ...) { return (Obj->*f)(static_cast<FuncArgs>(InArgs)...); };
		return ftor(Conv<FuncArgsCnt, Is, TArgs>(Args)...);
	}
	template<typename F, typename... TArgs>
	FORCEINLINE static decltype(auto) ProxyApply(std::true_type, const F& f, TArgs&&... Args) { return ApplyImpl(f, std::forward<TArgs>(Args)...); }
	template<typename F, typename... TArgs>
	FORCEINLINE static decltype(auto) ProxyApply(std::false_type, const F& f, TArgs&&... Args) { return ApplyImpl2((std::index_sequence_for<TArgs...>*)nullptr, f, std::forward<TArgs>(Args)...); }
#	define Z_GMP_PROXY_APPLY(...) ProxyApply(std::integral_constant<bool, sizeof...(TArgs) == sizeof...(FuncArgs)>{}, __VA_ARGS__)
#else
#	define Z_GMP_PROXY_APPLY(...) ApplyImpl(__VA_ARGS__)
#endif

public:
	template<typename F, typename... TArgs>
	FORCEINLINE static decltype(auto) Apply(const F& f, TArgs&&... Args)
	{
		static_assert(sizeof...(TArgs) >= sizeof...(FuncArgs), "Args count error");
		return Z_GMP_PROXY_APPLY(f, std::forward<TArgs>(Args)...);
	}
#undef Z_GMP_PROXY_APPLY
};
}  // namespace Details
// clang-format on

struct FSigElmData
{
	const auto& GetHandler() const { return Handler; }
	const auto& GetSource() const { return Source; }
	template<typename F>
	FORCEINLINE bool TestInvokable(const F& Func)
	{
		return !Handler.IsStale(true) && (Times != 0) && (Func(), (Times != 0 && (Times < 0 || --Times > 0)));
	}
	auto GetGMPKey() const { return GMPKey; }

	void SetLeftTimes(int32 InTimes) { Times = (InTimes < 0 ? -1 : InTimes); }
	void SetListenOrder(int32 InOrder) { Order = InOrder; }

protected:
	FSigSource Source = FSigSource::NullSigSrc;
	FWeakObjectPtr Handler;
	FGMPKey GMPKey = {};
	int32 Times = -1;
	int32 Order = 0;
};

#define SLOT_STORAGE_INLINE_SIZE GMP_FUNCTION_PREDEFINED_ALIGN_SIZE
#ifndef SLOT_STORAGE_INLINE_SIZE
#define SLOT_STORAGE_INLINE_SIZE GMP_FUNCTION_PREDEFINED_INLINE_SIZE
#endif  // SLOT_STORAGE_INLINE_SIZE
#define GMP_ALWAYS_USE_INLINE_SIGNAL (SLOT_STORAGE_INLINE_SIZE < GMP_FUNCTION_PREDEFINED_INLINE_SIZE)

class FSigElm final : public TAttachedCallableStore<FSigElmData, SLOT_STORAGE_INLINE_SIZE>
{
#if GMP_ALWAYS_USE_INLINE_SIGNAL
public:
	void* operator new(size_t Size, uint32 AdditionalSize)
	{
		auto AllocSize = FMath::Max(sizeof(FSigElm), offsetofINLINE() + FMath::Max((uint32)FStorageEraseBase::kAlignSize, AdditionalSize));
		return FMemory::Malloc(AllocSize, alignof(FSigElm));
	}
	void operator delete(void* Ptr) { return FMemory::Free(Ptr); }
#endif

private:
	static FSigElm* Alloc(FGMPKey InKey, uint32 AdditionalSize = 0)
	{
#if GMP_ALWAYS_USE_INLINE_SIGNAL
		return new (AdditionalSize) FSigElm(InKey);
#else
		return new FSigElm(InKey);
#endif
	}
	using Super = TAttachedCallableStore<FSigElmData, SLOT_STORAGE_INLINE_SIZE>;

	template<typename Functor, uint32 INLINE_SIZE = sizeof(TTypedObject<std::decay_t<Functor>>)>
	static FSigElm* Construct(FGMPKey InKey, Functor&& InFunc, int32 InTimes = -1)
	{
		FSigElm* Impl = Alloc(InKey, INLINE_SIZE);
		Impl->SetLeftTimes(InTimes);
		Impl->BindOrMove(std::forward<Functor>(InFunc));
		return Impl;
	}

	template<typename Functor, uint32 INLINE_SIZE = sizeof(TTypedObject<std::decay_t<Functor>>), std::enable_if_t<!Internal::TIsGMPCallable<std::decay_t<Functor>>::value, int> = 0>
	auto BindOrMove(Functor&& InFunc)
	{
		return Super::Bind(std::forward<Functor>(InFunc));
	}

	template<typename Functor, uint32 INLINE_SIZE = sizeof(TTypedObject<std::decay_t<Functor>>), std::enable_if_t<Internal::TIsGMPCallable<std::decay_t<Functor>>::value, int> = 0>
	auto BindOrMove(Functor&& InFunc)
	{
		static_assert(std::is_rvalue_reference<decltype(InFunc)>::value, "err");
		constexpr uint32 ACTUAL_INLINE_SIZE = SLOT_STORAGE_INLINE_SIZE > INLINE_SIZE ? SLOT_STORAGE_INLINE_SIZE : INLINE_SIZE;
		return Super::Move(std::move(InFunc), ACTUAL_INLINE_SIZE);
	}

	FSigElm(FGMPKey InKey = 0) { GMPKey = InKey; }
	FSigElm(const FSigElm&) = delete;
	FSigElm& operator=(const FSigElm&) = delete;

	friend class FSignalStore;
	template<bool, typename...>
	friend class TSignal;
};

using FMsgKeyArray = TArray<FGMPKey, TInlineAllocator<8>>;

template<typename T>
constexpr bool TIsSupported = !!(std::is_base_of<UObject, T>::value || std::is_base_of<FSigCollection, T>::value);

#ifndef GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
#define GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE 0
#endif

class GMP_API FSignalStore : public TSharedFromThis<FSignalStore, FSignalBase::SPMode>
{
public:
	FSignalStore(FSignalStore&&) = delete;
	FSignalStore& operator=(FSignalStore&&) = delete;
	FSignalStore(const FSignalStore&) = delete;
	FSignalStore& operator=(const FSignalStore&) = delete;
	FSignalStore();
	~FSignalStore();

	FSigElm* FindSigElm(FGMPKey Key) const;

	template<typename ArrayT = TArray<FGMPKey>>
	ArrayT GetKeysBySrc(FSigSource InSigSrc, bool bIncludeNoSrc = true) const;

	TArray<FGMPKey> GetKeysByHandler(const UObject* InHandler) const;
	bool IsAlive(const UObject* InHandler, FSigSource InSigSrc) const;
	bool IsAlive(FGMPKey Key) const;

	template<bool bAllowDuplicate = false>
	FSigElm* AddSigElm(FGMPKey Key, const UObject* InHandler, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor)
	{
		ensure(!!Key);
		GMP_IF_CONSTEXPR(!bAllowDuplicate)
		{
			if (InHandler && IsAlive(InHandler, InSigSrc))
			{
				GMP_NOTE(!!ShouldEnsureOnRepeatedListening(), TEXT("oops! listen twice on %s"), *GetNameSafe(InHandler));
				return nullptr;
			}
		}
		return AddSigElmImpl(Key, InHandler, InSigSrc, Ctor);
	}

	bool IsFiring() const { return ScopeCnt != 0; }

private:
	std::atomic<int32> ScopeCnt{0};
	mutable TMap<FGMPKey, TUniquePtr<FSigElm>> SigElmMap;
	TMap<FGMPKey, TUniquePtr<FSigElm>>& GetStorageMap() const { return SigElmMap; }
	using FSigElmKeySet = TSet<FGMPKey, DefaultKeyFuncs<FGMPKey>, TInlineSetAllocator<1>>;
	TMap<FSigSource, FSigElmKeySet> SourceObjs;
	mutable TMap<FWeakObjectPtr, FSigElmKeySet> HandlerObjs;

	FSigElm* AddSigElmImpl(FGMPKey Key, const UObject* InHandler, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor);

	void RemoveSigElmStorage(FGMPKey InSigKey);
	friend struct FSignalUtils;
	friend class FSignalImpl;
};
extern template auto FSignalStore::GetKeysBySrc<>(FSigSource InSigSrc, bool bIncludeNoSrc) const;

class GMP_API FSignalImpl : public FSignalBase
{
public:
	static TSharedRef<FSignalStore, FSignalBase::SPMode> MakeSignals();
	bool IsEmpty() const;

	template<typename... Ts>
	auto IsAlive(const Ts... ts) const
	{
		GMP_CHECK_SLOW(IsInGameThread());
		return Impl()->IsAlive(ts...);
	}

	FORCEINLINE void DisconnectAll() { Disconnect(); }

protected:
	void Disconnect();
	void Disconnect(FGMPKey Key);
	template<bool bAllowDuplicate>
	void Disconnect(const UObject* Listener);

#if GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
	template<typename R, typename... Ts, typename P>
	void Disconnect(const TDelegate<R(Ts...), P>& Delegate)
	{
		GMP_CHECK_SLOW(IsInGameThread());
		Disconnect(GetDelegateHandleID(Delegate.GetHandle()));
	}

	void Disconnect(const FDelegateHandle& Handle);
#endif

	template<bool bAllowDuplicate>
	void DisconnectExactly(const UObject* Listener, FSigSource InSigSrc);

	FSignalImpl() { Store = MakeSignals(); }
	friend class FMessageHub;
	void BindSignalConnection(const FSigCollection& Collection, FGMPKey Key) const;

#if GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
	FORCEINLINE auto GetNextSequence()
	{
		// same to delegate handle id
		return GetDelegateHandleID(FDelegateHandle(FDelegateHandle::GenerateNewHandle));
	}
	template<typename H>
	static FGMPKey GetDelegateHandleID(const H& handle)
	{
		static_assert(sizeof(handle) == sizeof(FGMPKey), "err");
		return *reinterpret_cast<const FGMPKey*>(&handle);
	}
#if UE_5_03_OR_LATER
	template<typename T>
	FGMPKey GetGMPKey(const TDelegateBase<T>& f, FGMPListenOptions Options)
	{
		return GetDelegateHandleID(f.GetHandle());
	}
	template<typename F>
	FGMPKey GetGMPKey(const F& f, FGMPListenOptions Options)
	{
		return GetNextSequence();
	}
#else
	template<typename F>
	FGMPKey GetGMPKey(const F& f, FGMPListenOptions Options, std::enable_if_t<std::is_base_of<FDelegateBase, F>::value>* = nullptr)
	{
		return GetDelegateHandleID(f.GetHandle());
	}
	template<typename F>
	FGMPKey GetGMPKey(const F& f, FGMPListenOptions Options, std::enable_if_t<!std::is_base_of<FDelegateBase, F>::value>* = nullptr)
	{
		return GetNextSequence();
	}
#endif
#else
	template<typename F>
	FGMPKey GetGMPKey(const F& f, FGMPListenOptions Options)
	{
		return FGMPKey::NextGMPKey(Options);
	}
#endif

	FSignalStore* Impl() const
	{
		static_assert(sizeof(*this) == sizeof(FSignalBase), "err");
		return Store.Get();
	}

	using FOnFireResultArray = TArray<FGMPKey, TInlineAllocator<16>>;
#if GMP_DEBUG_SIGNAL
	using FOnFireResults = FOnFireResultArray;
#else
	using FOnFireResults = void;
#endif

	template<bool bAllowDuplicate>
	void OnFire(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
	template<bool bAllowDuplicate>
	FOnFireResults OnFireWithSigSource(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
};
extern template GMP_API void FSignalImpl::Disconnect<true>(const UObject* Listener);
extern template GMP_API void FSignalImpl::Disconnect<false>(const UObject* Listener);
extern template GMP_API void FSignalImpl::DisconnectExactly<true>(const UObject* Listener, FSigSource InSigSrc);
extern template GMP_API void FSignalImpl::DisconnectExactly<false>(const UObject* Listener, FSigSource InSigSrc);
extern template GMP_API void FSignalImpl::OnFire<true>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
extern template GMP_API void FSignalImpl::OnFire<false>(const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
extern template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;
extern template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker) const;

template<bool bAllowDuplicate, typename... TArgs>
class TSignal final : public FSignalImpl
{
public:
	TSignal() = default;
	TSignal(TSignal&&) = default;
	TSignal& operator=(TSignal&&) = default;
	TSignal(const TSignal&) = delete;
	TSignal& operator=(const TSignal&) = delete;

	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) == sizeof...(TArgs), FSigElm *> Connect(T * const Obj, R(T:: * const MemFunc)(FuncArgs...), FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[=](ForwardParam<TArgs>... Args) { (Obj->*MemFunc)(static_cast<TArgs>(Args)...); },
			InSigSrc, Options);
	}

	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) != sizeof...(TArgs), FSigElm*> Connect(T* const Obj, R (T::*const MemFunc)(FuncArgs...), FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(sizeof...(FuncArgs) < sizeof...(TArgs), "overflow");
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[=](ForwardParam<TArgs>... Args) { Details::Invoker<FuncArgs...>::Apply(MemFunc, Obj, ForwardParam<TArgs>(Args)...); },
			InSigSrc, Options);
	}

	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) == sizeof...(TArgs), FSigElm*> Connect(const T* const Obj, R (T::*const MemFunc)(FuncArgs...) const, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[=](ForwardParam<TArgs>... Args) { (Obj->*MemFunc)(static_cast<TArgs>(Args)...); },
			InSigSrc, Options);
	}

	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) != sizeof...(TArgs), FSigElm*> Connect(const T* const Obj, R (T::*const MemFunc)(FuncArgs...) const, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(sizeof...(FuncArgs) < sizeof...(TArgs), "overflow");
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[=](ForwardParam<TArgs>... Args) { Details::Invoker<FuncArgs...>::Apply(MemFunc, Obj, ForwardParam<TArgs>(Args)...); },
			InSigSrc, Options);
	}

	template<typename T, typename F>
	FSigElm* Connect(T* const Obj, F&& Callable, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectFunctor(Obj, std::forward<F>(Callable), &std::decay_t<F>::operator(), InSigSrc, Options);
	}

#if GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
	template<typename T, typename R, typename P>
	auto Connect(T* const Obj, TDelegate<R(TArgs...), P>&& Delegate, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[Delegate{std::forward<decltype(Delegate)>(Delegate)}](ForwardParam<TArgs>... Args) { Delegate.ExecuteIfBound(ForwardParam<TArgs>(Args)...); },
			InSigSrc, Options);
	}
#endif

	void Fire(TArgs... Args) const
	{
		OnFire<bAllowDuplicate>([&](FSigElm* Elem) { InvokeSlot(Elem, ForwardParam<TArgs>(Args)...); });
	}

	auto FireWithSigSource(FSigSource InSigSrc, TArgs... Args) const
	{
		return OnFireWithSigSource<bAllowDuplicate>(InSigSrc, [&](FSigElm* Elem) { InvokeSlot(Elem, ForwardParam<TArgs>(Args)...); });
	}

	using FSignalImpl::Disconnect;
	FORCEINLINE void Disconnect(const UObject* Listener, FSigSource InSigSrc) { FSignalImpl::DisconnectExactly<bAllowDuplicate>(Listener, InSigSrc); }
	FORCEINLINE void Disconnect(const UObject* Listener) { FSignalImpl::Disconnect<bAllowDuplicate>(Listener); }

private:
	static void InvokeSlot(FSigElm* Item, TArgs... Args)
	{
		Item->CheckCallable();
		reinterpret_cast<void (*)(void*, TArgs...)>(Item->GetCallable())(Item->GetObjectAddress(), Args...);
	}

	template<typename T, typename R, typename F, typename C, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) == sizeof...(TArgs), FSigElm*> ConnectFunctor(const T* Obj, F&& Callable, R (C::*const)(FuncArgs...) const, FSigSource InSigSrc, FGMPListenOptions Options)
	{
		return ConnectImpl(HasCollectionBase<T>{}, Obj, std::forward<F>(Callable), InSigSrc, Options, GetGMPKey(Callable, Options));
	}

	template<typename T, typename R, typename F, typename C, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) != sizeof...(TArgs), FSigElm*> ConnectFunctor(const T* Obj, F&& Callable, R (C::*const)(FuncArgs...) const, FSigSource InSigSrc, FGMPListenOptions Options)
	{
		static_assert(sizeof...(FuncArgs) < sizeof...(TArgs), "overflow");
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[Callable{std::forward<F>(Callable)}](ForwardParam<TArgs>... Args) { Details::Invoker<FuncArgs...>::Apply(Callable, ForwardParam<TArgs>(Args)...); },
			InSigSrc, Options,
			GetGMPKey(Callable, Options));
	}

	template<typename T, typename Lambda>
	auto ConnectImpl(std::true_type, T* const Obj, Lambda&& Callable, FSigSource InSigSrc, FGMPListenOptions Options, FGMPKey Seq = {})
	{
		static_assert(std::is_base_of<FSigCollection, T>::value, "must HasCollectionBase!");
		auto Item = ConnectImpl(std::false_type{}, Obj, std::forward<Lambda>(Callable), InSigSrc, Options, Seq);
		if (Item)
			BindSignalConnection(*Obj, Item->GetGMPKey());
		return Item;
	}

	template<typename T, typename Lambda>
	auto ConnectImpl(std::false_type, T* const Obj, Lambda&& Callable, FSigSource InSigSrc, FGMPListenOptions Options, FGMPKey Seq = {})
	{
		auto Key = Seq ? Seq : GetGMPKey(Callable, Options);
		auto Item = Store->AddSigElm<bAllowDuplicate>(Key, ToUObject(Obj), InSigSrc, [&] { return FSigElm::Construct(Key, std::forward<Lambda>(Callable), Options.Times); });
		return Item;
	}
};
}  // namespace GMP
