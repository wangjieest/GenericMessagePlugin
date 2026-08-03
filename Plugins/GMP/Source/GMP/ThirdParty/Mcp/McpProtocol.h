//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// MCP (Model Context Protocol) base — shared vocabulary for the tool interface, registry and dispatcher.
// Transport-agnostic on purpose: nothing here touches sockets, so a host can drive dispatch over HTTP,
// an existing binary channel, or stdio. Message shapes follow the 2025-06-18 spec, and the tool-facing
// types mirror UE 5.8's ModelContextProtocol plugin so tools can later be re-registered against the
// engine implementation through an adapter instead of being rewritten.
// The platform layer (CoreMinimal.h / UECompat.h) must supply TCHAR/FString/TArray/TSharedPtr.
#pragma once

// Content guard: identical copies may exist under other hosts' ThirdParty; first-seen wins.
#ifndef UNREAL_GMP_MCP_PROTOCOL_H
#define UNREAL_GMP_MCP_PROTOCOL_H

// Feature switch. Editor-only by default; predefine to 1 to also serve from a runtime dev build.
#ifndef GMP_WITH_MCP
#if defined(WITH_EDITOR)
#define GMP_WITH_MCP WITH_EDITOR
#else
#define GMP_WITH_MCP 0
#endif
#endif

#if GMP_WITH_MCP

// Namespace configuration, mirroring JsonDom: retarget either before including any Mcp header.
#ifndef MCP_NAMESPACE
#define MCP_NAMESPACE gmpmcp
#endif
#ifndef MCP_ALIAS
#define MCP_ALIAS mcp
#endif

// JsonDom expects the platform layer to already supply TCHAR/FString/TArray/TSharedPtr. Under UE this
// copy always compiles inside a module, so pull it in rather than relying on unity build ordering.
#if __has_include("CoreMinimal.h")
#include "CoreMinimal.h"
#endif

#include "JsonDom/JsonDom.h"
#include "JsonDom/JsonSerializer.h"

namespace MCP_NAMESPACE
{
namespace json = JSONDOM_ALIAS;

using FJsonObjectPtr = json::FJsonObjectPtr;
using FJsonValuePtr = json::FJsonValuePtr;
using FJsonDoc = json::FJsonDoc;

// Latest spec revision implemented here.
inline const TCHAR* ProtocolVersion() { return TEXT("2025-11-25"); }

// Every revision this server answers to, newest first. Older ones stay listed because a client that
// skipped negotiation may keep sending them, and nothing we implement is revision-specific.
inline const TArray<FString>& GetSupportedProtocolVersions()
{
	static const TArray<FString> Versions = {TEXT("2025-11-25"), TEXT("2025-06-18"), TEXT("2024-11-05")};
	return Versions;
}

inline bool IsSupportedProtocolVersion(const FString& Version)
{
	return GetSupportedProtocolVersions().Contains(Version);
}

// Echo the client's revision when it is one we serve, otherwise offer ours and let the client decide.
inline FString NegotiateProtocolVersion(const FString& ClientRequestedVersion)
{
	return IsSupportedProtocolVersion(ClientRequestedVersion) ? ClientRequestedVersion : FString(ProtocolVersion());
}

// Tool name rules introduced in 2025-11-25 (SEP-986).
enum class EToolNameValidation : uint8
{
	Valid,
	Empty,
	ExceedsMaxLength,
	InvalidCharacters,
};

// 1-128 characters of A-Z, a-z, 0-9, underscore, hyphen or dot.
inline EToolNameValidation ValidateToolName(const FString& ToolName)
{
	if (ToolName.IsEmpty())
	{
		return EToolNameValidation::Empty;
	}
	if (ToolName.Len() > 128)
	{
		return EToolNameValidation::ExceedsMaxLength;
	}

	for (const TCHAR Character : ToolName)
	{
		const bool bAllowed = (Character >= TEXT('A') && Character <= TEXT('Z')) || (Character >= TEXT('a') && Character <= TEXT('z'))
							  || (Character >= TEXT('0') && Character <= TEXT('9')) || Character == TEXT('_') || Character == TEXT('-')
							  || Character == TEXT('.');
		if (!bAllowed)
		{
			return EToolNameValidation::InvalidCharacters;
		}
	}
	return EToolNameValidation::Valid;
}

inline const TCHAR* DescribeToolNameValidation(EToolNameValidation Result)
{
	switch (Result)
	{
		case EToolNameValidation::Valid: return TEXT("valid");
		case EToolNameValidation::Empty: return TEXT("name is empty");
		case EToolNameValidation::ExceedsMaxLength: return TEXT("name exceeds 128 characters");
		case EToolNameValidation::InvalidCharacters: return TEXT("name allows only A-Z, a-z, 0-9, underscore, hyphen and dot");
	}
	return TEXT("unknown");
}

// JSON-RPC 2.0 codes. MCP adds none: an unknown tool and bad arguments both report InvalidParams.
namespace EJsonRpcError
{
enum Type : int32
{
	ParseError = -32700,
	InvalidRequest = -32600,
	MethodNotFound = -32601,
	InvalidParams = -32602,
	InternalError = -32603,
};
}

namespace Methods
{
inline const TCHAR* const Initialize = TEXT("initialize");
inline const TCHAR* const Initialized = TEXT("notifications/initialized");
inline const TCHAR* const Ping = TEXT("ping");
inline const TCHAR* const ToolsList = TEXT("tools/list");
inline const TCHAR* const ToolsCall = TEXT("tools/call");
}

// Identity reported in InitializeResult; a host overrides it before serving.
struct FServerInfo
{
	FString Name = TEXT("gmp-mcp");
	FString Version = TEXT("0.1");
	// Optional human-readable context, added to Implementation in 2025-11-25.
	FString Description;
	FString Instructions;
};

}  // namespace MCP_NAMESPACE

#ifndef UNREAL_MCP_ALIAS_DEFINED
#define UNREAL_MCP_ALIAS_DEFINED 1
namespace MCP_ALIAS = MCP_NAMESPACE;
#endif

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_PROTOCOL_H
