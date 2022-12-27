// Copyright UnrealCompatibility, Inc. All Rights Reserved.

#pragma once

#if !defined(UNREAL_COMPATIBILITY_GUARD_H)
#define UNREAL_COMPATIBILITY_GUARD_H

#include "Runtime/Launch/Resources/Version.h"

#ifndef UE_5_01_OR_LATER
#define UE_5_01_OR_LATER (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 1))
#endif

#ifndef UE_5_00_OR_LATER
#define UE_5_00_OR_LATER (ENGINE_MAJOR_VERSION >= 5)
#endif

#if UE_5_01_OR_LATER
#else
class FEditorStyle;
using FAppStyle = FEditorStyle;
#endif

#if UE_5_00_OR_LATER
#define ANY_PACKAGE_COMPATIABLE nullptr
#else
#define ANY_PACKAGE_COMPATIABLE ANY_PACKAGE
#endif

#ifndef UE_4_27_OR_LATER
#define UE_4_27_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 27))
#endif

#ifndef UE_4_26_OR_LATER
#define UE_4_26_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 26))
#endif

#ifndef UE_4_25_OR_LATER
#define UE_4_25_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 25))
#endif

#ifndef UE_4_24_OR_LATER
#define UE_4_24_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 24))
#endif

#ifndef UE_4_23_OR_LATER
#define UE_4_23_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 23))
#endif

#ifndef UE_4_22_OR_LATER
#define UE_4_22_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 22))
#endif

#ifndef UE_4_21_OR_LATER
#define UE_4_21_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 21))
#endif

#ifndef UE_4_20_OR_LATER
#define UE_4_20_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 20))
#endif

#ifndef UE_4_19_OR_LATER
#define UE_4_19_OR_LATER (ENGINE_MAJOR_VERSION > 4 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION >= 19))
#endif

#if UE_5_00_OR_LATER
#include "UObject/ObjectSaveContext.h"
#else
#define MarkAsGarbage() MarkPendingKill()
#define ClearGarbage() ClearPendingKill()
#define UETypes_Private UE4Types_Private
#define IsReloadActive() GIsHotReload
#define UEVer() UE4Ver()
#define UECodeGen_Private UE4CodeGen_Private

FORCEINLINE bool IsValidChecked(const UObject* Test)
{
	check(Test);
	return IsValid(Test);
}
template<typename T>
T* GetValid(T* Test)
{
	static_assert(std::is_base_of<UObject, T>::value, "GetValid can only work with UObject-derived classes");
	return IsValid(Test) ? Test : nullptr;
}
template<typename T>
const T* GetValid(const T* Test)
{
	static_assert(std::is_base_of<UObject, T>::value, "GetValid can only work with UObject-derived classes");
	return IsValid(Test) ? Test : nullptr;
}
template<typename T>
FORCEINLINE T* ToRawPtr(T* Ptr)
{
	return Ptr;
}
#endif

template<typename T>
auto ToArrayView(T& t)
{
	return MakeArrayView(reinterpret_cast<std::remove_const_t<T>*>(&t), 1);
}

#include "Delegates/Delegate.h"
#if !UE_4_26_OR_LATER
#define Z_TYPENAME_USER_POLICY_IMPL
#define Z_TYPENAME_USER_POLICY_DECLARE
#define Z_TYPENAME_USER_POLICY_DEFAULT
#define Z_TYPENAME_USER_POLICY
template<typename FuncType, typename... VarTypes>
class TCommonDelegateInstanceState;

template<typename InRetValType, typename... ParamTypes, typename... VarTypes>
class TCommonDelegateInstanceState<InRetValType(ParamTypes...), VarTypes...> : IBaseDelegateInstance<InRetValType(ParamTypes...)>
{
public:
	using RetValType = typename TUnwrapType<InRetValType>::Type;

	explicit TCommonDelegateInstanceState(VarTypes... Vars)
		: Payload(Vars...)
		, Handle(FDelegateHandle::GenerateNewHandle)
	{
	}

	FDelegateHandle GetHandle() const final { return Handle; }

protected:
	// Payload member variables (if any).
	TTuple<VarTypes...> Payload;

	// The handle of this delegate
	FDelegateHandle Handle;
};

