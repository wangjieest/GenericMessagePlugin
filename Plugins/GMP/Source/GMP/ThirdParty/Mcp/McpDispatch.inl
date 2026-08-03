//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Dispatcher implementation.
//
// Arena discipline: a JsonDom node is only valid while some handle keeps its doc alive. Responses mix
// nodes from three arenas — the parsed request (id), tool-owned schemas and results, and the response
// doc itself — so every borrowed handle is held in a local (or captured by the async lambda) until
// serialization has finished.
#pragma once

#ifndef UNREAL_GMP_MCP_DISPATCH_INL
#define UNREAL_GMP_MCP_DISPATCH_INL

#if GMP_WITH_MCP

namespace MCP_NAMESPACE
{
namespace DispatchDetail
{

inline FString SerializeObject(const FJsonObjectPtr& Object)
{
	FString Out;
	json::FJsonSerializer::Serialize(Object, json::TJsonWriterFactory<>::Create(&Out));
	return Out;
}

inline void SetResponseId(const FJsonDoc& Doc, const FJsonObjectPtr& Root, const FJsonValuePtr& Id)
{
	// A response always carries id; null is the correct value when the request had none or was unparsable.
	Root->SetField(TEXT("id"), Id.IsValid() ? Id : Doc.MakeNull());
}

inline FString MakeResultResponse(const FJsonDoc& Doc, const FJsonValuePtr& Id, const FJsonObjectPtr& Result)
{
	FJsonObjectPtr Root = Doc.MakeObject();
	Root->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	SetResponseId(Doc, Root, Id);
	Root->SetObjectField(TEXT("result"), Result);
	return SerializeObject(Root);
}

inline FString MakeErrorResponse(const FJsonDoc& Doc, const FJsonValuePtr& Id, int32 Code, const FString& Message)
{
	FJsonObjectPtr Error = Doc.MakeObject();
	Error->SetNumberField(TEXT("code"), (double)Code);
	Error->SetStringField(TEXT("message"), Message);

	FJsonObjectPtr Root = Doc.MakeObject();
	Root->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	SetResponseId(Doc, Root, Id);
	Root->SetObjectField(TEXT("error"), Error);
	return SerializeObject(Root);
}

inline FMcpToolRequestId MakeToolRequestId(const FJsonValuePtr& Id)
{
	FMcpToolRequestId Out;
	if (Id.IsValid())
	{
		Out.Value = Id->Type == json::EJson::String ? Id->AsString() : json::NumberToJsonString(Id->AsNumber());
	}
	return Out;
}

inline FJsonObjectPtr MakeInitializeResult(const FJsonDoc& Doc, const FString& ClientVersion)
{
	const FServerInfo& Info = FRegistry::Get().GetServerInfo();

	FJsonObjectPtr ToolsCapability = Doc.MakeObject();
	ToolsCapability->SetBoolField(TEXT("listChanged"), false);

	FJsonObjectPtr Capabilities = Doc.MakeObject();
	Capabilities->SetObjectField(TEXT("tools"), ToolsCapability);

	FJsonObjectPtr ServerInfoObject = Doc.MakeObject();
	ServerInfoObject->SetStringField(TEXT("name"), Info.Name);
	ServerInfoObject->SetStringField(TEXT("version"), Info.Version);
	if (!Info.Description.IsEmpty())
	{
		ServerInfoObject->SetStringField(TEXT("description"), Info.Description);
	}

	FJsonObjectPtr Result = Doc.MakeObject();
	// Echo the client's revision when we serve it; otherwise answer with ours and let it decide.
	Result->SetStringField(TEXT("protocolVersion"), NegotiateProtocolVersion(ClientVersion));
	Result->SetObjectField(TEXT("capabilities"), Capabilities);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfoObject);
	if (!Info.Instructions.IsEmpty())
	{
		Result->SetStringField(TEXT("instructions"), Info.Instructions);
	}
	return Result;
}

inline FString MakeToolsListResponse(const FJsonDoc& Doc, const FJsonValuePtr& Id)
{
	// Each schema owns a separate arena; hold the handles until the response has been serialized.
	TArray<FJsonObjectPtr> SchemaKeepAlive;
	TArray<FJsonValuePtr> ToolValues;

	for (const TSharedRef<IMcpTool>& Tool : FRegistry::Get().GetTools())
	{
		FJsonObjectPtr Entry = Doc.MakeObject();
		Entry->SetStringField(TEXT("name"), Tool->GetName());
		Entry->SetStringField(TEXT("description"), Tool->GetDescription());

		FJsonObjectPtr InputSchema = Tool->GetInputJsonSchema();
		if (InputSchema.IsValid())
		{
			SchemaKeepAlive.Add(InputSchema);
			Entry->SetObjectField(TEXT("inputSchema"), InputSchema);
		}

		FJsonObjectPtr OutputSchema = Tool->GetOutputJsonSchema();
		if (OutputSchema.IsValid())
		{
			SchemaKeepAlive.Add(OutputSchema);
			Entry->SetObjectField(TEXT("outputSchema"), OutputSchema);
		}

		ToolValues.Add(Doc.MakeValueObject(Entry));
	}

	FJsonObjectPtr Result = Doc.MakeObject();
	Result->SetArrayField(TEXT("tools"), ToolValues);
	return MakeResultResponse(Doc, Id, Result);
}

}  // namespace DispatchDetail

