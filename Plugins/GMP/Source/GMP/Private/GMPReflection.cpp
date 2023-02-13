//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPReflection.h"

#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/ObjectLibrary.h"
#include "Engine/UserDefinedStruct.h"
#include "GMPClass2Name.h"
#include "GMPClass2Prop.h"
#include "GMPRpcProxy.h"
#include "GMPStruct.h"
#include "Internationalization/Regex.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "UObject/Interface.h"
#include "UObject/ObjectKey.h"
#include "UObject/TextProperty.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "UnrealCompatibility.h"

namespace GMP
{
namespace CustomVersion
{
	const FGuid GMPCustomVersionGUID{0x3CF6DC6C, 0x2D214B99, 0xBB320FE0, 0xCBD03B0D};
	const FGuid& VersionGUID() { return GMPCustomVersionGUID; }

	const TCHAR GMPCustomVersionDesc[] = TEXT("GMPCustomVersion");
	const FCustomVersion& DefaultVersion()
	{
		static auto GMPVer = FCustomVersion(GMPCustomVersionGUID, LatestVersion, GMPCustomVersionDesc);
		return GMPVer;
	}

	// Current versions base on source code compiling
	static TArray<FGuid, TInlineAllocator<64>> MyGMPCustomVersions;
	class FGMPCustomVersionRegistration : public FCustomVersionRegistration
	{
	public:
		/** @param InFriendlyName must be a string literal */
		template<int N>
		FGMPCustomVersionRegistration(FGuid InKey, int32 Version, const TCHAR (&InFriendlyName)[N], CustomVersionValidatorFunc InValidatorFunc = nullptr)
			: FCustomVersionRegistration(InKey, Version, InFriendlyName, InValidatorFunc)
		{
			RecordGMPVersion(InKey);
		}

	private:
		static void RecordGMPVersion(FGuid Key) { MyGMPCustomVersions.Add(Key); }
	};

	FGMPCustomVersionRegistration GGMPCustomVersionRegistration(GMPCustomVersionGUID, LatestVersion, GMPCustomVersionDesc);
}  // namespace CustomVersion
}  // namespace GMP

#if defined(GENERICSTORAGES_API)
#include "GenericSingletons.h"
#else
namespace GMP
{
namespace Reflection
{
	static UObject* DynamicReflectionImpl(const FString& TypeName, UClass* TypeClass)
	{
		TypeClass = TypeClass ? TypeClass : UObject::StaticClass();
		bool bIsValidName = true;
		FString FailureReason;
		if (TypeName.Contains(TEXT(" ")))
		{
			FailureReason = FString::Printf(TEXT("contains a space."));
			bIsValidName = false;
		}
		else if (!FPackageName::IsShortPackageName(TypeName))
		{
			if (TypeName.Contains(TEXT(".")))
			{
				FString PackageName;
				FString ObjectName;
				TypeName.Split(TEXT("."), &PackageName, &ObjectName);

				const bool bIncludeReadOnlyRoots = true;
				FText Reason;
				if (!FPackageName::IsValidLongPackageName(PackageName, bIncludeReadOnlyRoots, &Reason))
				{
					FailureReason = Reason.ToString();
					bIsValidName = false;
				}
			}
			else
			{
				FailureReason = TEXT("names with a path must contain a dot. (i.e /Script/Engine.StaticMeshActor)");
				bIsValidName = false;
			}
		}

		UObject* NewReflection = nullptr;
		if (bIsValidName)
		{
			UObject* ClassPackage = ANY_PACKAGE_COMPATIABLE;
			if (FPackageName::IsShortPackageName(TypeName))
			{
				if (TypeClass->IsChildOf(UEnum::StaticClass()))
					NewReflection = StaticFindObject(TypeClass, ClassPackage, *TypeName.Mid(0, [&] {
						auto Index = TypeName.Find(TEXT("::"));
						return Index == INDEX_NONE ? MAX_int32 : Index;
					}()));
				else
					NewReflection = StaticFindObject(TypeClass, ClassPackage, *TypeName);
			}
			else
			{
				NewReflection = StaticFindObject(TypeClass, nullptr, *TypeName);
			}

			if (!NewReflection)
			{
				if (UObjectRedirector* RenamedClassRedirector = FindObject<UObjectRedirector>(ClassPackage, *TypeName))
				{
					NewReflection = RenamedClassRedirector->DestinationObject;
				}
			}

			if (!NewReflection)
				FailureReason = TEXT("failed to find class.");
		}

		return NewReflection;
	}
	template<typename T>
	FORCEINLINE T* DynamicReflection(const FString& TypeName)
	{
		return static_cast<T*>(DynamicReflectionImpl(TypeName, T::StaticClass()));
	}
}  // namespace Reflection
}  // namespace GMP
#endif

namespace GMP
{
namespace Reflection
{
	FName GetScriptStructTypeName(UScriptStruct* Struct, const FProperty* InProp = nullptr)
	{
		if (auto BPStruct = Cast<UUserDefinedStruct>(Struct))
		{
#if 1
			return *FSoftObjectPath(BPStruct).ToString();
#else
			extern ENGINE_API FString GetPathPostfix(const UObject* ForObject);
			return *FString::Printf(TEXT("%s.%s"), *BPStruct->GetName(), *GetPathPostfix(BPStruct));
#endif
		}
		else if (ensure(Struct))
		{
			return Struct->GetFName();
		}
		return NAME_None;
	}

	static TMap<FName, FProperty*> PropertyStorage;
	static auto& GetPropertyStorage() { return PropertyStorage; }
#if GMP_USE_NEW_PROP_FROM_STRING
	static TMap<FName, FProperty* (*)()> BasePropertyStorageGen;
	static auto& GenBaseProperty() { return BasePropertyStorageGen; }
#endif
}  // namespace Reflection
}  // namespace GMP

void UGMPPropertiesContainer::AddCppProperty(FProperty* Property)
{
	using namespace GMP;
#if !UE_4_25_OR_LATER
	Property->AddToCluster(this);
#endif

#if GMP_WITH_PROPERTY_NAME_PREFIX
	auto Str = Property->GetFName().ToString();
	Str.RemoveFromStart(Class2Prop::GMPPropPrefix);
	FastLookups.Add(Property, FName(*Str));
#else
	FastLookups.Add(Property, Property->GetFName());
#endif
}

FName UGMPPropertiesContainer::FindPropertyName(const FProperty* Property)
{
	auto Find = FastLookups.Find(Property);
	return Find ? *Find : NAME_None;
}

static inline FName GetInnerPropertyName(const FProperty* InProperty)
{
	using namespace GMP;
	return static_cast<UGMPPropertiesContainer*>(Class2Prop::GMPGetPropertiesHolder())->FindPropertyName(InProperty);
}

