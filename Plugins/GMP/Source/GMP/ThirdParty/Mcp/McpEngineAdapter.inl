//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Engine bridge implementation — the sole place that includes the engine MCP plugin's headers. A single
// host TU includes this file once.
//
// JSON crosses the boundary as text rather than through a node-by-node converter: the two DOMs evolve
// independently, while the serialized form does not. A tool call happens at agent-interaction rate, so
// the extra round-trip is irrelevant next to the maintenance it saves.
#pragma once

#ifndef UNREAL_GMP_MCP_ENGINEADAPTER_INL
#define UNREAL_GMP_MCP_ENGINEADAPTER_INL

#include "Mcp/McpEngineAdapter.h"

#if GMP_WITH_MCP && GMP_WITH_ENGINE_MCP

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolSession.h"
#include "ModelContextProtocolToolResults.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace MCP_NAMESPACE
{
namespace EngineAdapterDetail
{

inline FJsonObjectPtr ToArenaJson(const TSharedPtr<FJsonObject>& Engine)
{
	if (!Engine.IsValid())
	{
		return FJsonObjectPtr();
	}

	FString Text;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Text);
	if (!::FJsonSerializer::Serialize(Engine.ToSharedRef(), Writer))
	{
		return FJsonObjectPtr();
	}

	FJsonValuePtr Parsed;
	if (!json::FJsonSerializer::Deserialize(json::TJsonReaderFactory<>::Create(Text), Parsed) || !Parsed.IsValid())
	{
		return FJsonObjectPtr();
	}
	return Parsed->AsObject();
}

inline TSharedPtr<FJsonObject> ToEngineJson(const FJsonObjectPtr& Arena)
{
	if (!Arena.IsValid())
	{
		return nullptr;
	}

	FString Text;
	json::FJsonSerializer::Serialize(Arena, json::TJsonWriterFactory<>::Create(&Text));

	TSharedPtr<FJsonObject> Parsed;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	return ::FJsonSerializer::Deserialize(Reader, Parsed) ? Parsed : nullptr;
}

// The engine carries the raw JSON-RPC id; ours is its text form, which is all CancelAsync needs.
inline FMcpToolRequestId ToArenaRequestId(const FModelContextProtocolToolRequestId& EngineId)
{
	FMcpToolRequestId Out;
	if (!EngineId.RequestId.IsValid())
	{
		return Out;
	}

	if (!EngineId.RequestId->TryGetString(Out.Value))
	{
		double Number = 0.0;
		if (EngineId.RequestId->TryGetNumber(Number))
		{
			Out.Value = json::NumberToJsonString(Number);
		}
	}
	return Out;
}

struct FEngineToolAdapter : IModelContextProtocolTool
{
	TSharedRef<IMcpTool> Inner;

	explicit FEngineToolAdapter(const TSharedRef<IMcpTool>& InInner)
		: Inner(InInner)
	{
	}

	virtual FString GetName() const override { return Inner->GetName(); }

	virtual FString GetDescription() const override { return Inner->GetDescription(); }

	virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override { return ToEngineJson(Inner->GetInputJsonSchema()); }

	virtual TSharedPtr<FJsonObject> GetOutputJsonSchema() const override { return ToEngineJson(Inner->GetOutputJsonSchema()); }

	// Only RunAsync is overridden: the base Run would force a synchronous answer out of tools that are
	// deliberately asynchronous, and our RunAsync already falls back to Run when a tool is synchronous.
	virtual void RunAsync(const FModelContextProtocolToolRequestId& RequestId, const TSharedPtr<FJsonObject>& Params, const FResultCallback& OnComplete) override
	{
		Inner->RunAsync(ToArenaRequestId(RequestId), ToArenaJson(Params),
			[OnComplete](const FMcpToolResult& Result)
			{
				OnComplete(FModelContextProtocolToolResult(ToEngineJson(Result.Json)));
			});
	}

	virtual void CancelAsync(const FModelContextProtocolToolRequestId& RequestId) override { Inner->CancelAsync(ToArenaRequestId(RequestId)); }

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override { Inner->AddReferencedObjects(Collector); }
};

inline TArray<TSharedRef<IModelContextProtocolTool>>& PublishedAdapters()
{
	static TArray<TSharedRef<IModelContextProtocolTool>> Instances;
	return Instances;
}

}  // namespace EngineAdapterDetail

MCP_API int32 FEngineBridge::PublishTools()
{
	using namespace EngineAdapterDetail;

	IModelContextProtocolModule* Module = IModelContextProtocolModule::Get();
	if (!Module || PublishedAdapters().Num() > 0)
	{
		return 0;
	}

	int32 Accepted = 0;
	for (const TSharedRef<IMcpTool>& Tool : FRegistry::Get().GetTools())
	{
		const TSharedRef<IModelContextProtocolTool> Adapter = MakeShared<FEngineToolAdapter>(Tool);
		if (Module->AddTool(Adapter))
		{
			PublishedAdapters().Add(Adapter);
			++Accepted;
		}
	}
	return Accepted;
}

MCP_API void FEngineBridge::UnpublishTools()
{
	using namespace EngineAdapterDetail;

	if (IModelContextProtocolModule* Module = IModelContextProtocolModule::Get())
	{
		for (const TSharedRef<IModelContextProtocolTool>& Adapter : PublishedAdapters())
		{
			Module->RemoveTool(Adapter);
		}
	}
	PublishedAdapters().Reset();
}

MCP_API bool FEngineBridge::IsPublished()
{
	return EngineAdapterDetail::PublishedAdapters().Num() > 0;
}

}  // namespace MCP_NAMESPACE

#endif  // GMP_WITH_MCP && GMP_WITH_ENGINE_MCP
#endif  // UNREAL_GMP_MCP_ENGINEADAPTER_INL
