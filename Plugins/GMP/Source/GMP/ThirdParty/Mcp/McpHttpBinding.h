//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Optional HTTP transport for the dispatcher — declaration only. No HTTPServer type appears here, so a
// consumer includes this header and links against the host module; only the host TU that includes
// McpHttpBinding.inl takes the HTTPServer dependency. Same isolated-impl shape as JsonDom.
//
// Streamable HTTP without SSE, which the spec permits: a POST is answered with one `application/json`
// body, and GET (the server-push stream) answers 405. That keeps this working on engines whose
// HTTPServer cannot stream, at the cost of server-initiated notifications and progress updates.
//
// It moves bytes and nothing else: the body text reaches the dispatcher untouched and its answer comes
// back untouched. HTTP status codes report transport failures only; JSON-RPC errors travel in a 200 body.
#pragma once

#ifndef UNREAL_GMP_MCP_HTTPBINDING_H
#define UNREAL_GMP_MCP_HTTPBINDING_H

#include "Mcp/McpDispatch.h"

#if GMP_WITH_MCP

// Linkage of the transport entry points. The host predefines this to its export macro (e.g. GMP_API)
// so consumers link the implementation instead of compiling their own.
#ifndef MCP_HTTP_API
#define MCP_HTTP_API
#endif

namespace MCP_NAMESPACE
{

class FHttpBinding
{
public:
	struct FConfig
	{
		// Clear of Epic's 8000 and of the Anvil control plane on 8793.
		uint32 Port = 8790;
		FString Path = TEXT("/mcp");
		// Refuse to serve when the listener is not on loopback: this endpoint has no authentication.
		bool bLoopbackOnly = true;
	};

	enum class EStartResult : uint8
	{
		Started,
		AlreadyRunning,
		NotLoopback,
		RouterUnavailable,
		RouteBindFailed,
	};

	// Idempotent: an already-serving binding reports AlreadyRunning rather than rebinding.
	static MCP_HTTP_API EStartResult Start(const FConfig& Config);

	// Must run before module shutdown, or the router outlives this binding.
	static MCP_HTTP_API void Stop();

	static MCP_HTTP_API bool IsRunning();

	static MCP_HTTP_API uint32 GetPort();

	static const TCHAR* DescribeStartResult(EStartResult Result)
	{
		switch (Result)
		{
			case EStartResult::Started: return TEXT("started");
			case EStartResult::AlreadyRunning: return TEXT("already running");
			case EStartResult::NotLoopback: return TEXT("refused: [HTTPServer.Listeners] bind address is not loopback");
			case EStartResult::RouterUnavailable: return TEXT("failed: no router for this port");
			case EStartResult::RouteBindFailed: return TEXT("failed: route already bound on this port");
		}
		return TEXT("unknown");
	}
};

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_HTTPBINDING_H
