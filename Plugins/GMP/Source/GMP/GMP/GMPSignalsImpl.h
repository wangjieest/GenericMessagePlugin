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

#define GMP_WITH_INLINE_FIRE_ENABLED (GMP_WITH_INLINE_FIRE && GMP_WITH_STATIC_STORE)

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

#if !defined(__clang__) && defined(_MSC_VER) && _MSC_VER < 1925
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
	FORCEINLINE_DEBUGGABLE bool TestInvokable(const F& Func)
	{
		return IsInvokable() && (Func(), (Times != 0 && (Times < 0 || --Times > 0)));
	}
	FORCEINLINE_DEBUGGABLE bool IsInvokable() const
	{
		return !Handler.IsStale(true) && (Times != 0);
	}
	bool TestTimes() { return (Times != 0 && (Times < 0 || --Times > 0)); }
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

	struct FKeyFuncs : BaseKeyFuncs<TUniquePtr<FSigElm>, FGMPKey, false>
	{
		static FGMPKey GetSetKey(const TUniquePtr<FSigElm>& Element) { return Element->GetGMPKey(); }
		static bool Matches(const FGMPKey& A, const FGMPKey& B) { return A == B; }
		static uint32 GetKeyHash(const FGMPKey& Key) { return GetTypeHash(Key); }
	};

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

#if GMP_SIGNAL_BACKEND_FLEX
	template<typename Functor, typename ThunkGen, uint32 INLINE_SIZE = sizeof(TTypedObject<std::decay_t<Functor>>)>
	static FSigElm* ConstructFlex(FGMPKey InKey, Functor&& InFunc, ThunkGen&& InThunkGen, int32 InTimes = -1)
	{
		FSigElm* Impl = Alloc(InKey, INLINE_SIZE);
		Impl->SetLeftTimes(InTimes);
		Impl->BindCallableAs(std::forward<Functor>(InFunc), std::forward<ThunkGen>(InThunkGen));
		return Impl;
	}
	template<bool, typename...>
	friend class TFlexMsgSignal;
#endif
};

using FMsgKeyArray = TArray<FGMPKey, TInlineAllocator<8>>;

#if GMP_WITH_DIRECT_SIGNAL
FORCEINLINE void GMPInvokeRaw(FSigElm* Elem, const void* a0, const void* a1)
{
	Elem->CheckCallable();
	reinterpret_cast<void (*)(void*, const void*, const void*)>(Elem->GetCallable())(Elem->GetObjectAddress(), a0, a1);
}
#endif

#if GMP_WITH_INLINE_FIRE_ENABLED
GMP_API void GMPEraseKeysAfterFire(FSignalStore* RawStore, const FGMPKey* Keys, int32 Num, bool bAllowDuplicate);
#endif

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

	FName MessageKey;
	FSigElm* FindSigElm(FGMPKey Key) const;

	template<typename ArrayT = TArray<FGMPKey>>
	ArrayT GetKeysBySrc(FSigSource InSigSrc, bool bIncludeNoSrc = true) const;

	TArray<FGMPKey> GetKeysByHandler(const UObject* InHandler) const;
	bool IsAlive(const UObject* InHandler, FSigSource InSigSrc) const;
	bool IsAlive(FGMPKey Key) const;
	bool IsAlive() const;

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

	void Cleanup();

#if GMP_WITH_INLINE_FIRE_ENABLED
	FORCEINLINE
#if WITH_EDITOR
	TArray<FGMPKey, TInlineAllocator<16>>
#else
	void
#endif
	ForEachMatchedRaw(FSigSource InSigSrc, const void* a0, const void* a1)
	{
		GMP_CHECK(IsInGameThread());
		TScopeCounter<decltype(ScopeCnt)> ScopeCounter(ScopeCnt);

		const FSigSource SrcWorld = InSigSrc.GetSigSourceWorld();

		TArray<FSigElm*, TInlineAllocator<16>> Matched;
		for (auto& Up : SigElmArray)
		{
			FSigElm* Elem = Up.Get();
			if (!Elem)
				continue;
			const FSigSource ElmSrc = Elem->GetSource();
			const bool bMatch = (ElmSrc == InSigSrc)
				|| (SrcWorld.IsValid() && ElmSrc == SrcWorld)
				|| (ElmSrc == FSigSource::AnySigSrc);
			if (bMatch)
				Matched.Add(Elem);
		}
		if (Matched.Num() > 1)
			Matched.Sort([](const FSigElm& A, const FSigElm& B) { return A.GetGMPKey() < B.GetGMPKey(); });

		FMsgKeyArray EraseIDs;
#if WITH_EDITOR
		TArray<FGMPKey, TInlineAllocator<16>> CallbackIDs;
#endif
		for (FSigElm* Elem : Matched)
		{
#if WITH_EDITOR
			CallbackIDs.Add(Elem->GetGMPKey());
#endif
#if GMP_DEBUG_SIGNAL
			auto Listener = Elem->GetHandler();
			if (!Listener.IsStale())
			{
				auto SigObj = InSigSrc.TryGetUObject();
				if (Listener.Get() && SigObj && Listener.Get()->GetWorld() != SigObj->GetWorld())
					continue;
			}
#endif
			bool bShouldErase = !Elem->IsInvokable();
			if (!bShouldErase)
			{
				GMPInvokeRaw(Elem, a0, a1);
				bShouldErase = !Elem->TestTimes();
			}
			if (bShouldErase)
				EraseIDs.Add(Elem->GetGMPKey());
		}

		if (EraseIDs.Num())
			GMPEraseKeysAfterFire(this, EraseIDs.GetData(), EraseIDs.Num(), false);
#if WITH_EDITOR
		return CallbackIDs;
#endif
	}