template<typename R, typename... ParamTypes>
using TUnrealDelegate = TBaseDelegate<R, ParamTypes...>;
template<typename R, typename... ParamTypes>
using TUnrealMulticastDelegate = TMulticastDelegate<R(ParamTypes...)>;
#else
template<typename R, typename... ParamTypes>
using TUnrealDelegate = TDelegate<R(ParamTypes...)>;
template<typename R, typename... ParamTypes>
using TUnrealMulticastDelegate = TMulticastDelegate<R(ParamTypes...)>;

#define Z_TYPENAME_USER_POLICY_IMPL , FDefaultDelegateUserPolicy
#define Z_TYPENAME_USER_POLICY_DECLARE , typename UserPolicy
#define Z_TYPENAME_USER_POLICY_DEFAULT , typename UserPolicy = Z_TYPENAME_USER_POLICY_IMPL
#define Z_TYPENAME_USER_POLICY , UserPolicy
template<typename T>
struct TUnwrapType
{
	typedef T Type;
};
#endif

#include "UObject/UnrealType.h"

#include <type_traits>
#include <utility>

// void_t
template<typename... T>
struct MakeVoid
{
	using type = void;
};
template<typename... T>
using VoidType = typename MakeVoid<T...>::type;

// Init Once
template<typename Type>
bool TrueOnFirstCall(const Type&)
{
	static bool bValue = true;
	bool Result = bValue;
	bValue = false;
	return Result;
}

template<typename T, typename A, typename... TArgs>
inline auto& Add_GetRef(TArray<T, A>& Arr, TArgs&&... Args)
{
	const auto Index = Arr.AddUninitialized(1);
	T* Ptr = Arr.GetData() + Index;
	new (Ptr) T(Forward<TArgs>(Args)...);
	return *Ptr;
}

#define GET_UFUNCTION_CHECKED(CLASS, NAME) CLASS::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(CLASS, NAME))

#if !UE_4_19_OR_LATER
#define DEFINE_FUNCTION(func) void func(FFrame& Stack, RESULT_DECL)
#endif

inline auto ToString(const FName& Name)
{
	return Name.ToString();
}
inline const auto& ToString(const FString& Str)
{
	return Str;
}
inline auto ToString(FString&& Str)
{
	return MoveTemp(Str);
}

inline auto ToName(const FString& Name)
{
	return FName(*Name);
}
inline const auto& ToName(const FName& Name)
{
	return Name;
}
inline auto ToName(FName&& Name)
{
	return MoveTemp(Name);
}

namespace ITS
{
template<char... Cs>
struct list
{
	using type = list;
	static const char* Get()
	{
		static const char value[] = {Cs..., '\0'};
		return value;
	}
};
template<typename In, typename Ret>
struct eval;
template<char I, char... In, char... Ret>
struct eval<list<I, In...>, list<Ret...>> : eval<list<In...>, list<Ret..., I>>
{
};
template<char... In, char... Ret>
struct eval<list<0, In...>, list<Ret...>> : list<Ret...>
{
};
#if defined(_MSC_VER)
template<char... In, char... Ret>
struct eval<list<' ', In...>, list<Ret...>> : eval<list<In...>, list<Ret...>>
{
};
#else
template<char... In, char... Ret>
struct eval<list<' ', In...>, list<Ret...>> : list<Ret...>
{
};
#endif
template<char... Ret>
struct eval<list<>, list<Ret...>> : list<Ret...>
{
};
template<typename In, typename Ret = list<>>
using eval_t = typename eval<In, Ret>::type;
}  // namespace ITS

#define Z_ITS_ARRAY_AT(s, i) (i < sizeof(s) ? s[i] : '\0')
#define Z_ITS_ARRAY_1(s, i, m) m(s, i)
#define Z_ITS_ARRAY_4(s, i, m) Z_ITS_ARRAY_1(s, i, m), Z_ITS_ARRAY_1(s, i + 1, m), Z_ITS_ARRAY_1(s, i + 2, m), Z_ITS_ARRAY_1(s, i + 3, m)
#define Z_ITS_ARRAY_16(s, i, m) Z_ITS_ARRAY_4(s, i, m), Z_ITS_ARRAY_4(s, i + 4, m), Z_ITS_ARRAY_4(s, i + 8, m), Z_ITS_ARRAY_4(s, i + 12, m)
#define Z_ITS_ARRAY_64(s, i, m) Z_ITS_ARRAY_16(s, i, m), Z_ITS_ARRAY_16(s, i + 16, m), Z_ITS_ARRAY_16(s, i + 32, m), Z_ITS_ARRAY_16(s, i + 48, m)
#define Z_ITS_ARRAY_128(s, i, m) Z_ITS_ARRAY_64(s, i, m), Z_ITS_ARRAY_64(s, i + 64, m)
#define Z_ITS_ARRAY_256(s, i, m) Z_ITS_ARRAY_64(s, i, m), Z_ITS_ARRAY_64(s, i + 64, m), Z_ITS_ARRAY_64(s, i + 128, m), Z_ITS_ARRAY_64(s, i + 192, m)
// #define Z_ITS_ARRAY_1024(s, i, m) Z_ITS_ARRAY_256(s, i, m), Z_ITS_ARRAY_256(s, i + 256, m), Z_ITS_ARRAY_256(s, i + 512, m), Z_ITS_ARRAY_256(s, i + 768, m)
#define C_STRING_TYPE_IMPL(str, N) ITS::eval_t<ITS::list<Z_ITS_ARRAY_##N(str, 0, Z_ITS_ARRAY_AT)>>

