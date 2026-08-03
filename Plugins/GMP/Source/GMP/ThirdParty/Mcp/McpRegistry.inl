//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// The single definition of the registry accessor. Exactly one TU in the host includes this; consumers
// link the exported symbol.
#pragma once

#include "Mcp/McpRegistry.h"

#if GMP_WITH_MCP && MCP_REGISTRY_ISOLATED_IMPL

namespace MCP_NAMESPACE
{

FRegistry& FRegistry::Get()
{
	static FRegistry Instance;
	return Instance;
}

}  // namespace MCP_NAMESPACE

#endif
