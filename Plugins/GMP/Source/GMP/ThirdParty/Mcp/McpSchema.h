//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Schema and argument helpers shared by tools. Spelling out a JSON Schema by hand is the bulk of a
// small tool, and once tools live in separate files per module there is nowhere else to keep this.
#pragma once

#ifndef UNREAL_GMP_MCP_SCHEMA_H
#define UNREAL_GMP_MCP_SCHEMA_H

#include "Mcp/McpToolResults.h"

#if GMP_WITH_MCP

namespace MCP_NAMESPACE
{

// Params may be an invalid handle when the call carried no arguments.
inline FString ParamString(const FJsonObjectPtr& Params, const TCHAR* Field, const FString& Fallback = FString())
{
	if (!Params.IsValid())
	{
		return Fallback;
	}
	FString Value;
	return Params->TryGetStringField(Field, Value) && !Value.IsEmpty() ? Value : Fallback;
}

// An object of typed properties, none required. Fields are name -> JSON type ("string", "number",
// "boolean", "array"). Mostly for output schemas, which the dispatcher publishes so a client knows the
// shape of structuredContent before calling.
inline FJsonObjectPtr TypedPropsSchema(const FJsonDoc& Doc, std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Fields)
{
	FJsonObjectPtr Schema = Doc.MakeObject();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	FJsonObjectPtr Props = Doc.MakeObject();
	for (const TPair<const TCHAR*, const TCHAR*>& Field : Fields)
	{
		FJsonObjectPtr Prop = Doc.MakeObject();
		Prop->SetStringField(TEXT("type"), Field.Value);
		Props->SetObjectField(Field.Key, Prop);
	}
	Schema->SetObjectField(TEXT("properties"), Props);
	return Schema;
}

// An object of named string properties with descriptions, none required. Fields are name -> description.
inline FJsonObjectPtr StringPropsSchema(const FJsonDoc& Doc, std::initializer_list<TPair<const TCHAR*, const TCHAR*>> Fields)
{
	FJsonObjectPtr Schema = Doc.MakeObject();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	FJsonObjectPtr Props = Doc.MakeObject();
	for (const TPair<const TCHAR*, const TCHAR*>& Field : Fields)
	{
		FJsonObjectPtr Prop = Doc.MakeObject();
		Prop->SetStringField(TEXT("type"), TEXT("string"));
		Prop->SetStringField(TEXT("description"), Field.Value);
		Props->SetObjectField(Field.Key, Prop);
	}
	Schema->SetObjectField(TEXT("properties"), Props);
	return Schema;
}

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_SCHEMA_H