inline void FDispatcher::HandleMessage(const FString& RequestText, const FResponseCallback& OnResponse)
{
	using namespace DispatchDetail;

	// Backs every node this call creates; captured by the async path so it outlives the return.
	FJsonDoc Doc;

	FJsonValuePtr RequestValue;
	if (!json::FJsonSerializer::Deserialize(json::TJsonReaderFactory<>::Create(RequestText), RequestValue) || !RequestValue.IsValid())
	{
		OnResponse(MakeErrorResponse(Doc, FJsonValuePtr(), EJsonRpcError::ParseError, TEXT("Parse error")));
		return;
	}

	FJsonObjectPtr Request = RequestValue->AsObject();
	if (!Request.IsValid())
	{
		OnResponse(MakeErrorResponse(Doc, FJsonValuePtr(), EJsonRpcError::InvalidRequest, TEXT("Request must be a JSON object")));
		return;
	}

	// Borrowed from the request arena; RequestValue keeps it alive for the whole call.
	const FJsonValuePtr Id(Request->FindField(TEXT("id")), Request.Doc);
	const bool bIsNotification = !Id.IsValid();
	const FString Method = Request->GetStringField(TEXT("method"));

	if (Method.IsEmpty())
	{
		if (bIsNotification)
		{
			OnResponse(FString());
			return;
		}
		OnResponse(MakeErrorResponse(Doc, Id, EJsonRpcError::InvalidRequest, TEXT("Missing method")));
		return;
	}

	if (Method == Methods::Initialized || bIsNotification)
	{
		OnResponse(FString());
		return;
	}

	if (Method == Methods::Initialize)
	{
		const FJsonValuePtr InitParamsValue(Request->FindField(TEXT("params")), Request.Doc);
		const FJsonObjectPtr InitParams = InitParamsValue.IsValid() ? InitParamsValue->AsObject() : FJsonObjectPtr();
		const FString ClientVersion = InitParams.IsValid() ? InitParams->GetStringField(TEXT("protocolVersion")) : FString();
		OnResponse(MakeResultResponse(Doc, Id, MakeInitializeResult(Doc, ClientVersion)));
		return;
	}

	if (Method == Methods::Ping)
	{
		OnResponse(MakeResultResponse(Doc, Id, Doc.MakeObject()));
		return;
	}

	if (Method == Methods::ToolsList)
	{
		OnResponse(MakeToolsListResponse(Doc, Id));
		return;
	}

	if (Method == Methods::ToolsCall)
	{
		const FJsonValuePtr ParamsValue(Request->FindField(TEXT("params")), Request.Doc);
		const FJsonObjectPtr Params = ParamsValue.IsValid() ? ParamsValue->AsObject() : FJsonObjectPtr();
		if (!Params.IsValid())
		{
			OnResponse(MakeErrorResponse(Doc, Id, EJsonRpcError::InvalidParams, TEXT("tools/call requires a params object")));
			return;
		}

		const FString ToolName = Params->GetStringField(TEXT("name"));
		TSharedPtr<IMcpTool> Tool = FRegistry::Get().FindTool(ToolName);
		if (!Tool.IsValid())
		{
			OnResponse(MakeErrorResponse(Doc, Id, EJsonRpcError::InvalidParams, FString::Printf(TEXT("Unknown tool: %s"), *ToolName)));
			return;
		}

		const FJsonValuePtr ArgumentsValue(Params->FindField(TEXT("arguments")), Params.Doc);
		const FJsonObjectPtr Arguments = ArgumentsValue.IsValid() ? ArgumentsValue->AsObject() : FJsonObjectPtr();

		// Capturing RequestValue keeps the request arena (and therefore Id) alive for an async tool.
		Tool->RunAsync(MakeToolRequestId(Id), Arguments,
			[Doc, Id, RequestValue, OnResponse](const FMcpToolResult& Result)
			{
				const FMcpToolResult Answered = Result.IsValid() ? Result : MakeErrorResult(TEXT("Tool returned no result"));
				OnResponse(MakeResultResponse(Doc, Id, Answered.Json));
			});
		return;
	}

	OnResponse(MakeErrorResponse(Doc, Id, EJsonRpcError::MethodNotFound, FString::Printf(TEXT("Unknown method: %s"), *Method)));
}

inline FString FDispatcher::HandleMessageSync(const FString& RequestText)
{
	FString Response;
	bool bAnswered = false;
	HandleMessage(RequestText,
		[&Response, &bAnswered](const FString& ResponseText)
		{
			Response = ResponseText;
			bAnswered = true;
		});

	if (!bAnswered)
	{
		// The request id lives inside HandleMessage, so this degraded answer cannot echo it.
		FJsonDoc Doc;
		return DispatchDetail::MakeErrorResponse(Doc, FJsonValuePtr(), EJsonRpcError::InternalError, TEXT("Tool did not answer synchronously"));
	}
	return Response;
}

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP
#endif  // UNREAL_GMP_MCP_DISPATCH_INL
