//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// IMcpTool — the tool contract. Signatures mirror UE 5.8's IModelContextProtocolTool one for one, with
// the JSON type swapped for JsonDom handles, so moving onto the engine plugin is an adapter rather than
// a rewrite of every tool.
#pragma once

#ifndef UNREAL_GMP_MCP_TOOL_H
#define UNREAL_GMP_MCP_TOOL_H

#include "Mcp/McpToolResults.h"

#if GMP_WITH_MCP

class FReferenceCollector;

namespace MCP_NAMESPACE
{

// Names one in-flight call so CancelAsync can refer to it.
struct FMcpToolRequestId
{
	FString Value;

	bool operator==(const FMcpToolRequestId& Other) const { return Value == Other.Value; }
};

struct IMcpTool : TSharedFromThis<IMcpTool>
{
	// Invoked exactly once per call and from any thread; the dispatcher marshals before consuming the
	// result. Invocations after the request is served or cancelled are dropped.
	typedef TFunction<void(const FMcpToolResult& Result)> FResultCallback;

	virtual ~IMcpTool() = default;

	// Tool identifier, e.g. select_object.
	virtual FString GetName() const = 0;

	// What the tool does and when to reach for it; this is what the model reads.
	virtual FString GetDescription() const = 0;

	// JSON Schema the arguments must follow. Required by spec to be an object schema; adherence is
	// assumed rather than enforced here. The default accepts any arguments.
	virtual FJsonObjectPtr GetInputJsonSchema() const
	{
		FJsonDoc Doc;
		FJsonObjectPtr Schema = Doc.MakeObject();
		Schema->SetStringField(TEXT("type"), TEXT("object"));
		return Schema;
	}

	// Optional schema for structuredContent. When present, returned structured results must conform.
	virtual FJsonObjectPtr GetOutputJsonSchema() const { return FJsonObjectPtr(); }

	// Implement either Run or RunAsync. Params may be an invalid handle when the call carried none.
	// Report bad arguments with MakeErrorResult, not a JSON-RPC error: since 2025-11-25 validation
	// failures are tool execution errors so the model can see them and correct itself.
	virtual FMcpToolResult Run(const FJsonObjectPtr& Params)
	{
		return MakeErrorResult(TEXT("IMcpTool must implement either Run or RunAsync"));
	}

	// Long work belongs here: the dispatcher runs tools on the game thread, so anything slower than a
	// frame should return immediately and report through OnComplete.
	virtual void RunAsync(const FMcpToolRequestId& RequestId, const FJsonObjectPtr& Params, const FResultCallback& OnComplete)
	{
		OnComplete(Run(Params));
	}

	virtual void CancelAsync(const FMcpToolRequestId& RequestId) {}

	// Report any UObject held across calls. A tool that caches one without reporting it will have it
	// collected between invocations — the registry routes this from its own GC pass.
	virtual void AddReferencedObjects(FReferenceCollector& Collector) {}
};

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_TOOL_H
