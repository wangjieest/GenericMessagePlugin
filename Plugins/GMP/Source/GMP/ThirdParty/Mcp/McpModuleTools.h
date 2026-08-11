//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Self-registration for tools. A tool declares itself next to its own implementation with
// MCP_REGISTER_TOOL; the module compiling it claims its bucket once with MCP_IMPLEMENT_MODULE_TOOLS.
// No central list, and a module can live in a different DLL from the one hosting the registry.
#pragma once

#ifndef UNREAL_GMP_MCP_MODULE_TOOLS_H
#define UNREAL_GMP_MCP_MODULE_TOOLS_H

#include "Mcp/McpRegistry.h"

#if GMP_WITH_MCP

namespace MCP_NAMESPACE
{

// The tools of one compiling module. Keyed by module name rather than by a per-DLL static, because a
// monolithic build links every module into one image and would otherwise merge all the buckets.
class FModuleTools
{
public:
	typedef TSharedRef<IMcpTool> (*FFactory)();

	static MCP_API FModuleTools& Get(const TCHAR* ModuleName);

	// Called during static init, so it only records the factory. One arriving after the bucket has
	// already registered — a sibling translation unit constructed later — is instantiated on the spot.
	MCP_API void AddFactory(FFactory Factory);

	MCP_API void Register();
	MCP_API void Unregister();

	// Registers every bucket that has not registered yet. Whatever opens a transport calls this first:
	// UE broadcasts multicast delegates in reverse order, so a provider cannot be scheduled to run
	// before the endpoint by registering its delegate earlier.
	static MCP_API void RegisterAll();

	bool IsRegistered() const { return bRegistered; }
	const TArray<TSharedRef<IMcpTool>>& GetLiveTools() const { return Live; }

private:
	void Instantiate(FFactory Factory);
	void InstantiateAll();
	void OnRefresh();

	TArray<FFactory> Factories;
	TArray<TSharedRef<IMcpTool>> Live;
	FDelegateHandle RefreshHandle;
	bool bRegistered = false;
};

// Registers the module's bucket once the engine is up, and drops it while the module's code is still
// mapped. ModulesChangedEvent cannot serve here: it is broadcast after the DLL has been freed.
struct FModuleToolsScope
{
	MCP_API explicit FModuleToolsScope(const TCHAR* InModuleName);
	MCP_API ~FModuleToolsScope();

private:
	const TCHAR* ModuleName;
};

struct FToolFactoryRegistrar
{
	FToolFactoryRegistrar(const TCHAR* ModuleName, FModuleTools::FFactory Factory) { FModuleTools::Get(ModuleName).AddFactory(Factory); }
};

}  // namespace MCP_NAMESPACE

#define MCP_JOIN_INNER(A, B) A##B
#define MCP_JOIN(A, B) MCP_JOIN_INNER(A, B)

// UBT gives every module its own; a standalone build has one nameless bucket.
#ifndef UE_MODULE_NAME
#define UE_MODULE_NAME "Mcp"
#endif

// At file scope in the tool's own cpp, right below the class.
#define MCP_REGISTER_TOOL(ToolClass)                                                                                    \
	static const MCP_NAMESPACE::FToolFactoryRegistrar MCP_JOIN(McpToolRegistrar_, __LINE__)(TEXT(UE_MODULE_NAME), []() -> \
		TSharedRef<MCP_NAMESPACE::IMcpTool> { return MakeShared<ToolClass>(); })

// At file scope in any one cpp of a module that registers tools.
#define MCP_IMPLEMENT_MODULE_TOOLS() static const MCP_NAMESPACE::FModuleToolsScope GMcpModuleToolsScope(TEXT(UE_MODULE_NAME))

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_MODULE_TOOLS_H