#define ENABLE_FAST_CHAR_SEQ (PLATFORM_COMPILER_CLANG && (__clang_major__ > 3 || (__clang_major__ == 3 && __clang_minor__ >= 5)))
#if ENABLE_FAST_CHAR_SEQ
template<typename T, T... Chars>
FORCEINLINE constexpr ITS::list<Chars...> operator""_fast_char_seq_udl()
{
	return {};
}
#define C_STRING_TYPE(str) decltype(str##_fast_char_seq_udl)
#else
#define C_STRING_TYPE_(str, N) C_STRING_TYPE_IMPL(str, N)
#define C_STRING_TYPE(str) C_STRING_TYPE_(str, 64)
#endif

static_assert(std::is_same<C_STRING_TYPE("str"), ITS::list<'s', 't', 'r'>>::value, "err");

namespace ITS
{
// FNV1a c++11 constexpr compile time hash functions, 32 and 64 bit
// str should be a null terminated string literal, value should be left out
// e.g hash_32_fnv1a_const("example")
// code license: public domain or equivalent
// post: https://notes.underscorediscovery.com/constexpr-fnv1a/
constexpr uint32_t val_32_const = 0x811c9dc5;
constexpr uint64_t prime_32_const = 0x1000193;
constexpr uint64_t val_64_const = 0xcbf29ce484222325;
constexpr unsigned long long prime_64_const = 0x100000001b3;
constexpr uint32_t hash_32_fnv1a_const(const char* const str, const uint32_t value = val_32_const)
{
	return (str[0] == '\0') ? value : hash_32_fnv1a_const(&str[1], static_cast<uint32_t>((value ^ uint32_t(str[0])) * prime_32_const));
}

constexpr uint64_t hash_64_fnv1a_const(const char* const str, const uint64_t value = val_64_const)
{
	return (str[0] == '\0') ? value : hash_64_fnv1a_const(&str[1], static_cast<uint64_t>((value ^ uint64_t(str[0])) * prime_64_const));
}
template<typename E>
using is_scoped_enum = std::integral_constant<bool, std::is_enum<E>::value && !std::is_convertible<E, int>::value>;

/*
GCC:
	const char *ITS::TypeStr() [with Type = {type}]
clang:
	const char *ITS::TypeStr() [Type = {type}]
MSVC:
	const char *__cdecl ITS::TypeStr<{type}>(void)
*/
#if defined(_MSC_VER)
#define Z_ITS_TYPE_NAME_ __FUNCSIG__
template<typename Type>
constexpr uint32 FRONT_SIZE = sizeof("const char *__cdecl ITS::TypeStr<") - 1u + (std::is_enum<Type>::value ? 5u : (std::is_class<Type>::value ? 6u : 0u));
constexpr uint32 BACK_SIZE = sizeof(">(void)");
#else
#define Z_ITS_TYPE_NAME_ __PRETTY_FUNCTION__
constexpr uint32 BACK_SIZE = sizeof("]");
#if defined(__clang__)
template<typename Type>
constexpr uint32 FRONT_SIZE = sizeof("const char *ITS::TypeStr() [Type = ") - 1u;
#else
template<typename Type>
constexpr uint32 FRONT_SIZE = sizeof("const char *ITS::TypeStr() [with Type = ") - 1u;
#endif
#endif

template<typename Type>
const char* TypeStr(void)
{
#define Z_ITS_TYPE_SPLIT_AT(s, i) ((i + BACK_SIZE) < sizeof(s) ? s[i] : '\0')
#define Z_ITS_TYPE(str, N, Offset) eval_t<list<Z_ITS_ARRAY_##N(str, Offset, Z_ITS_TYPE_SPLIT_AT)>>
	static_assert(sizeof(Z_ITS_TYPE_NAME_) <= 256, "its type name too long");
	auto Ret = Z_ITS_TYPE(Z_ITS_TYPE_NAME_, 256, ITS::FRONT_SIZE<Type>)::Get();
	return Ret;
}
template<typename Type>
const TCHAR* TypeWStr(void)
{
	static FUTF8ToTCHAR Conv(TypeStr<Type>());
	return Conv.Get();
}
}  // namespace ITS

