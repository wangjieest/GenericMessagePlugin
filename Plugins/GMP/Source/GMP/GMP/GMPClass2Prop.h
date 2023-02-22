//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#if !defined(GMP_CLASS_TO_PROP_GUARD_H)
#define GMP_CLASS_TO_PROP_GUARD_H

#include "Engine/NetSerialization.h"
#include "GMPClass2Name.h"
#include "GMPReflection.h"
#include "GMPTypeTraits.h"
#include "UObject/UnrealType.h"

#include <tuple>
#include <type_traits>

#ifndef GMP_WITH_FINDORADD_UNIQUE_PROPERTY
// WE NEED THIS TO KEEP PROPERTY UNIQUE
#define GMP_WITH_FINDORADD_UNIQUE_PROPERTY (WITH_EDITOR || 1)
#endif

#ifndef GMP_WITH_PROPERTY_NAME_PREFIX
#define GMP_WITH_PROPERTY_NAME_PREFIX (WITH_EDITOR || 1)
#endif

// CppType --> Property
namespace GMP
{
using namespace TypeTraits;
namespace Class2Prop
{
	GMP_API UObject* GMPGetPropertiesHolder();
	GMP_API FProperty*& FindOrAddProperty(FName PropTypeName);
	GMP_API FProperty* FindOrAddProperty(FName PropTypeName, FProperty* Prop);

#if GMP_WITH_PROPERTY_NAME_PREFIX
	const TCHAR GMPPropPrefix[] = TEXT("GMP.");
	inline FName GMPPropFullName(const FName& TypeName)
	{
		TStringBuilder<256> Builder;
		Builder.Append(GMPPropPrefix);
		TypeName.AppendString(Builder);
		return FName(*Builder);
	}
#else
#define GMPPropFullName(x) x
#endif

