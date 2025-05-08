//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.

#include "GMPUtils.h"

#include "Engine/LatentActionManager.h"
#include "Modules/ModuleManager.h"

namespace GMP
{
extern bool IsGMPModuleInited();

void FMessageUtils::UnbindMessage(const FMSGKEYFind& MessageId, const UObject* Obj)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsGMPModuleInited() && ensure(Obj))
#endif
	{
		GetMessageHub()->UnbindMessage(MessageId, Obj);
	}
}

void FMessageUtils::UnbindMessage(const FMSGKEYFind& MessageId, FGMPKey GMPKey)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (IsGMPModuleInited() && ensure(GMPKey))
#endif
	{
		GetMessageHub()->UnbindMessage(MessageId, GMPKey);
	}
}

void FMessageUtils::ScriptUnbindMessage(const FMSGKEYAny& K, FGMPKey InKey)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		GetMessageHub()->ScriptUnbindMessage(FMSGKEYFind(K), InKey);
	}
}

void FMessageUtils::ScriptUnbindMessage(const FMSGKEYAny& K, const UObject* Listener)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		GetMessageHub()->ScriptUnbindMessage(FMSGKEYFind(K), Listener);
	}
}

void FMessageUtils::ScriptRemoveSigSource(const FSigSource InSigSrc)
{
#if !UE_BUILD_SHIPPING && !UE_BUILD_TEST
	if (ensure(IsGMPModuleInited()))
#endif
	{
		FSigSource::RemoveSource(InSigSrc);
	}
}

FMessageBody* FMessageUtils::GetCurrentMessageBody()
{
	return GetMessageHub()->GetCurrentMessageBody();
}

UGMPManager* FMessageUtils::GetManager()
{
	auto Ret = ::GetMutableDefault<UGMPManager>();
	GMP_CHECK(Ret && IsGMPModuleInited());
	return Ret;
}

FMessageHub* FMessageUtils::GetMessageHub()
{
	return &GetManager()->GetHub();
}

struct FLifetimePair
{
	using FFunctionArray = TArray<TUniqueFunction<void(IModuleInterface*)>>;
	FFunctionArray Startups;
	FFunctionArray Shutdowns;
	void OnStartup(IModuleInterface* Inc)
	{
		FFunctionArray Arr;
		Swap(Arr, Startups);
		for (auto& Startup : Arr)
		{
			Startup(Inc);
		}
	}
	void OnShutdown(IModuleInterface* Inc)
	{
		FFunctionArray Arr;
		Swap(Arr, Shutdowns);
		for (auto& Shutdown : Arr)
		{
			Shutdown(Inc);
		}
	}
};

void FGMPModuleUtils::OnModuleLifetimeImpl(FName ModuleName, TUniqueFunction<void(IModuleInterface*)> Startup, TUniqueFunction<void(IModuleInterface*)> Shutdown)
{
	if (ModuleName == NAME_None)
	{
		ModuleName = "GMP";
	}

	auto& ModuleManager = FModuleManager::Get();
	if (ModuleManager.IsModuleLoaded(ModuleName) && Startup)
	{
		auto ModulePtr = ModuleManager.GetModule(ModuleName);
		Startup(ModulePtr);
		Startup = nullptr;
	}

	if (Startup || Shutdown)
	{
		static TMap<FName, FLifetimePair> ModuleCallbacks;
		auto& LifetimePair = ModuleCallbacks.FindOrAdd(ModuleName);
		if (Startup)
		{
			LifetimePair.Startups.Emplace(MoveTemp(Startup));
		}
		if (Shutdown)
		{
			LifetimePair.Shutdowns.Emplace(MoveTemp(Shutdown));
		}

		if (TrueOnFirstCall([] {}))
		{
			ModuleManager.OnModulesChanged().AddLambda([](FName InModuleName, EModuleChangeReason InReason) {
				if (FLifetimePair* Pair = ModuleCallbacks.Find(InModuleName))
				{
					if (InReason == EModuleChangeReason::ModuleLoaded)
					{
						auto ModulePtr = FModuleManager::Get().GetModule(InModuleName);
						Pair->OnStartup(ModulePtr);
					}
					else if (InReason == EModuleChangeReason::ModuleUnloaded)
					{
						auto ModulePtr = FModuleManager::Get().GetModule(InModuleName);
						Pair->OnShutdown(ModulePtr);
						ModuleCallbacks.Remove(InModuleName);
					}
				}
			});
		}
	}
}

}  // namespace GMP
