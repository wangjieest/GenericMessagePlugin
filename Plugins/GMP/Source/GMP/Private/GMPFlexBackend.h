//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
#pragma once

#include "GMPSignalsImpl.h"

#include <type_traits>
#include <utility>

namespace GMP
{
namespace FlexSig
{
struct FGmpFunctionStoragePolicy
{
	static constexpr bool bRequiresTrailing = false;

	using Handle = ::GMP::Internal::FEmptyCallableStore;

	template<typename D>
	static Handle Store(D&& d)
	{
		return Handle(std::forward<D>(d));
	}

	static void* GetSelf(const Handle& h) { return h.GetObjectAddress(); }
};
}  // namespace FlexSig
}  // namespace GMP

#if GMP_SIGNAL_BACKEND_FLEX

namespace GMP
{
namespace FlexSig
{
	template<typename Func, typename... TArgs>
	struct TFlexBackendThunk
	{
		static void FlexThunk(void* Self, TArgs... Args) { (*static_cast<Func*>(Self))(static_cast<TArgs>(Args)...); }
	};

	template<typename... TArgs>
	struct TFlexThunkGen
	{
		template<typename DecayedFunctor>
		auto operator()(DecayedFunctor*) const
		{
			return &TFlexBackendThunk<DecayedFunctor, TArgs...>::FlexThunk;
		}
	};
}  // namespace FlexSig

template<bool bAllowDuplicate, typename... TArgs>
class TFlexMsgSignal final : public FSignalImpl
{
public:
	TFlexMsgSignal() = default;
	TFlexMsgSignal(FName In)
		: FSignalImpl(In)
	{
	}
	TFlexMsgSignal(TFlexMsgSignal&&) = default;
	TFlexMsgSignal& operator=(TFlexMsgSignal&&) = default;
	TFlexMsgSignal(const TFlexMsgSignal&) = delete;
	TFlexMsgSignal& operator=(const TFlexMsgSignal&) = delete;

	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) == sizeof...(TArgs), FSigElm*> Connect(T* const Obj, R (T::*const MemFunc)(FuncArgs...), FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(HasCollectionBase<T>{}, Obj, [=](ForwardParam<TArgs>... Args) { (Obj->*MemFunc)(static_cast<TArgs>(Args)...); }, InSigSrc, Options);
	}
	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) != sizeof...(TArgs), FSigElm*> Connect(T* const Obj, R (T::*const MemFunc)(FuncArgs...), FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(sizeof...(FuncArgs) < sizeof...(TArgs), "overflow");
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(HasCollectionBase<T>{}, Obj, [=](ForwardParam<TArgs>... Args) { Details::Invoker<FuncArgs...>::Apply(MemFunc, Obj, ForwardParam<TArgs>(Args)...); }, InSigSrc, Options);
	}
	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) == sizeof...(TArgs), FSigElm*> Connect(const T* const Obj, R (T::*const MemFunc)(FuncArgs...) const, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(HasCollectionBase<T>{}, Obj, [=](ForwardParam<TArgs>... Args) { (Obj->*MemFunc)(static_cast<TArgs>(Args)...); }, InSigSrc, Options);
	}
	template<typename R, typename T, typename... FuncArgs>
	inline std::enable_if_t<sizeof...(FuncArgs) != sizeof...(TArgs), FSigElm*> Connect(const T* const Obj, R (T::*const MemFunc)(FuncArgs...) const, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(sizeof...(FuncArgs) < sizeof...(TArgs), "overflow");
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectImpl(HasCollectionBase<T>{}, Obj, [=](ForwardParam<TArgs>... Args) { Details::Invoker<FuncArgs...>::Apply(MemFunc, Obj, ForwardParam<TArgs>(Args)...); }, InSigSrc, Options);
	}

	template<typename T, typename F>
	FSigElm* Connect(T* const Obj, F&& Callable, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		GMP_CHECK_SLOW(IsInGameThread() && (!std::is_base_of<FSigCollection, T>::value || Obj));
		return ConnectFunctor(Obj, std::forward<F>(Callable), &std::decay_t<F>::operator(), InSigSrc, Options);
	}

	template<typename T, typename R, typename... PS, typename P>
	auto Connect(T* const Obj, TDelegate<R(PS...), P>&& Delegate, FSigSource InSigSrc = FSigSource::NullSigSrc, FGMPListenOptions Options = {})
	{
		static_assert(TIsSupported<T>, "unsupported Obj type");
		return ConnectImpl(HasCollectionBase<T>{}, Obj, [Delegate{std::forward<decltype(Delegate)>(Delegate)}](PS... Parms) { Delegate.ExecuteIfBound(ForwardParam<PS>(Parms)...); }, InSigSrc, Options);
	}

	FORCEINLINE void Fire(TArgs... Args) const { OnFire<bAllowDuplicate>([&](FSigElm* Elem) { InvokeSlot(Elem, ForwardParam<TArgs>(Args)...); }); }

	FORCEINLINE auto FireWithSigSource(FSigSource InSigSrc, TArgs... Args) const { return OnFireWithSigSource<bAllowDuplicate>(InSigSrc, [&](FSigElm* Elem) { InvokeSlot(Elem, ForwardParam<TArgs>(Args)...); }); }

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
		return ConnectImpl(HasCollectionBase<T>{}, Obj, [Callable{std::forward<F>(Callable)}](ForwardParam<TArgs>... Args) { Details::Invoker<FuncArgs...>::Apply(Callable, ForwardParam<TArgs>(Args)...); }, InSigSrc, Options, GetGMPKey(Callable, Options));
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
		GMP_CHECK(Store.IsValid());
		auto Key = Seq ? Seq : GetGMPKey(Callable, Options);
		auto Item = Store->AddSigElm<bAllowDuplicate>(Key, ToUObject(Obj), InSigSrc, [&] { return FSigElm::ConstructFlex(Key, std::forward<Lambda>(Callable), FlexSig::TFlexThunkGen<TArgs...>{}, Options.Times); });
		return Item;
	}
};
}  // namespace GMP

#endif  // GMP_SIGNAL_BACKEND_FLEX
