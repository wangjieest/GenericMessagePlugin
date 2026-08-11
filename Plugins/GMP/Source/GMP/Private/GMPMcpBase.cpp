//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Compilation anchor for the header-only MCP base in ThirdParty/Mcp, so it keeps building with GMP
// rather than rotting until the first consumer appears. Registers the one built-in tool, and — where
// the HTTP transport is compiled in — owns the console commands that start and stop the endpoint.

#include "Mcp/McpDispatch.h"

#if GMP_WITH_MCP

#include "Mcp/McpModuleTools.h"
#include "Misc/DelayedAutoRegister.h"

// Sole TU defining the shared registry accessor, so every module resolves to this one instance.
// Independent of the transport: turning HTTP off must not leave the accessor undefined.
#if MCP_REGISTRY_ISOLATED_IMPL
#include "Mcp/McpRegistry.inl"
// Same reason: the per-module buckets a registering module in another DLL links against.
#include "Mcp/McpModuleTools.inl"
#endif

#if GMP_WITH_MCP_HTTP
#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

// Sole TU carrying the transport implementation, so HTTPServer never leaks past this module.
#include "Mcp/McpHttpBinding.inl"
#endif

#if GMP_WITH_ENGINE_MCP
// Sole TU carrying the engine-plugin bridge, so its headers never leak past this module either.
#include "Mcp/McpEngineAdapter.inl"
#endif

DEFINE_LOG_CATEGORY_STATIC(LogGMPMcp, Log, All);

namespace
{
// Minimal built-in: lets a client confirm the endpoint is live and see what the server calls itself.
struct FGMPServerInfoTool : public mcp::IMcpTool
{
	virtual FString GetName() const override { return TEXT("gmp_server_info"); }

	virtual FString GetDescription() const override { return TEXT("Report the name and version of this MCP server."); }

	virtual mcp::FMcpToolResult Run(const mcp::FJsonObjectPtr& Params) override
	{
		const mcp::FServerInfo& Info = mcp::FRegistry::Get().GetServerInfo();

		mcp::FJsonDoc Doc;
		mcp::FJsonObjectPtr Data = Doc.MakeObject();
		Data->SetStringField(TEXT("name"), Info.Name);
		Data->SetStringField(TEXT("version"), Info.Version);
		Data->SetStringField(TEXT("protocolVersion"), mcp::ProtocolVersion());
		return mcp::MakeStructuredContentResult(Doc, Data);
	}
};

MCP_REGISTER_TOOL(FGMPServerInfoTool);

#if GMP_WITH_MCP_HTTP

void StartEndpoint(const TArray<FString>& Args)
{
	mcp::FModuleTools::RegisterAll();

	mcp::FHttpBinding::FConfig Config;
	if (Args.Num() > 0)
	{
		Config.Port = (uint32)FCString::Atoi(*Args[0]);
	}

	const mcp::FHttpBinding::EStartResult Result = mcp::FHttpBinding::Start(Config);
	if (Result == mcp::FHttpBinding::EStartResult::Started)
	{
		UE_LOG(LogGMPMcp, Display, TEXT("listening on http://127.0.0.1:%u%s (%d tool(s), no authentication)"), Config.Port, *Config.Path,
			mcp::FRegistry::Get().GetTools().Num());
	}
	else
	{
		UE_LOG(LogGMPMcp, Warning, TEXT("port %u: %s"), Config.Port, mcp::FHttpBinding::DescribeStartResult(Result));
	}
}

void StopEndpoint()
{
	mcp::FHttpBinding::Stop();
	UE_LOG(LogGMPMcp, Display, TEXT("endpoint stopped"));
}

void ReportStatus()
{
	UE_LOG(LogGMPMcp, Display, TEXT("running=%s port=%u tools=%d"), mcp::FHttpBinding::IsRunning() ? TEXT("yes") : TEXT("no"), mcp::FHttpBinding::GetPort(),
		mcp::FRegistry::Get().GetTools().Num());
}

const FAutoConsoleCommand GMPMcpStartCommand(TEXT("GMP.Mcp.Start"), TEXT("Start the MCP HTTP endpoint. Optional arg: port (default 8000)."),
	FConsoleCommandWithArgsDelegate::CreateStatic(&StartEndpoint));

const FAutoConsoleCommand GMPMcpStopCommand(TEXT("GMP.Mcp.Stop"), TEXT("Stop the MCP HTTP endpoint."), FConsoleCommandDelegate::CreateStatic(&StopEndpoint));

const FAutoConsoleCommand GMPMcpStatusCommand(TEXT("GMP.Mcp.Status"), TEXT("Report MCP endpoint state."), FConsoleCommandDelegate::CreateStatic(&ReportStatus));

// Unattended sessions have no console: -GMPMcpPort=<n> starts the endpoint at boot instead.
void StartFromCommandLineIfRequested()
{
	int32 Port = 0;
	if (FParse::Value(FCommandLine::Get(), TEXT("GMPMcpPort="), Port) && Port > 0)
	{
		StartEndpoint({FString::FromInt(Port)});
	}
}

#endif  // GMP_WITH_MCP_HTTP

#if GMP_WITH_MCP_HTTP
// Never auto-started otherwise: an unauthenticated port must be opened deliberately.
const FDelayedAutoRegisterHelper GMPMcpBootstrap(EDelayedRegisterRunPhase::EndOfEngineInit, &StartFromCommandLineIfRequested);
#endif

MCP_IMPLEMENT_MODULE_TOOLS();

}  // namespace

#endif  // GMP_WITH_MCP