	template<typename C>
	bool VerifyPropertyType(FProperty* Prop, C* Class)
	{
		return (Prop && ensureAlways(Prop->IsA(Class)));
	}
	template<typename T>
	bool VerifyPropertyType(FProperty* Prop)
	{
		return VerifyPropertyType(Prop, T::StaticClass());
	}
	template<typename T>
	T*& FindOrAddProperty(FName PropTypeName)
	{
		FProperty*& Prop = FindOrAddProperty(GMPPropFullName(PropTypeName));
#if !UE_BUILD_SHIPPING
		if (!VerifyPropertyType<T>(Prop))
			Prop = nullptr;
#endif
		return *reinterpret_cast<T**>(&Prop);
	}

#define GMP_OBJECT_FLAGS (RF_Public | RF_DuplicateTransient | RF_Transient | RF_MarkAsNative | RF_MarkAsRootSet)
#if UE_4_25_OR_LATER
	template<typename T, typename... TArgs>
	T* NewNativeProperty(const FName& ObjName, uint64 Flag, TArgs... Args)
	{
		PRAGMA_DISABLE_DEPRECATION_WARNINGS
		return new T(GMPGetPropertiesHolder(), ObjName, GMP_OBJECT_FLAGS, 0, (EPropertyFlags)Flag, Args...);
		PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
#else
	template<typename T, typename... TArgs>
	T* NewNativeProperty(const FName& ObjName, uint64 Flag, TArgs... Args)
	{
		return new (EC_InternalUseOnlyConstructor, GMPGetPropertiesHolder(), ObjName, GMP_OBJECT_FLAGS) T(FObjectInitializer(), EC_CppProperty, 0, (EPropertyFlags)Flag, Args...);
	}
#endif

	struct EmptyValue
	{
		enum
		{
			value = 0,
			nested = 0,
		};
	};

	struct TBasePropertyTraitsBase
	{
		enum
		{
			tag = 1,
			value = tag,
		};

		template<typename T>
		static FName GetBaseTypeName()
		{
			FName PropTypeName = TClass2Name<T>::GetFName();
			return PropTypeName;
		}
	};

	template<typename T>
	struct TBasePropertyTraits : EmptyValue
	{
	};

#if WITH_EDITOR
#define GMP_MAP_BASE_PROPERTY(T, P)                                                                                              \
	template<>                                                                                                                   \
	struct TBasePropertyTraits<T> : TBasePropertyTraitsBase                                                                      \
	{                                                                                                                            \
		static P* NewProperty() { return NewNativeProperty<P>(GMPPropFullName(GetBaseTypeName<T>()), CPF_HasGetValueTypeHash); } \
		static P* GetProperty()                                                                                                  \
		{                                                                                                                        \
			P*& NewProp = FindOrAddProperty<P>(GetBaseTypeName<T>());                                                            \
			if (!NewProp)                                                                                                        \
				NewProp = NewProperty();                                                                                         \
			return NewProp;                                                                                                      \
		}                                                                                                                        \
	}
#else
#define GMP_MAP_BASE_PROPERTY(T, P)                                                                                              \
	template<>                                                                                                                   \
	struct TBasePropertyTraits<T> : TBasePropertyTraitsBase                                                                      \
	{                                                                                                                            \
		static P* NewProperty() { return NewNativeProperty<P>(GMPPropFullName(GetBaseTypeName<T>()), CPF_HasGetValueTypeHash); } \
		static P* GetProperty()                                                                                                  \
		{                                                                                                                        \
			static auto NewProp = NewProperty();                                                                                 \
			return NewProp;                                                                                                      \
		}                                                                                                                        \
	}
#endif

	GMP_MAP_BASE_PROPERTY(int32, FIntProperty);
	GMP_MAP_BASE_PROPERTY(int64, FInt64Property);
#if !GMP_FORCE_DOUBLE_PROPERTY
	GMP_MAP_BASE_PROPERTY(float, FFloatProperty);
#endif
	GMP_MAP_BASE_PROPERTY(double, FDoubleProperty);
#if UE_5_00_OR_LATER
	// GMP_MAP_BASE_PROPERTY(FLargeWorldCoordinatesReal, FLargeWorldCoordinatesRealProperty);
#endif
	GMP_MAP_BASE_PROPERTY(int16, FInt16Property);
	GMP_MAP_BASE_PROPERTY(uint16, FUInt16Property);
	GMP_MAP_BASE_PROPERTY(uint32, FUInt32Property);
	GMP_MAP_BASE_PROPERTY(uint64, FUInt64Property);

	GMP_MAP_BASE_PROPERTY(FString, FStrProperty);
	GMP_MAP_BASE_PROPERTY(FName, FNameProperty);
	GMP_MAP_BASE_PROPERTY(FText, FTextProperty);

	GMP_MAP_BASE_PROPERTY(uint8, FByteProperty);
	GMP_MAP_BASE_PROPERTY(int8, FByteProperty);

	template<>
	struct TBasePropertyTraits<bool> : TBasePropertyTraitsBase
	{
		static FBoolProperty* NewProperty() { return NewNativeProperty<FBoolProperty>(GMPPropFullName(GetBaseTypeName<bool>()), 0, 255, 1, true); }
		static FBoolProperty* GetProperty()
		{
#if WITH_EDITOR
			FBoolProperty*& NewProp = FindOrAddProperty<FBoolProperty>(GetBaseTypeName<bool>());
			if (!NewProp)
				NewProp = NewProperty();
#else
			static auto NewProp = NewProperty();
#endif
			return NewProp;
		}
	};
#undef GMP_MAP_BASE_PROPERTY

	// FEnumProperty
	// FStructProperty
	// FClassProperty
	// FObjectProperty
	// FArrayProperty
	// FMapProperty
	// FSetProperty
	// FWeakObjectProperty
	// FSoftObjectProperty
	// FSoftClassProperty
	// FInterfaceProperty
	// FLazyObjectProperty

	// FDelegateProperty
	// FMulticastDelegateProperty

	// FFieldPathProperty

	// enum
	struct TEnumPropertyBase
	{
		enum
		{
			tag = 2,
		};
		enum : uint64
		{
			CPF_EnumAsByteMark = 0x1000000000000000ull,
			CPF_EnumAsByteMask = 0xF000000000000000ull,
		};
		template<uint32 N>
		static FEnumProperty* NewEnumProperty(FName InPropName, UEnum* EnumPtr)
		{
			using T = std::conditional_t<N == 2, FUInt16Property, std::conditional_t<N == 4, FIntProperty, std::conditional_t<N == 8, FInt64Property, FByteProperty>>>;
			FEnumProperty* NewProp = NewNativeProperty<FEnumProperty>(InPropName, CPF_HasGetValueTypeHash | (CPF_EnumAsByteMark * N), EnumPtr);
			if (N != 1)
			{
#if UE_4_25_OR_LATER
				auto* UnderlyingProp = new T(NewProp, Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<N>(), RF_Transient);
#else
				auto* UnderlyingProp = NewObject<T>(Property, Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<N>());
#endif
				NewProp->AddCppProperty(UnderlyingProp);
			}
			return NewProp;
		}

		static FEnumProperty* NewProperty(UEnum* InEnum, uint32 Bytes = 1)
		{
			FName TypeName = Class2Name::TTraitsEnumBase::GetFName(InEnum, Bytes);
			static auto NewEnumProp = [](FName InPropName, UEnum* EnumPtr, uint32 InBytes) {
				switch (InBytes)
				{
					case 2:
						return NewEnumProperty<2>(InPropName, EnumPtr);
					case 4:
						return NewEnumProperty<4>(InPropName, EnumPtr);
					case 8:
						return NewEnumProperty<8>(InPropName, EnumPtr);
					default:
						return NewEnumProperty<1>(InPropName, EnumPtr);
				}
			};
			return NewEnumProp(GMPPropFullName(TypeName), InEnum, Bytes);
		}

		static FEnumProperty* GetProperty(UEnum* InEnum, uint32 Bytes = 1)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FEnumProperty*& NewProp = FindOrAddProperty<FEnumProperty>(Class2Name::TTraitsEnumBase::GetFName(InEnum, Bytes));
			if (!NewProp)
				NewProp = NewProperty(InEnum, Bytes);
#else
			FEnumProperty* NewProp = NewProperty(InEnum, Bytes);
#endif
			return NewProp;
		}
	};