#endif  // GMP_WITH_INLINE_FIRE_ENABLED

private:
	mutable TArray<TUniquePtr<FSigElm>, TInlineAllocator<1>> SigElmArray;

	using FSigElmKeySet = TSet<FGMPKey, DefaultKeyFuncs<FGMPKey>, TInlineSetAllocator<1>>;
	std::atomic<int32> ScopeCnt{0};

	FSigElm* AddSigElmImpl(FGMPKey Key, const UObject* InHandler, FSigSource InSigSrc, const TGMPFunctionRef<FSigElm*()>& Ctor);

	void Reset();
	void RemoveSigElmStorage(FGMPKey InSigKey);
	friend struct FSignalUtils;
	friend class FSignalImpl;

public:
#if GMP_WITH_DIRECT_SIGNAL && !GMP_WITH_STATIC_STORE
	struct FStaticSignalSlot* OwnerSlot = nullptr;
#endif
};

#if !GMP_WITH_STATIC_STORE
struct FStaticSignalSlot
{
	const ANSICHAR* KeyStr;       // compile-time literal
	FName Key;                    // computed from KeyStr at bind time
	FSignalStore* Ptr;            // hot-path raw pointer; written back by GMP at store create/rebuild/destroy
	constexpr explicit FStaticSignalSlot(const ANSICHAR* In) noexcept
		: KeyStr(In)
		, Key()
		, Ptr(nullptr)
	{
	}
	FORCEINLINE operator FName() const { return Key; }

	GMP_API FSignalStore* ResolvePtr() const;
	FORCEINLINE FSignalStore* GetStore() const { return ResolvePtr(); }
};
#endif  // !GMP_WITH_STATIC_STORE

#if GMP_WITH_STATIC_STORE
struct FStaticStoreEntry
{
	FSignalStore* Store;
	const ANSICHAR* KeyStr;
};
GMP_API TArray<FStaticStoreEntry>& GMPGetStaticStoreRegistry();
struct FStaticStoreDeleter
{
	void operator()(FSignalStore* P) const {}
};
GMP_API TSharedRef<FSignalStore, FSignalBase::SPMode> GMPBindStaticStore(FSignalStore* InStore, FName Key);
GMP_API void GMPEnsureStaticStoreRegistered(FSignalStore* InStore);
#endif  // GMP_WITH_STATIC_STORE

extern template auto FSignalStore::GetKeysBySrc<>(FSigSource InSigSrc, bool bIncludeNoSrc) const;

class GMP_API FSignalImpl : public FSignalBase
{
	friend struct FSignalUtils;

public:
	static TSharedRef<FSignalStore, FSignalBase::SPMode> MakeSignals(FName MessageKey);

	template<typename... Ts>
	auto IsAlive(const Ts... ts) const
	{
		GMP_CHECK_SLOW(IsInGameThread());
		return Impl()->IsAlive(ts...);
	}

	FORCEINLINE void DisconnectAll() { Disconnect(); }
	TSharedPtr<void> BindSignalConnection(FSigElm* SigElm) const { return SigElm ? BindSignalConnection(SigElm->GetGMPKey()) : TSharedPtr<void>{}; }

#if GMP_ENABLE_STATIC_DISCONNECT
	// Disconnect a listener by its FGMPKey handle alone, without holding the owning signal/store. Resolved through the
	// global connection pool (key -> weak store). Only available when GMP_ENABLE_STATIC_DISCONNECT is enabled.
	static void StaticDisconnect(FGMPKey Key);
#endif

protected:
	void Disconnect(FGMPKey Key);
	void Disconnect();
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

