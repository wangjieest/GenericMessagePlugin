//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "GMPMacros.h"

#include <tuple>
#include <type_traits>
#include <utility>

static const FName NAME_GMPSkipValidate{TEXT("SkipValidate")};

namespace GMP
{
namespace TypeTraits
{
#ifdef __clang__
	template<class _Ty1, class _Ty2>
	GMP_INLINE constexpr bool IsSameV = __is_same(_Ty1, _Ty2);
#else
	template<class, class>
	GMP_INLINE constexpr bool IsSameV = false;
	template<class _Ty>
	GMP_INLINE constexpr bool IsSameV<_Ty, _Ty> = true;
#endif

	template<class T, std::size_t = sizeof(T)>
	std::true_type IsCompleteImpl(T*);
	std::false_type IsCompleteImpl(...);

	template<class T>
	using IsComplete = decltype(IsCompleteImpl(std::declval<T*>()));

	// is_detected
	template<typename, template<typename...> class Op, typename... T>
	struct IsDetectedImpl : std::false_type
	{
	};
	template<template<typename...> class Op, typename... T>
	struct IsDetectedImpl<VoidType<Op<T...>>, Op, T...> : std::true_type
	{
	};
	template<template<typename...> class Op, typename... T>
	using IsDetected = IsDetectedImpl<void, Op, T...>;

	template<typename... Ts>
	struct TDisjunction;
	template<typename V, typename... Is>
	struct TDisjunction<V, Is...>
	{
		enum
		{
			Tag = V::value ? V::value : TDisjunction<Is...>::Tag,
		};
		static constexpr bool value = Tag != 0;
	};
	template<>
	struct TDisjunction<>
	{
		enum
		{
			Tag = 0,
		};
		static constexpr bool value = false;
	};

	struct UnUseType
	{
	};

	template<typename Derived, template<typename...> class BaseTmpl>
	struct IsDerivedFromTemplate
	{
	private:
		template<typename... TArgs>
		static constexpr std::true_type Detect(const BaseTmpl<TArgs...>&);
		static constexpr std::false_type Detect(...);

	public:
		static constexpr bool value = std::is_same<decltype(Detect(std::declval<Derived>())), std::true_type>::value;

		template<typename... TArgs>
		static auto BaseType(const BaseTmpl<TArgs...>&) -> BaseTmpl<TArgs...>;
	};

	template<typename T>
	struct TIsCallable
	{
	private:
		template<typename V>
		using IsCallableType = decltype(&V::operator());
		template<typename V>
		using IsCallable = IsDetected<IsCallableType, V>;

	public:
		static const bool value = IsCallable<std::decay_t<T>>::value;
	};

	template<typename T>
	struct TIsUnrealDelegate
	{
	private:
		template<typename V>
		using HasExecuteIfBoundType = decltype(&V::ExecuteIfBound);
		template<typename V>
		using HasExecuteIfBound = IsDetected<HasExecuteIfBoundType, V>;

	public:
#if !UE_4_26_OR_LATER
		static const bool value = IsDerivedFromTemplate<T, TBaseDelegate>::value || HasExecuteIfBound<T>::value;
#else
		static const bool value = IsDerivedFromTemplate<T, TDelegate>::value || HasExecuteIfBound<T>::value;
#endif
	};

	template<typename... TArgs>
	struct TGetFirst;
	template<>
	struct TGetFirst<>
	{
		using type = UnUseType;
	};
	template<typename T, typename... TArgs>
	struct TGetFirst<T, TArgs...>
	{
		using type = T;
	};

	template<typename... TArgs>
	using TGetFirstType = typename TGetFirst<TArgs...>::type;

	template<typename T, typename... TArgs>
	struct TIsFirstSame
	{
		using FirstType = TGetFirstType<TArgs...>;
		enum
		{
			value = TIsSame<std::decay_t<FirstType>, T>::Value
		};
	};
	template<typename T>
	struct TIsFirstSame<T>
	{
		enum
		{
			value = 0
		};
	};

