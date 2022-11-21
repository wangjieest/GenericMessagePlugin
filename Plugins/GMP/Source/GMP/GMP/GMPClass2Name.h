//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if !defined(GMP_CLASS_TO_NAME_GUARD_H)
#define GMP_CLASS_TO_NAME_GUARD_H

#include "CoreMinimal.h"

#include "Containers/EnumAsByte.h"
#include "Engine/UserDefinedEnum.h"
#include "GMPTypeTraits.h"
#include "HAL/PlatformAtomics.h"
#include "Misc/AssertionMacros.h"
#include "Templates/SubclassOf.h"
#include "UObject/Interface.h"
#include "UObject/LazyObjectPtr.h"
#include "UObject/Object.h"
#include "UObject/ScriptInterface.h"
#include "UObject/SoftObjectPtr.h"
#include "UObject/TextProperty.h"
#include "UObject/WeakObjectPtr.h"
#include "UnrealCompatibility.h"

#if !defined(GMP_WITH_EXACT_OBJECT_TYPE)
#define GMP_WITH_EXACT_OBJECT_TYPE 0
#endif

#define GMP_WITH_STATIC_MSGKEY (!GMP_DEBUGGAME)
template<typename T>
const FName GMP_MSGKEY_HOLDER{T::Get()};

#define Z_GMP_OBJECT_NAME TObjectPtr
#define NAME_GMP_TObjectPtr TEXT(GMP_TO_STR(Z_GMP_OBJECT_NAME))
#if !UE_5_00_OR_LATER
struct FObjectPtr
{
	UObject* Ptr;
};
template<typename T>
struct Z_GMP_OBJECT_NAME : private FObjectPtr
{
};
static_assert(std::is_base_of<FObjectPtr, Z_GMP_OBJECT_NAME<UObject>>::value, "err");
#else
static_assert(sizeof(FObjectPtr) == sizeof(Z_GMP_OBJECT_NAME<UObject>), "err");
#endif

#define Z_GMP_NATIVE_INC_NAME TGMPNativeInterface
#define NAME_GMP_TNativeInterfece TEXT(GMP_TO_STR(Z_GMP_NATIVE_INC_NAME))
template<typename InterfaceType>
class Z_GMP_NATIVE_INC_NAME : TScriptInterface<InterfaceType>
{
	InterfaceType*& Ref;

public:
	Z_GMP_NATIVE_INC_NAME(InterfaceType*& Inc)
		: Ref(Inc)
	{
		if (ensure(Inc))
		{
			TScriptInterface<InterfaceType>::SetObject(Inc->_getUObject());
			TScriptInterface<InterfaceType>::SetInterface(Inc);
		}
	}

	Z_GMP_NATIVE_INC_NAME(const Z_GMP_NATIVE_INC_NAME& Other)
		: Z_GMP_NATIVE_INC_NAME(Other.Ref)
	{
	}

	InterfaceType*& GetNativeAddr() { return Ref; }
};

// CppType --> Name
namespace GMP
{
using namespace TypeTraits;

template<typename T>
struct TUnwrapObjectPtr
{
	using Type = std::decay_t<std::remove_pointer_t<T>>;
};
template<typename T>
struct TUnwrapObjectPtr<Z_GMP_OBJECT_NAME<T>>
{
	using Type = std::decay_t<std::remove_pointer_t<T>>;
};
template<typename T>
using TUnwrapObjectPtrType = typename TUnwrapObjectPtr<Z_GMP_OBJECT_NAME<T>>::Type;

struct GMP_API FNameSuccession
{
	static FName GetClassName(UClass* InClass);
	static FName GetNativeClassName(UClass* InClass);
	static FName GetNativeClassPtrName(UClass* InClass);
	static bool IsDerivedFrom(FName Type, FName ParentType);
	static bool MatchEnums(FName IntType, FName EnumType);
	static bool IsTypeCompatible(FName lhs, FName rhs);

	static decltype(auto) ObjectPtrFormatStr() { return NAME_GMP_TObjectPtr TEXT("<%s>"); }
	static FName FormatObjectPtr(UClass* InClass) { return *FString::Printf(ObjectPtrFormatStr(), *GetClassName(InClass).ToString()); }
	template<typename T>
	static FName FormatObjectPtr()
	{
		return *FString::Printf(ObjectPtrFormatStr(), *TUnwrapObjectPtrType<T>::StaticClass()->GetName());
	}
};

namespace Class2Name
{
	template<typename T>
	struct TManualGeneratedName
	{
		enum
		{
			value = 0
		};
	};