	template<typename T, uint32 Bits = sizeof(T) * 8, bool b = TypeTraits::TIsEnumByte<T>::value>
	struct TEnumProperty : TEnumPropertyBase
	{
		enum
		{
			value = (b ? TEnumPropertyBase::tag : 0)
		};
		using type = std::decay_t<T>;
	};
	template<typename T, uint32 Bits>
	struct TEnumProperty<T, Bits, true> : TEnumPropertyBase
	{
		enum
		{
			value = TEnumPropertyBase::tag
		};
		using type = std::underlying_type_t<typename TypeTraits::TIsEnumByte<std::decay_t<T>>::type>;
	};

	// struct
	struct TTraitsStructBase : EmptyValue
	{
		enum
		{
			tag = 3,
		};
		static FStructProperty* NewProperty(UScriptStruct* InStruct, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? InStruct->GetFName() : Override;
			return NewNativeProperty<FStructProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash, InStruct);
		}
		static FStructProperty* GetProperty(UScriptStruct* InStruct, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FStructProperty*& NewProp = FindOrAddProperty<FStructProperty>(Override.IsNone() ? InStruct->GetFName() : Override);
			if (!NewProp)
				NewProp = NewProperty(InStruct, Override);
#else
			FStructProperty* NewProp = NewProperty(InStruct, Override);
#endif
			return NewProp;
		}
	};
	template<typename T>
	struct TTraitsStruct : TTraitsStructBase
	{
		template<typename V>
		using HasStaticStructType = decltype(V::StaticStruct());
		template<typename V>
		using HasStaticStruct = TypeTraits::IsDetected<HasStaticStructType, V>;
		enum
		{
			value = (HasStaticStruct<T>::value || Class2Name::TBasicStructure<T>::value) ? TTraitsStructBase::tag : 0
		};
	};

	template<typename T>
	struct TTraitsBaseClassValue;

	struct TTraitsTemplateBase : EmptyValue
	{
		enum
		{
			object_related = 1,
			is_container = 0,
			tag = 4,
			value = tag,
		};
		enum : uint64
		{
			CPF_GMPMark = 0x8000000000000000ull,
		};
	};

	template<typename T, bool bExactType = false>
	struct TTraitsTemplate : EmptyValue
	{
		enum
		{
			object_related = TTraitsStruct<T>::value,
			is_container = 0,
		};
	};

	// TSubclassOf
	struct TTraitsSubclassOfBase : TTraitsTemplateBase
	{
		static FClassProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TSubclassOf<UObject>>::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FClassProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass, UObject::StaticClass());
		};
		static FClassProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FClassProperty*& NewProp = FindOrAddProperty<FClassProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TSubclassOf<UObject>>::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FClassProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		};
	};

	template<typename T, bool bExactType>
	struct TTraitsTemplate<TSubclassOf<T>, bExactType> : TTraitsSubclassOfBase
	{
		static FClassProperty* NewProperty() { return TTraitsSubclassOfBase::NewProperty(std::decay_t<T>::StaticClass(), TClass2Name<TSubclassOf<T>, bExactType>::GetFName()); }
		static FClassProperty* GetProperty()
		{
			static FClassProperty* NewProp = TTraitsSubclassOfBase::GetProperty(std::decay_t<T>::StaticClass(), TClass2Name<TSubclassOf<T>, bExactType>::GetFName());
			return NewProp;
		}
	};

	template<typename T>
	struct TTraitsBaseClassValue;

	// UClass
	template<>
	struct TTraitsBaseClassValue<UClass> : TTraitsTemplate<TSubclassOf<UObject>>
	{
		enum
		{
			tag = 5,
		};
	};

	// UObject
	struct TTraitsObjectBase
	{
		enum
		{
			tag = 6,
		};
		static FObjectProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? InClass->GetFName() : Override;
			return NewNativeProperty<FObjectProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash, InClass);
		};
		static FObjectProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FObjectProperty*& NewProp = FindOrAddProperty<FObjectProperty>(Override.IsNone() ? InClass->GetFName() : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FObjectProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};

	template<typename T, bool bExactType = false>
	struct TTraitsObject : TTraitsObjectBase
	{
		using ClassType = std::conditional_t<bExactType, std::decay_t<std::remove_pointer_t<T>>, UObject>;
		static FProperty* NewProperty() { return TTraitsObjectBase::NewProperty(StaticClass<ClassType>(), TClass2Name<ClassType, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsObjectBase::GetProperty(StaticClass<ClassType>(), TClass2Name<ClassType, bExactType>::GetFName());
			return NewProp;
		}
	};

	template<>
	struct TTraitsBaseClassValue<UObject> : TTraitsObjectBase
	{
	};
	template<typename T, typename B>
	struct TTraitsBaseClass
	{
		enum
		{
			tag = TTraitsBaseClassValue<B>::tag,
			value = (std::is_base_of<B, std::remove_pointer_t<T>>::value) ? tag : 0,
		};
	};

	struct TTraitsCustomStructBase
	{
		enum
		{
			tag = 7,
			value = 0,
		};
		static FProperty* GetProperty() { return nullptr; }
		static FProperty* NewProperty() { return nullptr; }
	};

	struct TTraitsContainerBase : TTraitsTemplateBase
	{
		enum
		{
			is_container = 1,
		};
		static FArrayProperty* NewArrayProperty(FProperty* InProp, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateBase::GetTArrayName(*Reflection::GetPropertyName(InProp).ToString()) : Override;
			FArrayProperty* NewProp = NewNativeProperty<FArrayProperty>(GMPPropFullName(TypeName),
																		CPF_HasGetValueTypeHash
#if UE_4_25_OR_LATER
																		,
																		EArrayPropertyFlags::None
#endif
			);
			NewProp->AddCppProperty(InProp);
			return NewProp;
		}
		static FArrayProperty* GetArrayProperty(FProperty* InProp, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FArrayProperty*& NewProp = FindOrAddProperty<FArrayProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateBase::GetTArrayName(*Reflection::GetPropertyName(InProp).ToString()) : Override);
			if (!NewProp)
				NewProp = NewArrayProperty(InProp, Override);
#else
			FArrayProperty* NewProp = NewArrayProperty(InProp, Override);
#endif
			return NewProp;
		}
		static FSetProperty* NewSetProperty(FProperty* InProp, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateBase::GetTSetName(*Reflection::GetPropertyName(InProp).ToString()) : Override;

			FSetProperty* NewProp = NewNativeProperty<FSetProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash);
			NewProp->AddCppProperty(InProp);
			return NewProp;
		}

		static FSetProperty* GetSetProperty(FProperty* InProp, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FSetProperty*& NewProp = FindOrAddProperty<FSetProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateBase::GetTSetName(*Reflection::GetPropertyName(InProp).ToString()) : Override);
			if (!NewProp)
				NewProp = NewSetProperty(InProp, Override);