	template<typename... TArgs>
	struct TIsFirstTypeCallable;
	template<>
	struct TIsFirstTypeCallable<>
	{
		using type = std::false_type;
		enum
		{
			value = type::value
		};
	};
	template<typename T, typename... TArgs>
	struct TIsFirstTypeCallable<T, TArgs...>
	{
		using type = std::conditional_t<TIsCallable<T>::value, std::true_type, std::false_type>;
		enum
		{
			value = type::value
		};
	};

	template<typename T>
	struct TIsEnumByte
	{
		enum
		{
			value = std::is_enum<T>::value
		};
		using type = T;
	};

	template<typename T>
	struct TIsEnumByte<TEnumAsByte<T>>
	{
		enum
		{
			value = true
		};
		using type = T;
	};

	template<class Tuple, uint32 N>
	struct TRemoveTupleLast;

	template<typename... TArgs, uint32 N>
	struct TRemoveTupleLast<std::tuple<TArgs...>, N>
	{
	private:
		using Tuple = std::tuple<TArgs...>;
		template<std::size_t... n>
		static std::tuple<std::tuple_element_t<n, Tuple>...> extract(std::index_sequence<n...>);
		static_assert(sizeof...(TArgs) >= N, "err");

	public:
		using type = decltype(extract(std::make_index_sequence<sizeof...(TArgs) - N>()));
	};

	template<class Tuple, uint32 N>
	using TRemoveTupleLastType = typename TRemoveTupleLast<Tuple, N>::type;

	template<typename R, typename Tup>
	struct TSigTraitsImpl;
	template<typename R, typename... TArgs>
	struct TSigTraitsImpl<R, std::tuple<TArgs...>>
	{
		using TFuncType = R(TArgs...);
		using ReturnType = R;
		using Tuple = std::tuple<TArgs...>;
		using DecayTuple = std::tuple<std::decay_t<TArgs>...>;

		template<typename T, typename... Args>
		static auto GetLastType(std::tuple<T, Args...>) -> std::tuple_element_t<sizeof...(Args), std::tuple<T, Args...>>;
		static auto GetLastType(std::tuple<>) -> void;
		using LastType = decltype(GetLastType(std::declval<Tuple>()));

		enum
		{
			TupleSize = std::tuple_size<Tuple>::value
		};
	};

	template<typename TFunc, uint32 N = 0>
	struct TInvokableTraitsImpl
	{
		template<typename R, typename... TArgs>
		static auto GetSigType(R (*)(TArgs...)) -> TSigTraitsImpl<R, std::tuple<TArgs...>>;
		template<typename R, typename... TArgs>
		static auto GetSigType(R (&)(TArgs...)) -> TSigTraitsImpl<R, std::tuple<TArgs...>>;
		template<typename R, typename FF, typename... TArgs>
		static auto GetSigType(R (FF::*)(TArgs...)) -> TSigTraitsImpl<R, std::tuple<TArgs...>>;
		template<typename R, typename FF, typename... TArgs>
		static auto GetSigType(R (FF::*)(TArgs...) const) -> TSigTraitsImpl<R, std::tuple<TArgs...>>;
		using TSig = decltype(GetSigType(std::declval<TFunc>()));
		using TFuncType = typename TSig::TFuncType;
	};
#if GMP_DELEGATE_INVOKABLE
#if !UE_4_26_OR_LATER
	template<typename R, typename... TArgs, uint32 N>
	struct TInvokableTraitsImpl<TBaseDelegate<R, TArgs...>, N>
	{
		using DelegateType = TBaseDelegate<R, TArgs...>;
		static auto GetSigType(const DelegateType&) -> TSigTraitsImpl<R, std::tuple<TArgs...>>;
		using TSig = TSigTraitsImpl<R, std::tuple<TArgs...>>;
		using TFuncType = typename TSig::TFuncType;
	};
#else
	template<typename R, typename... TArgs, typename UserPolicy, uint32 N>
	struct TInvokableTraitsImpl<TDelegate<R(TArgs...), UserPolicy>, N>
	{
		using DelegateType = TDelegate<R(TArgs...), UserPolicy>;
		static auto GetSigType(const DelegateType&) -> TSigTraitsImpl<R, std::tuple<TArgs...>>;
		using TSig = TSigTraitsImpl<R, std::tuple<TArgs...>>;
		using TFuncType = typename TSig::TFuncType;
	};
#endif
#endif
	template<typename T, typename = void>
	struct TInvokableTraits;
	template<typename T>
	struct TInvokableTraits<T, VoidType<decltype(&std::decay_t<T>::operator())>> : public TInvokableTraitsImpl<decltype(&std::decay_t<T>::operator())>
	{
		using typename TInvokableTraitsImpl<decltype(&std::decay_t<T>::operator())>::TSig;
	};
	template<typename R, typename... TArgs>
	struct TInvokableTraits<R(TArgs...), void> : public TInvokableTraitsImpl<R(TArgs...)>
	{
		using typename TInvokableTraitsImpl<R(TArgs...)>::TSig;
	};
	template<typename T>
	struct TInvokableTraits<T, std::enable_if_t<std::is_member_function_pointer<T>::value>> : public TInvokableTraitsImpl<T>
	{
		using typename TInvokableTraitsImpl<T>::TSig;
	};
	template<typename T>
	struct TInvokableTraits<T, std::enable_if_t<std::is_pointer<T>::value && std::is_function<std::remove_pointer_t<T>>::value>> : public TInvokableTraitsImpl<T>
	{
		using typename TInvokableTraitsImpl<T>::TSig;
	};

#if GMP_DELEGATE_INVOKABLE
	template<typename T>
	struct TInvokableTraits<T, std::enable_if_t<TIsUnrealDelegate<T>::value>> : public TInvokableTraitsImpl<T>
	{
		using typename TInvokableTraitsImpl<T>::TSig;
	};
#endif

