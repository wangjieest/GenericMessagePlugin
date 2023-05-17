//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "Engine/GameEngine.h"
#include "Engine/GameInstance.h"
#include "Engine/ObjectReferencer.h"
#include "GMPReflection.h"
#include "GMPSignalsImpl.h"
#include "GMPTypeTraits.h"
#include "Misc/DelayedAutoRegister.h"
#include "Modules/ModuleInterface.h"
#include "UObject/CoreRedirects.h"

#if WITH_EDITOR
#include "Editor.h"
#endif

namespace GMP
{
namespace WorldLocals
{
	void AddObjectReference(UWorld* World, UObject* Obj)
	{
		GMP_CHECK_SLOW(IsValid(Obj));
		if (!IsValid(World))
		{
#if UE_4_20_OR_LATER
			static auto FindGameInstance = [] {
				UGameInstance* Instance = nullptr;
#if WITH_EDITOR
				if (GIsEditor)
				{
					ensureAlwaysMsgf(!GIsInitialLoad && GEngine, TEXT("Is it needed to get singleton before engine initialized?"));
					UWorld* World = nullptr;
					for (const FWorldContext& Context : GEngine->GetWorldContexts())
					{
						auto CurWorld = Context.World();
						if (IsValid(CurWorld))
						{
							if (Context.WorldType == EWorldType::PIE /*&& Context.PIEInstance == 0*/)
							{
								World = CurWorld;
								break;
							}

							if (Context.WorldType == EWorldType::Game)
							{
								World = CurWorld;
								break;
							}

							if (CurWorld->GetNetMode() == ENetMode::NM_Standalone || (CurWorld->GetNetMode() == ENetMode::NM_Client && Context.PIEInstance == 2))
							{
								World = CurWorld;
								break;
							}
						}
					}
					Instance = World ? World->GetGameInstance() : nullptr;
				}
				else
#endif
				{
					if (UGameEngine* GameEngine = Cast<UGameEngine>(GEngine))
					{
						Instance = GameEngine->GameInstance;
					}
				}
				return Instance;
			};

			auto Instance = FindGameInstance();
			ensureAlwaysMsgf(GIsEditor || Instance != nullptr, TEXT("GameInstance Error"));
			if (Instance)
			{
				Instance->RegisterReferencedObject(Obj);
			}
			else
#endif
			{
				Obj->AddToRoot();
#if WITH_EDITOR
				// FGameDelegates::Get().GetEndPlayMapDelegate().Add(CreateWeakLambda(Obj, [Obj] { Obj->RemoveFromRoot(); }));
				FEditorDelegates::EndPIE.Add(CreateWeakLambda(Obj, [Obj](const bool) { Obj->RemoveFromRoot(); }));
#endif
			}
		}
#if WITH_EDITOR
		else if (World->IsGameWorld())
		{
			World->PerModuleDataObjects.AddUnique(Obj);
		}
		else if (World)
		{
			// EditorWorld
			FWorldContext* Ctx = GEngine->GetWorldContextFromWorld(World);
			if (ensure(Ctx))
			{
				static const FName ObjName = FName("__GS_Referencers__");
#if UE_5_00_OR_LATER
				static auto IndexOfObjectReferencers = [](auto& ObjectReferencers) { return ObjectReferencers.IndexOfByPredicate([&](TObjectPtr<UObjectReferencer> Obj) { return Obj && (Obj->GetFName() == ObjName); }); };
#else
				static auto IndexOfObjectReferencers = [](auto& ObjectReferencers) { return ObjectReferencers.IndexOfByPredicate([&](UObjectReferencer* Obj) { return Obj && (Obj->GetFName() == ObjName); }); };
#endif

				auto ReferencerIdx = IndexOfObjectReferencers(Ctx->ObjectReferencers);
				if (ReferencerIdx != INDEX_NONE)
				{
					Ctx->ObjectReferencers[ReferencerIdx]->ReferencedObjects.AddUnique(Obj);
				}
				else
				{
					UObjectReferencer* Referencer = static_cast<UObjectReferencer*>(NewObject<UObject>(World, GMP::Reflection::DynamicClass(TEXT("ObjectReferencer")), ObjName, RF_Transient));
					Referencer->ReferencedObjects.AddUnique(Obj);
					Ctx->ObjectReferencers.Add(Referencer);
					if (TrueOnFirstCall([] {}))
					{
						FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool bSessionEnded, bool bCleanupResources) {
							do
							{
								FWorldContext* WorldCtx = GEngine->GetWorldContextFromWorld(World);
								if (!WorldCtx)
									break;
								auto Idx = IndexOfObjectReferencers(WorldCtx->ObjectReferencers);
								if (Idx == INDEX_NONE)
									break;
								WorldCtx->ObjectReferencers.RemoveAtSwap(Idx);
							} while (false);
						});
					}
				}
			}
		}
#endif
		else
		{
			World->PerModuleDataObjects.AddUnique(Obj);
		}
	}

}  // namespace WorldLocals

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
	if (UnSupportedName.Contains(InClassName))
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
void CreateGMPSourceAndHandlerDeleter();
void DestroyGMPSourceAndHandlerDeleter();

static bool GMPModuleInited = false;
static FSimpleMulticastDelegate Callbacks;

GMP_API void OnGMPTagReady(FSimpleDelegate Callback)
{
#if WITH_EDITOR
	if (!GIsRunning && GIsEditor)
	{
		FCoreDelegates::OnFEngineLoopInitComplete.Add(MoveTemp(Callback));
	}
	else
#endif
	{
		GMPModuleInited = true;
		Callback.ExecuteIfBound();
	}
}
bool IsGMPModuleInited()
{
	return GMPModuleInited;
}

static FDelayedAutoRegisterHelper DelayOnEngineInitCompleted(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
	GMPModuleInited = true;
	FSimpleMulticastDelegate Tmps;
	Swap(Tmps, Callbacks);
	Tmps.Broadcast();
});
}  // namespace GMP

class FGMPPlugin final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if WITH_EDITOR
		TArray<FCoreRedirect> Redirects{FCoreRedirect(ECoreRedirectFlags::Type_Struct, TEXT("/Script/GMP.MessageAddr"), TEXT("/Script/GMP.GMPTypedAddr"))};
		FCoreRedirects::AddRedirectList(Redirects, TEXT("redirects GMP"));

		if (TrueOnFirstCall([] {}))
		{
			using namespace GMP;
			Class2Prop::InitPropertyMapBase();
			static auto EmptyInfo = [] {
				ParentsInfo.Empty();
				UnSupportedName.Empty();
			};

			FCoreUObjectDelegates::PreLoadMap.AddLambda([](const FString& MapName) { EmptyInfo(); });
			if (GIsEditor)
				FEditorDelegates::PreBeginPIE.AddLambda([](bool bIsSimulating) { EmptyInfo(); });
		}
#endif
		GMP::GMPModuleInited = true;
		GMP::CreateGMPSourceAndHandlerDeleter();
	}
	virtual void ShutdownModule() override
	{
		GMP::DestroyGMPSourceAndHandlerDeleter();
		GMP::GMPModuleInited = false;
	}
};
IMPLEMENT_MODULE(FGMPPlugin, GMP)
