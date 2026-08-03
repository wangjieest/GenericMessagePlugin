//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Bridge to the engine-side MCP plugin (UE 5.8+). Wraps every tool in the registry as an
// IModelContextProtocolTool and hands it to IModelContextProtocolModule, so the engine's server —
// including its SSE support — serves the same tools with no change to the tool implementations.
//
// This never becomes a hard dependency: the engine plugin is NoRedist, so GMP cannot list it in its
// uplugin. Both paths therefore stay compiled-out-able and a project opts in via GMP_WITH_ENGINE_MCP.
#pragma once

#ifndef UNREAL_GMP_MCP_ENGINEADAPTER_H
#define UNREAL_GMP_MCP_ENGINEADAPTER_H

#include "Mcp/McpRegistry.h"

#ifndef GMP_WITH_ENGINE_MCP
#define GMP_WITH_ENGINE_MCP 0
#endif

#if GMP_WITH_MCP && GMP_WITH_ENGINE_MCP

namespace MCP_NAMESPACE
{

class FEngineBridge
{
public:
	// Wraps and registers every tool currently in the registry. Returns how many the engine accepted.
	// Idempotent: publishing twice does not double-register.
	static MCP_API int32 PublishTools();

	// Removes the wrappers this bridge registered, leaving tools registered by others alone.
	static MCP_API void UnpublishTools();

	static MCP_API bool IsPublished();
};

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP && GMP_WITH_ENGINE_MCP
#endif  // UNREAL_GMP_MCP_ENGINEADAPTER_H