namespace GMP
{
namespace Class2Prop
{
	void InitPropertyMapBase()
	{
		auto& PropertyMap = Reflection::GetPropertyStorage();
#if GMP_USE_NEW_PROP_FROM_STRING
		auto& PropertyGen = Reflection::GenBaseProperty();
#define GMP_INSERT_NAME_TYPE(NAME, CLASS)                                       \
	PropertyGen.Add(NAME, []() -> FProperty* { return CLASS::NewProperty(); }); \
	PropertyMap.Add(NAME, CLASS::GetProperty())
#else
#define GMP_INSERT_NAME_TYPE(NAME, CLASS) PropertyMap.Add(NAME, CLASS::GetProperty())
#endif

		GMP_INSERT_NAME_TYPE(TEXT("bool"), TClass2Prop<bool>);
		GMP_INSERT_NAME_TYPE(TEXT("char"), TClass2Prop<int8>);
		GMP_INSERT_NAME_TYPE(TEXT("byte"), TClass2Prop<uint8>);
		GMP_INSERT_NAME_TYPE(TEXT("int8"), TClass2Prop<int8>);
		GMP_INSERT_NAME_TYPE(TEXT("uint8"), TClass2Prop<uint8>);
		GMP_INSERT_NAME_TYPE(TEXT("int16"), TClass2Prop<int16>);
		GMP_INSERT_NAME_TYPE(TEXT("uint16"), TClass2Prop<uint16>);
		GMP_INSERT_NAME_TYPE(TEXT("int"), TClass2Prop<int32>);
		GMP_INSERT_NAME_TYPE(TEXT("uint"), TClass2Prop<uint32>);
		GMP_INSERT_NAME_TYPE(TEXT("int32"), TClass2Prop<int32>);
		GMP_INSERT_NAME_TYPE(TEXT("uint32"), TClass2Prop<uint32>);
		GMP_INSERT_NAME_TYPE(TEXT("int64"), TClass2Prop<int64>);
		GMP_INSERT_NAME_TYPE(TEXT("uint64"), TClass2Prop<uint64>);
		GMP_INSERT_NAME_TYPE(TEXT("float"), TClass2Prop<float>);
		GMP_INSERT_NAME_TYPE(TEXT("double"), TClass2Prop<double>);

		GMP_INSERT_NAME_TYPE(TEXT("Name"), TClass2Prop<FName>);
		GMP_INSERT_NAME_TYPE(TEXT("Text"), TClass2Prop<FText>);
		GMP_INSERT_NAME_TYPE(TEXT("String"), TClass2Prop<FString>);

		GMP_INSERT_NAME_TYPE(TEXT("Object"), TClass2Prop<UObject>);
		GMP_INSERT_NAME_TYPE(TEXT("Class"), TClass2Prop<UClass>);

		GMP_INSERT_NAME_TYPE(TEXT("SoftObjectPath"), TClass2Prop<FSoftObjectPath>);
		GMP_INSERT_NAME_TYPE(TEXT("SoftClassPath"), TClass2Prop<FSoftClassPath>);

		GMP_INSERT_NAME_TYPE(TEXT("ScriptInterface"), TClass2Prop<FScriptInterface>);

		GMP_INSERT_NAME_TYPE(TEXT("SoftObjectPtr"), TClass2Prop<TSoftObjectPtr<UObject>>);
		GMP_INSERT_NAME_TYPE(TEXT("WeakObjectPtr"), TClass2Prop<TWeakObjectPtr<UObject>>);
		GMP_INSERT_NAME_TYPE(TEXT("LazyObjectPtr"), TClass2Prop<TLazyObjectPtr<UObject>>);

#if UE_5_00_OR_LATER
		GMP_INSERT_NAME_TYPE(TEXT("ObjectPtr"), TClass2Prop<TObjectPtr<UObject>>);
		PropertyMap.Add(TEXT("TObjectPtr<Object>"), TClass2Prop<TObjectPtr<UObject>>::GetProperty());
#endif
		PropertyMap.Add(TEXT("TSubclassOf<Object>"), TClass2Prop<UClass>::GetProperty());
		PropertyMap.Add(TEXT("TSoftObjectPtr<Object>"), TClass2Prop<TSoftObjectPtr<UObject>>::GetProperty());
		PropertyMap.Add(TEXT("TSoftClassPtr<Object>"), TClass2Prop<TSoftClassPtr<UObject>>::GetProperty());
		PropertyMap.Add(TEXT("TWeakObjectPtr<Object>"), TClass2Prop<TWeakObjectPtr<UObject>>::GetProperty());
		PropertyMap.Add(TEXT("TLazyObjectPtr<Object>"), TClass2Prop<TLazyObjectPtr<UObject>>::GetProperty());
#undef GMP_INSERT_NAME_TYPE
	}

	UObject* GMPGetPropertiesHolder()
	{
		static auto Ret = [] {
			auto Container = NewObject<UGMPPropertiesContainer>();
			Container->AddToRoot();
			Container->CreateCluster();
			return Container;
		}();
		return Ret;
	}

	FProperty*& FindOrAddProperty(FName PropTypeName) { return Reflection::GetPropertyStorage().FindOrAdd(PropTypeName); }
	FProperty* FindOrAddProperty(FName PropTypeName, FProperty* InTypeProp)
	{
#if GMP_WITH_NULL_PROPERTY
		auto*& Prop = Class2Prop::FindOrAddProperty(PropTypeName);
		if (InTypeProp && !Class2Prop::VerifyPropertyType(Prop, InTypeProp->GetClass()))
			Prop = InTypeProp;
		return Prop;
#else
		auto Find = Reflection::GetPropertyStorage().Find(PropTypeName);
		if (InTypeProp)
		{
			if (!Find)
			{
				Find = &Reflection::GetPropertyStorage().Add(PropTypeName, InTypeProp);
			}
			else if (!(*Find) || !Class2Prop::VerifyPropertyType(*Find, InTypeProp->GetClass()))
			{
				*Find = InTypeProp;
			}
		}
		return Find ? *Find : InTypeProp;
#endif
	}
}  // namespace Class2Prop
}  // namespace GMP

namespace GMP
{
namespace Reflection
{
	// clang-format off
	const FGraphPinNameType PC_Boolean	  {TEXT("bool")};
	const FGraphPinNameType PC_Byte		  {TEXT("byte")};
	const FGraphPinNameType PC_Int		  {TEXT("int")};
	const FGraphPinNameType PC_Int64	  {TEXT("int64")};

	const FGraphPinNameType PC_Real		  {TEXT("real")};
	const FGraphPinNameType PC_Float	  {TEXT("float")};
	const FGraphPinNameType PC_Double	  {TEXT("double")};
	const FGraphPinNameType PC_Enum		  {TEXT("enum")};

	const FGraphPinNameType PC_Name		  {TEXT("name")};
	const FGraphPinNameType PC_String	  {TEXT("string")};
	const FGraphPinNameType PC_Text		  {TEXT("text")};

	const FGraphPinNameType PC_Delegate	  {TEXT("delegate")};
	const FGraphPinNameType PC_MCDelegate {TEXT("mcdelegate")};

	const FGraphPinNameType PC_Object	  {TEXT("object")};
	const FGraphPinNameType PC_Class	  {TEXT("class")};
	const FGraphPinNameType PC_SoftObject {TEXT("softobject")};
	const FGraphPinNameType PC_SoftClass  {TEXT("softclass")};
	const FGraphPinNameType PC_Interface  {TEXT("interface")};

	const FGraphPinNameType PC_Struct	  {TEXT("struct")};
	const FGraphPinNameType PC_Wildcard	  {TEXT("wildcard")};
	const FGraphPinNameType PC_FieldPath  {TEXT("fieldpath")};
	// clang-format on

	FName GetPinPropertyName(bool bExactType, const FEdGraphPinType& PinType, EGMPPropertyClass* PropertyType, EGMPPropertyClass* ElemPropType, EGMPPropertyClass* KeyPropType)
	{
		using namespace Class2Name;
		auto PinSubCategoryObject = PinType.PinSubCategoryObject.Get();
		if (PinType.IsArray())
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Array;
			auto Type = PinType;
			Type.ContainerType = EPinContainerType::None;
			return TTraitsTemplateBase::GetTArrayName(*GetPinPropertyName(bExactType, Type, ElemPropType).ToString());
		}
		else if (PinType.IsSet())
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Set;
			auto Type = PinType;
			Type.ContainerType = EPinContainerType::None;
			return TTraitsTemplateBase::GetTSetName(*GetPinPropertyName(bExactType, Type, ElemPropType).ToString());
		}
		else if (PinType.IsMap())
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Map;