#if !UE_4_20_OR_LATER
#define EPropertyFlags uint64
using FGraphPinNameType = FString;
#define ToGraphPinNameType ToString
#define GraphPinNameTypeConstDesc
template<typename T>
typename TEnableIf<TIsArithmetic<T>::Value, FString>::Type LexToString(const T& Value)
{
	return FString::Printf(TFormatSpecifier<T>::GetFormatSpecifier(), Value);
}

template<typename CharType>
typename TEnableIf<TIsCharType<CharType>::Value, FString>::Type LexToString(const CharType* Ptr)
{
	return FString(Ptr);
}

inline FString LexToString(bool Value)
{
	return Value ? TEXT("true") : TEXT("false");
}

FORCEINLINE FString LexToString(FString&& Str)
{
	return MoveTemp(Str);
}

FORCEINLINE FString LexToString(const FString& Str)
{
	return Str;
}
#else
using FGraphPinNameType = FName;
#define ToGraphPinNameType ToName
#define GraphPinNameTypeConstDesc const
#endif

#if !UE_4_21_OR_LATER

/**
 * Implements a weak object delegate binding for C++ functors, e.g. lambdas.
 */
template<typename UserClass, typename FuncType Z_TYPENAME_USER_POLICY_DECLARE, typename FunctorType, typename... VarTypes>
class TWeakBaseFunctorDelegateInstance;

