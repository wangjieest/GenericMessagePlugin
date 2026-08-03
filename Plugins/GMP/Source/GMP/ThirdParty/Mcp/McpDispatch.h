//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// JSON-RPC dispatcher: one request text in, one response text out. It owns no transport, so a host can
// feed it from an HTTP endpoint, an existing binary channel, or stdio.
//
// Handles initialize / notifications/initialized / ping / tools/list / tools/call. Notifications produce
// an empty response — over HTTP that is the 202-with-no-body case.
#pragma once

#ifndef UNREAL_GMP_MCP_DISPATCH_H
#define UNREAL_GMP_MCP_DISPATCH_H

#include "Mcp/McpRegistry.h"

#if GMP_WITH_MCP

namespace MCP_NAMESPACE
{

class FDispatcher
{
public:
	// Invoked exactly once; an empty string means "notification, send no response".
	typedef TFunction<void(const FString& ResponseText)> FResponseCallback;

	// Async-capable entry point: a tool implementing RunAsync may answer after this returns.
	static void HandleMessage(const FString& RequestText, const FResponseCallback& OnResponse);

	// For transports that must answer inline. A tool that has not completed by return time yields an
	// InternalError response, so async tools need the callback form above.
	static FString HandleMessageSync(const FString& RequestText);
};

}  // namespace MCP_NAMESPACE

#include "Mcp/McpDispatch.inl"

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_DISPATCH_H