			FEdGraphPinType ValuePinType;
			ValuePinType.PinCategory = PinType.PinValueType.TerminalCategory;
			ValuePinType.PinSubCategory = PinType.PinValueType.TerminalSubCategory;
			ValuePinType.PinSubCategoryObject = PinType.PinValueType.TerminalSubCategoryObject;
			ValuePinType.bIsWeakPointer = PinType.PinValueType.bTerminalIsWeakPointer;
			auto Type = PinType;
			Type.ContainerType = EPinContainerType::None;
			return TTraitsTemplateBase::GetTMapName(*GetPinPropertyName(bExactType, Type, KeyPropType).ToString(), *GetPinPropertyName(bExactType, ValuePinType, ElemPropType).ToString());
		}
		else if (PinType.PinCategory == PC_MCDelegate)
		{
			if (PropertyType)
			{
#if UE_4_23_OR_LATER
				*PropertyType = EGMPPropertyClass::InlineMulticastDelegate;  // SparseMulticastDelegate
#else
				*PropertyType = EGMPPropertyClass::MulticastDelegate;
#endif
			}
			return *TTraitsScriptDelegateBase::GetDelegateNameImpl(true, Cast<UFunction>(PinSubCategoryObject), bExactType);
		}
		else if (PinType.PinCategory == PC_Delegate)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Delegate;
			return *TTraitsScriptDelegateBase::GetDelegateNameImpl(false, Cast<UFunction>(PinSubCategoryObject), bExactType);
		}
		else if (PinType.PinCategory == PC_Interface)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Interface;
			return bExactType ? TTraitsScriptIncBase::GetFName(Cast<UClass>(PinSubCategoryObject)) : TTraitsScriptIncBase::GetFName();
		}
		else if (PinType.PinCategory == PC_SoftClass)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::SoftClass;
			return bExactType ? TTraitsTemplateUtils<TSoftClassPtr<UObject>>::GetFName(Cast<UClass>(PinSubCategoryObject)) : TTraitsTemplateUtils<TSoftClassPtr<UObject>>::GetFName();
		}
		else if (PinType.PinCategory == PC_SoftObject)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::SoftObject;
			return bExactType ? TTraitsTemplateUtils<TSoftObjectPtr<UObject>>::GetFName(Cast<UClass>(PinSubCategoryObject)) : TTraitsTemplateUtils<TSoftObjectPtr<UObject>>::GetFName();
		}
		else if (PinType.PinCategory == PC_Class)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Class;
			// PinType.bIsUObjectWrapper --> TSubClassOf
			return bExactType ? TTraitsTemplateUtils<TSubclassOf<UObject>>::GetFName(Cast<UClass>(PinSubCategoryObject)) : TTraitsTemplateUtils<TSubclassOf<UObject>>::GetFName();
		}
		else if (PinType.PinCategory == PC_Object)
		{
			if (PropertyType)
				*PropertyType = PinType.bIsWeakPointer ? EGMPPropertyClass::WeakObject : EGMPPropertyClass::Object;
			if (PinType.bIsWeakPointer)
				return bExactType ? TTraitsTemplateUtils<TWeakObjectPtr<UObject>>::GetFName(Cast<UClass>(PinSubCategoryObject)) : TTraitsTemplateUtils<TWeakObjectPtr<UObject>>::GetFName();
			else
				return bExactType ? TTraitsBaseClassValue<UObject>::GetFName(Cast<UClass>(PinSubCategoryObject)) : TClass2Name<UObject>::GetFName();
		}
		else if (PinType.PinCategory == PC_Struct)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Struct;
			return PinSubCategoryObject ? GetScriptStructTypeName(Cast<UScriptStruct>(PinSubCategoryObject)) : NAME_None;
		}
		else if (PinType.PinCategory == PC_Float || (PinType.PinCategory == PC_Real && PinType.PinSubCategory == PC_Float))
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Float;
			return TClass2Name<float>::GetFName();
		}
		else if (PinType.PinCategory == PC_Double || (PinType.PinCategory == PC_Real && PinType.PinSubCategory == PC_Double))
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Double;
			return TClass2Name<double>::GetFName();
		}
		else if (PinType.PinCategory == PC_Int)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Int;
			return TClass2Name<int32>::GetFName();
		}
#if UE_4_22_OR_LATER
		else if (PinType.PinCategory == PC_Int64)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Int64;
			return TClass2Name<int64>::GetFName();
		}
#endif
		else if (PinType.PinCategory == PC_Boolean)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Bool;
			return TClass2Name<bool>::GetFName();
		}
		else if (PinType.PinCategory == PC_Name)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Name;
			return TClass2Name<FName>::GetFName();
		}
		else if (PinType.PinCategory == PC_String)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Str;
			return TClass2Name<FString>::GetFName();
		}
		else if (PinType.PinCategory == PC_Text)
		{
			if (PropertyType)
				*PropertyType = EGMPPropertyClass::Text;
			return TClass2Name<FText>::GetFName();
		}
		else if (PinType.PinCategory == PC_Enum)
		{
			// K2 only supports byte enums right now - any violations should have been caught by UHT or the editor
			UEnum* SubEnum = Cast<UEnum>(PinSubCategoryObject);
			if (ensure(SubEnum))
			{
				if (PropertyType)
					*PropertyType = EGMPPropertyClass::Byte;
				return TTraitsEnumBase::GetFName(SubEnum, 1);
			}
			else
			{
				if (PropertyType)
					*PropertyType = EGMPPropertyClass::Enum;
				return TClass2Name<uint8>::GetFName();
			}
		}
		else if (PinType.PinCategory == PC_Byte)
		{
			if (UEnum* SubEnum = Cast<UEnum>(PinSubCategoryObject))
			{
				if (PropertyType)
					*PropertyType = SubEnum->GetCppForm() == UEnum::ECppForm::EnumClass ? EGMPPropertyClass::Byte : EGMPPropertyClass::Int;
				return TTraitsEnumBase::GetFName(SubEnum, 1);
			}
			else
			{
				if (PropertyType)
					*PropertyType = EGMPPropertyClass::Byte;
				return TClass2Name<uint8>::GetFName();
			}
		}
#if UE_4_25_OR_LATER && 0
		else if (PinType.PinCategory == PC_FieldPath)
		{
			// FFieldPath / TFieldPath<FProperty>
		}
