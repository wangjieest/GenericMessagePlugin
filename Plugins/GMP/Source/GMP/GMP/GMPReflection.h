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

#ifndef GMP_USE_NEW_PROP_FROM_STRING
#define GMP_USE_NEW_PROP_FROM_STRING 1
#endif  // GMP_USE_NEW_PROP_FROM_STRING

#if UE_4_22_OR_LATER
using EGMPPropertyClass = UECodeGen_Private::EPropertyGenFlags;
#else
using EGMPPropertyClass = UECodeGen_Private::EPropertyClass;
#endif

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
	GMP_API const struct FCustomVersion& DefaultVersion();
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
	GMP_API FName GetPinPropertyName(bool bExactType, const struct FEdGraphPinType& PinType, EGMPPropertyClass* PropertyType = nullptr, EGMPPropertyClass* ElemPropType = nullptr, EGMPPropertyClass* KeyPropType = nullptr);
	FORCEINLINE FName GetPinPropertyName(const struct FEdGraphPinType& PinType, EGMPPropertyClass* PropertyType = nullptr, EGMPPropertyClass* ElemPropType = nullptr, EGMPPropertyClass* KeyPropType = nullptr)
	{
		return GetPinPropertyName(true, PinType, PropertyType, ElemPropType, KeyPropType);
	}

	// Name --> Pin
	GMP_API bool PinTypeFromString(FString TypeString, struct FEdGraphPinType& OutPinType, bool bTemplateSub = false, bool bContainerSub = false);
	GMP_API FString GetDefaultValueOnType(const struct FEdGraphPinType& PinType);

	template<typename T, bool bExactType = true>
	inline FName GetPropertyName()
	{
		return TClass2Name<std::remove_pointer_t<T>, bExactType>::GetFName();
	}

	// Property --> Name
	GMP_API FName GetPropertyName(const FProperty* Property, bool bExactType = true);
	GMP_API FName GetPropertyName(const FProperty* Property, EGMPPropertyClass PropertyType, EGMPPropertyClass ElemPropType = PropertyTypeInvalid, EGMPPropertyClass KeyPropType = PropertyTypeInvalid);

	GMP_API bool EqualPropertyName(const FProperty* Property, FName TypeName, bool bExactType = true);
	template<typename T, bool bExactType = true>
	inline bool EqualPropertyType(const FProperty* Property)
	{
		checkSlow(Property);
		return EqualPropertyName(Property, GetPropertyName<T, bExactType>(), bExactType);
	}

	inline bool EqualPropertyType(const FProperty* Lhs, const FProperty* Rhs, bool bExactType = true)
	{
		checkSlow(Lhs && Rhs);
		return GetPropertyName(Lhs, bExactType) == GetPropertyName(Rhs, bExactType);
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
			checkSlow(IsValid(StructType) && StructType->IsNative());
			return StructType;
		}
	};

	template<char... Cs>
	struct TDynamicStruct<ITS::list<Cs...>, UClass>
	{
		static auto GetStruct()
		{
			static auto ClassType = DynamicClass(ITS::list<Cs...>::Get());
			checkSlow(IsValid(ClassType) && ClassType->IsNative());
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

template<uint8 N = 4>
struct TWorldFlag
{
protected:
	template<typename F>
	bool TestImpl(const UObject* WorldContextObj, const F& f)
	{
		UWorld* World = WorldContextObj ? WorldContextObj->GetWorld() : nullptr;
		check(!World || IsValid(World));
		for (int32 i = 0; i < Storage.Num(); ++i)
		{
			auto WeakWorld = Storage[i];
			if (!WeakWorld.IsStale(true))
			{
				if (World == WeakWorld.Get())
					return f(true, World);
			}
			else
			{
				Storage.RemoveAtSwap(i);
				--i;
			}
		}
		return f(false, World);
	}

public:
	bool Test(const UObject* WorldContextObj, bool bAdd = false)
	{
		return TestImpl(WorldContextObj, [bAdd, this](bool b, UWorld* World) {
			if (!b)
				Storage.Add(MakeWeakObjectPtr(World));
			return b;
		});
	}

	bool TrueOnWorldFisrtCall(const UObject* WorldContextObj)
	{
		return TestImpl(WorldContextObj, [&](bool b, UWorld* World) {
			if (!b)
			{
				Storage.Add(MakeWeakObjectPtr(World));
				return true;
			}
			return false;
		});
	}

protected:
	TArray<TWeakObjectPtr<UWorld>, TInlineAllocator<N>> Storage;
};

template<typename F>
bool FORCENOINLINE UE_DEBUG_SECTION TrueOnWorldFisrtCall(const UObject* Obj, const F& f)
{
	static TWorldFlag<> Flag;
	return Flag.TrueOnWorldFisrtCall(Obj) && f();
}
}  // namespace GMP

#if WITH_EDITOR
#if UE_5_00_OR_LATER
#define Z_GMP_FMT_DEBUG(A, C, F, ...)                                                                                                         \
	[A]() FORCENOINLINE UE_DEBUG_SECTION {                                                                                                    \
		FDebug::OptionallyLogFormattedEnsureMessageReturningFalse(true, #C, __FILE__, __LINE__, PLATFORM_RETURN_ADDRESS(), F, ##__VA_ARGS__); \
		if (!FPlatformMisc::IsDebuggerPresent())                                                                                              \
		{                                                                                                                                     \
			FPlatformMisc::PromptForRemoteDebugging(true);                                                                                    \
			return false;                                                                                                                     \
		}                                                                                                                                     \
		return true;                                                                                                                          \
	}
#elif UE_5_00_OR_LATER
#define Z_GMP_FMT_DEBUG(A, C, F, ...)                                                                                                                               \
	[A]() FORCENOINLINE UE_DEBUG_SECTION {                                                                                                                          \
		FDebug::OptionallyLogFormattedEnsureMessageReturningFalse(true, FDebug::FFailureInfo{#C, __FILE__, __LINE__, PLATFORM_RETURN_ADDRESS()}, F, ##__VA_ARGS__); \
		if (!FPlatformMisc::IsDebuggerPresent())                                                                                                                    \
		{                                                                                                                                                           \
			FPlatformMisc::PromptForRemoteDebugging(true);                                                                                                          \
			return false;                                                                                                                                           \
		}                                                                                                                                                           \
		return true;                                                                                                                                                \
	}
#else
#define Z_GMP_FMT_DEBUG(A, C, F, ...)                                                                              \
	[A]() FORCENOINLINE UE_DEBUG_SECTION {                                                                         \
		FDebug::OptionallyLogFormattedEnsureMessageReturningFalse(true, #C, __FILE__, __LINE__, F, ##__VA_ARGS__); \
		if (!FPlatformMisc::IsDebuggerPresent())                                                                   \
		{                                                                                                          \
			FPlatformMisc::PromptForRemoteDebugging(true);                                                         \
			return false;                                                                                          \
		}                                                                                                          \
		return true;                                                                                               \
	}
#endif
#define ensureWorld(W, C) (LIKELY(!!(C)) || (GMP::TrueOnWorldFisrtCall(W, Z_GMP_FMT_DEBUG(, C, TEXT(""))) && ([]() { PLATFORM_BREAK(); }(), false)))
#define ensureWorldMsgf(W, C, F, ...) (LIKELY(!!(C)) || (GMP::TrueOnWorldFisrtCall(W, Z_GMP_FMT_DEBUG(&, C, F, ##__VA_ARGS__)) && ([]() { PLATFORM_BREAK(); }(), false)))
#else
#define ensureWorld(W, C) ensure(C)
#define ensureWorldMsgf(W, C, F, ...) ensureMsgf(C, F, ##__VA_ARGS__)
#endif
#define ensureThis(C) ensureWorld(this, C)
#define ensureThisMsgf(C, F, ...) ensureWorldMsgf(this, C, F, ##__VA_ARGS__)