	template<>
	struct TManualGeneratedName<void>
	{
		FORCEINLINE static const TCHAR* GetFName() { return TEXT("void"); }
		enum
		{
			dispatch_value = 1,
			value = 0
		};
	};

	// clang-format off
	// inline const auto MSGKEY_HOLDER_TEST() { return  GMP_MSGKEY_HOLDER<C_STRING_TYPE("str")>; }
	// clang-format on

#if !GMP_WITH_STATIC_MSGKEY
#define GMP_MANUAL_GENERATE_NAME(TYPE, NAME)                       \
	template<>                                                     \
	struct TManualGeneratedName<TYPE>                              \
	{                                                              \
		enum                                                       \
		{                                                          \
			value = TManualGeneratedName<void>::dispatch_value,    \
		};                                                         \
		FORCEINLINE static const FName GetFName() { return NAME; } \
	};
#else
#define GMP_MANUAL_GENERATE_NAME(TYPE, NAME)                                                         \
	template<>                                                                                       \
	struct TManualGeneratedName<TYPE>                                                                \
	{                                                                                                \
		enum                                                                                         \
		{                                                                                            \
			value = TManualGeneratedName<void>::dispatch_value,                                      \
		};                                                                                           \
		FORCEINLINE static const FName GetFName() { return GMP_MSGKEY_HOLDER<C_STRING_TYPE(NAME)>; } \
	};
#endif

#define GMP_NAME_OF(Class) GMP_MANUAL_GENERATE_NAME(Class, #Class)
	GMP_NAME_OF(bool)
	GMP_NAME_OF(char)
	GMP_NAME_OF(int8)
	GMP_NAME_OF(uint8)
	GMP_NAME_OF(int16)
	GMP_NAME_OF(uint16)
	GMP_NAME_OF(int32)
	GMP_NAME_OF(uint32)
	GMP_NAME_OF(int64)
	GMP_NAME_OF(uint64)
	GMP_NAME_OF(float)
	GMP_NAME_OF(double)

	// GMP_NAME_OF(wchar_t)
	// GMP_NAME_OF(long)
	// GMP_NAME_OF(unsigned long)
#undef GMP_NAME_OF

	// GMP_MANUAL_GENERATE_NAME(UObject, "Object")

	// custom def
#define GMP_RAW_NAME_OF(Class)                      \
	namespace GMP                                   \
	{                                               \
		namespace Class2Name                        \
		{                                           \
			GMP_MANUAL_GENERATE_NAME(Class, #Class) \
		}                                           \
	}

	struct TTraitsStructBase
	{
		static FName GetFName(UScriptStruct* InStruct) { return InStruct->GetFName(); }
	};

	// THasBaseStructure
	template<typename T>
	struct THasBaseStructure
	{
		enum
		{
			value = 0,
		};
		template<typename R>
		FORCEINLINE static FName GetFName()
		{
			return R::StaticStruct()->GetFName();
		}
	};
#define GMP_MANUAL_GENERATE_STRUCT_NAME(NAME)                 \
	GMP_MANUAL_GENERATE_NAME(F##NAME, #NAME)                  \
	template<>                                                \
	struct THasBaseStructure<F##NAME>                         \
	{                                                         \
		enum                                                  \
		{                                                     \
			value = 1                                         \
		};                                                    \
		template<typename R>                                  \
		FORCEINLINE static FName GetFName()                   \
		{                                                     \
			return TManualGeneratedName<F##NAME>::GetFName(); \
		}                                                     \
	};

	GMP_MANUAL_GENERATE_STRUCT_NAME(String)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Name)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Text)
	// provided by TBaseStructure
	GMP_MANUAL_GENERATE_STRUCT_NAME(Rotator)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Quat)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Transform)
	GMP_MANUAL_GENERATE_STRUCT_NAME(LinearColor)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Color)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Plane)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Vector)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Vector2D)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Vector4)
	GMP_MANUAL_GENERATE_STRUCT_NAME(RandomStream)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Guid)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Box2D)
	GMP_MANUAL_GENERATE_STRUCT_NAME(FallbackStruct)
	GMP_MANUAL_GENERATE_STRUCT_NAME(FloatRangeBound)
	GMP_MANUAL_GENERATE_STRUCT_NAME(FloatRange)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Int32RangeBound)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Int32Range)
	GMP_MANUAL_GENERATE_STRUCT_NAME(FloatInterval)
	GMP_MANUAL_GENERATE_STRUCT_NAME(Int32Interval)
	GMP_MANUAL_GENERATE_STRUCT_NAME(SoftObjectPath)
	GMP_MANUAL_GENERATE_STRUCT_NAME(SoftClassPath)
	GMP_MANUAL_GENERATE_STRUCT_NAME(PrimaryAssetType)
	GMP_MANUAL_GENERATE_STRUCT_NAME(PrimaryAssetId)
	GMP_MANUAL_GENERATE_STRUCT_NAME(DateTime)