#else
			FSetProperty* NewProp = NewSetProperty(InProp, Override);
#endif
			return NewProp;
		}
		static FMapProperty* NewMapProperty(FProperty* InKeyProp, FProperty* InValueProp, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateBase::GetTMapName(*Reflection::GetPropertyName(InKeyProp).ToString(), *Reflection::GetPropertyName(InValueProp).ToString()) : Override;
			FMapProperty* NewProp = NewNativeProperty<FMapProperty>(GMPPropFullName(TypeName),
																	CPF_HasGetValueTypeHash
#if UE_4_25_OR_LATER
																	,
																	EMapPropertyFlags::None
#endif
			);
			NewProp->AddCppProperty(InKeyProp);
			NewProp->AddCppProperty(InValueProp);
			return NewProp;
		}
		static FMapProperty* GetMapProperty(FProperty* InKeyProp, FProperty* InValueProp, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FMapProperty*& NewProp =
				FindOrAddProperty<FMapProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateBase::GetTMapName(*Reflection::GetPropertyName(InKeyProp).ToString(), *Reflection::GetPropertyName(InValueProp).ToString()) : Override);
			if (!NewProp)
				NewProp = NewMapProperty(InKeyProp, InValueProp, Override);
#else
			FMapProperty* NewProp = NewMapProperty(InKeyProp, InValueProp, Override);
#endif
			return NewProp;
		}
	};

	template<typename T>
	struct TTraitsCustomStruct : TTraitsCustomStructBase
	{
	};

// return null property pointer
#define GMP_NULL_PROPERTY_OF(T)                                                         \
	namespace GMP                                                                       \
	{                                                                                   \
		namespace Class2Prop                                                            \
		{                                                                               \
			template<>                                                                  \
			struct TTraitsCustomStruct<T> : TTraitsCustomStructBase                     \
			{                                                                           \
				enum                                                                    \
				{                                                                       \
					value = std::is_class<T>::value ? TTraitsCustomStructBase::tag : 0, \
				};                                                                      \
			};                                                                          \
		}                                                                               \
	}

	namespace Inner
	{
		// none object relatived
		template<class T>
		struct TNotObjectRefRelated
		{
			using Type = std::decay_t<T>;
			enum
			{
				value = !std::is_pointer<T>::value && (!!TBasePropertyTraits<T>::value || !!TEnumProperty<T>::value || !!TTraitsStruct<T>::value /* || !!TTraitsCustomStruct<T>::value*/),

			};
		};
	}  // namespace Inner

	// clang-format off
	template<typename T, bool bExactType = false, int Tag = TypeTraits::TDisjunction <
		TBasePropertyTraits<T>
		, TEnumProperty<T>
		, TTraitsBaseClass<T, UClass>
		, TTraitsBaseClass<T, UObject>
		, TTraitsTemplate<T>
		, TTraitsStruct<T> // struct must be last
		, TTraitsCustomStruct<T>
		>::value>
	struct TClass2Prop
	{
	// 	static FProperty* GetProperty() { return nullptr; }
	// 	static FProperty* NewProperty() { return nullptr; }
	};
	// clang-format on

