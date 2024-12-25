//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "Engine/AssetManager.h"
#include "Engine/Engine.h"
#include "Engine/GameEngine.h"
#include "Engine/GameInstance.h"
#include "Engine/ObjectReferencer.h"
#include "Engine/StreamableManager.h"
#include "Engine/World.h"
#include "GMPCore.h"
#include "GMPReflection.h"
#include "GMPSignalsImpl.h"
#include "GMPTypeTraits.h"
#include "Misc/CommandLine.h"
#include "Misc/DelayedAutoRegister.h"
#include "Modules/ModuleInterface.h"
#include "UObject/CoreRedirects.h"

#include <algorithm>

#if WITH_EDITOR
#include "Editor.h"
#include "GameDelegates.h"
#endif

namespace GMP
{
namespace WorldLocals
{
	UWorld* GetGameWorldChecked(bool bEnsureGameWorld)
	{
		auto World = GWorld;
#if WITH_EDITOR
		if (GIsEditor && !GIsPlayInEditorWorld)
		{
			static auto FindFirstPIEWorld = [] {
				UWorld* World = nullptr;
				auto& WorldContexts = GEngine->GetWorldContexts();
				for (const FWorldContext& Context : WorldContexts)
				{
					auto CurWorld = Context.World();
					if (IsValid(CurWorld) && (CurWorld->WorldType == EWorldType::PIE /* || CurWorld->WorldType == EWorldType::Game*/))
					{
						World = CurWorld;
						break;
					}
				}

				// ensure(World);
				return World;
			};

			FWorldContext* WorldContext = GEngine->GetWorldContextFromPIEInstance(std::max(0, UE::GetPlayInEditorID()));
			if (WorldContext && ensure(WorldContext->WorldType == EWorldType::PIE /* || WorldContext->WorldType == EWorldType::Game*/))
			{
				World = WorldContext->World();
			}
			else
			{
				World = FindFirstPIEWorld();
			}

			if (!World && !bEnsureGameWorld)
			{
				World = GEditor->GetEditorWorldContext().World();
			}
		}
#else
		check(World);
#endif
		return World;
	}
	static UGameInstance* FindGameInstance()
	{
		UGameInstance* Instance = nullptr;
#if WITH_EDITOR
		if (GIsEditor)
		{
			ensureAlwaysMsgf(!GIsInitialLoad && GEngine, TEXT("Is it needed to get singleton before engine initialized?"));
			UWorld* World = GetGameWorldChecked(false);
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
	void AddObjectReference(UObject* InCtx, UObject* Obj)
	{
		check(IsValid(Obj));
		static auto TryGetGameWorld = [](UObject* In) -> UWorld* {
			auto CurWorld = Cast<UWorld>(In);
			if (CurWorld && CurWorld->IsGameWorld())
				return CurWorld;
			return nullptr;
		};
		if (!IsValid(InCtx))
		{
			auto Instance = FindGameInstance();
			ensureAlwaysMsgf(GIsEditor || Instance != nullptr, TEXT("GameInstance Error"));
#if UE_4_20_OR_LATER
			if (Instance)
			{
				Instance->RegisterReferencedObject(Obj);
			}
			else
#endif
			{
				UE_LOG(LogGMP, Log, TEXT("GMPLocalStorage::AddObjectReference RootObject Added [%s]"), *GetNameSafe(Obj));
				Obj->AddToRoot();
#if WITH_EDITOR
				// FGameDelegates::Get().GetEndPlayMapDelegate().Add(CreateWeakLambda(Obj, [Obj] { Obj->RemoveFromRoot(); }));
				FEditorDelegates::EndPIE.Add(CreateWeakLambda(Obj, [Obj](const bool) {
					UE_LOG(LogGMP, Log, TEXT("GMPLocalStorage::AddObjectReference RootObject Removed [%s]"), *GetNameSafe(Obj));
					Obj->RemoveFromRoot();
				}));
#endif
			}
		}
		else if (UGameInstance* Instance = Cast<UGameInstance>(InCtx))
		{
			Instance->RegisterReferencedObject(Obj);
		}
#if WITH_EDITOR
		else if (auto CurWorld = TryGetGameWorld(InCtx))
		{
			CurWorld->PerModuleDataObjects.AddUnique(Obj);
		}
		else if (UWorld* World = InCtx->GetWorld())
		{
			// EditorWorld
			if (auto Ctx = GEngine->GetWorldContextFromWorld(World))
			{
				static FName ObjName = FName("__GS_Referencers__");
#if UE_5_00_OR_LATER
				TObjectPtr<UObjectReferencer>* ReferencerPtr = Ctx->ObjectReferencers.FindByPredicate([](TObjectPtr<UObjectReferencer> Obj) { return Obj && (Obj->GetFName() == ObjName); });
#else
				UObjectReferencer** ReferencerPtr = Ctx->ObjectReferencers.FindByPredicate([](UObjectReferencer* Obj) { return Obj && (Obj->GetFName() == ObjName); });
#endif
				if (ReferencerPtr)
				{
					(*ReferencerPtr)->ReferencedObjects.AddUnique(Obj);
				}
				else
				{
					//FindObject<UClass>(nullptr, TEXT("/Script/Engine.ObjectReferencer"), true)
					auto Referencer = static_cast<UObjectReferencer*>(NewObject<UObject>(World, GMP::Reflection::DynamicClass(TEXT("ObjectReferencer")), ObjName, RF_Transient));
					Referencer->ReferencedObjects.AddUnique(Obj);
					Ctx->ObjectReferencers.Add(Referencer);
					if (TrueOnFirstCall([] {}))
					{
						FWorldDelegates::OnWorldCleanup.AddLambda([](UWorld* World, bool bSessionEnded, bool bCleanupResources) {
							do
							{
								auto WorldCtx = GEngine->GetWorldContextFromWorld(World);
								if (!WorldCtx)
									break;

								auto Idx = WorldCtx->ObjectReferencers.IndexOfByPredicate([](class UObjectReferencer* Obj) { return Obj && (Obj->GetFName() == ObjName); });
								if (Idx == INDEX_NONE)
									break;
								WorldCtx->ObjectReferencers.RemoveAtSwap(Idx);
							} while (false);
						});
					}
				}
			}
			else
			{
				World->PerModuleDataObjects.AddUnique(Obj);
			}
		}
#endif
		else if (auto ThisWorld = InCtx->GetWorld())

		{
			ThisWorld->PerModuleDataObjects.AddUnique(Obj);
		}
		else
		{
			ensure(false);
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

static TMap<FName, TArray<FName>> NativeParentsInfo;

static TSet<FName> UnSupportedName;
static TMap<FName, TArray<FName>> ParentsInfo;

static const TArray<FName>* GetClassInfos(FName InClassName)
{
	if (UnSupportedName.Contains(InClassName))
		return nullptr;

	if (auto FindDynamic = ParentsInfo.Find(InClassName))
		return FindDynamic;

	if (auto Cls = Reflection::DynamicClass(InClassName.ToString()))
	{
		auto TypeName = Cls->IsNative() ? Cls->GetFName() : FName(*FSoftClassPath(Cls).ToString());
		auto& Arr = ParentsInfo.Emplace(TypeName);
		do
		{
			Arr.Insert(Cls->GetFName(), 0);
			Cls = Cls->GetSuperClass();
		} while (Cls);
		return &Arr;
	}
	UnSupportedName.Add(InClassName);
	return nullptr;
}

FName FNameSuccession::GetClassName(UClass* InClass)
{
	auto TypeName = InClass->IsNative() ? InClass->GetFName() : FName(*FSoftClassPath(InClass).ToString());
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
FName FNameSuccession::FindCommonBase(FName lhs, FName rhs)
{
	// static auto ObjectClsName = GetNativeClassPtrName(UObject::StaticClass());
	// IsDerivedFrom(lhs, ObjectClsName)
	do
	{
		if (lhs == rhs)
			return lhs;

		auto LInfos = GetClassInfos(lhs);
		if (!LInfos)
			break;
		auto RInfos = GetClassInfos(rhs);
		if (!RInfos)
			break;

		auto MaxIdx = FMath::Max(LInfos->Num(), RInfos->Num()) - 1;
		for (auto i = MaxIdx; i >= 0; --i)
		{
			if ((*LInfos)[i] == (*RInfos)[i])
				return (*LInfos)[i];
		}
	} while (false);
	return NAME_None;
}
void CreateGMPSourceAndHandlerDeleter();
void DestroyGMPSourceAndHandlerDeleter();

static bool GMPModuleInited = false;
static bool GMPEngineInited = false;
bool IsGMPModuleInited()
{
	return GMPModuleInited;
}
bool IsBothInited()
{
	return GMPModuleInited && GMPEngineInited;
}

static FSimpleMulticastDelegate Startups;
static FSimpleMulticastDelegate Shutdowns;

GMP_API void OnModuleLifetime(FSimpleDelegate Startup, FSimpleDelegate Shutdown)
{
	if (Shutdown.IsBound())
	{
		Shutdowns.Add(Shutdown);
	}

	if (IsBothInited())
	{
		Startup.ExecuteIfBound();
	}
	else
	{
		Startups.Add(Startup);
	}
}
GMP_API void OnGMPTagReady(FSimpleDelegate Callback)
{
	OnModuleLifetime(MoveTemp(Callback), {});
}

static void BroadcastOnTmp(FSimpleMulticastDelegate& Delegates)
{
	FSimpleMulticastDelegate Tmps;
	Swap(Tmps, Delegates);
	Tmps.Broadcast();
}
static FDelayedAutoRegisterHelper DelayOnEngineInitCompleted(EDelayedRegisterRunPhase::EndOfEngineInit, [] {
	GMPEngineInited = true;
	if (IsBothInited())
	{
		BroadcastOnTmp(Startups);
	}
});
}  // namespace GMP

class FGMPPlugin final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		GMP::Class2Prop::InitPropertyMapBase();
#if WITH_EDITOR
		TArray<FCoreRedirect> Redirects{FCoreRedirect(ECoreRedirectFlags::Type_Struct, TEXT("/Script/GMP.MessageAddr"), TEXT("/Script/GMP.GMPTypedAddr"))};
		FCoreRedirects::AddRedirectList(Redirects, TEXT("redirects GMP"));

		if (TrueOnFirstCall([]{}))
		{
			using namespace GMP;
			static auto EmptyInfo = [] {
				ParentsInfo.Empty();
				UnSupportedName.Empty();
			};

			FCoreUObjectDelegates::PreLoadMap.AddLambda([](const FString& MapName) { EmptyInfo(); });
			if (GIsEditor)
			{
				FEditorDelegates::PreBeginPIE.AddLambda([](bool bIsSimulating) { EmptyInfo(); });
			}
		}
#endif

		auto Delegate = FSimpleDelegate::CreateLambda([] {
			GMP::GMPModuleInited = true;
			if (GMP::IsBothInited())
			{
				GMP::BroadcastOnTmp(GMP::Startups);
			}
		});
#if 1
		Delegate.ExecuteIfBound();
#else

#if WITH_EDITOR
		if (!GIsRunning && GIsEditor)
		{
			FCoreDelegates::OnFEngineLoopInitComplete.Add(MoveTemp(Delegate));
		}
		else
#endif
		{
			Delegate.ExecuteIfBound();
		}
#endif
		GMP::CreateGMPSourceAndHandlerDeleter();

		extern void ProcessXCommandFromCmdline(UWorld * InWorld, const TCHAR* Msg);

#if WITH_EDITOR
		FEditorDelegates::OnMapOpened.AddLambda([](const FString& /* Filename */, bool /*bAsTemplate*/) {
			if (GIsEditor && ensure(GWorld))
			{
				if (TrueOnFirstCall([]{}) && FCString::Strstr(FCommandLine::GetOriginal(), TEXT(" --- ")))
				{
					ProcessXCommandFromCmdline(GWorld, TEXT("OnMapOpened"));
				}
			}
		});
#endif
#if PLATFORM_DESKTOP
		struct FHandleResult
		{
			FDelegateHandle Handle;
		};
		auto HandleResult = MakeShared<FDelegateHandle>();
		*HandleResult = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda([HandleResult](UWorld* NewWorld) {
			ProcessXCommandFromCmdline(NewWorld, TEXT("PostLoadMapWithWorld"));
			FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(*HandleResult);
		});

		//GMP::OnGMPTagReady(FSimpleDelegate::CreateLambda([] {
		FGMPHelper::UnsafeListenMessage(
			MSGKEY("GameState.OnPostStartPlay"),
			[](UWorld* NewWorld) { ProcessXCommandFromCmdline(NewWorld, TEXT("PostStartPlay")); },
			1);
		//}));
#endif
	}
	virtual void ShutdownModule() override
	{
		GMP::DestroyGMPSourceAndHandlerDeleter();
		GMP::BroadcastOnTmp(GMP::Shutdowns);
		GMP::GMPModuleInited = false;
	}
};

IMPLEMENT_MODULE(FGMPPlugin, GMP)