	template<typename T>
	using TSigTraits = typename TInvokableTraits<std::decay_t<T>>::TSig;

	template<typename T>
	using TSigFuncType = typename TSigTraits<T>::TFuncType;

	template<typename... TArgs>
	struct TGetLast;
	template<>
	struct TGetLast<>
	{
		using type = UnUseType;
	};
	template<typename T, typename... TArgs>
	struct TGetLast<T, TArgs...>
	{
		using type = std::tuple_element_t<sizeof...(TArgs), std::tuple<T, TArgs...>>;
	};

	template<typename... TArgs>
	using TGetLastType = typename TGetLast<TArgs...>::type;

	template<typename T, typename... TArgs>
	struct TIsLastSame
	{
		using LastType = TGetLastType<TArgs...>;
		enum
		{
			value = std::is_same<typename std::remove_cv<LastType>::type, T>::value
		};
	};
	template<typename T>
	struct TIsLastSame<T>
	{
		enum
		{
			value = 0
		};
	};

	template<typename... TArgs>
	struct TIsLastTypeCallable;
	template<>
	struct TIsLastTypeCallable<>
	{
		enum
		{
			callable_type = 0,
			delegate_type = 0,
			value = 0
		};
		using type = std::false_type;
	};
	template<typename C, typename... TArgs>
	struct TIsLastTypeCallable<C, TArgs...>
	{
		using LastType = std::tuple_element_t<sizeof...(TArgs), std::tuple<C, TArgs...>>;
		enum
		{
			callable_type = TIsCallable<LastType>::value,
			delegate_type = TIsUnrealDelegate<LastType>::value,
			value = callable_type || delegate_type
		};
		using type = std::conditional_t<value, std::true_type, std::false_type>;
	};

	template<typename Tuple>
	struct TTupleRemoveLast;

	template<>
	struct TTupleRemoveLast<std::tuple<>>
	{
		using type = std::tuple<>;
	};

	template<typename T, typename... TArgs>
	struct TTupleRemoveLast<std::tuple<T, TArgs...>>
	{
		using Tuple = std::tuple<T, TArgs...>;
		template<std::size_t... Is>
		static auto extract(std::index_sequence<Is...>) -> std::tuple<std::tuple_element_t<Is, Tuple>...>;
		using type = decltype(extract(std::make_index_sequence<std::tuple_size<Tuple>::value - 1>()));
	};
	template<typename Tuple>
	using TTupleRemoveLastType = typename TTupleRemoveLast<std::remove_cv_t<std::remove_reference_t<Tuple>>>::type;