#if UE_4_20_OR_LATER
	GMP_MANUAL_GENERATE_STRUCT_NAME(PolyglotTextData)
#endif
#if UE_4_26_OR_LATER
	GMP_MANUAL_GENERATE_STRUCT_NAME(FrameTime)
	GMP_MANUAL_GENERATE_STRUCT_NAME(AssetBundleData)
#endif
#if UE_5_00_OR_LATER
	//GMP_MANUAL_GENERATE_STRUCT_NAME(LargeWorldCoordinatesReal)
#endif
	// UObject
	template<typename T>
	struct TTraitsBaseClassValue;

	// UClass
	template<>
	struct TTraitsBaseClassValue<UClass>
	{
		enum
		{
			dispatch_value = 2,
		};
		static FName GetFName(UClass* MetaClass) { return *FString::Printf(TEXT("TSubClassOf<%s>"), MetaClass->IsNative() ? *MetaClass->GetName() : *FSoftClassPath(MetaClass).ToString()); }
	};

	// UObject
	struct TTraitsObjectBase
	{
		static decltype(auto) GetFormatStr() { return TEXT("%s"); }
		enum
		{
			dispatch_value = 3,
		};
	};
	template<>
	struct TTraitsBaseClassValue<UObject> : TTraitsObjectBase
	{
		static FName GetFName(UClass* ObjClass) { return (ensure(ObjClass) && ObjClass->IsNative()) ? ObjClass->GetFName() : FName(*FSoftClassPath(ObjClass).ToString()); }
	};

	template<typename T, bool bExactType = false>
	struct TTraitsObject : TTraitsObjectBase
	{
		static const FName& GetFName()
		{
			using ClassType = std::conditional_t<bExactType, std::decay_t<std::remove_pointer_t<T>>, UObject>;
#if WITH_EDITOR
			static FName Name = FNameSuccession::GetNativeClassName(StaticClass<ClassType>());
#else
			static FName Name = StaticClass<ClassType>()->GetFName();
#endif
			return Name;
		}
	};

	template<typename T>
	struct TTraitsClassType
	{
		using class_type = UObject;
		using type = TSubclassOf<class_type>;
		enum
		{
			value = IsSameV<UClass, std::remove_pointer_t<T>>,
		};
	};

	template<>
	struct TTraitsClassType<UClass>
	{
		using class_type = UObject;
		using type = TSubclassOf<class_type>;
		enum
		{
			value = 1,
		};
	};

	template<typename T>
	struct TTraitsClassType<TSubclassOf<T>>
	{
		using class_type = std::decay_t<T>;
		using type = TSubclassOf<class_type>;
		enum
		{
			value = std::is_base_of<UObject, std::remove_pointer_t<T>>::value,
		};
	};

	template<typename T, typename B>
	struct TTraitsClass
	{
		enum
		{
			dispatch_value = TTraitsBaseClassValue<B>::dispatch_value,
			value = (std::is_base_of<B, std::remove_pointer_t<T>>::value) ? dispatch_value : 0,
		};
	};

	// Interface
	template<typename T>
	struct TTraitsInterface
	{
		using type = std::decay_t<std::remove_pointer_t<T>>;
		enum
		{
			dispatch_value = 4,
			value = TIsIInterface<type>::Value ? dispatch_value : 0
		};
	};

	// Struct
	template<typename T>
	struct THasStaticStruct
	{
		template<typename V>
		using HasStaticStructType = decltype(V::StaticStruct());
		template<typename V>
		using HasStaticStruct = TypeTraits::IsDetected<HasStaticStructType, V>;
		enum
		{
			dispatch_value = 5,
			has_staticstruct = (std::is_class<T>::value && HasStaticStruct<T>::value),
			value = (std::is_class<T>::value && (HasStaticStruct<T>::value || THasBaseStructure<T>::value)) ? dispatch_value : 0,
		};
	};

	struct TTraitsEnumBase
	{
		enum
		{
			dispatch_value = 6,
		};

		template<uint32 N>
		static auto EnumAsBytesPrefix() -> const TCHAR (&)[12]
		{
			static_assert(N == 1 || N == 2 || N == 4 || N == 8, "err");
			switch (N)
			{
				case 2:
					return TEXT("TEnum2Bytes");
				case 4:
					return TEXT("TEnum4Bytes");
				case 8:
					return TEXT("TEnum8Bytes");
				default:
					return TEXT("TEnumAsByte");
			}
		}

		static auto EnumAsBytesPrefix(uint32 N) -> const TCHAR (&)[12]
		{
			checkSlow(N == 1 || N == 2 || N == 4 || N == 8);
			static_assert(sizeof(EnumAsBytesPrefix<1>()) == 12 * sizeof(TCHAR)  //
							  && sizeof(EnumAsBytesPrefix<1>()) == sizeof(EnumAsBytesPrefix<2>()) && sizeof(EnumAsBytesPrefix<2>()) == sizeof(EnumAsBytesPrefix<4>()) && sizeof(EnumAsBytesPrefix<4>()) == sizeof(EnumAsBytesPrefix<8>()),
						  "err");
			switch (N)
			{
				case 2:
					return EnumAsBytesPrefix<2>();
				case 4:
					return EnumAsBytesPrefix<4>();
				case 8:
					return EnumAsBytesPrefix<8>();
				default:
					return EnumAsBytesPrefix<1>();
			}
		}

		static FName EnumAsBytesFName(const TCHAR* EnumName, uint32 Bytes = 1) { return FName(*FString::Printf(TEXT("%s<%s>"), EnumAsBytesPrefix(Bytes), EnumName)); }

		static FName GetFName(UEnum* InEnum, uint32 Bytes = 1)
		{
			// always treats enumclass as TEnumAsByte
			Bytes = InEnum->GetCppForm() == UEnum::ECppForm::EnumClass ? sizeof(uint8) : Bytes;
			return EnumAsBytesFName(InEnum->CppType.IsEmpty() ? *InEnum->GetName() : *InEnum->CppType, Bytes);
		}
	};

	template<typename T>
	struct TTraitsEnumUtils : TTraitsEnumBase
	{
		static_assert(std::is_enum<T>::value, "err");
		static_assert(!ITS::is_scoped_enum<T>::value || IsSameV<std::underlying_type_t<T>, uint8>, "use enum class : uint8 instead");
		static const FName& GetFName(nullptr_t = nullptr)
		{
#if WITH_EDITOR
			checkSlow(FString(ITS::TypeStr<T>()) == StaticEnum<T>()->CppType);
#endif
			static FName Name = TTraitsEnumBase::GetFName(StaticEnum<T>(), sizeof(T));
			return Name;
		}
	};

	template<typename T, bool b = std::is_enum<T>::value>
	struct TTraitsEnum : TTraitsEnumBase
	{
		using underlying_type = std::decay_t<T>;
		enum
		{
			value = (b ? TTraitsEnumBase::dispatch_value : 0),
		};
	};
	template<typename T>
	struct TTraitsEnum<T, true> : TTraitsEnumUtils<T>
	{
		using underlying_type = std::underlying_type_t<std::decay_t<T>>;
		enum
		{
			value = TTraitsEnumBase::dispatch_value,
		};
	};

	template<typename T>
	struct TTraitsArithmetic
	{
		using type = T;
		using underlying_type = typename TTraitsEnum<T>::underlying_type;
		enum
		{
			value = std::is_arithmetic<std::decay_t<T>>::value,
			enum_as_byte = 0,
		};
	};
	template<typename T>
	struct TTraitsArithmetic<TEnumAsByte<T>>
	{
		using type = TEnumAsByte<T>;
		using underlying_type = typename TTraitsEnum<T>::underlying_type;
		enum
		{
			value = 1,
			enum_as_byte = 1,
		};
	};

	struct TTraitsTemplateBase
	{
		enum
		{
			dispatch_value = 7,
			value = dispatch_value,
			nested = 0,
		};
		static FName GetTArrayName(const TCHAR* Inner) { return *FString::Printf(TEXT("TArray<%s>"), Inner); }
		static FName GetTMapName(const TCHAR* InnerKey, const TCHAR* InnerValue) { return *FString::Printf(TEXT("TMap<%s,%s>"), InnerKey, InnerValue); }
		static FName GetTSetName(const TCHAR* Inner) { return *FString::Printf(TEXT("TSet<%s>"), Inner); }
	};

	template<typename T, bool bExactType>
	struct TTraitsTemplate : TTraitsTemplateBase
	{
		enum
		{
			value = 0
		};
	};

	// clang-format off
	template <typename T, bool bExactType = false, size_t IMetaType = TypeTraits::TDisjunction<
		  TTraitsEnum<T>
		, TTraitsClass<T, UClass>
		, TTraitsClass<T, UObject>
		, TTraitsInterface<T>
		, TTraitsTemplate<T, bExactType>
		, THasStaticStruct<T>
	// 	, TManualGeneratedName<T>
		>::value
	>
	struct TClass2NameImpl
	{
		static const FName& GetFName()
		{
			using DT = std::remove_pointer_t<std::decay_t<T>>;
			using Type = std::conditional_t<!bExactType && std::is_base_of<UObject, DT>::value, UObject, DT>;
			static FName Name = TManualGeneratedName<Type>::GetFName();
			return Name;
		}
	};
	// clang-format on

	// TArray
	template<typename InElementType, typename InAllocator, bool bExactType>
	struct TTraitsTemplate<TArray<InElementType, InAllocator>, bExactType> : TTraitsTemplateBase
	{
		static_assert(IsSameV<InAllocator, FDefaultAllocator>, "only support FDefaultAllocator");
		static_assert(!TTraitsTemplate<InElementType, bExactType>::nested, "not support nested container");
		enum
		{
			nested = 1
		};
		static auto GetFName();
	};

	// TMap
	template<typename InKeyType, typename InValueType, typename SetAllocator, typename KeyFuncs, bool bExactType>
	struct TTraitsTemplate<TMap<InKeyType, InValueType, SetAllocator, KeyFuncs>, bExactType> : TTraitsTemplateBase
	{
		static_assert(IsSameV<SetAllocator, FDefaultSetAllocator>, "only support FDefaultSetAllocator");
		static_assert(IsSameV<KeyFuncs, TDefaultMapHashableKeyFuncs<InKeyType, InValueType, false>>, "only support TDefaultMapHashableKeyFuncs");
		static_assert(!TTraitsTemplate<InKeyType, bExactType>::nested && !TTraitsTemplate<InValueType, bExactType>::nested, "not support nested container");
		enum
		{
			nested = 1
		};
		static auto GetFName();
	};

	// TSet
	template<typename InElementType, typename KeyFuncs, typename InAllocator, bool bExactType>
	struct TTraitsTemplate<TSet<InElementType, KeyFuncs, InAllocator>, bExactType> : TTraitsTemplateBase
	{
		static_assert(IsSameV<KeyFuncs, DefaultKeyFuncs<InElementType>>, "only support DefaultKeyFuncs");
		static_assert(IsSameV<InAllocator, FDefaultSetAllocator>, "only support FDefaultSetAllocator");
		static_assert(!TTraitsTemplate<InElementType, bExactType>::nested, "not support nested container");
		enum
		{
			nested = 1
		};
		static auto GetFName();
	};

	template<typename T>
	struct TTraitsTemplateUtils
	{
		static decltype(auto) GetFormatStr() { return TEXT("%s"); }
		enum
		{
			is_tmpl = 0,
			is_subclassof = 0,
			tmpl_as_struct = 0,
		};
	};