template<typename UserClass, typename WrappedRetValType, typename... ParamTypes Z_TYPENAME_USER_POLICY_DECLARE, typename FunctorType, typename... VarTypes>
class TWeakBaseFunctorDelegateInstance<UserClass, WrappedRetValType(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...> : public TCommonDelegateInstanceState<WrappedRetValType(ParamTypes...) Z_TYPENAME_USER_POLICY, VarTypes...>
{
private:
	static_assert(TAreTypesEqual<FunctorType, typename TRemoveReference<FunctorType>::Type>::Value, "FunctorType cannot be a reference");
	using Super = TCommonDelegateInstanceState<WrappedRetValType(ParamTypes...) Z_TYPENAME_USER_POLICY, VarTypes...>;
	using RetValType = typename Super::RetValType;
	using UnwrappedThisType = TWeakBaseFunctorDelegateInstance<UserClass, RetValType(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...>;

public:
	TWeakBaseFunctorDelegateInstance(UserClass* InContextObject, const FunctorType& InFunctor, VarTypes... Vars)
		: Super(Vars...)
		, ContextObject(InContextObject)
		, Functor(MoveTemp(InFunctor))
	{
	}

	TWeakBaseFunctorDelegateInstance(UserClass* InContextObject, FunctorType&& InFunctor, VarTypes... Vars)
		: Super(Vars...)
		, ContextObject(InContextObject)
		, Functor(MoveTemp(InFunctor))
	{
	}

	// IDelegateInstance interface

#if USE_DELEGATE_TRYGETBOUNDFUNCTIONNAME
	virtual FName TryGetBoundFunctionName() const final { return NAME_None; }
#endif

	virtual UObject* GetUObject() const final { return ContextObject.Get(); }

#if !UE_4_20_OR_LATER
	virtual FName GetFunctionName() const final { return NAME_None; }
	virtual const void* GetRawMethodPtr() const final { nullptr; }
	virtual const void* GetRawUserObject() const final { return GetUObject(); }
	virtual EDelegateInstanceType::Type GetType() const final { return EDelegateInstanceType::Functor; }
#endif

#if UE_4_21_OR_LATER
	virtual const void* GetObjectForTimerManager() const final { return ContextObject.Get(); }
#endif

#if UE_4_23_OR_LATER
	virtual uint64 GetBoundProgramCounterForTimerManager() const final { return 0; }
#endif

	// Deprecated
	virtual bool HasSameObject(const void* InContextObject) const final { return GetUObject() == InContextObject; }

	virtual bool IsCompactable() const final { return !ContextObject.Get(true); }

	virtual bool IsSafeToExecute() const final { return ContextObject.IsValid(); }

public:
	// IBaseDelegateInstance interface
	virtual void CreateCopy(FDelegateBase& Base) final { new (Base) UnwrappedThisType(*(UnwrappedThisType*)this); }

	virtual RetValType Execute(ParamTypes... Params) const final { return Super::Payload.ApplyAfter(Functor, Params...); }

public:
	/**
	 * Creates a new static function delegate binding for the given function pointer.
	 *
	 * @param InFunctor C++ functor
	 * @return The new delegate.
	 */
	FORCEINLINE static void Create(FDelegateBase& Base, UserClass* InContextObject, const FunctorType& InFunctor, VarTypes... Vars) { new (Base) UnwrappedThisType(InContextObject, InFunctor, Vars...); }
	FORCEINLINE static void Create(FDelegateBase& Base, UserClass* InContextObject, FunctorType&& InFunctor, VarTypes... Vars) { new (Base) UnwrappedThisType(InContextObject, MoveTemp(InFunctor), Vars...); }

private:
	// Context object - the validity of this object controls the validity of the lambda
	TWeakObjectPtr<UserClass> ContextObject;

	// C++ functor
	// We make this mutable to allow mutable lambdas to be bound and executed.  We don't really want to
	// model the Functor as being a direct subobject of the delegate (which would maintain transivity of
	// const - because the binding doesn't affect the substitutability of a copied delegate.
	mutable typename TRemoveConst<FunctorType>::Type Functor;
};

template<typename UserClass, typename FunctorType Z_TYPENAME_USER_POLICY_DECLARE, typename... ParamTypes, typename... VarTypes>
class TWeakBaseFunctorDelegateInstance<UserClass, void(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...>
	: public TWeakBaseFunctorDelegateInstance<UserClass, TTypeWrapper<void>(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...>
{
	using Super = TWeakBaseFunctorDelegateInstance<UserClass, TTypeWrapper<void>(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...>;

public:
	/**
	 * Creates and initializes a new instance.
	 *
	 * @param InFunctor C++ functor
	 */
	TWeakBaseFunctorDelegateInstance(UserClass* InContextObject, const FunctorType& InFunctor, VarTypes... Vars)
		: Super(InContextObject, InFunctor, Vars...)
	{
	}

	TWeakBaseFunctorDelegateInstance(UserClass* InContextObject, FunctorType&& InFunctor, VarTypes... Vars)
		: Super(InContextObject, MoveTemp(InFunctor), Vars...)
	{
	}

	virtual bool ExecuteIfSafe(ParamTypes... Params) const final
	{
		if (Super::IsSafeToExecute())
		{
			Super::Execute(Params...);
			return true;
		}

		return false;
	}
};
#endif

#if !UE_4_22_OR_LATER
#if !UE_4_20_OR_LATER
namespace UNREALCOMPATIBILITY
{
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
}  // namespace UNREALCOMPATIBILITY

template<typename T>
struct TDetectBaseStructure
{
private:
	template<typename V>
	using HasGetType = decltype(&TBaseStructure<T>::Get);
	template<typename V>
	using HasGet = UNREALCOMPATIBILITY::IsDetected<HasGetType, V>;

public:
	enum
	{
		Value = HasGet<T>::value
	};
};
template<typename StructType>
TEnableIf<TDetectBaseStructure<StructType>::Value, UScriptStruct*> StaticStruct()
{
	return TBaseStructure<StructType>::Get();
}
template<typename StructType>
TEnableIf<!TDetectBaseStructure<StructType>::Value, UScriptStruct*> StaticStruct()
{
	return StructType::StaticStruct();
}
#else
template<typename StructType>
UScriptStruct* StaticStruct()
{
	return TBaseStructure<StructType>::Get();
}

#endif

template<typename EnumType>
UEnum* StaticEnum()
{
	static_assert(std::is_enum<EnumType>::value, "err");
	static UEnum* Ret = ::FindObject<UEnum>(ANY_PACKAGE, ANSI_TO_TCHAR(ITS::TypeStr<EnumType>()), true);
	return Ret;
}
template<typename ClassType>
UClass* StaticClass()
{
	return ClassType::StaticClass();
}

#define UE_DEPRECATED(VERSION, MESSAGE) DEPRECATED(VERSION, MESSAGE)

#else
#include "UObject/ReflectedTypeAccessors.h"
#endif

template<typename StructType>
UScriptStruct* StaticScriptStruct()
{
	return StaticStruct<std::decay_t<StructType>>();
}

#if !UE_4_21_OR_LATER
#include "Engine/LevelStreamingKismet.h"
using ULevelStreamingDynamic = ULevelStreamingKismet;
#else
#include "Engine/LevelStreamingDynamic.h"
#endif

using FUnsizedIntProperty = UETypes_Private::TIntegerPropertyMapping<signed int>::Type;
using FUnsizedUIntProperty = UETypes_Private::TIntegerPropertyMapping<unsigned int>::Type;

#if !UE_4_23_OR_LATER
#define DISABLE_REPLICATED_PROPERTY(c, v)
#endif

#if !UE_4_24_OR_LATER
#if !defined(UE_ARRAY_COUNT)
#define UE_ARRAY_COUNT ARRAY_COUNT
#endif
FORCEINLINE bool IsEngineExitRequested()
{
	return GIsRequestingExit;
}
#endif

#if !UE_4_25_OR_LATER
#include "PropertyCompatibility.include"
#else
#include "UObject/FieldPath.h"
using FFieldPropertyType = FProperty;

//////////////////////////////////////////////////////////////////////////
FORCEINLINE EClassCastFlags GetPropertyCastFlags(const FProperty* Prop)
{
	return (EClassCastFlags)Prop->GetCastFlags();
}
template<typename T>
using TFieldPathCompatible = TFieldPath<T>;

FORCEINLINE UObject* GetPropertyOwnerUObject(const FProperty* Prop)
{
	return Prop->GetOwnerUObject();
}
template<typename T = UObject>
FORCEINLINE T* GetPropertyOwnerUObject(const FProperty* Prop)
{
	return Cast<T>(Prop->GetOwnerUObject());
}
template<typename T>
FORCEINLINE UClass* GetPropertyOwnerClass(const T* Prop)
{
	return Prop->GetOwnerClass();
}
FORCEINLINE FString GetPropertyOwnerName(const FProperty* Prop)
{
	return Prop->GetOwnerVariant().GetName();
}
#endif

template<typename T>
FORCEINLINE T* GetPropPtr(T* Prop)
{
	return Prop;
}
template<typename T>
FORCEINLINE T* GetPropPtr(const TFieldPath<T>& Prop)
{
	return Prop.Get();
}

template<typename To>
FORCEINLINE auto CastField(const FFieldVariant& Field)
{
	return CastField<To>(GetPropPtr(Field.ToField()));
}

template<typename DelegateType, typename LambdaType, typename... PayloadTypes>
inline auto CreateWeakLambda(const UObject* InUserObject, LambdaType&& InFunctor, PayloadTypes... InputPayload)
{
	DelegateType Result;
	using FWeakBaseFunctorDelegateInstance = TWeakBaseFunctorDelegateInstance<UObject, typename DelegateType::TFuncType Z_TYPENAME_USER_POLICY_IMPL, std::remove_reference_t<LambdaType>, PayloadTypes...>;
	FWeakBaseFunctorDelegateInstance::Create(Result, const_cast<UObject*>(InUserObject), Forward<LambdaType>(InFunctor), InputPayload...);
	return Result;
}

template<class UserClass, ESPMode SPMode, typename FuncType Z_TYPENAME_USER_POLICY_DECLARE, typename FunctorType, typename... VarTypes>
class TBaseSPLambdaDelegateInstance;

template<class UserClass, ESPMode SPMode, typename WrappedRetValType, typename... ParamTypes Z_TYPENAME_USER_POLICY_DECLARE, typename FunctorType, typename... VarTypes>
class TBaseSPLambdaDelegateInstance<UserClass, SPMode, WrappedRetValType(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...>
	: public TCommonDelegateInstanceState<typename TUnwrapType<WrappedRetValType>::Type(ParamTypes...) Z_TYPENAME_USER_POLICY, VarTypes...>
{
private:
	static_assert(TAreTypesEqual<FunctorType, typename TRemoveReference<FunctorType>::Type>::Value, "FunctorType cannot be a reference");
	using Super = TCommonDelegateInstanceState<typename TUnwrapType<WrappedRetValType>::Type(ParamTypes...) Z_TYPENAME_USER_POLICY, VarTypes...>;
	using RetValType = typename Super::RetValType;
	using UnwrappedThisType = TBaseSPLambdaDelegateInstance<UserClass, SPMode, RetValType(ParamTypes...) Z_TYPENAME_USER_POLICY, FunctorType, VarTypes...>;

public:
	TBaseSPLambdaDelegateInstance(const TSharedPtr<UserClass, SPMode>& InUserObject, const FunctorType& InFunctor, VarTypes... Vars)
		: Super(Vars...)
		, UserObject(InUserObject)
		, Functor(MoveTemp(InFunctor))
	{
	}

	TBaseSPLambdaDelegateInstance(const TSharedPtr<UserClass, SPMode>& InUserObject, FunctorType&& InFunctor, VarTypes... Vars)
		: Super(Vars...)
		, UserObject(InUserObject)
		, Functor(MoveTemp(InFunctor))
	{
	}
	// IDelegateInstance interface
	const void* GetRawPtr() const { return UserObject.Pin().Get(); }

#if USE_DELEGATE_TRYGETBOUNDFUNCTIONNAME
	virtual FName TryGetBoundFunctionName() const final { return NAME_None; }
#endif

	virtual UObject* GetUObject() const final { return nullptr; }

#if !UE_4_20_OR_LATER
	virtual FName GetFunctionName() const final { return NAME_None; }
	virtual const void* GetRawMethodPtr() const final { nullptr; }
	virtual const void* GetRawUserObject() const final { return GetRawPtr(); }
	virtual EDelegateInstanceType::Type GetType() const final { return EDelegateInstanceType::Functor; }
#endif

#if UE_4_21_OR_LATER
	virtual const void* GetObjectForTimerManager() const final { return GetRawPtr(); }
#endif

#if UE_4_23_OR_LATER
	virtual uint64 GetBoundProgramCounterForTimerManager() const final
	{
#if PLATFORM_64BITS
		return *(uint64*)GetRawPtr();
#else
		return *(uint32*)GetRawPtr();
#endif
	}
#endif

	// Deprecated
	virtual bool HasSameObject(const void* InUserObject) const final { return UserObject.HasSameObject(InUserObject); }

	virtual bool IsSafeToExecute() const final { return UserObject.IsValid(); }

public:
	// IBaseDelegateInstance interface

	virtual void CreateCopy(FDelegateBase& Base) final { new (Base) UnwrappedThisType(*(UnwrappedThisType*)this); }

	virtual RetValType Execute(ParamTypes... Params) const final
	{
		checkSlow(IsSafeToExecute());
		return Super::Payload.ApplyAfter(Functor, Params...);
	}

	virtual bool ExecuteIfSafe(ParamTypes... Params) const
	{
		if (IsSafeToExecute())
		{
			Execute(Params...);
			return true;
		}
		return false;
	}

public:
	FORCEINLINE static void Create(FDelegateBase& Base, const TSharedPtr<UserClass, SPMode>& InUserObjectRef, const FunctorType& InFunctor, VarTypes... Vars) { new (Base) UnwrappedThisType(InUserObjectRef, InFunctor, Vars...); }
	FORCEINLINE static void Create(FDelegateBase& Base, const TSharedPtr<UserClass, SPMode>& InUserObjectRef, FunctorType&& InFunctor, VarTypes... Vars) { new (Base) UnwrappedThisType(InUserObjectRef, MoveTemp(InFunctor), Vars...); }

protected:
	// Weak reference to an instance of the user's class which contains a method we would like to call.
	TWeakPtr<UserClass, SPMode> UserObject;

	// C++ functor
	mutable typename TRemoveConst<FunctorType>::Type Functor;
};

namespace UnrealCompatibility
{
template<typename TFunc>
struct TFunctionTraitsImpl;

template<typename R, typename... TArgs>
struct TFunctionTraitsImpl<R (*)(TArgs...)>
{
	using TFuncType = R(TArgs...);
	using ParameterTuple = std::tuple<TArgs...>;
	using TDelegateType = TUnrealDelegate<R, TArgs...>;
};
template<typename R, typename FF, typename... TArgs>
struct TFunctionTraitsImpl<R (FF::*)(TArgs...)>
{
	using TFuncType = R(TArgs...);
	using ParameterTuple = std::tuple<TArgs...>;
	using TDelegateType = TUnrealDelegate<R, TArgs...>;
};
template<typename R, typename FF, typename... TArgs>
struct TFunctionTraitsImpl<R (FF::*)(TArgs...) const>
{
	using TFuncType = R(TArgs...);
	using ParameterTuple = std::tuple<TArgs...>;
	using TDelegateType = TUnrealDelegate<R, TArgs...>;
};
template<typename T, typename = void>
struct TFunctionTraits;
template<typename T>
struct TFunctionTraits<T, VoidType<decltype(&std::decay_t<T>::operator())>> : public TFunctionTraitsImpl<decltype(&std::decay_t<T>::operator())>
{
};
template<typename R, typename... TArgs>
struct TFunctionTraits<R(TArgs...), void> : public TFunctionTraitsImpl<R(TArgs...)>
{
};

template<typename T>
struct TFunctionTraits<T, std::enable_if_t<std::is_member_function_pointer<T>::value>> : public TFunctionTraitsImpl<T>
{
};
template<typename T>
struct TFunctionTraits<T, std::enable_if_t<std::is_pointer<T>::value && std::is_function<std::remove_pointer_t<T>>::value>> : public TFunctionTraitsImpl<T>
{
};
}  // namespace UnrealCompatibility

template<typename UserClass, ESPMode SPMode, typename LambdaType, typename... PayloadTypes>
inline auto CreateSPLambda(const TSharedRef<UserClass, SPMode>& InUserObject, LambdaType&& InFunctor, PayloadTypes... InputPayload)
{
	using DetectType = UnrealCompatibility::TFunctionTraits<std::remove_reference_t<LambdaType>>;
	typename DetectType::TDelegateType Result;
	using FBaseSPLambdaDelegateInstance = TBaseSPLambdaDelegateInstance<UserClass, SPMode, typename DetectType::TFuncType Z_TYPENAME_USER_POLICY_IMPL, std::remove_reference_t<LambdaType>, PayloadTypes...>;
	FBaseSPLambdaDelegateInstance::Create(Result, InUserObject, Forward<LambdaType>(InFunctor), InputPayload...);
	return Result;
}

template<typename UserClass, typename LambdaType, typename... PayloadTypes, typename TEnableIf<TIsDerivedFrom<UserClass, UObject>::IsDerived, int32>::Type = 0>
inline auto CreateWeakLambda(const UserClass* InUserObject, LambdaType&& InFunctor, PayloadTypes... InputPayload)
{
	using DetectType = UnrealCompatibility::TFunctionTraits<std::remove_reference_t<LambdaType>>;
	typename DetectType::TDelegateType Result;
	using FWeakBaseFunctorDelegateInstance = TWeakBaseFunctorDelegateInstance<UObject, typename DetectType::TFuncType Z_TYPENAME_USER_POLICY_IMPL, std::remove_reference_t<LambdaType>, PayloadTypes...>;
	FWeakBaseFunctorDelegateInstance::Create(Result, const_cast<UObject*>(static_cast<const UObject*>(InUserObject)), Forward<LambdaType>(InFunctor), InputPayload...);
	return Result;
}

template<typename UserClass, typename LambdaType, typename... PayloadTypes, typename TEnableIf<!TIsDerivedFrom<UserClass, UObject>::IsDerived, int32>::Type = 0>
FORCEINLINE auto CreateWeakLambda(const UserClass* InUserObject, LambdaType&& InFunctor, PayloadTypes&&... InputPayload)
{
	checkSlow(InUserObject);
	return CreateSPLambda(InUserObject->AsShared(), Forward<LambdaType>(InFunctor), Forward<PayloadTypes>(InputPayload)...);
}

template<typename ENUM_TYPE>
static FString UEnum2Str(ENUM_TYPE InEnumValue, bool bFullName = false)
{
	FString EnumNameString;
	static UEnum* FoundEnum = ::StaticEnum<ENUM_TYPE>();
	if (ensure(FoundEnum))
	{
		EnumNameString = FoundEnum->GetNameByValue((int64)InEnumValue).ToString();
		if (!bFullName)
		{
			int32 ScopeIndex = EnumNameString.Find(TEXT("::"), ESearchCase::CaseSensitive);
			if (ScopeIndex != INDEX_NONE)
			{
				EnumNameString.MidInline(ScopeIndex + 2);
			}
		}
	}
	return EnumNameString;
}

#endif  // !defined(UNREAL_COMPATIBILITY_GUARD_H)
