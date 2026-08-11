//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// The single definition of the module buckets. Exactly one TU in the host includes this; modules in
// other DLLs link the exported symbols and share these buckets.
#pragma once

#include "Mcp/McpModuleTools.h"

#if GMP_WITH_MCP && MCP_REGISTRY_ISOLATED_IMPL

#include "CoreGlobals.h"
#include "Misc/DelayedAutoRegister.h"

namespace MCP_NAMESPACE
{
namespace
{
	// Never destroyed on purpose: a module's scope object can outlive this map at process exit.
	TMap<FString, TUniquePtr<FModuleTools>>& Buckets()
	{
		static TMap<FString, TUniquePtr<FModuleTools>>* Map = new TMap<FString, TUniquePtr<FModuleTools>>();
		return *Map;
	}
}  // namespace

FModuleTools& FModuleTools::Get(const TCHAR* ModuleName)
{
	TUniquePtr<FModuleTools>& Slot = Buckets().FindOrAdd(ModuleName);
	if (!Slot)
	{
		Slot = MakeUnique<FModuleTools>();
	}
	return *Slot;
}

void FModuleTools::Instantiate(FFactory Factory)
{
	TSharedRef<IMcpTool> Tool = Factory();
	if (FRegistry::Get().AddTool(Tool))
	{
		Live.Add(Tool);
	}
}

void FModuleTools::InstantiateAll()
{
	for (FFactory Factory : Factories)
	{
		Instantiate(Factory);
	}
}

void FModuleTools::AddFactory(FFactory Factory)
{
	Factories.Add(Factory);
	if (bRegistered)
	{
		Instantiate(Factory);
	}
}

void FModuleTools::Register()
{
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;
	RefreshHandle = FRegistry::Get().OnRefreshTools().AddRaw(this, &FModuleTools::OnRefresh);
	InstantiateAll();
}

void FModuleTools::RegisterAll()
{
	for (const TPair<FString, TUniquePtr<FModuleTools>>& Bucket : Buckets())
	{
		if (Bucket.Value)
		{
			Bucket.Value->Register();
		}
	}
}

void FModuleTools::OnRefresh()
{
	// RefreshTools emptied the registry, so the refs held here are the stale ones.
	Live.Reset();
	InstantiateAll();
}

void FModuleTools::Unregister()
{
	if (!bRegistered)
	{
		return;
	}
	bRegistered = false;
	FRegistry::Get().OnRefreshTools().Remove(RefreshHandle);
	RefreshHandle.Reset();
	for (const TSharedRef<IMcpTool>& Tool : Live)
	{
		FRegistry::Get().RemoveTool(Tool);
	}
	Live.Reset();
}

FModuleToolsScope::FModuleToolsScope(const TCHAR* InModuleName)
	: ModuleName(InModuleName)
{
	// Deferred rather than registered here: for a module loaded after the phase has passed the helper
	// runs the body immediately, which is mid static init, before sibling TUs have added their
	// factories. Whatever arrives late is picked up by AddFactory.
	FDelayedAutoRegisterHelper Deferred(
		EDelayedRegisterRunPhase::EndOfEngineInit, [Name = FString(InModuleName)] { FModuleTools::Get(*Name).Register(); }, /*bRerunOnLiveCodingReload*/ true);
}

FModuleToolsScope::~FModuleToolsScope()
{
	// Unloading a module runs this while its code is still mapped. At process exit there is nothing
	// worth unwinding and the registry may already be gone.
	if (!IsEngineExitRequested())
	{
		FModuleTools::Get(ModuleName).Unregister();
	}
}

}  // namespace MCP_NAMESPACE

#endif
