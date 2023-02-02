//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPReflection.h"
#include "GMPSignalsImpl.h"
#include "GMPTypeTraits.h"
#include "Modules/ModuleInterface.h"
#include "UObject/CoreRedirects.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace GMP
{
int32 LastCountEnsureForRepeatListen = 1;
static FAutoConsoleVariableRef CVarEnsureOnRepeatedListening(TEXT("GMP.EnsureOnRepeatedListening"), LastCountEnsureForRepeatListen, TEXT("Whether to enusre repeated listening with same message key and listener"), ECVF_Cheat);
int32& ShouldEnsureOnRepeatedListening()
{
	LastCountEnsureForRepeatListen = (LastCountEnsureForRepeatListen > 0) ? LastCountEnsureForRepeatListen - 1 : LastCountEnsureForRepeatListen;
	return LastCountEnsureForRepeatListen;
}

namespace Class2Prop
{
	void InitPropertyMapBase();
}  // namespace Class2Prop

static TMap<FName, TSet<FName>> NativeParentsInfo;

static TSet<FName> UnSupportedName;
static TMap<FName, TSet<FName>> ParentsInfo;

static const TSet<FName>* GetClassInfos(FName InClassName)
{
	if(UnSupportedName.Contains(InClassName))
		return nullptr;

	if (auto FindDynamic = ParentsInfo.Find(InClassName))
		return FindDynamic;

	if (auto Cls = Reflection::DynamicClass(InClassName.ToString()))
	{
		auto TypeName = Cls->IsNative() ? *Cls->GetName() : *FSoftClassPath(Cls).ToString();
		auto& Set = ParentsInfo.Emplace(TypeName);
		do
		{
			Set.Add(Cls->GetFName());
			Cls = Cls->GetSuperClass();
		} while (Cls);
		return &Set;
	}
	UnSupportedName.Add(InClassName);
	return nullptr;
}

FName FNameSuccession::GetClassName(UClass* InClass)
{
	auto TypeName = InClass->IsNative() ? *InClass->GetName() : *FSoftClassPath(InClass).ToString();
	auto& Set = ParentsInfo.Emplace(TypeName);
	do
	{
		Set.Add(InClass->GetFName());
		InClass = InClass->GetSuperClass();
	} while (InClass);

	return TypeName;
}

FName FNameSuccession::GetNativeClassName(UClass* InClass)
{
	auto TypeName = InClass->GetFName();
	if (!NativeParentsInfo.Contains(TypeName))
	{
		auto& Set = NativeParentsInfo.Emplace(TypeName);
		do
		{
			Set.Add(InClass->GetFName());
			InClass = InClass->GetSuperClass();
		} while (InClass);
	}
	return TypeName;
}

FName FNameSuccession::GetNativeClassPtrName(UClass* InClass)
{
	auto TypeName = InClass->GetFName();
	if (!NativeParentsInfo.Contains(TypeName))
	{
		auto& Set = NativeParentsInfo.Emplace(TypeName);
		do
		{
			Set.Add(InClass->GetFName());
			InClass = InClass->GetSuperClass();
		} while (InClass);
	}
	return *FString::Printf(NAME_GMP_TObjectPtr TEXT("<%s>"), *TypeName.ToString());
}

namespace Reflection
{
	extern bool MatchEnum(uint32 Bytes, FName TypeName);
}  // namespace Reflection

bool FNameSuccession::MatchEnums(FName IntType, FName EnumType)
{
	auto Bytes = Reflection::IsInterger(IntType);
	return !!Bytes && Reflection::MatchEnum(Bytes, EnumType);
}

bool FNameSuccession::IsDerivedFrom(FName Type, FName ParentType)
{
	auto FindNative = NativeParentsInfo.Find(Type);
	if (FindNative && FindNative->Contains(ParentType))
		return true;

	if (auto Find = GetClassInfos(Type))
	{
		return Find->Contains(ParentType);
	}
	return false;
}

bool FNameSuccession::IsTypeCompatible(FName lhs, FName rhs)
{
	do
	{
		if ((lhs == rhs))
			break;

		if (lhs == NAME_GMPSkipValidate || rhs == NAME_GMPSkipValidate)
			break;

		if (!lhs.IsValid() || !rhs.IsValid())
			break;

		if (lhs.IsNone() || rhs.IsNone())
			break;

		if (MatchEnums(lhs, rhs))
			break;
		if (MatchEnums(rhs, lhs))
			break;

		if (IsDerivedFrom(lhs, rhs))
			break;
		if (IsDerivedFrom(rhs, lhs))
			break;
		return false;
	} while (false);
	return true;
}

void OnGMPTagReady(FSimpleDelegate Callback)
{
#if WITH_EDITOR
	if (!GIsRunning && GIsEditor)
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Add(MoveTemp(Callback));
	}
	else
#endif
	{
		Callback.ExecuteIfBound();
	}
}

}  // namespace GMP

class FGMPPlugin final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		TArray<FCoreRedirect> Redirects{FCoreRedirect(ECoreRedirectFlags::Type_Struct, TEXT("/Script/GMP.MessageAddr"), TEXT("/Script/GMP.GMPTypedAddr"))};
		FCoreRedirects::AddRedirectList(Redirects, TEXT("redirects GMP"));

		using namespace GMP;
		Class2Prop::InitPropertyMapBase();
#if WITH_EDITOR
		if (TrueOnFirstCall([] {}))
		{
			static auto EmptyInfo = [] {
				ParentsInfo.Empty();
				UnSupportedName.Empty();
			};

			FCoreUObjectDelegates::PreLoadMap.AddLambda([](const FString& MapName) { EmptyInfo(); });
			if (GIsEditor)
				FEditorDelegates::PreBeginPIE.AddLambda([](bool bIsSimulating) { EmptyInfo(); });
		}
#endif
	}
	virtual void ShutdownModule() override {}
};
IMPLEMENT_MODULE(FGMPPlugin, GMP)
