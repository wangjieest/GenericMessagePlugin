//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Tool registry — the only type consumers outside this folder need. It mirrors the tool half of UE 5.8's
// IModelContextProtocolModule, so adopting the engine implementation rewrites this file's body and
// nothing else.
#pragma once

#ifndef UNREAL_GMP_MCP_REGISTRY_H
#define UNREAL_GMP_MCP_REGISTRY_H

#include "Mcp/McpTool.h"

#if GMP_WITH_MCP

// Linkage of the registry accessor. The host predefines this to its export macro so every module
// resolves to the one instance in the host; see MCP_REGISTRY_ISOLATED_IMPL below.
#ifndef MCP_API
#define MCP_API
#endif

#ifndef MCP_REGISTRY_ISOLATED_IMPL
#define MCP_REGISTRY_ISOLATED_IMPL 0
#endif

// Under UE the registry joins the GC pass so a tool can hold a UObject between calls. Without the
// engine's GC (standalone), tools simply must not hold one.
#if __has_include("UObject/GCObject.h")
#include "UObject/GCObject.h"
#define MCP_WITH_GC_TRACKING 1
#else
#define MCP_WITH_GC_TRACKING 0
#endif

namespace MCP_NAMESPACE
{

#if MCP_WITH_GC_TRACKING
class FRegistry : public FGCObject
#else
class FRegistry
#endif
{
public:
#if MCP_REGISTRY_ISOLATED_IMPL
	// Out of line on purpose. A local static inside an inline accessor is per-DLL on Windows, so an
	// in-header body gives each consumer its own registry and the host's dispatcher only ever sees the
	// tools registered inside the host.
	static MCP_API FRegistry& Get();
#else
	static FRegistry& Get()
	{
		static FRegistry Instance;
		return Instance;
	}
#endif

	const TArray<TSharedRef<IMcpTool>>& GetTools() const { return Tools; }

	// Case-insensitive, matching the engine implementation.
	TSharedPtr<IMcpTool> FindTool(const FString& ToolName) const
	{
		for (const TSharedRef<IMcpTool>& Tool : Tools)
		{
			if (Tool->GetName().Equals(ToolName, ESearchCase::IgnoreCase))
			{
				return Tool;
			}
		}
		return nullptr;
	}

	// Refuses a duplicate name instead of shadowing the tool already serving it, and refuses a name the
	// spec disallows rather than letting a client reject the whole tool list later.
	bool AddTool(const TSharedRef<IMcpTool>& Tool)
	{
		const FString Name = Tool->GetName();
		if (ValidateToolName(Name) != EToolNameValidation::Valid)
		{
			return false;
		}
		if (FindTool(Name).IsValid())
		{
			return false;
		}
		Tools.Add(Tool);
		return true;
	}

	bool RemoveTool(const TSharedRef<IMcpTool>& Tool) { return Tools.RemoveSingle(Tool) > 0; }

	DECLARE_MULTICAST_DELEGATE(FOnRefreshTools);

	// Providers register from this delegate so a host can rebuild the whole tool set without restarting.
	FOnRefreshTools& OnRefreshTools() { return RefreshToolsDelegate; }

	void RefreshTools()
	{
		Tools.Reset();
		RefreshToolsDelegate.Broadcast();
	}

	const FServerInfo& GetServerInfo() const { return ServerInfo; }
	void SetServerInfo(const FServerInfo& InServerInfo) { ServerInfo = InServerInfo; }

#if MCP_WITH_GC_TRACKING
	// Routes the GC pass to every registered tool; a tool caching a UObject would otherwise have it
	// collected between invocations.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		for (const TSharedRef<IMcpTool>& Tool : Tools)
		{
			Tool->AddReferencedObjects(Collector);
		}
	}

	virtual FString GetReferencerName() const override { return TEXT("gmpmcp::FRegistry"); }
#endif

private:
	TArray<TSharedRef<IMcpTool>> Tools;
	FOnRefreshTools RefreshToolsDelegate;
	FServerInfo ServerInfo;
};

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_REGISTRY_H