	FSignalImpl(FName In = NAME_None) { Store = MakeSignals(In); }
	friend class FMessageHub;
	void BindSignalConnection(const FSigCollection& Collection, FGMPKey Key) const;
	TSharedPtr<void> BindSignalConnection(FGMPKey Key) const;

#if GMP_SIGNAL_COMPATIBLE_WITH_BASEDELEGATE
	FORCEINLINE auto GetNextSequence()
	{
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
#if WITH_EDITOR
	using FOnFireResults = FOnFireResultArray;
#else
	using FOnFireResults = void;
#endif

	template<bool bAllowDuplicate>
	void OnFire(const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig = 0) const;
	template<bool bAllowDuplicate>
	FOnFireResults OnFireWithSigSource(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig = 0) const;
};
extern template GMP_API void FSignalImpl::Disconnect<true>(const UObject* Listener);
extern template GMP_API void FSignalImpl::Disconnect<false>(const UObject* Listener);
extern template GMP_API void FSignalImpl::DisconnectExactly<true>(const UObject* Listener, FSigSource InSigSrc);
extern template GMP_API void FSignalImpl::DisconnectExactly<false>(const UObject* Listener, FSigSource InSigSrc);
extern template GMP_API void FSignalImpl::OnFire<true>(const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;
extern template GMP_API void FSignalImpl::OnFire<false>(const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;
extern template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<true>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;
extern template GMP_API FSignalImpl::FOnFireResults FSignalImpl::OnFireWithSigSource<false>(FSigSource InSigSrc, const TGMPFunctionRef<void(FSigElm*)>& Invoker, uint32 ExpectSig) const;

#if GMP_WITH_DIRECT_SIGNAL
#if WITH_EDITOR
GMP_API TArray<FGMPKey, TInlineAllocator<16>> GMPFireWithSigSourceDirectRaw(FSignalStore* RawStore, FSigSource InSigSrc, const void* a0, const void* a1);
#else
GMP_API void GMPFireWithSigSourceDirectRaw(FSignalStore* RawStore, FSigSource InSigSrc, const void* a0, const void* a1);
#endif
#endif

template<bool bAllowDuplicate, typename... TArgs>
class TSignal final : public FSignalImpl
{
public:
	TSignal() = default;
	TSignal(FName In)
		: FSignalImpl(In)
	{
	}
	TSignal(TSignal&&) = default;
	TSignal& operator=(TSignal&&) = default;
	TSignal(const TSignal&) = delete;
	TSignal& operator=(const TSignal&) = delete;

	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) == sizeof...(TArgs), FSigElm*> Connect(T* const Obj, R (T::*const MemFunc)(FuncArgs...), FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[=](ForwardParam<TArgs>... Args) { (Obj->*MemFunc)(static_cast<TArgs>(Args)...); },
			InSigSrc,
			Options);
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
			InSigSrc,
			Options);
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
			InSigSrc,
			Options);
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
			InSigSrc,
			Options);
	}

	template<typename T, typename F>
	FSigElm* Connect(T* const Obj, F&& Callable, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectFunctor(Obj, std::forward<F>(Callable), &std::decay_t<F>::operator(), InSigSrc, Options);
	}

	template<typename T, typename R, typename ...PS, typename P>
	auto Connect(T* const Obj, TDelegate<R(PS...), P>&& Delegate, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		return ConnectImpl(
			HasCollectionBase<T>{},
			Obj,
			[Delegate{std::forward<decltype(Delegate)>(Delegate)}](PS... Parms) { Delegate.ExecuteIfBound(ForwardParam<PS>(Parms)...); },
			InSigSrc,
			Options);
	}

	FORCEINLINE void Fire(TArgs... Args) const
	{
		OnFire<bAllowDuplicate>([&](FSigElm* Elem) { InvokeSlot(Elem, ForwardParam<TArgs>(Args)...); });
	}

	FORCEINLINE auto FireWithSigSource(FSigSource InSigSrc, TArgs... Args) const
	{
		return OnFireWithSigSource<bAllowDuplicate>(InSigSrc, [&](FSigElm* Elem) { InvokeSlot(Elem, ForwardParam<TArgs>(Args)...); });
	}

	using FSignalImpl::Disconnect;
	FORCEINLINE void Disconnect(const UObject* Listener, FSigSource InSigSrc) { FSignalImpl::DisconnectExactly<bAllowDuplicate>(Listener, InSigSrc); }
	FORCEINLINE void Disconnect(const UObject* Listener) { FSignalImpl::Disconnect<bAllowDuplicate>(Listener); }

private:
	friend class FMessageHub;
	FORCEINLINE_DEBUGGABLE static void InvokeSlot(FSigElm* Item, TArgs... Args)
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
			InSigSrc,
			Options,
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