#if UE_5_00_OR_LATER
	// TObjectPtr
	struct TTraitsObjectPtrBase : TTraitsTemplateBase
	{
		static FObjectPtrProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TWeakObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FObjectPtrProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass);
		}
		static FObjectPtrProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FObjectPtrProperty*& NewProp = FindOrAddProperty<FObjectPtrProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TWeakObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FObjectPtrProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TObjectPtr<T>, bExactType> : TTraitsObjectPtrBase
	{
		static FProperty* NewProperty() { return TTraitsObjectPtrBase::NewProperty(std::decay_t<T>::StaticClass(), TClass2Name<TObjectPtr<T>, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsObjectPtrBase::GetProperty(std::decay_t<T>::StaticClass(), TClass2Name<TObjectPtr<T>, bExactType>::GetFName());
			return NewProp;
		}
	};
	template<bool bExactType>
	struct TTraitsTemplate<FObjectPtr, bExactType> : TTraitsTemplate<TObjectPtr<UObject>>
	{
	};
#endif

	// TArray
	template<typename InElementType, typename InAllocator, bool bExactType>
	struct TTraitsTemplate<TArray<InElementType, InAllocator>, bExactType> : TTraitsContainerBase
	{
		static_assert(IsSameV<InAllocator, FDefaultAllocator>, "only support FDefaultAllocator");
		static_assert(!TTraitsTemplate<InElementType>::is_container, "not support nested container");
		enum
		{
			object_related = !Inner::TNotObjectRefRelated<InElementType>::value,
		};
		static FProperty* NewProperty() { return TTraitsContainerBase::NewArrayProperty(TClass2Prop<std::remove_cv_t<InElementType>, bExactType>::NewProperty(), TClass2Name<TArray<InElementType, InAllocator>, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static auto NewProp = TTraitsContainerBase::GetArrayProperty(TClass2Prop<std::remove_cv_t<InElementType>, bExactType>::GetProperty(), TClass2Name<TArray<InElementType, InAllocator>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// TSet
	template<typename InElementType, typename KeyFuncs, typename InAllocator, bool bExactType>
	struct TTraitsTemplate<TSet<InElementType, KeyFuncs, InAllocator>, bExactType> : TTraitsContainerBase
	{
		static_assert(IsSameV<KeyFuncs, DefaultKeyFuncs<InElementType>>, "only support DefaultKeyFuncs");
		static_assert(IsSameV<InAllocator, FDefaultSetAllocator>, "only support FDefaultSetAllocator");
		static_assert(!TTraitsTemplate<InElementType>::is_container, "not support nested container");
		enum
		{
			object_related = !Inner::TNotObjectRefRelated<InElementType>::value,
		};
		static FProperty* NewProperty()
		{
			return TTraitsContainerBase::NewSetProperty(TClass2Prop<std::remove_cv_t<InElementType>, bExactType>::NewProperty(), TClass2Name<TSet<InElementType, KeyFuncs, InAllocator>, bExactType>::GetFName());
		}
		static FProperty* GetProperty()
		{
			static auto NewProp = TTraitsContainerBase::GetSetProperty(TClass2Prop<std::remove_cv_t<InElementType>, bExactType>::GetProperty(), TClass2Name<TSet<InElementType, KeyFuncs, InAllocator>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// TMap
	template<typename InKeyType, typename InValueType, typename SetAllocator, typename KeyFuncs, bool bExactType>
	struct TTraitsTemplate<TMap<InKeyType, InValueType, SetAllocator, KeyFuncs>, bExactType> : TTraitsContainerBase
	{
		static_assert(IsSameV<SetAllocator, FDefaultSetAllocator>, "only support FDefaultSetAllocator");
		static_assert(IsSameV<KeyFuncs, TDefaultMapHashableKeyFuncs<InKeyType, InValueType, false>>, "only support TDefaultMapHashableKeyFuncs");
		static_assert(!TTraitsTemplate<InKeyType>::is_container && !TTraitsTemplate<InValueType>::is_container, "not support nested container");
		enum
		{
			object_related = !Inner::TNotObjectRefRelated<InKeyType>::value || !Inner::TNotObjectRefRelated<InValueType>::value,
		};
		static FProperty* NewProperty()
		{
			return TTraitsContainerBase::NewMapProperty(TClass2Prop<std::remove_cv_t<InKeyType>, bExactType>::NewProperty(),
														TClass2Prop<std::remove_cv_t<InValueType>, bExactType>::NewProperty(),
														TClass2Name<TMap<InKeyType, InValueType, SetAllocator, KeyFuncs>, bExactType>::GetFName());
		}
		static FProperty* GetProperty()
		{
			static auto NewProp = TTraitsContainerBase::GetMapProperty(TClass2Prop<std::remove_cv_t<InKeyType>, bExactType>::GetProperty(),
																	   TClass2Prop<std::remove_cv_t<InValueType>, bExactType>::GetProperty(),
																	   TClass2Name<TMap<InKeyType, InValueType, SetAllocator, KeyFuncs>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// WeakObjectPtr
	struct TTraitsWeakObjectBase : TTraitsTemplateBase
	{
		static FWeakObjectProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TWeakObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FWeakObjectProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass);
		}
		static FWeakObjectProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FWeakObjectProperty*& NewProp = FindOrAddProperty<FWeakObjectProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TWeakObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FWeakObjectProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TWeakObjectPtr<T>, bExactType> : TTraitsWeakObjectBase
	{
		static FProperty* NewProperty() { return TTraitsWeakObjectBase::NewProperty(std::decay_t<T>::StaticClass(), TClass2Name<TWeakObjectPtr<T>, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsWeakObjectBase::GetProperty(std::decay_t<T>::StaticClass(), TClass2Name<TWeakObjectPtr<T>, bExactType>::GetFName());
			return NewProp;
		}
	};
	template<bool bExactType>
	struct TTraitsTemplate<FWeakObjectPtr, bExactType> : TTraitsTemplate<TWeakObjectPtr<UObject>>
	{
	};

	// SoftClassPtr
	struct TTraitsSoftClassBase : TTraitsTemplateBase
	{
		static FSoftClassProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TSoftClassPtr<UObject>>::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FSoftClassProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass);
		}

		static FSoftClassProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FSoftClassProperty*& NewProp = FindOrAddProperty<FSoftClassProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TSoftClassPtr<UObject>>::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FSoftClassProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TSoftClassPtr<T>, bExactType> : TTraitsSoftClassBase
	{
		static FProperty* NewProperty() { return TTraitsSoftClassBase::NewProperty(std::decay_t<T>::StaticClass(), TClass2Name<TSoftClassPtr<T>, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsSoftClassBase::GetProperty(std::decay_t<T>::StaticClass(), TClass2Name<TSoftClassPtr<T>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// SoftObjectPtr
	struct TTraitsSoftObjectBase : TTraitsTemplateBase
	{
		static FSoftObjectProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TSoftObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FSoftObjectProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass);
		}

		static FSoftObjectProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FSoftObjectProperty*& NewProp = FindOrAddProperty<FSoftObjectProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TSoftObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FSoftObjectProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};

	template<typename T, bool bExactType>
	struct TTraitsTemplate<TSoftObjectPtr<T>, bExactType> : TTraitsSoftObjectBase
	{
		static FProperty* NewProperty() { return TTraitsSoftObjectBase::NewProperty(std::decay_t<T>::StaticClass(), TClass2Name<TSoftObjectPtr<T>, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsSoftObjectBase::GetProperty(std::decay_t<T>::StaticClass(), TClass2Name<TSoftObjectPtr<T>, bExactType>::GetFName());
			return NewProp;
		}
	};
	template<bool bExactType>
	struct TTraitsTemplate<FSoftObjectPtr, bExactType> : TTraitsTemplate<TSoftObjectPtr<UObject>>
	{
	};

	// LazyObjectPtr
	struct TTraitsLazyObjectBase : TTraitsTemplateBase
	{
		static FLazyObjectProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TLazyObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FLazyObjectProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass);
		}

		static FLazyObjectProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FLazyObjectProperty*& NewProp = FindOrAddProperty<FLazyObjectProperty>(Override.IsNone() ? Class2Name::TTraitsTemplateUtils<TLazyObjectPtr<UObject>>::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FLazyObjectProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TLazyObjectPtr<T>, bExactType> : TTraitsLazyObjectBase
	{
		static FProperty* NewProperty() { return TTraitsLazyObjectBase::NewProperty(std::decay_t<T>::StaticClass(), TClass2Name<TLazyObjectPtr<T>, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsLazyObjectBase::GetProperty(std::decay_t<T>::StaticClass(), TClass2Name<TLazyObjectPtr<T>, bExactType>::GetFName());
			return NewProp;
		}
	};
	template<bool bExactType>
	struct TTraitsTemplate<FLazyObjectPtr, bExactType> : TTraitsTemplate<TLazyObjectPtr<UObject>>
	{
	};

	// ScriptDelegate
	struct TTraitsDelegateBase : TTraitsTemplateBase
	{
		static FDelegateProperty* NewProperty(UFunction* InFunc, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? TClass2Name<TScriptDelegate<FWeakObjectPtr>>::GetFName() : Override;
			return NewNativeProperty<FDelegateProperty>(GMPPropFullName(TypeName), CPF_GMPMark, InFunc);
		}

		static FDelegateProperty* GetProperty(UFunction* InFunc, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FDelegateProperty*& NewProp = FindOrAddProperty<FDelegateProperty>(Override.IsNone() ? TClass2Name<TScriptDelegate<FWeakObjectPtr>>::GetFName() : Override);
			if (!NewProp)
				NewProp = NewProperty(InFunc, Override);
#else
			FDelegateProperty* NewProp = NewProperty(InFunc, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TScriptDelegate<T>, bExactType>
	{
		static FDelegateProperty* NewProperty() { return TTraitsDelegateBase::NewProperty(nullptr, TClass2Name<TScriptDelegate<T>, bExactType>::GetFName()); }
		static FDelegateProperty* GetProperty()
		{
			static FDelegateProperty* NewProp = TTraitsDelegateBase::GetProperty(nullptr, TClass2Name<TScriptDelegate<T>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// MultiScriptDelegate
	struct TTraitsMulticastDelegateBase : TTraitsTemplateBase
	{
		static FMulticastDelegateProperty* NewProperty(UFunction* InFunc, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? TClass2Name<TScriptDelegate<FWeakObjectPtr>>::GetFName() : Override;
			return NewNativeProperty<FMulticastDelegateProperty>(GMPPropFullName(TypeName), CPF_GMPMark, InFunc);
		}

		static FMulticastDelegateProperty* GetProperty(UFunction* InFunc, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FMulticastDelegateProperty*& NewProp = FindOrAddProperty<FMulticastDelegateProperty>(Override.IsNone() ? TClass2Name<TMulticastScriptDelegate<FWeakObjectPtr>>::GetFName() : Override);
			if (!NewProp)
				NewProp = NewProperty(InFunc, Override);
#else
			FMulticastDelegateProperty* NewProp = NewProperty(InFunc, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TMulticastScriptDelegate<T>, bExactType>
	{
		static FMulticastDelegateProperty* NewProperty() { return TTraitsLazyObjectBase::NewProperty(nullptr, TClass2Name<TMulticastScriptDelegate<T>, bExactType>::GetFName()); }
		static FMulticastDelegateProperty* GetProperty()
		{
			static FMulticastDelegateProperty* NewProp = TTraitsLazyObjectBase::GetProperty(nullptr, TClass2Name<TMulticastScriptDelegate<T>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// FScriptInterface
	template<bool bExactType>
	struct TTraitsTemplate<FScriptInterface, bExactType> : TTraitsTemplateBase
	{
		static FInterfaceProperty* NewProperty()
		{
			FName TypeName = TClass2Name<FScriptInterface, bExactType>::GetFName();
			return NewNativeProperty<FInterfaceProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, UInterface::StaticClass());
		}

		static FInterfaceProperty* GetProperty()
		{
#if WITH_EDITOR
			FInterfaceProperty*& NewProp = FindOrAddProperty<FInterfaceProperty>(TClass2Name<FScriptInterface, bExactType>::GetFName());
			if (!NewProp)
				NewProp = NewProperty();
#else
			static FInterfaceProperty* NewProp = NewProperty();
#endif
			return NewProp;
		}
	};

	// TScriptInterface
	struct TTraitsScriptIncBase : TTraitsTemplateBase
	{
		static FInterfaceProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsScriptIncBase::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FInterfaceProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper, InClass);
		}
		static FInterfaceProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FInterfaceProperty*& NewProp = FindOrAddProperty<FInterfaceProperty>(Override.IsNone() ? Class2Name::TTraitsScriptIncBase::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FInterfaceProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<TScriptInterface<T>, bExactType> : TTraitsScriptIncBase
	{
		static FInterfaceProperty* NewProperty() { return TTraitsScriptIncBase::NewProperty(std::decay_t<T>::UClassType::StaticClass(), TClass2Name<TScriptInterface<T>, bExactType>::GetFName()); }
		static FInterfaceProperty* GetProperty()
		{
			static FInterfaceProperty* NewProp = TTraitsScriptIncBase::GetProperty(std::decay_t<T>::UClassType::StaticClass(), TClass2Name<TScriptInterface<T>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// TNativeInterface
	struct TTraitsNativeIncBase : TTraitsTemplateBase
	{
		enum : uint64
		{
			CPF_GMPMark = 0x8000000000000000ull,
		};
		static FInterfaceProperty* NewProperty(UClass* InClass, FName Override = NAME_None)
		{
			FName TypeName = Override.IsNone() ? Class2Name::TTraitsNativeIncBase::GetFName(*InClass->GetName()) : Override;
			return NewNativeProperty<FInterfaceProperty>(GMPPropFullName(TypeName), CPF_HasGetValueTypeHash | CPF_UObjectWrapper | CPF_GMPMark, InClass);
		}
		static FInterfaceProperty* GetProperty(UClass* InClass, FName Override = NAME_None)
		{
#if GMP_WITH_FINDORADD_UNIQUE_PROPERTY
			FInterfaceProperty*& NewProp = FindOrAddProperty<FInterfaceProperty>(Override.IsNone() ? Class2Name::TTraitsNativeIncBase::GetFName(*InClass->GetName()) : Override);
			if (!NewProp)
				NewProp = NewProperty(InClass, Override);
#else
			FInterfaceProperty* NewProp = NewProperty(InClass, Override);
#endif
			return NewProp;
		}
	};
	template<typename T, bool bExactType>
	struct TTraitsTemplate<Z_GMP_NATIVE_INC_NAME<T>, bExactType> : TTraitsNativeIncBase
	{
		static FInterfaceProperty* NewProperty() { return TTraitsNativeIncBase::NewProperty(std::decay_t<T>::UClassType::StaticClass(), TClass2Name<Z_GMP_NATIVE_INC_NAME<T>, bExactType>::GetFName()); }
		static FInterfaceProperty* GetProperty()
		{
			static FInterfaceProperty* NewProp = TTraitsNativeIncBase::GetProperty(std::decay_t<T>::UClassType::StaticClass(), TClass2Name<Z_GMP_NATIVE_INC_NAME<T>, bExactType>::GetFName());
			return NewProp;
		}
	};

	// BaseProperty
	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TBasePropertyTraitsBase::tag> : TBasePropertyTraits<T>
	{
	};

	// Enum
	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TEnumPropertyBase::tag>
	{
		static FProperty* NewProperty() { return TBasePropertyTraits<typename TEnumProperty<T>::type>::NewProperty(); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TBasePropertyTraits<typename TEnumProperty<T>::type>::GetProperty();
			return NewProp;
		}
	};

	// Struct
	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TTraitsStructBase::tag>
	{
		static FProperty* NewProperty() { return TTraitsStructBase::NewProperty(TypeTraits::StaticStruct<T>(), TClass2Name<T, bExactType>::GetFName()); }
		static FProperty* GetProperty()
		{
			static FProperty* NewProp = TTraitsStructBase::GetProperty(TypeTraits::StaticStruct<T>(), TClass2Name<T, bExactType>::GetFName());
			return NewProp;
		}
	};

	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TTraitsBaseClassValue<UClass>::tag> : TTraitsTemplate<TSubclassOf<UObject>>
	{
	};
	static_assert(std::is_base_of<TTraitsTemplate<TSubclassOf<UObject>>, TClass2Prop<UClass>>::value, "err");

	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TTraitsBaseClassValue<UObject>::tag> : TTraitsObject<T, bExactType>
	{
	};
	static_assert(std::is_base_of<TTraitsObject<UObject>, TClass2Prop<UObject>>::value, "err");

	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TTraitsTemplateBase::tag> : TTraitsTemplate<std::decay_t<T>, bExactType>
	{
	};

	template<typename T, bool bExactType>
	struct TClass2Prop<T, bExactType, TTraitsCustomStructBase::tag> : TTraitsCustomStruct<T>
	{
	};

	template<class T>
	struct TIsObjectRelated
	{
		using Type = std::decay_t<T>;
		enum
		{
			value = std::is_pointer<T>::value || TTraitsBaseClass<Type, UObject>::value != 0 || (TTraitsStruct<Type>::value == 0 && TTraitsTemplate<Type>::object_related != 0),

		};
	};

	//////////////////////////////////////////////////////////////////////////
	template<typename... TArgs>
	struct TPropertiesTraits
	{
		static const TArray<FProperty*>& GetProperties()
		{
			static TArray<FProperty*> Properties{static_cast<FProperty*>(TClass2Prop<std::decay_t<std::remove_cv_t<std::remove_reference_t<TArgs>>>>::GetProperty())...};
			ensure(Properties.Num() == sizeof...(TArgs));
			return Properties;
		}
		static const TArray<FName>& GetNames()
		{
			static TArray<FName> Names{TClass2Name<TArgs>::GetFName()...};
			ensure(Names.Num() == sizeof...(TArgs));
			return Names;
		}
		enum
		{
			object_related = TypeTraits::TDisjunction<TIsObjectRelated<TArgs>...>::value,
		};
	};

	template<typename TMemFunc>
	struct TMemFuncPropertiesTraits
	{
		template<typename R, typename FF, typename... TArgs>
		static auto GetPropertiesTraits(R (FF::*)(TArgs...)) -> TPropertiesTraits<std::decay_t<TArgs>...>;
		template<typename R, typename FF, typename... TArgs>
		static auto GetPropertiesTraits(R (FF::*)(TArgs...) const) -> TPropertiesTraits<std::decay_t<TArgs>...>;
		using ResultTraits = decltype(GetPropertiesTraits(std::declval<TMemFunc>()));
	};

	template<typename T, typename = void>
	struct TFunctionPropertiesTraits;
	template<typename T>
	struct TFunctionPropertiesTraits<T, VoidType<decltype(&std::remove_cv_t<std::remove_reference_t<T>>::operator())>> : TMemFuncPropertiesTraits<decltype(&std::remove_cv_t<std::remove_reference_t<T>>::operator())>
	{
	};
	template<typename T>
	struct TFunctionPropertiesTraits<T, std::enable_if_t<std::is_member_function_pointer<T>::value>> : TMemFuncPropertiesTraits<T>
	{
	};
}  // namespace Class2Prop
template<typename T, bool bExactType = false>
using TClass2Prop = Class2Prop::TClass2Prop<std::remove_cv_t<std::remove_reference_t<T>>, bExactType>;
}  // namespace GMP

#endif  // !deinfed(GMP_CLASS_TO_PROP_GUARD_H)