#endif
		// 	else if (PinType.PinCategory == PC_Wildcard)
		// 	{
		// 		return FName(TEXT("Wildcard"));
		// 	}
		return NAME_None;
	}

	struct FStrMatcher
	{
		explicit operator bool() const { return bMatched; }
		FStrMatcher(const TCHAR* Prefix, int32 N, const FString& Input)
		{
			bMatched = (Input.Len() > N + 2) && Input[N - 1] == TEXT('<') && Input[Input.Len() - 1] == TEXT('>') && Input.StartsWith(Prefix);
			if (bMatched)
				Matched = Input.Mid(N, Input.Len() - N - 1);
		}
		template<int32 N>
		FStrMatcher(const TCHAR (&Prefix)[N], const FString& Input)
			: FStrMatcher(Prefix, N, Input)
		{
		}

		operator FString() const { return Matched; }
		bool bMatched;
		FString Matched;
	};

	bool PinTypeFromString(FString TypeString, FEdGraphPinType& OutPinType, bool bInTemplate, bool bInContainer)
	{
		TypeString = TypeString.Replace(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
		if (TypeString.Len() < 2)
			return false;

		static auto GetPinType = [](FGraphPinNameType Category, UObject* SubCategoryObj = nullptr, FName SubCat = NAME_None) {
			FEdGraphPinType PinType;
			PinType.PinCategory = ToGraphPinNameType(Category);
			PinType.PinSubCategory = ToGraphPinNameType(SubCat);
			PinType.PinSubCategoryObject = SubCategoryObj;
			return PinType;
		};

		static auto GetObjectPinType = [](bool bWeak = false, UObject* SubCategoryObj = UObject::StaticClass()) {
			FEdGraphPinType PinType;
			PinType.PinCategory = PC_Object;
			PinType.PinSubCategoryObject = SubCategoryObj;
			PinType.bIsWeakPointer = bWeak;
			return PinType;
		};

		static TMap<FName, FEdGraphPinType> NativeMap = [&]() {
			TMap<FName, FEdGraphPinType> Ret;
			Ret.Add(TEXT("bool"), GetPinType(PC_Boolean));

			Ret.Add(TEXT("char"), GetPinType(PC_Byte));
			Ret.Add(TEXT("byte"), GetPinType(PC_Byte));
			Ret.Add(TEXT("int8"), GetPinType(PC_Byte));
			Ret.Add(TEXT("uint8"), GetPinType(PC_Byte));

			Ret.Add(TEXT("int"), GetPinType(PC_Int));
			Ret.Add(TEXT("uint"), GetPinType(PC_Int));
			Ret.Add(TEXT("int32"), GetPinType(PC_Int));
			Ret.Add(TEXT("uint32"), GetPinType(PC_Int));

#if UE_4_22_OR_LATER
			Ret.Add(TEXT("int64"), GetPinType(PC_Int64));
			Ret.Add(TEXT("uint64"), GetPinType(PC_Int64));
#endif
#if UE_5_00_OR_LATER
			Ret.Add(TEXT("float"), GetPinType(PC_Real, nullptr, PC_Float));
			Ret.Add(TEXT("double"), GetPinType(PC_Real, nullptr, PC_Double));
#else
			Ret.Add(TEXT("float"), GetPinType(PC_Float));
			Ret.Add(TEXT("double"), GetPinType(PC_Double));
#endif
			Ret.Add(TEXT("Name"), GetPinType(PC_Name));
			Ret.Add(TEXT("Text"), GetPinType(PC_Text));
			Ret.Add(TEXT("String"), GetPinType(PC_String));

			Ret.Add(TEXT("ScriptInterface"), GetPinType(PC_Interface, UInterface::StaticClass()));

			Ret.Add(TEXT("Object"), GetObjectPinType(false));

#if UE_5_00_OR_LATER
			Ret.Add(TEXT("ObjectPtr"), GetObjectPinType(false));
			Ret.Add(TEXT("TObjectPtr<Object>"), GetObjectPinType(false));
#endif
			Ret.Add(TEXT("Class"), GetPinType(PC_Class, UObject::StaticClass()));
			Ret.Add(TEXT("TSubclassOf<Object>"), GetPinType(PC_Class, UObject::StaticClass()));

			Ret.Add(TEXT("SoftObjectPtr"), GetPinType(PC_SoftObject, UObject::StaticClass()));
			Ret.Add(TEXT("TSoftObjectPtr<Object>"), GetPinType(PC_SoftObject, UObject::StaticClass()));

			Ret.Add(TEXT("TSoftClassPtr<Object>"), GetPinType(PC_SoftClass, UObject::StaticClass()));

			Ret.Add(TEXT("WeakObjectPtr"), GetObjectPinType(true));
			Ret.Add(TEXT("TWeakOjectPtr<Object>"), GetObjectPinType(true));
			return Ret;
		}();

		if (auto Find = NativeMap.Find(*TypeString))
		{
			OutPinType = *Find;
			return true;
		}

		do
		{
			if (!bInTemplate && TypeString.StartsWith(TEXT("T")) && TypeString.EndsWith(TEXT(">")))
			{
				if (auto MatchByte = FStrMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<1>(), TypeString))
				{
					if (ensure(PinTypeFromString(MatchByte, OutPinType, true, false)))
					{
						ensureAlways(OutPinType.PinCategory == PC_Enum || OutPinType.PinCategory == PC_Byte);
						OutPinType.PinCategory = PC_Byte;
						// OutPinType.PinSubCategory = PC_Enum;
					}
					else
					{
						OutPinType.PinCategory = PC_Byte;
					}
				}
				else if (auto MatchInt16 = FStrMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<2>(), TypeString))
				{
					if (!ensure(PinTypeFromString(MatchInt16, OutPinType, true, false)))
						break;
				}
				else if (auto MatchInt32 = FStrMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<4>(), TypeString))
				{
					if (!ensure(PinTypeFromString(MatchInt32, OutPinType, true, false)))
					{
						OutPinType.PinCategory = PC_Int;
					}
				}
				else if (auto MatchInt64 = FStrMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<8>(), TypeString))
				{
					if (!ensure(PinTypeFromString(MatchInt64, OutPinType, true, false)))
					{
						OutPinType.PinCategory = PC_Int64;
					}
				}
				else if (PinTypeFromString(FStrMatcher(TEXT("TSubclassOf"), TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_Class;
				}
				else if (PinTypeFromString(FStrMatcher(TEXT("TSoftClassPtr"), TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_SoftClass;
				}
				else if (PinTypeFromString(FStrMatcher(TEXT("TSoftClassPtr"), TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_SoftObject;
				}
				else if (PinTypeFromString(FStrMatcher(TEXT("TWeakObjectPtr"), TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_Object;
					OutPinType.bIsWeakPointer = true;
				}
				else if (PinTypeFromString(FStrMatcher(TEXT("TScriptInterface"), TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_Interface;
				}
				else if (PinTypeFromString(FStrMatcher(NAME_GMP_TNativeInterfece, TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_Interface;
				}
				else if (PinTypeFromString(FStrMatcher(NAME_GMP_TObjectPtr, TypeString), OutPinType, true, false))
				{
					OutPinType.PinCategory = PC_Object;
				}
				else if (!bInContainer)
				{
					if (PinTypeFromString(FStrMatcher(TEXT("TArray"), TypeString), OutPinType, false, true))
					{
						OutPinType.ContainerType = EPinContainerType::Array;
					}
					else if (PinTypeFromString(FStrMatcher(TEXT("TSet"), TypeString), OutPinType, false, true))
					{
						OutPinType.ContainerType = EPinContainerType::Set;
					}
					else if (auto Matcher = FStrMatcher(TEXT("TMap"), TypeString))
					{
						FString Left;
						FString Right;
						if (!ensure(FString(Matcher).Split(TEXT(","), &Left, &Right)))
							break;
						if (!ensure(PinTypeFromString(Left, OutPinType, false, true)))
							break;
						FEdGraphPinType ValueType;
						if (!ensure(PinTypeFromString(Right, ValueType, false, true)))
							break;
						OutPinType.PinValueType = FEdGraphTerminalType::FromPinType(ValueType);
						OutPinType.ContainerType = EPinContainerType::Map;
					}
					else
					{
						break;
					}
				}
				return true;
			}

			if (auto Class = DynamicReflection<UClass>(TypeString))
			{
				OutPinType = GetPinType(PC_Object, Class);
			}
			else if (auto Struct = DynamicReflection<UScriptStruct>(TypeString))
			{
				OutPinType = GetPinType(PC_Struct, Struct);
			}
			else if (auto Enum = DynamicReflection<UEnum>(TypeString))
			{
				OutPinType = GetPinType(PC_Byte, Enum);
				// always treats enum class as TEnumAsByte
				if (!bInTemplate && Enum->GetCppForm() == UEnum::ECppForm::EnumClass)
					OutPinType.PinCategory = PC_Byte;
			}
			else
			{
				break;
			}
			return true;
		} while (0);
		ensureMsgf(bInTemplate, TEXT("type not supported for %s"), *TypeString);
		OutPinType = GetPinType(PC_Wildcard);
		return false;
	}

	FString GetDefaultValueOnType(const FEdGraphPinType& PinType)
	{
		FString NewValue;
		if (PinType.IsContainer())
		{
		}
		else if (PinType.PinCategory == Reflection::PC_Int
#if UE_4_22_OR_LATER
				 || PinType.PinCategory == Reflection::PC_Int64
#endif
		)
		{
			NewValue = TEXT("0");
		}
		else if (PinType.PinCategory == Reflection::PC_Byte)
		{
			UEnum* EnumPtr = Cast<UEnum>(PinType.PinSubCategoryObject.Get());
			if (EnumPtr)
			{
				NewValue = FString();
				int32 NumEnums = (EnumPtr->NumEnums() - 1);
				for (int32 EnumIdx = 0; EnumIdx < NumEnums; EnumIdx++)
				{
#if WITH_EDITOR
					if (!EnumPtr->HasMetaData(TEXT("Hidden"), EnumIdx) || EnumPtr->HasMetaData(TEXT("Spacer"), EnumIdx))
#endif
					{
						NewValue = EnumPtr->GetNameStringByIndex(EnumIdx);
						break;
					}
				}
			}
			else
			{
				NewValue = TEXT("0");
			}
		}
		else if (PinType.PinCategory == Reflection::PC_Float || PinType.PinCategory == Reflection::PC_Double
				 || (PinType.PinCategory == Reflection::PC_Real && (PinType.PinSubCategory == Reflection::PC_Float || PinType.PinSubCategory == Reflection::PC_Double)))
		{
			NewValue = TEXT("0.0");
		}
		else if (PinType.PinCategory == Reflection::PC_Boolean)
		{
			NewValue = TEXT("false");
		}
		else if (PinType.PinCategory == Reflection::PC_Name)
		{
			NewValue = TEXT("None");
		}
		else if ((PinType.PinCategory == Reflection::PC_Struct))
		{
			if (PinType.PinSubCategoryObject == TBaseStructure<FVector>::Get() || PinType.PinSubCategoryObject == TBaseStructure<FRotator>::Get())
			{
				NewValue = TEXT("0, 0, 0");
			}
			else if (PinType.PinSubCategoryObject == TBaseStructure<FSoftClassPath>::Get() || PinType.PinSubCategoryObject == TBaseStructure<FSoftObjectPath>::Get())
			{
				NewValue = TEXT("None");
			}
		}
		return NewValue;
	}

	//////////////////////////////////////////////////////////////////////////
	FName GetPropertyName(const FProperty* InProperty, bool bExactType)
	{
		using namespace Class2Name;

		using ValueType = FName (*)(const FProperty* Property, bool bExactType);
		using KeyType = const FFieldClass*;
#define GMP_DEF_PAIR_CELL(PropertyTypeName) Ret.Add(PropertyTypeName::StaticClass(), [](const FProperty* Property, bool bExactType) { return GetPropertyName<typename PropertyTypeName::TCppType>(); })
#define GMP_DEF_PAIR_CELL_CUSTOM(PropertyTypeName, ...) Ret.Add(PropertyTypeName::StaticClass(), [](const FProperty* Property, bool bExactType) { return __VA_ARGS__; })
		static TMap<KeyType, ValueType> DispatchMap = [&]() {
			TMap<KeyType, ValueType> Ret;
			GMP_DEF_PAIR_CELL(FBoolProperty);

			GMP_DEF_PAIR_CELL_CUSTOM(FByteProperty, [&] {
				auto ByteProp = CastFieldChecked<FByteProperty>(Property);
				return ByteProp->Enum ? Class2Name::TTraitsEnumBase::GetFName(ByteProp->Enum) : GetPropertyName<typename FByteProperty::TCppType>();
			}());

			GMP_DEF_PAIR_CELL(FInt16Property);
			GMP_DEF_PAIR_CELL(FInt8Property);
			GMP_DEF_PAIR_CELL(FUInt16Property);

			GMP_DEF_PAIR_CELL(FIntProperty);
			GMP_DEF_PAIR_CELL(FUInt32Property);

			GMP_DEF_PAIR_CELL(FFloatProperty);
			GMP_DEF_PAIR_CELL(FDoubleProperty);

#if UE_4_22_OR_LATER
			GMP_DEF_PAIR_CELL(FInt64Property);
			GMP_DEF_PAIR_CELL(FUInt64Property);
#endif

			GMP_DEF_PAIR_CELL(FStrProperty);
			GMP_DEF_PAIR_CELL(FNameProperty);
			GMP_DEF_PAIR_CELL(FTextProperty);

			GMP_DEF_PAIR_CELL_CUSTOM(FDelegateProperty, [&] {
				static FName Ret = *TTraitsScriptDelegateBase::GetDelegateNameImpl(false, CastField<FDelegateProperty>(Property)->SignatureFunction, bExactType);
				return Ret;
			}());
			GMP_DEF_PAIR_CELL_CUSTOM(FMulticastDelegateProperty, [&] {
				static FName Ret = *TTraitsScriptDelegateBase::GetDelegateNameImpl(true, CastField<FMulticastDelegateProperty>(Property)->SignatureFunction, bExactType);
				return Ret;
			}());
#if UE_4_23_OR_LATER && 0
			GMP_DEF_PAIR_CELL_CUSTOM(FMulticastInlineDelegateProperty, [&] {
				static FName Ret = *TTraitsScriptDelegateBase::GetDelegateNameImpl(true, CastField<FMulticastInlineDelegateProperty>(Property)->SignatureFunction, bExactType);
				return Ret;
			}());
			GMP_DEF_PAIR_CELL_CUSTOM(FMulticastSparseDelegateProperty, [&] {
				static FName Ret = *TTraitsScriptDelegateBase::GetDelegateNameImpl(true, CastField<FMulticastSparseDelegateProperty>(Property)->SignatureFunction, bExactType);
				return Ret;
			}());
#endif
#if UE_4_25_OR_LATER
			// GMP_DEF_PAIR_CELL_CUSTOM(FFieldPathProperty, bExactType ? CastField<FFieldPathProperty>(Property)->PropertyClass->GetFName() : TEXT("FFieldPath"));
#endif
			GMP_DEF_PAIR_CELL_CUSTOM(FObjectProperty, bExactType ? TTraitsBaseClassValue<UObject>::GetFName(CastField<FObjectProperty>(Property)->PropertyClass) : GetPropertyName<UObject>());
			GMP_DEF_PAIR_CELL_CUSTOM(FSoftObjectProperty,
									 bExactType ? TTraitsTemplateUtils<TSoftObjectPtr<UObject>>::GetFName(CastField<FSoftObjectProperty>(Property)->PropertyClass) : TTraitsTemplateUtils<TSoftObjectPtr<UObject>>::GetFName());
			GMP_DEF_PAIR_CELL_CUSTOM(FClassProperty, bExactType ? TTraitsBaseClassValue<UClass>::GetFName(CastField<FClassProperty>(Property)->MetaClass) : TTraitsBaseClassValue<UClass>::GetFName(UObject::StaticClass()));
			GMP_DEF_PAIR_CELL_CUSTOM(FSoftClassProperty, TTraitsTemplateUtils<TSoftClassPtr<UObject>>::GetFName(bExactType ? CastField<FSoftClassProperty>(Property)->MetaClass : (UClass*)nullptr));

			GMP_DEF_PAIR_CELL_CUSTOM(FInterfaceProperty, TTraitsScriptIncBase::GetFName(bExactType ? CastField<FInterfaceProperty>(Property)->InterfaceClass : nullptr));
			GMP_DEF_PAIR_CELL_CUSTOM(FWeakObjectProperty, TTraitsTemplate<FWeakObjectPtr, false>::GetFName(bExactType ? CastField<FWeakObjectProperty>(Property)->PropertyClass : nullptr));
			GMP_DEF_PAIR_CELL_CUSTOM(FLazyObjectProperty, TTraitsTemplate<FLazyObjectPtr, false>::GetFName(bExactType ? CastField<FLazyObjectProperty>(Property)->PropertyClass : nullptr));

			GMP_DEF_PAIR_CELL_CUSTOM(FArrayProperty, TTraitsTemplateBase::GetTArrayName(*GetPropertyName(CastField<FArrayProperty>(Property)->Inner, bExactType).ToString()));
			GMP_DEF_PAIR_CELL_CUSTOM(
				FMapProperty,
				TTraitsTemplateBase::GetTMapName(*GetPropertyName(CastField<FMapProperty>(Property)->KeyProp, bExactType).ToString(), *GetPropertyName(CastField<FMapProperty>(Property)->ValueProp, bExactType).ToString()));
			GMP_DEF_PAIR_CELL_CUSTOM(FSetProperty, TTraitsTemplateBase::GetTSetName(*GetPropertyName(CastField<FSetProperty>(Property)->ElementProp, bExactType).ToString()));

			GMP_DEF_PAIR_CELL_CUSTOM(FStructProperty, GetScriptStructTypeName(CastField<FStructProperty>(Property)->Struct, Property));
			// GMP_DEF_PAIR_CELL_CUSTOM(FEnumProperty, FName(*CastField<FEnumProperty>(Property)->GetEnum()->CppType));
			GMP_DEF_PAIR_CELL_CUSTOM(FEnumProperty, [&] {
				auto EnumProp = CastFieldChecked<FEnumProperty>(Property);
				auto Bytes = static_cast<uint32>((EnumProp->GetPropertyFlags() & (Class2Prop::TEnumPropertyBase::CPF_EnumAsByteMask)) >> 28);
				if (Bytes != 0)
				{
					return Class2Name::TTraitsEnumBase::GetFName(EnumProp->GetEnum(), Bytes);
				}
				else
				{
					auto SubProp = EnumProp->GetUnderlyingProperty();
					if (EnumProp->GetEnum()->GetCppForm() == UEnum::ECppForm::EnumClass)
					{
						ensure(SubProp->IsA<FByteProperty>() || SubProp->IsA<FInt8Property>());
						Bytes = 1;
					}
					else if (SubProp->IsA<FByteProperty>() || SubProp->IsA<FInt8Property>())
					{
						Bytes = 1;
					}
					else if (SubProp->IsA<FUInt16Property>() || SubProp->IsA<FInt16Property>())
					{
						Bytes = 2;
					}
					else if (SubProp->IsA<FUInt32Property>() || SubProp->IsA<FIntProperty>())
					{
						Bytes = 4;
					}
					else
					{
						Bytes = 8;
					}
					return Class2Name::TTraitsEnumBase::GetFName(EnumProp->GetEnum(), Bytes);
				}
			}());

			return Ret;
		}();
#undef GMP_DEF_PAIR_CELL
#undef GMP_DEF_PAIR_CELL_CUSTOM
		FName Result = bExactType ? GetInnerPropertyName(InProperty) : NAME_None;
		if (!Result.IsNone())
			return Result;

		if (auto Func = DispatchMap.Find(InProperty ? InProperty->GetClass() : nullptr))
		{
			Result = (*Func)(InProperty, bExactType);
		}
		else
		{
			ensureAlwaysMsgf(false, TEXT("unsupported property types %s"), *GetNameSafe(InProperty));
		}
		return Result;
	}

	FName GetPropertyName(const FProperty* InProperty, EGMPPropertyClass PropertyType, EGMPPropertyClass ValueEnum, EGMPPropertyClass KeyPropType)
	{
		using namespace Class2Name;

		using KeyType = EGMPPropertyClass;
		using ValueType = FName (*)(const FProperty* Property, EGMPPropertyClass, EGMPPropertyClass);

#define GMP_DEF_PAIR_CELL(Index) Ret.Add(KeyType::Index, [](const FProperty* Property, EGMPPropertyClass InValueEnum, EGMPPropertyClass InKeyEnum) { return GetPropertyName<typename F##Index##Property ::TCppType>(); })
#define GMP_DEF_PAIR_CELL_CUSTOM(Index, ...) Ret.Add(KeyType::Index, [](const FProperty* Property, EGMPPropertyClass InValueEnum, EGMPPropertyClass InKeyEnum) __VA_ARGS__)

		static TMap<KeyType, ValueType> DispatchMap = [&]() {
			TMap<KeyType, ValueType> Ret;
			GMP_DEF_PAIR_CELL(Bool);

			GMP_DEF_PAIR_CELL(Byte);
			GMP_DEF_PAIR_CELL(Int8);

			GMP_DEF_PAIR_CELL(Int16);
			GMP_DEF_PAIR_CELL(UInt16);

			GMP_DEF_PAIR_CELL(Int);
			GMP_DEF_PAIR_CELL(UInt32);

			GMP_DEF_PAIR_CELL(UnsizedInt);
			GMP_DEF_PAIR_CELL(UnsizedUInt);

			GMP_DEF_PAIR_CELL(Float);
			GMP_DEF_PAIR_CELL(Double);

			GMP_DEF_PAIR_CELL(Name);
			GMP_DEF_PAIR_CELL(Str);
			GMP_DEF_PAIR_CELL(Text);

			GMP_DEF_PAIR_CELL(Object);
			GMP_DEF_PAIR_CELL(Class);

			GMP_DEF_PAIR_CELL(SoftObject);
			GMP_DEF_PAIR_CELL(SoftClass);
			GMP_DEF_PAIR_CELL(WeakObject);
			GMP_DEF_PAIR_CELL(LazyObject);
			GMP_DEF_PAIR_CELL_CUSTOM(Interface, {
				auto IncProp = CastFieldChecked<FInterfaceProperty>(Property);
				if (IncProp->HasAnyPropertyFlags(Class2Prop::TTraitsNativeIncBase::CPF_GMPMark))
					return Class2Name::TTraitsNativeIncBase::GetFName(*IncProp->InterfaceClass->GetName());
				else
					return Class2Name::TTraitsScriptIncBase::GetFName(*IncProp->InterfaceClass->GetName());
			});
			GMP_DEF_PAIR_CELL_CUSTOM(Array, { return TTraitsTemplateBase::GetTArrayName(*GetPropertyName(CastFieldChecked<FArrayProperty>(Property)->Inner, InValueEnum).ToString()); });
			GMP_DEF_PAIR_CELL_CUSTOM(Map, {
				return TTraitsTemplateBase::GetTMapName(*GetPropertyName(CastFieldChecked<FMapProperty>(Property)->KeyProp, InKeyEnum).ToString(),
														*GetPropertyName(CastFieldChecked<FMapProperty>(Property)->ValueProp, InValueEnum).ToString());
			});
			GMP_DEF_PAIR_CELL_CUSTOM(Set, { return TTraitsTemplateBase::GetTSetName(*GetPropertyName(CastFieldChecked<FSetProperty>(Property)->ElementProp, InValueEnum).ToString()); });
			GMP_DEF_PAIR_CELL(Delegate);
			//GMP_DEF_PAIR_CELL(InlineMulticastDelegate);

			GMP_DEF_PAIR_CELL_CUSTOM(Struct, { return GetScriptStructTypeName(CastFieldChecked<FStructProperty>(Property)->Struct, Property); });
			GMP_DEF_PAIR_CELL_CUSTOM(Enum, {
				auto EnumProp = CastFieldChecked<FEnumProperty>(Property);
				auto Bytes = static_cast<uint32>((EnumProp->GetPropertyFlags() & (Class2Prop::TEnumPropertyBase::CPF_EnumAsByteMask)) >> 28);
				if (Bytes <= 0)
				{
					auto SubProp = EnumProp->GetUnderlyingProperty();
					if (EnumProp->GetEnum()->GetCppForm() == UEnum::ECppForm::EnumClass)
					{
						ensure(SubProp->IsA<FByteProperty>() || SubProp->IsA<FInt8Property>());
						Bytes = 1;
					}
					else if (SubProp->IsA<FByteProperty>() || SubProp->IsA<FInt8Property>())
					{
						Bytes = 1;
					}
					else if (SubProp->IsA<FUInt16Property>() || SubProp->IsA<FInt16Property>())
					{
						Bytes = 2;
					}
					else if (SubProp->IsA<FUInt32Property>() || SubProp->IsA<FIntProperty>())
					{
						Bytes = 4;
					}
					else
					{
						Bytes = 8;
					}
				}
				return Class2Name::TTraitsEnumBase::GetFName(EnumProp->GetEnum(), Bytes);
			});
#if UE_4_22_OR_LATER
			GMP_DEF_PAIR_CELL(Int64);
			GMP_DEF_PAIR_CELL(UInt64);
#endif
			return Ret;
		}();
#undef GMP_DEF_PAIR_CELL
#undef GMP_DEF_PAIR_CELL_CUSTOM
		FName Result = GetInnerPropertyName(InProperty);
		if (!Result.IsNone())
			return Result;

		if (auto Func = DispatchMap.Find(PropertyType))
		{
			Result = (*Func)(InProperty, ValueEnum, KeyPropType);
		}
		else
		{
			Result = GetPropertyName(InProperty);
		}

		return Result;
	}

	static TAtomic<EExactTestMask> GlobalExactTestBits = EExactTestMask::TestExactly;
	FExactTestMaskScope::FExactTestMaskScope(EExactTestMask Lv)
	{
		Old = Lv;
		GlobalExactTestBits.Exchange(Old);
	}

	FExactTestMaskScope::~FExactTestMaskScope() { GlobalExactTestBits.Store(Old); }

	bool EqualPropertyName(const FProperty* Property, FName TypeName, EExactTestMask ExactTestBits)
	{
		auto ExactName = GetPropertyName(Property, true);

		if (ExactName == TypeName)
		{
			return true;
		}

		ExactTestBits |= GlobalExactTestBits;
		if (ExactTestBits == 0)
			return false;

		if (ExactTestBits & GMP::Reflection::TestSkip)
		{
			if (TypeName.IsNone() || TypeName == NAME_GMPSkipValidate)
				return true;
		}

		if (ExactTestBits & GMP::Reflection::TestEnum)
		{
			if (ensure(Property->IsA<FEnumProperty>()) && FNameSuccession::MatchEnums(TypeName, ExactName))
				return true;
		}

		if (ExactTestBits & GMP::Reflection::TestDerived)
		{
			if (FNameSuccession::IsDerivedFrom(ExactName, TypeName))
				return true;

			auto WeakName = GetPropertyName(Property, false);
			if (WeakName == ExactName)
				return true;
		}
		return false;
	}

	struct FMyMatcher
	{
		explicit operator bool() const { return bMatched; }
		FMyMatcher(const TCHAR* Prefix, int32 N, const FString& Input)
		{
			bMatched = (Input.Len() > N + 2) && Input[N - 1] == TEXT('<') && Input[Input.Len() - 1] == TEXT('>') && Input.StartsWith(Prefix);
			if (bMatched)
				Matched = Input.Mid(N, Input.Len() - N - 1);
		}
		template<int32 N>
		FMyMatcher(const TCHAR (&Prefix)[N], const FString& Input)
			: FMyMatcher(Prefix, N, Input)
		{
		}

		template<typename T>
		FMyMatcher(const TCHAR* Prefix, int32 N, const FString& Input, T*& Out)
			: FMyMatcher(Prefix, N, Input)
		{
			Out = bMatched ? DynamicReflection<T>(Matched) : nullptr;
		}

		template<int32 N, typename T>
		FMyMatcher(const TCHAR (&Prefix)[N], const FString& Input, T*& Out)
			: FMyMatcher(Prefix, N, Input, Out)
		{
		}
		const auto& MatchedStr() const { return Matched; }
		operator FString() const { return Matched; }
		bool bMatched;
		FString Matched;
	};

	// clang-format off
template<bool bUseNewProp>
bool PropertyFromStringImpl(FString TypeString, FProperty*& OutProp, bool bInTemplate, bool bInContainer)
{
	TypeString = TypeString.Replace(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
	if (TypeString.Len() < 2)
		return false;

#if GMP_USE_NEW_PROP_FROM_STRING
	static constexpr bool bNew = bUseNewProp;
#else
	static constexpr bool bNew = false;
#endif	
	GMP_IF_CONSTEXPR (bNew)
	{
		if (auto Find = Reflection::GenBaseProperty().Find(*TypeString))
		{
			OutProp = (*Find)();
			return ensure(OutProp);
		}
	}
	else
	{
		if (auto Find = Reflection::GetPropertyStorage().Find(*TypeString))
		{
			OutProp = *Find;
			return true;
		}
	}
	

	using namespace Class2Prop;
	bool bRet = false;
	do
	{
		if (!bInTemplate && TypeString[0] == TEXT('T') && TypeString[TypeString.Len() - 1] == TEXT('>'))
		{
			UEnum* EnumPtr = nullptr;
			UClass* ClassPtr = nullptr;
			if (FMyMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<1>(), TypeString, EnumPtr))
			{
				if (ensure(EnumPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TEnumPropertyBase::NewProperty(EnumPtr, true); else OutProp = TEnumPropertyBase::GetProperty(EnumPtr, true);
				}
			}
			else if (FMyMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<2>(), TypeString, EnumPtr))
			{
				if (ensure(EnumPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TEnumPropertyBase::NewProperty(EnumPtr, true); else OutProp = TEnumPropertyBase::GetProperty(EnumPtr, true);
				}
			}
			else if (FMyMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<4>(), TypeString, EnumPtr))
			{
				if (ensure(EnumPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TEnumPropertyBase::NewProperty(EnumPtr, true); else OutProp = TEnumPropertyBase::GetProperty(EnumPtr, true);
				}
			}
			else if (FMyMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<8>(), TypeString, EnumPtr))
			{
				if (ensure(EnumPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TEnumPropertyBase::NewProperty(EnumPtr, true); else OutProp = TEnumPropertyBase::GetProperty(EnumPtr, true);
				}
			}
			else if (FMyMatcher(TEXT("TSubclassOf"), TypeString, ClassPtr))
			{
				if (ensure(ClassPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsSubclassOfBase::NewProperty(ClassPtr); else OutProp = TTraitsSubclassOfBase::GetProperty(ClassPtr);
				}
			}
			else if (FMyMatcher(TEXT("TSoftClassPtr"), TypeString, ClassPtr))
			{
				if (ensure(ClassPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsSoftClassBase::NewProperty(CastChecked<UClass>(ClassPtr)); else OutProp = TTraitsSoftClassBase::GetProperty(CastChecked<UClass>(ClassPtr));
				}
			}
			else if (FMyMatcher(TEXT("TSoftObjectPtr"), TypeString, ClassPtr))
			{
				if (ensure(ClassPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsSoftObjectBase::NewProperty(CastChecked<UClass>(ClassPtr)); else OutProp = TTraitsSoftObjectBase::GetProperty(CastChecked<UClass>(ClassPtr));
				}
			}
			else if (FMyMatcher(TEXT("TWeakObjectPtr"), TypeString, ClassPtr))
			{
				auto Class = Cast<UClass>(ClassPtr);
				if (ensure(ClassPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsWeakObjectBase::NewProperty(CastChecked<UClass>(ClassPtr)); else OutProp = TTraitsWeakObjectBase::GetProperty(CastChecked<UClass>(ClassPtr));
				}
			}
			else if (FMyMatcher(TEXT("TScriptInterface"), TypeString, ClassPtr))
			{
				if (ensure(ClassPtr && ClassPtr->IsChildOf<UInterface>()))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsScriptIncBase::NewProperty(ClassPtr); else OutProp = TTraitsScriptIncBase::GetProperty(ClassPtr);
				}
			}
			else if (FMyMatcher(NAME_GMP_TObjectPtr, TypeString, ClassPtr))
			{
				if (ensure(ClassPtr))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsObjectBase::NewProperty(ClassPtr); else OutProp = TTraitsObjectBase::GetProperty(ClassPtr);
				}
			}
			else if (FMyMatcher(NAME_GMP_TNativeInterfece, TypeString, ClassPtr))
			{
				if (ensure(ClassPtr && ClassPtr->IsChildOf<UInterface>()))
				{
					GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsNativeIncBase::NewProperty(ClassPtr); else OutProp = TTraitsNativeIncBase::GetProperty(ClassPtr);
				}
			}
			else if (!bInContainer)
			{
				FProperty* SubProp = nullptr;
				static auto SubPropertyFromString = [](const auto& InPrefix, const FString& InTemplateType, FProperty*& InSubProp) { return PropertyFromStringImpl<bNew>(FMyMatcher(InPrefix, InTemplateType), InSubProp, false, true); };
				if (SubPropertyFromString(TEXT("TArray"), TypeString, SubProp))
				{
					if (ensure(SubProp))
					{
						GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsContainerBase::NewArrayProperty(SubProp); else OutProp = TTraitsContainerBase::GetArrayProperty(SubProp);
					}
				}
				if (SubPropertyFromString(TEXT("TSet"), TypeString, SubProp))
				{
					if (ensure(SubProp))
					{
						GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsContainerBase::NewSetProperty(SubProp); else OutProp = TTraitsContainerBase::GetSetProperty(SubProp);
					}
				}
				else if (auto MapMatcher = FMyMatcher(TEXT("TMap"), TypeString))
				{
					FProperty* KeyProp = nullptr;
					FProperty* ValueProp = nullptr;
					FString Left;
					FString Right;
					if (!ensure(MapMatcher.MatchedStr().Split(TEXT(","), &Left, &Right)))
						break;
					ensure(PropertyFromStringImpl<bNew>(Left, KeyProp, false, true));
					ensure(PropertyFromStringImpl<bNew>(Right, ValueProp, false, true));
					if (ensure(KeyProp && ValueProp))
					{
						GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsContainerBase::NewMapProperty(KeyProp, ValueProp); else OutProp = TTraitsContainerBase::GetMapProperty(KeyProp, ValueProp);
					}
				}
			}
			bRet = !!OutProp;
			break;
		}

		if (auto Class = DynamicReflection<UClass>(TypeString))
		{
			GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsObjectBase::NewProperty(Class); else OutProp = TTraitsObjectBase::GetProperty(Class);
		}
		else if (auto Struct = DynamicReflection<UScriptStruct>(TypeString))
		{
			GMP_IF_CONSTEXPR(bNew) OutProp = TTraitsStructBase::NewProperty(Struct); else OutProp = TTraitsStructBase::GetProperty(Struct);
		}
		else if (auto Enum = DynamicReflection<UEnum>(TypeString))
		{
			ensure(bInTemplate);
			GMP_IF_CONSTEXPR(bNew) OutProp = TEnumPropertyBase::NewProperty(Enum, false); else OutProp = TEnumPropertyBase::GetProperty(Enum, false);
		}
		else
		{
			break;
		}
		bRet = true;
	} while (0);
	if (!bRet)
		OutProp = nullptr;

	GMP_IF_CONSTEXPR(!bNew)
	{
		if (OutProp)
			Reflection::GetPropertyStorage().Add(*TypeString, OutProp);
	}

	return ensure(bRet);
}
	// clang-format on

	bool PropertyFromString(FString TypeString, FProperty*& OutProp, bool bInTemplate, bool bInContainer, bool bNew)
	{
		return bNew ? PropertyFromStringImpl<true>(TypeString, OutProp, bInTemplate, bInContainer) : PropertyFromStringImpl<false>(TypeString, OutProp, bInTemplate, bInContainer);
	}

#if GMP_USE_NEW_PROP_FROM_STRING
	bool NewPropertyFromString(FString TypeString, FProperty*& OutProp, bool bInTemplate, bool bInContainer) { return PropertyFromStringImpl<true>(TypeString, OutProp, bInTemplate, bInContainer); }
#endif

	uint32 IsInterger(FName InTypeName)
	{
		static TMap<FName, uint32> IntergerNames = [] {
			TMap<FName, uint32> Ret;
#define GMP_INSERT_CELL(x) Ret.Add(TClass2Name<x>::GetFName(), sizeof(x))
			GMP_INSERT_CELL(bool);
			GMP_INSERT_CELL(int8);
			GMP_INSERT_CELL(uint8);
			GMP_INSERT_CELL(int16);
			GMP_INSERT_CELL(uint16);
			GMP_INSERT_CELL(int32);
			GMP_INSERT_CELL(uint32);
			GMP_INSERT_CELL(int64);
			GMP_INSERT_CELL(uint64);
#undef GMP_INSERT_CELL
			return Ret;
		}();

		auto Bytes = IntergerNames.Find(InTypeName);
		return Bytes ? *Bytes : 0u;
	}

	UEnum* FindEnum(FString InTypeName, bool& bAsByte)
	{
		UEnum* EnumPtr = nullptr;
		InTypeName = InTypeName.Replace(TEXT(" "), TEXT(""), ESearchCase::CaseSensitive);
		InTypeName.RemoveFromStart(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix<1>(), ESearchCase::CaseSensitive);
		if (InTypeName.Len() >= 2 && InTypeName[0] == TEXT('<') && InTypeName[InTypeName.Len() - 1] == TEXT('>'))
		{
			InTypeName = InTypeName.LeftChop(1).RightChop(1);
			bAsByte = true;
		}
		EnumPtr = Reflection::DynamicEnum(InTypeName);
		if (EnumPtr && !bAsByte)
			bAsByte = EnumPtr->GetCppForm() == UEnum::ECppForm::EnumClass;

		return EnumPtr;
	}

	const FStructProperty* PropertyContainsStruct(const FProperty* InProperty, EContainerType* OutType)
	{
		static auto GetTargetStructType = [](const FProperty* InProp) -> const FStructProperty* { return (InProp && InProp->IsA<FStructProperty>()) ? static_cast<const FStructProperty*>(InProp) : nullptr; };
		auto StructType = GetTargetStructType(InProperty);
		if (StructType)
		{
			if (OutType)
				*OutType = EContainerType::Struct;
		}
		else if (auto ArrProp = CastField<FArrayProperty>(InProperty))
		{
			StructType = GetTargetStructType(ArrProp->Inner);
			if (OutType)
				*OutType = EContainerType::Array;
		}
		else if (auto SetProp = CastField<FSetProperty>(InProperty))
		{
			StructType = GetTargetStructType(SetProp->ElementProp);
			if (OutType)
				*OutType = EContainerType::Set;
		}
		return StructType;
	}

	bool PropertyContainsScriptStruct(const FProperty* Prop, UScriptStruct* StructType)
	{
		checkSlow(Prop && StructType);
		static auto IsTargetStructType = [](const FProperty* InProp, auto InStructType) { return InProp && InProp->IsA<FStructProperty>() && static_cast<const FStructProperty*>(InProp)->Struct->IsChildOf(InStructType); };
		do
		{
			if (IsTargetStructType(Prop, StructType))
			{
				break;
			}
			else if (auto ArrProp = CastField<FArrayProperty>(Prop))
			{
				if (IsTargetStructType(ArrProp->Inner, StructType))
					break;
			}
			else if (auto SetProp = CastField<FSetProperty>(Prop))
			{
				if (IsTargetStructType(SetProp->ElementProp, StructType))
					break;
			}
			return false;
		} while (false);
		return true;
	}

	UScriptStruct* DynamicStruct(const FString& StructName) { return DynamicReflection<UScriptStruct>(StructName); }

	UClass* DynamicClass(const FString& ClassName) { return DynamicReflection<UClass>(ClassName); }

	UEnum* DynamicEnum(const FString& EnumName) { return DynamicReflection<UEnum>(EnumName); }
	bool MatchEnum(uint32 Bytes, FName TypeName)
	{
		bool bMatch = false;
		UEnum* EnumPtr = nullptr;
		if (Reflection::FMyMatcher(Class2Name::TTraitsEnumBase::EnumAsBytesPrefix(Bytes), TypeName.ToString(), EnumPtr))
		{
			bMatch = true;
		}
		else
		{
			EnumPtr = Reflection::DynamicEnum(TypeName.ToString());
			bMatch = EnumPtr
					 && ensureAlwaysMsgf((Bytes == 1 && EnumPtr->GetCppForm() == UEnum::ECppForm::EnumClass)  //
											 || (Bytes == 4 && EnumPtr->GetCppForm() != UEnum::ECppForm::EnumClass),
										 TEXT("normal enum always as int32/uint32"));
			if (!bMatch)
				GMP_WARNING(TEXT("bitwise cast enum [%s] to %u bytes interger"), *TypeName.ToString(), Bytes);
		}
		return bMatch;
	}
}  // namespace Reflection

namespace Class2Name
{
	FString TTraitsScriptDelegateBase::GetDelegateNameImpl(bool bMulticast, UFunction* SignatureFunc, bool bExactType)
	{
		FName RetType = TClass2Name<void>::GetFName();
		TStringBuilder<256> ParamsType;
		for (TFieldIterator<FProperty> It(SignatureFunc); It && (It->PropertyFlags & CPF_Parm); ++It)
		{
			if (It->PropertyFlags & CPF_ReturnParm)
			{
				RetType = Reflection::GetPropertyName(*It, bExactType);
			}
			else
			{
				if (ParamsType.Len() > 0)
					ParamsType.Append(TEXT(","));
				ParamsType.Append(Reflection::GetPropertyName(*It, bExactType).ToString());
			}
		}
		return GetDelegateNameImpl(bMulticast, RetType, *ParamsType);
	}

}  // namespace Class2Name

template<uint32 N>
static bool MatchMessageType(const TCHAR (&TMPL)[N], FName TypeName, UClass* TargetClass)
{
	UClass* FromClass = nullptr;
	if (!Reflection::FMyMatcher(TMPL, TypeName.ToString(), FromClass) || !FromClass)
		FromClass = Reflection::DynamicClass(TypeName.ToString());
	return ensureMsgf(FromClass && TargetClass && FromClass->IsChildOf(TargetClass), TEXT("Message Type Mismatch From:%s To:%s"), *GetNameSafe(FromClass), *GetNameSafe(TargetClass));
}

}  // namespace GMP

bool FGMPTypedAddr::MatchEnum(uint32 Bytes) const
{
	using namespace GMP;
#if GMP_WITH_TYPENAME
	return Reflection::MatchEnum(Bytes, TypeName);
#else
	return ensureMsgf(false, TEXT("please enable GMP_WITH_TYPENAME"));
#endif
}

bool FGMPTypedAddr::MatchObjectType(UClass* TargetClass) const
{
#if GMP_WITH_TYPENAME
	return GMP::MatchMessageType(NAME_GMP_TObjectPtr, TypeName, TargetClass);
#else
	return ensureMsgf(false, TEXT("please enable GMP_WITH_TYPENAME"));
#endif
}
bool FGMPTypedAddr::MatchObjectClass(UClass* TargetClass) const
{
#if GMP_WITH_TYPENAME
	return GMP::MatchMessageType(TEXT("TSubclassOf"), TypeName, TargetClass);
#else
	return ensureMsgf(false, TEXT("please enable GMP_WITH_TYPENAME"));
#endif
}