	template<typename... TArgs>
	struct TupleTypeRemoveLast;

	template<>
	struct TupleTypeRemoveLast<>
	{
		using Tuple = std::tuple<>;
		using type = void;
	};

	template<typename T, typename... TArgs>
	struct TupleTypeRemoveLast<T, TArgs...>
	{
		using Tuple = std::tuple<T, TArgs...>;
		using type = TTupleRemoveLastType<Tuple>;
	};

	template<typename... TArgs>
	using TupleTypeRemoveLastType = typename TupleTypeRemoveLast<TArgs...>::type;

	template<typename E>
	constexpr std::underlying_type_t<E> ToUnderlying(E e)
	{
		return static_cast<std::underlying_type_t<E>>(e);
	}

	template<typename TValue, typename TAddr>
	union HorribleUnion
	{
		TValue Value;
		TAddr Addr;
	};
	template<typename TValue, typename TAddr>
	FORCEINLINE TValue HorribleFromAddr(TAddr Addr)
	{
		HorribleUnion<TValue, TAddr> u;
		static_assert(sizeof(TValue) >= sizeof(TAddr), "Cannot use horrible_cast<>");
		u.Value = 0;
		u.Addr = Addr;
		return u.Value;
	}
	template<typename TAddr, typename TValue>
	FORCEINLINE TAddr HorribleToAddr(TValue Value)
	{
		HorribleUnion<TValue, TAddr> u;
		static_assert(sizeof(TValue) >= sizeof(TAddr), "Cannot use horrible_cast<>");
		u.Value = Value;
		return u.Addr;
	}
	template<uint32_t N>
	struct const32
	{
		enum : uint32_t
		{
			Value = N
		};
	};

	template<uint64_t UUID>
	struct const64
	{
		enum : uint64_t
		{
			Value = UUID
		};

		template<std::size_t... First, std::size_t... Second>
		static auto Combine(const char* str, std::index_sequence<First...>, std::index_sequence<Second...>)
		{
			// init only once by UUID
			static auto Result = [](const char* p) {
				constexpr auto FirstSize = (sizeof...(First)) + 1;
				const char s[(sizeof...(Second)) + FirstSize] = {p[1 + First]..., '.', p[FirstSize + 2 + Second]...};
				return FName(s);
			}(str);
			return Result;
		}
		template<std::size_t... First, std::size_t... Second>
		static auto CombineRPC(const char* str, std::index_sequence<First...>, std::index_sequence<Second...>)
		{
			// init only once by UUID
			static auto Result = [](const char* p) {
				constexpr auto FirstSize = (sizeof...(First)) + 1;
				const char s[4 + (sizeof...(Second)) + FirstSize] = {'R', 'P', 'C', '.', p[1 + First]..., '.', p[FirstSize + 2 + Second]...};
				return FName(s);
			}(str);
			return Result;
		}
	};

	constexpr std::size_t GetSplitIndex(const char* str, const std::size_t value = 0) { return (str[0] == ':') ? value : GetSplitIndex(&str[1], value + 1); }

	template<uint64_t UUID, std::size_t N, std::size_t Index>
	FORCEINLINE constexpr auto ToMessageRPCId(const char* str)
	{
		return const64<UUID>::CombineRPC(str, std::make_index_sequence<Index - 1>{}, std::make_index_sequence<N - Index - 2>{});
	}
}  // namespace TypeTraits
}  // namespace GMP

#if 1
#define GMP_RPC_FUNC_NAME(t) GMP::TypeTraits::ToMessageRPCId<ITS::hash_64_fnv1a_const(t), UE_ARRAY_COUNT(t), GMP::TypeTraits::GetSplitIndex(t)>(t)
#else
#define GMP_RPC_FUNC_NAME(t) GMP::TypeTraits::const32<ITS::hash_32_fnv1a_const(t)>::Value
#endif
