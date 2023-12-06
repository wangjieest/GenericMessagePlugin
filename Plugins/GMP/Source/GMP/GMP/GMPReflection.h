//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

#include "Engine/World.h"
#include "GMPClass2Name.h"
#include "GMPTypeTraits.h"
#include "Internationalization/Text.h"
#include "Templates/SubclassOf.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "UnrealCompatibility.h"
#include "EdGraph/EdGraphPin.h"

#if UE_4_22_OR_LATER
using EGMPPropertyClass = UECodeGen_Private::EPropertyGenFlags;
#else
using EGMPPropertyClass = UECodeGen_Private::EPropertyClass;
#endif
struct FEdGraphPinType;
struct FCustomVersion;

namespace GMP
{
namespace CustomVersion
{
	enum Type
	{
		// Before any version changes were made
		BeforeCustomVersionWasAdded = 0,

		// GMPStartWithCustomVersion
		GMPStartWithCustomVersion,

		// -----<new versions can be added above this line>-------------------------------------------------
		VersionPlusOne,
		LatestVersion = VersionPlusOne - 1
	};

	GMP_API const FGuid& VersionGUID();
	GMP_API const FCustomVersion& DefaultVersion();
}  // namespace CustomVersion
}  // namespace GMP

namespace GMP
{
namespace Reflection
{
	inline FProperty* GetFunctionChildProperties(UFunction* InFunc)
	{
#if UE_4_25_OR_LATER
		return (FProperty*)InFunc->ChildProperties;
#else
		return (FProperty*)InFunc->Children;
#endif
	}
	const EGMPPropertyClass PropertyTypeInvalid = EGMPPropertyClass(255);
	// Pin --> Name
	GMP_API FName GetPinPropertyName(bool bExactType, const FEdGraphPinType& PinType, EGMPPropertyClass* PropertyType = nullptr, EGMPPropertyClass* ElemPropType = nullptr, EGMPPropertyClass* KeyPropType = nullptr);
	FORCEINLINE FName GetPinPropertyName(const FEdGraphPinType& PinType, EGMPPropertyClass* PropertyType = nullptr, EGMPPropertyClass* ElemPropType = nullptr, EGMPPropertyClass* KeyPropType = nullptr)
	{
		return GetPinPropertyName(true, PinType, PropertyType, ElemPropType, KeyPropType);
	}

	// Name --> Pin
	GMP_API bool PinTypeFromString(FString TypeString, FEdGraphPinType& OutPinType, bool bTemplateSub = false, bool bContainerSub = false);
	GMP_API FString GetDefaultValueOnType(const FEdGraphPinType& PinType);

	template<typename T, bool bExactType = true>
	FORCEINLINE FName GetPropertyName()
	{
		return TClass2Name<std::remove_pointer_t<T>, bExactType>::GetFName();
	}

	// Property --> Name
	GMP_API FName GetPropertyName(const FProperty* Property, bool bExactType = true);
	GMP_API FName GetPropertyName(const FProperty* Property, EGMPPropertyClass PropertyType, EGMPPropertyClass ElemPropType = PropertyTypeInvalid, EGMPPropertyClass KeyPropType = PropertyTypeInvalid);
	inline bool EqualPropertyPair(const FProperty* Lhs, const FProperty* Rhs, bool bExactType = true)
	{
		GMP_CHECK_SLOW(Lhs && Rhs);
		return GetPropertyName(Lhs, bExactType) == GetPropertyName(Rhs, bExactType);
	}

	enum EExactTestMask : uint32
	{
		TestExactly,
		TestEnum = 1 << 0,
		TestSkip = 1 << 1,
		TestDerived = 1 << 2,
		TestObjectPtr = 1 << 3,
		TestAll = 0xFFFFFFFF,
	};
	ENUM_CLASS_FLAGS(EExactTestMask);

	struct GMP_API FExactTestMaskScope
	{
		FExactTestMaskScope(EExactTestMask Lv = EExactTestMask::TestEnum);
		~FExactTestMaskScope();

	protected:
		EExactTestMask Old;
	};