#define GMP_MANUAL_GENERATE_NAME_FMT(NAME)                                                                      \
	template<typename T>                                                                                        \
	struct TTraitsTemplateUtils<T##NAME<T>>                                                                     \
	{                                                                                                           \
		using InnerType = std::remove_pointer_t<std::decay_t<T>>;                                               \
		static UClass* InnerClass() { return InnerType::StaticClass(); }                                        \
		static decltype(auto) GetFormatStr() { return TEXT(GMP_TO_STR(T##NAME)) TEXT("<%s>"); }                 \
		static auto GetFName(nullptr_t = nullptr)                                                               \
		{                                                                                                       \
			static FName Name = GetFName(T::StaticClass());                                                     \
			return Name;                                                                                        \
		}                                                                                                       \
		static FName GetFName(const TCHAR* Inner) { return *FString::Printf(GetFormatStr(), Inner); }           \
		static FName GetFName(UClass* InClass) { return InClass ? GetFName(*InClass->GetName()) : GetFName(); } \
		enum                                                                                                    \
		{                                                                                                       \
			is_tmpl = 1,                                                                                        \
			is_subclassof = IsSameV<TSubclassOf<T>, T##NAME<T>>,                                                \
			tmpl_as_struct = !is_subclassof,                                                                    \
		};                                                                                                      \
	};

#define GMP_MANUAL_GENERATE_NAME_TMPL(NAME)        \
	GMP_MANUAL_GENERATE_NAME_FMT(NAME)             \
	template<typename T, bool bExactType>          \
	struct TTraitsTemplate<T##NAME<T>, bExactType> \
		: TTraitsTemplateUtils<T##NAME<T>>         \
		, TTraitsTemplateBase                      \
	{                                              \
	};

	GMP_MANUAL_GENERATE_NAME_TMPL(SoftClassPtr)
	GMP_MANUAL_GENERATE_NAME_TMPL(SubclassOf)

#define GMP_MANUAL_GENERATE_NAME_FULL(NAME)                                                \
	GMP_MANUAL_GENERATE_NAME_TMPL(NAME)                                                    \
	template<bool bExactType>                                                              \
	struct TTraitsTemplate<F##NAME, bExactType> : TTraitsTemplate<T##NAME<UObject>, false> \
	{                                                                                      \
	};

	GMP_MANUAL_GENERATE_NAME_FULL(SoftObjectPtr)

	GMP_MANUAL_GENERATE_NAME(FScriptInterface, "ScriptInterface")

	GMP_MANUAL_GENERATE_NAME_FULL(ObjectPtr)
	GMP_MANUAL_GENERATE_NAME_FULL(WeakObjectPtr)
	GMP_MANUAL_GENERATE_NAME_FULL(LazyObjectPtr)

	template<int32 BufferSize>
	TStringBuilder<BufferSize>& operator<<(TStringBuilder<BufferSize>& Builder, const FName& Name)
	{
		Name.AppendString(Builder);
		return Builder;
	}

	struct TTraitsScriptDelegateBase
	{
		enum
		{
			dispatch_value = 8,
			value = dispatch_value,
		};

		template<bool bExactType, typename Tup, size_t... Is>
		static FString BuildParameterNameImpl(Tup* tup, const std::index_sequence<Is...>&)
		{
			static FString Ret = [&] {
#if 0
				TArray<FName> Names{TClass2Name<std::decay_t<std::tuple_element_t<Is, Tup>>, bExactType>::GetFName()...};
#else
				TArray<FName> Names{TClass2NameImpl<std::decay_t<std::tuple_element_t<Is, Tup>>, bExactType>::GetFName()...};
#endif
				TStringBuilder<256> Builder;
				return *Builder.Join(Names, TEXT(','));
			}();
			return Ret;
		}

		template<bool bExactType, typename... Ts>
		static const FString& GetParameterName()
		{
			static FString ParameterName = BuildParameterNameImpl<bExactType>((std::tuple<Ts...>*)nullptr, std::make_index_sequence<sizeof...(Ts)>{});
			return ParameterName;
		}

		static FString GetDelegateNameImpl(bool bMulticast, FName RetType, const TCHAR* ParamsType)
		{
			return FString::Printf(TEXT("<%s(%s)>"), bMulticast ? TEXT("TBaseDynamicDelegate") : TEXT("TBaseDynamicMulticastDelegate"), *RetType.ToString(), ParamsType);
		}
		GMP_API static FString GetDelegateNameImpl(bool bMulticast, UFunction* SignatureFunc, bool bExactType = true);

		template<bool bExactType, typename R, typename... Ts>
		static FName GetDelegateFName(bool bMulticast = false)
		{
#if 0
		static FName Ret = *GetDelegateNameImpl(bMulticast, TClass2Name<R, bExactType>::GetFName(), *GetParameterName<bExactType, Ts...>(), bExactType);
#else
			static FName Ret = *GetDelegateNameImpl(bMulticast, TClass2NameImpl<R, bExactType>::GetFName(), *GetParameterName<bExactType, Ts...>(), bExactType);
#endif
			return Ret;
		}
	};

	GMP_MANUAL_GENERATE_NAME(FScriptDelegate, "ScriptDelegate")
	GMP_MANUAL_GENERATE_NAME(FMulticastScriptDelegate, "MulticastScriptDelegate")
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TScriptDelegate<T>, bExactType> : TManualGeneratedName<FScriptDelegate>
	{
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TMulticastScriptDelegate<T>, bExactType> : TManualGeneratedName<FMulticastScriptDelegate>
	{
	};

	template<typename T, bool bExactType>
	struct TTraitsBaseDelegate;

	template<typename T, typename R, typename... Ts, bool bExactType>
	struct TTraitsBaseDelegate<TBaseDynamicDelegate<T, R, Ts...>, bExactType> : TTraitsScriptDelegateBase
	{
		static auto GetFName() { return TTraitsScriptDelegateBase::GetDelegateFName<bExactType, R, Ts...>(false); }
	};

	template<typename T, typename R, typename... Ts, bool bExactType>
	struct TTraitsBaseDelegate<TBaseDynamicMulticastDelegate<T, R, Ts...>, bExactType> : TTraitsScriptDelegateBase
	{
		static auto GetFName() { return TTraitsScriptDelegateBase::GetDelegateFName<bExactType, R, Ts...>(true); }
	};

	template<typename T, bool bExactType, typename = void>
	struct TTraitsScriptDelegate;
	template<typename T, bool bExactType>
	struct TTraitsScriptDelegate<T, bExactType, std::enable_if_t<TypeTraits::IsDerivedFromTemplate<TBaseDynamicDelegate, T>::value>>  //
		: TTraitsBaseDelegate<decltype(TypeTraits::IsDerivedFromTemplate<TBaseDynamicDelegate, T>::Types(nullptr)), bExactType>
	{
	};
	template<typename T, bool bExactType>
	struct TTraitsScriptDelegate<T, bExactType, std::enable_if_t<TypeTraits::IsDerivedFromTemplate<TBaseDynamicMulticastDelegate, T>::value>>
		: TTraitsBaseDelegate<decltype(TypeTraits::IsDerivedFromTemplate<TBaseDynamicMulticastDelegate, T>::Types(nullptr)), bExactType>
	{
	};

	template<typename T>
	struct TTraitsNativeInterface;

	template<typename T, bool bExactType>
	struct TTraitsTemplate<TScriptInterface<T>, bExactType>
		: TTraitsNativeInterface<TScriptInterface<T>>
		, TTraitsTemplateBase
	{
		using UClassType = typename std::decay_t<T>::UClassType;
		static UClass* InnerClass() { return UClassType::StaticClass(); }
	};

	template<typename T, bool bExactType>
	struct TTraitsTemplate<Z_GMP_NATIVE_INC_NAME<T>, bExactType>
		: TTraitsNativeInterface<Z_GMP_NATIVE_INC_NAME<T>>
		, TTraitsTemplateBase
	{
		using UClassType = typename std::decay_t<T>::UClassType;
		static UClass* InnerClass() { return UClassType::StaticClass(); }
	};

	/*
	TEnumAsByte<TEnum>
	{
		uint8 Value;
	}
	*/
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TEnumAsByte<T>, bExactType> : TTraitsTemplateBase
	{
		static const FName& GetFName()
		{
#if WITH_EDITOR
			checkSlow(FString(ITS::TypeStr<T>()) == StaticEnum<T>()->CppType);
#endif
			static FName Name = TTraitsEnumBase::EnumAsBytesFName(*StaticEnum<T>()->CppType, 1);
			return Name;
		}
	};

	struct TTraitsScriptIncBase
	{
		static FName GetBaseFName() { return TClass2NameImpl<FScriptInterface>::GetFName(); }
		static FName GetFName(nullptr_t = nullptr) { return GetBaseFName(); }

		static decltype(auto) GetFormatStr() { return TEXT(GMP_TO_STR(TScriptInterface)) TEXT("<%s>"); }
		static FName GetFName(const TCHAR* Inner) { return *FString::Printf(GetFormatStr(), Inner); }
		static FName GetFName(UClass* InClass)
		{
			ensure(!InClass || InClass->IsChildOf<UInterface>());
			return InClass ? GetFName(*InClass->GetName()) : GetBaseFName();
		}
	};

	struct TTraitsNativeIncBase
	{
		static decltype(auto) GetFormatStr() { return NAME_GMP_TNativeInterfece TEXT("<%s>"); }
		static FName GetFName(const TCHAR* Inner) { return *FString::Printf(GetFormatStr(), Inner); }
		static FName GetFName(UClass* InClass)
		{
			ensure(InClass->IsChildOf<UInterface>());
			return GetFName(*InClass->GetName());
		}
	};

	template<typename IncType>
	struct TTraitsNativeInterface
	{
		static bool IsCompatible(FName Name) { return TTraitsScriptIncBase::GetBaseFName() == Name; }
	};

	template<typename T>
	struct TTraitsNativeInterface<TScriptInterface<T>>
		: TTraitsScriptIncBase
		, TTraitsNativeInterface<T>
	{
		using UClassType = typename std::decay_t<T>::UClassType;
		static UClass* InnerClass() { return UClassType::StaticClass(); }
		static FName GetFName(nullptr_t = nullptr)
		{
			static FName Name = TTraitsScriptIncBase::GetFName(UClassType::StaticClass());
			return Name;
		}
		static bool IsCompatible(FName Name) { return GetFName() == Name || TTraitsScriptIncBase::GetBaseFName() == Name; }
	};

	template<typename T>
	struct TTraitsNativeInterface<Z_GMP_NATIVE_INC_NAME<T>>
		: TTraitsNativeIncBase
		, TTraitsNativeInterface<T>
	{
		using UClassType = typename std::decay_t<T>::UClassType;
		static UClass* InnerClass() { return UClassType::StaticClass(); }
		static FName GetFName(nullptr_t = nullptr)
		{
			static FName Name = TTraitsNativeIncBase::GetFName(UClassType::StaticClass());
			return Name;
		}
		static bool IsCompatible(FName Name) { return GetFName() == Name || TTraitsNativeInterface<T>::GetFName() == Name || TTraitsNativeInterface<TScriptInterface<T>>::GetFName() == Name; }
	};

	template<typename InElementType, typename KeyFuncs, typename InAllocator, bool bExactType>
	auto TTraitsTemplate<TSet<InElementType, KeyFuncs, InAllocator>, bExactType>::GetFName()
	{
		using ElementType = InElementType;
		return TTraitsTemplateBase::GetTSetName(*TClass2NameImpl<ElementType, bExactType>::GetFName().ToString());
	}

	template<typename InElementType, typename InAllocator, bool bExactType>
	auto TTraitsTemplate<TArray<InElementType, InAllocator>, bExactType>::GetFName()
	{
		using ElementType = InElementType;
		return TTraitsTemplateBase::GetTArrayName(*TClass2NameImpl<ElementType, bExactType>::GetFName().ToString());
	}

	template<typename InKeyType, typename InValueType, typename SetAllocator, typename KeyFuncs, bool bExactType>
	auto TTraitsTemplate<TMap<InKeyType, InValueType, SetAllocator, KeyFuncs>, bExactType>::GetFName()
	{
		using KeyType = InKeyType;
		using ValueType = InValueType;
		return TTraitsTemplateBase::GetTMapName(*TClass2NameImpl<KeyType, bExactType>::GetFName().ToString(), *TClass2NameImpl<ValueType, bExactType>::GetFName().ToString());
	}

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, TManualGeneratedName<void>::dispatch_value>
	{
		static const FName& GetFName()
		{
			static FName Name = TManualGeneratedName<T>::GetFName();
			return Name;
		}
	};

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, TTraitsBaseClassValue<UClass>::dispatch_value> : TTraitsTemplate<TSubclassOf<UObject>, bExactType>
	{
	};
	static_assert(std::is_base_of<TTraitsTemplate<TSubclassOf<UObject>, true>, TClass2NameImpl<UClass, true>>::value, "err");
	static_assert(std::is_base_of<TTraitsTemplate<TSubclassOf<UObject>, false>, TClass2NameImpl<UClass, false>>::value, "err");

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, TTraitsBaseClassValue<UObject>::dispatch_value> : TTraitsObject<T, bExactType>
	{
	};
	static_assert(std::is_base_of<TTraitsObject<UObject, false>, TClass2NameImpl<UObject, false>>::value, "err");
	static_assert(std::is_base_of<TTraitsObject<UObject, true>, TClass2NameImpl<UObject, true>>::value, "err");

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, TTraitsInterface<void>::dispatch_value>
	{
		static const FName& GetFName()
		{
			static FName Name = TTraitsNativeInterface<Z_GMP_NATIVE_INC_NAME<std::remove_pointer_t<T>>>::GetFName();
			return Name;
		}
	};

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, THasStaticStruct<void>::dispatch_value>
	{
		static const FName& GetFName()
		{
			static FName Name = THasBaseStructure<T>::template GetFName<T>();
			return Name;
		}
	};

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, TTraitsEnumBase::dispatch_value> : TTraitsEnumUtils<T>
	{
	};

	template<typename T, bool bExactType>
	struct TClass2NameImpl<T, bExactType, TTraitsTemplateBase::dispatch_value> : TTraitsTemplate<T, bExactType>
	{
	};

	template<typename T>
	constexpr bool is_native_inc_v = std::is_pointer<T>::value&& TIsIInterface<std::remove_pointer_t<std::decay_t<T>>>::Value;
	template<typename T>
	using native_inc_to_struct = Z_GMP_NATIVE_INC_NAME<std::remove_pointer_t<std::decay_t<T>>>;
	template<typename T>
	using InterfaceParamConvert = std::conditional_t<is_native_inc_v<T>, native_inc_to_struct<T>, typename std::add_lvalue_reference<T>::type>;
	template<typename T>
	using InterfaceTypeConvert = std::remove_reference_t<InterfaceParamConvert<T>>;

	template<typename T, typename V = void>
	struct TTraitsUStruct;

	template<typename T>
	struct TTraitsUStruct<T, std::enable_if_t<std::is_base_of<UObject, T>::value>>
	{
		static UStruct* GetUStruct() { return StaticClass<T>(); }
	};

	template<typename T>
	struct TTraitsUStruct<T, std::enable_if_t<!std::is_base_of<UObject, T>::value>>
	{
		static UStruct* GetUStruct() { return StaticStruct<T>(); }
	};
}  // namespace Class2Name
template<typename T, bool bExactType = true>
using TClass2Name = Class2Name::TClass2NameImpl<std::remove_cv_t<std::remove_reference_t<T>>, bExactType>;
}  // namespace GMP

#endif  // !defined(GMP_CLASS_TO_NAME_GUARD_H)
