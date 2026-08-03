//  Copyright GenericMessagePlugin, Inc. All Rights Reserved.
//
// Dispatcher round-trips against the built-in tool. These mainly guard the arena discipline: a response
// mixes nodes owned by the parsed request, by tool-supplied schemas and by the response doc, so a
// lifetime slip surfaces here as malformed JSON rather than as a compile error.

#include "Mcp/McpDispatch.h"

#if GMP_WITH_MCP && WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGMPMcpDispatchTest, "GMP.Mcp.Dispatch", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
// Reparsing proves the response survived serialization intact; a dangling arena shows up as a parse failure.
bool IsWellFormedJson(const FString& Text)
{
	mcp::FJsonValuePtr Parsed;
	return mcp::json::FJsonSerializer::Deserialize(mcp::json::TJsonReaderFactory<>::Create(Text), Parsed) && Parsed.IsValid();
}
}  // namespace

bool FGMPMcpDispatchTest::RunTest(const FString& Parameters)
{
	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-11-25\"}}"));
		TestTrue(TEXT("initialize response is well formed"), IsWellFormedJson(Response));
		TestTrue(TEXT("initialize reports the protocol version"), Response.Contains(mcp::ProtocolVersion()));
		TestTrue(TEXT("initialize reports serverInfo"), Response.Contains(TEXT("serverInfo")));
		TestTrue(TEXT("initialize declares the tools capability"), Response.Contains(TEXT("capabilities")));
		TestTrue(TEXT("initialize echoes the id"), Response.Contains(TEXT("\"id\":1")));
	}

	// A client on an older revision keeps it, rather than being forced onto ours.
	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":11,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"2025-06-18\"}}"));
		TestTrue(TEXT("negotiation echoes a supported older revision"), Response.Contains(TEXT("2025-06-18")));
	}

	// An unknown revision falls back to ours instead of failing the handshake.
	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":12,\"method\":\"initialize\",\"params\":{\"protocolVersion\":\"1.0.0\"}}"));
		TestTrue(TEXT("negotiation falls back to the latest supported revision"), Response.Contains(mcp::ProtocolVersion()));
		TestFalse(TEXT("negotiation does not echo an unsupported revision"), Response.Contains(TEXT("1.0.0")));
	}

	// Tool names are constrained since 2025-11-25; the registry rejects rather than serving a bad list.
	{
		TestEqual(TEXT("a conforming name validates"), mcp::ValidateToolName(TEXT("anvil_cook.status-1")), mcp::EToolNameValidation::Valid);
		TestEqual(TEXT("an empty name is rejected"), mcp::ValidateToolName(FString()), mcp::EToolNameValidation::Empty);
		TestEqual(TEXT("a spaced name is rejected"), mcp::ValidateToolName(TEXT("anvil cook")), mcp::EToolNameValidation::InvalidCharacters);
		TestEqual(TEXT("a slashed name is rejected"), mcp::ValidateToolName(TEXT("anvil/cook")), mcp::EToolNameValidation::InvalidCharacters);
		TestEqual(TEXT("an overlong name is rejected"), mcp::ValidateToolName(FString::ChrN(129, TEXT('a'))), mcp::EToolNameValidation::ExceedsMaxLength);
	}

	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(TEXT("{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\"}"));
		TestTrue(TEXT("tools/list response is well formed"), IsWellFormedJson(Response));
		TestTrue(TEXT("tools/list advertises the built-in tool"), Response.Contains(TEXT("gmp_server_info")));
		TestTrue(TEXT("tools/list carries an input schema"), Response.Contains(TEXT("inputSchema")));
	}

	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":{\"name\":\"gmp_server_info\",\"arguments\":{}}}"));
		TestTrue(TEXT("tools/call response is well formed"), IsWellFormedJson(Response));
		TestTrue(TEXT("tools/call returns structured content"), Response.Contains(TEXT("structuredContent")));
		TestTrue(TEXT("tools/call returns a content array"), Response.Contains(TEXT("\"content\"")));
		TestFalse(TEXT("tools/call did not report a tool error"), Response.Contains(TEXT("isError")));
	}

	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(
			TEXT("{\"jsonrpc\":\"2.0\",\"id\":4,\"method\":\"tools/call\",\"params\":{\"name\":\"no_such_tool\"}}"));
		TestTrue(TEXT("unknown tool response is well formed"), IsWellFormedJson(Response));
		TestTrue(TEXT("unknown tool reports InvalidParams"), Response.Contains(TEXT("-32602")));
	}

	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(TEXT("{\"jsonrpc\":\"2.0\",\"id\":5,\"method\":\"no/such/method\"}"));
		TestTrue(TEXT("unknown method reports MethodNotFound"), Response.Contains(TEXT("-32601")));
	}

	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(TEXT("not json at all"));
		TestTrue(TEXT("malformed input reports ParseError"), Response.Contains(TEXT("-32700")));
	}

	{
		const FString Response = mcp::FDispatcher::HandleMessageSync(TEXT("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}"));
		TestTrue(TEXT("a notification draws no response"), Response.IsEmpty());
	}

	return true;
}

#endif  // GMP_WITH_MCP && WITH_DEV_AUTOMATION_TESTS