	static bool TestEnumProp(const FProperty* Prop) { return Prop && (Prop->IsA<FEnumProperty>() || (CastField<FByteProperty>(Prop) && CastField<FByteProperty>(Prop)->GetIntPropertyEnum())); }
	template<typename T>
	static EExactTestMask EnumCompatibleFlag(const FProperty* Prop)
	{
		return ((std::is_same<T, uint8>::value || std::is_same<T, int32>::value) && TestEnumProp(Prop)) ? EExactTestMask::TestEnum : EExactTestMask::TestExactly;
	}

	GMP_API bool EqualPropertyName(const FProperty* Property, FName TypeName, EExactTestMask ExactLv);
	FORCEINLINE bool EqualPropertyName(const FProperty* Property, FName TypeName, bool bExactType = true)
	{
		return bExactType ? GetPropertyName(Property, bExactType) == TypeName : EqualPropertyName(Property, TypeName, EExactTestMask::TestAll);
	}

	template<typename T>
	FORCEINLINE bool EqualPropertyType(const FProperty* Property, EExactTestMask Lv)
	{
		GMP_CHECK_SLOW(Property);
		return EqualPropertyName(Property, GetPropertyName<T>(), Lv);
	}
	template<typename T, bool bExactType = true>
	FORCEINLINE bool EqualPropertyType(const FProperty* Property)
	{
		GMP_CHECK_SLOW(Property);
		return EqualPropertyName(Property, GetPropertyName<T, bExactType>(), bExactType);
	}

	// Name --> Property
	GMP_API bool PropertyFromString(FString TypeString, FProperty*& OutProp, bool bTemplateSub = false, bool bContainerSub = false, bool bNew = false);

#if GMP_USE_NEW_PROP_FROM_STRING
	GMP_API bool NewPropertyFromString(FString TypeString, FProperty*& OutProp, bool bTemplateSub = false, bool bContainerSub = false);
#endif

	// Name --> Type
	GMP_API UScriptStruct* DynamicStruct(const FString& StructName);
	GMP_API UClass* DynamicClass(const FString& ClassName);
	GMP_API UEnum* DynamicEnum(const FString& EnumName);

	template<typename T, typename U = UScriptStruct>
	struct TDynamicStruct;

	template<typename T>
	struct TDynamicStruct<T, UScriptStruct>
	{
		static auto GetStruct() { return StaticStruct<T>(); }
	};

	template<typename T>
	struct TDynamicStruct<T, UClass>
	{
		static auto GetStruct() { return StaticClass<T>(); }
	};

	template<char... Cs>
	struct TDynamicStruct<ITS::list<Cs...>, UScriptStruct>
	{
		static auto GetStruct()
		{
			static auto StructType = DynamicStruct(ITS::list<Cs...>::Get());
			GMP_CHECK_SLOW(IsValid(StructType) && StructType->IsNative());
			return StructType;
		}
	};

	template<char... Cs>
	struct TDynamicStruct<ITS::list<Cs...>, UClass>
	{
		static auto GetStruct()
		{
			static auto ClassType = DynamicClass(ITS::list<Cs...>::Get());
			GMP_CHECK_SLOW(IsValid(ClassType) && ClassType->IsNative());
			return ClassType;
		}
	};

	template<typename T>
	FORCEINLINE UScriptStruct* DynamicStruct()
	{
		return TDynamicStruct<T, UScriptStruct>::GetStruct();
	}
	template<typename T>
	FORCEINLINE UClass* DynamicClass()
	{
		return TDynamicStruct<T, UClass>::GetStruct();
	}

	GMP_API uint32 IsInterger(FName InTypeName);
	GMP_API UEnum* FindEnum(FString InTypeName, bool& bAsByte);

	enum EContainerType
	{
		None,
		Struct,
		Array,
		Set,
		// MapKey,
		// MapValue,
	};
	GMP_API const FStructProperty* PropertyContainsStruct(const FProperty* InProperty, EContainerType* OutType = nullptr);

	GMP_API bool PropertyContainsScriptStruct(const FProperty* Prop, UScriptStruct* StructType);
	template<typename T>
	FORCEINLINE bool PropertyContainsScriptStruct(const FProperty* Prop)
	{
		return PropertyContainsScriptStruct(Prop, DynamicStruct<T>());
	}
}  // namespace Reflection

}  // namespace GMP
