//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Tool result factories. A result is a complete MCP tool-result object (a `content` array and/or
// `structuredContent`), never bare data — the same contract as UE 5.8's FModelContextProtocolToolResult.
//
// Arena rule: every node inside one result must come from that result's own FJsonDoc. The doc-taking
// overloads exist so a tool can build its structured payload in the arena it is going to return; the
// returned handle keeps that arena alive on its own.
#pragma once

#ifndef UNREAL_GMP_MCP_TOOLRESULTS_H
#define UNREAL_GMP_MCP_TOOLRESULTS_H

#include "Mcp/McpProtocol.h"

#if GMP_WITH_MCP

namespace MCP_NAMESPACE
{

struct FMcpToolResult
{
	// Owns a shared ref to the arena every node below it was allocated from.
	FJsonObjectPtr Json;

	bool IsValid() const { return Json.IsValid(); }
};

inline FJsonObjectPtr MakeTextContentObject(const FJsonDoc& Doc, const FString& Text)
{
	FJsonObjectPtr Item = Doc.MakeObject();
	Item->SetStringField(TEXT("type"), TEXT("text"));
	Item->SetStringField(TEXT("text"), Text);
	return Item;
}

inline FMcpToolResult MakeTextResult(const FJsonDoc& Doc, const FString& Text)
{
	FJsonObjectPtr Root = Doc.MakeObject();
	Root->SetArrayField(TEXT("content"), {Doc.MakeValueObject(MakeTextContentObject(Doc, Text))});
	return FMcpToolResult{Root};
}

inline FMcpToolResult MakeTextResult(const FString& Text)
{
	return MakeTextResult(FJsonDoc(), Text);
}

// Structured must belong to Doc. Per spec the serialized form is also emitted as text for older clients.
inline FMcpToolResult MakeStructuredContentResult(const FJsonDoc& Doc, const FJsonObjectPtr& Structured)
{
	FString Serialized;
	json::FJsonSerializer::Serialize(Structured, json::TJsonWriterFactory<>::Create(&Serialized));
	FMcpToolResult Out = MakeTextResult(Doc, Serialized);
	Out.Json->SetObjectField(TEXT("structuredContent"), Structured);
	return Out;
}

// Execution failure: reported in-band with isError, not as a JSON-RPC error.
inline FMcpToolResult MakeErrorResult(const FJsonDoc& Doc, const FString& Message)
{
	FMcpToolResult Out = MakeTextResult(Doc, Message);
	Out.Json->SetBoolField(TEXT("isError"), true);
	return Out;
}

inline FMcpToolResult MakeErrorResult(const FString& Message)
{
	return MakeErrorResult(FJsonDoc(), Message);
}

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_TOOLRESULTS_H
